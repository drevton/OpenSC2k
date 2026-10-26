#include "base/file.h"
#include "city/saveload/runtime.h"
#include "city/saveload/services.h"
#include "city/saveload/state.h"
#include "city/engine/city_name.h"
#include "city/economy/facilities.h"
#include "city/presentation.h"
#include "game/state.h"
#include "city/saveload/city_file_internal.h"
#include "city/saveload/input.h"

#include "city/economy/economy.h"

#include "game/errors.h"
#include "city/state.h"
#include "base/byte_order.h"
#include <string.h>

// Load

// Apply an in-memory stock archive to the supplied City using the shared chunk codecs. Returns false on failure with
// actual partial writes retained; success-only map conversion and rebuilding must follow the admitted load path.
bool LoadCityArchive(City *city, const CityFile *archive) {
  if (archive == NULL || archive->source == NULL) return false;
  ClearGameError(city->game);
  CityFileInput input = {.kind = CITY_FILE_INPUT_MEMORY, .bytes = archive->source, .size = archive->source_size};
  CityFileRuntime *file = city->game->file;
  CityFileScratch scratch = {0};
  CityFileScratch *previous_scratch = file->scratch;
  CityMiscBuffer *previous_misc = file->misc;
  bool previous_misc_active = file->misc_active, previous_misc_owned = file->misc_owned;
  file->scratch = &scratch;
  file->misc = NULL;
  file->misc_active = file->misc_owned = false;
  bool loaded = LoadCityChunksFromInput(city, &input, archive->source_path, true);
  ReleaseCityMiscWorkspace(file);
  file->scratch = previous_scratch;
  file->misc = previous_misc;
  file->misc_active = previous_misc_active;
  file->misc_owned = previous_misc_owned;
  return loaded;
}

// Begin the 1200-dword serialization workspace shared by load and save. Requires an active file operation; returns
// workspace admission and uses the matching release path rather than giving callers heap ownership.
bool AllocateCityMiscBuffer(CityFileRuntime *file) {
  file->misc_active = false;
  if (!PrepareCityMiscWorkspace(file)) {
    SetGameError(file->game, SC2K_ERROR_GUEST_ALLOCATION_FAILURE);
    return false;
  }
  return true;
}

// Close the captured owned input during unwinding. Explicit closes clear the capture first, so a throwing close
// is never repeated by this cleanup. Its pointer slot must remain alive until the cleanup is popped or unwound.
static void DestroyLoadFile(void *context) {
  CityLoadStream **stream = context;
  if (*stream != NULL)
    CloseCityLoadStream(*stream);
}

// Decode City chunks from an already opened load stream using the common input adapter. Caller owns and closes the
// stream; failure retains partial writes and does not run successful-load rebuilding.
static bool LoadCityFileChunks(City *city, CityLoadStream *stream, const char *path) {
  CityFileInput input = {.kind = CITY_FILE_INPUT_PLATFORM, .load_stream = stream};
  return LoadCityChunksFromInput(city, &input, path, false);
}

// Own the host stream through decoding. Every normal decode return closes it, then restores the view and unlocks,
// including a false result. Throwing reads/closes instead follow the operation's cleanup stack with partial writes.
bool LoadCityFile(City *city, const char *path) {
  CityFileRuntime *file = city->game->file;
  CityLoadStream *input = OpenCityLoadStream(file, path);
  if (input == NULL) {
    FillCityLoadOpenError(file, &file->error);
    ShowCityFileException(city, CITY_NOTICE_ERROR_OPENING_FILE, (&file->error), path);
    return false;
  }
  CityLoadStream *owned_stream = input;
  CityFileCleanup cleanup = {DestroyLoadFile, &owned_stream, NULL};
  PushCityFileCleanup(city->game->file, &cleanup);
  if (!AllocateCityMiscBuffer(file)) {
    owned_stream = NULL;
    CloseCityLoadStream(input);
    PopCityFileCleanup(file, &cleanup);
    return false;
  }
  SetCityPathBytes(city->game, &city->game->city_path, (int)strlen(path), path);
  bool loaded = LoadCityFileChunks(city, input, path);
  owned_stream = NULL;
  CloseCityLoadStream(input);
  PopCityFileCleanup(city->game->file, &cleanup);
  RestoreCityLoadedView(city);
  RebuildCityUnlockState(city);
  return loaded;
}

// Chunks

// Read and discard one chunk payload through the configured input. Returns whether any bytes were read; throwing
// input failures retain their normal unwind semantics and the scratch allocation is registered for cleanup.
bool CityFileSkipCityChunk(CityFileRuntime *file, CityFileInput *input, u32 size) {
  void *scratch = AllocateCityFileScratch(file, size);
  if (scratch == NULL)
    return false;
  CityFileScratchCleanup buffer = {file, scratch};
  CityFileCleanup cleanup = {ReleaseCityFileScratchBuffer, &buffer, NULL};
  PushCityFileCleanup(file, &cleanup);
  u32 received = CityFileInputRead(input, scratch, size);
  PopCityFileCleanup(file, &cleanup);
  FreeCityFileScratch(file, scratch);
  return received != 0;
}

// Normal return checks only encoded bytes consumed; output may be underfilled. Unsafe packet overruns are rejected
// after retaining the preceding packets' writes. Destination must cover decoded_size strided bytes; stream errors
// unwind and malformed packets keep earlier writes instead of rolling back.
bool ReadCityCompressedChunkStrided(CityFileRuntime *file, CityFileInput *input, u32 encoded_size,
    void *destination, u32 decoded_size, int stride) {
  u8 *encoded = AllocateCityFileScratch(file, encoded_size);
  if (encoded == NULL) {
    SetGameError(file->game, SC2K_ERROR_GUEST_ALLOCATION_FAILURE);
    return false; // Defined replacement for the original throwing allocator service.
  }
  // Stock leaks this scratch allocation if Read throws. Native ownership cleanup
  // changes no City writes, error or command continuation.
  CityFileScratchCleanup buffer = {file, encoded};
  CityFileCleanup cleanup = {ReleaseCityFileScratchBuffer, &buffer, NULL};
  PushCityFileCleanup(file, &cleanup);
  if (CityFileInputRead(input, encoded, encoded_size) == 0) {
    ReportCityFileError(file, CITY_NOTICE_ERROR_READING_FILE, CITY_FILE_ERROR_WITH_CAUSE);
    PopCityFileCleanup(file, &cleanup);
    FreeCityFileScratch(file, encoded);
    return false;
  }
  u8 *decoded = destination;
  u32 source = 0, target = 0;
  bool bounded = true;
  while ((int)source < (int)encoded_size &&
         (int)target < (int)decoded_size) {
    if (!DecodeCityRlePacket(encoded, encoded_size, &source, decoded, decoded_size, &target, stride)) {
      bounded = false;
      break;
    }
  }
  PopCityFileCleanup(file, &cleanup);
  FreeCityFileScratch(file, encoded);
  if (!bounded || source != encoded_size) {
    ReportCityFileError(file, CITY_NOTICE_ERROR_DECOMPRESSING_FILE, CITY_FILE_ERROR_RESOURCE_ONLY);
    return false;
  }
  return true;
}

// Decode a compressed chunk into contiguous caller storage via the strided reader. Destination must hold decoded_size
// bytes; malformed packets retain prior writes and stream errors may unwind the command.
bool CityFileReadCompressedCityChunk(CityFileRuntime *file, CityFileInput *input, u32 encoded_size,
    void *destination, u32 decoded_size) {
  return ReadCityCompressedChunkStrided(file, input, encoded_size, destination, decoded_size, 1);
}

// Each header read accepts any nonzero byte count. Unread header bytes originate on the original stack; C supplies
// zeroed initial bytes. Input and output length must be valid; returns admission while retaining the original
// short-header semantics and backend exceptions.
bool CityFileValidateCityFileContainer(City *city, CityFileInput *input, const char *path, u32 *length) {
  CityFileRuntime *file = city->game->file;
  u32 tag = 0;
  if (input->has_first_tag) {
    tag = input->first_tag;
    input->has_first_tag = false;
  } else if (CityFileInputRead(input, &tag, 4) == 0) {
    ReportCityFileError(file, CITY_NOTICE_ERROR_READING_FILE, CITY_FILE_ERROR_WITH_CAUSE);
    return false;
  }
  tag = ReadBE32((const u8 *)&tag);
  if (tag != ReadBE32((const u8 *)"FORM")) {
    ReportCityFileError(file, CITY_NOTICE_UNRECOGNIZED_CITY_FORMAT, CITY_FILE_ERROR_RESOURCE_ONLY);
    return false;
  }
  if (CityFileInputRead(input, length, 4) == 0) {
    ReportCityFileError(file, CITY_NOTICE_ERROR_READING_FILE, CITY_FILE_ERROR_WITH_CAUSE);
    return false;
  }
  *length = ReadBE32((const u8 *)length);
  if (*length == 0 && city->game->repair_save_headers) {
    // Upstream L_OpenCityHeader repairs only .sc2's missing FORM length.
    // Keep subsequent reads and partial City writes on the ordinary load path.
    const char *extension = strrchr(path, '.');
    int position = CityFileInputPosition(input);
    int size = -1;
    bool supported = extension && extension[0] == '.' &&
        (extension[1] == 's' || extension[1] == 'S') &&
        (extension[2] == 'c' || extension[2] == 'C') && extension[3] == '2' && extension[4] == 0;
    if (supported && position >= 0 && CityFileInputSeek(input, 0, SEEK_END) == 0) {
      size = CityFileInputPosition(input);
      if (CityFileInputSeek(input, position, SEEK_SET) != 0) size = -1;
    }
    if (size < 12) {
      ReportCityFileError(file, CITY_NOTICE_ERROR_READING_FILE, CITY_FILE_ERROR_WITH_CAUSE);
      return false;
    }
    *length = size - 8;
    fprintf(stderr, "Recovered zero-length FORM header in %s using %d bytes; source file unchanged.\n", path, size);
  }
  if (CityFileInputRead(input, &tag, 4) == 0) {
    ReportCityFileError(file, CITY_NOTICE_ERROR_READING_FILE, CITY_FILE_ERROR_WITH_CAUSE);
    return false;
  }
  tag = ReadBE32((const u8 *)&tag);
  if (tag != ReadBE32((const u8 *)"SCDH")) {
    ReportCityFileError(file, CITY_NOTICE_INVALID_CITY_CONTENTS, CITY_FILE_ERROR_RESOURCE_ONLY);
    return false;
  }
  return true;
}

// Name

// Host filenames already use native UTF-8. Path must be terminated host text; copies the derived name into City-owned
// storage without retaining a pointer into the filename.
void AssignCityNameFromHostFilename(City *city, const char *path) {
  const char *name = PathFilename(path);
  AssignCityName(city, strlen(name), (const u8 *)name);
}
