#include "city/saveload/services.h"

#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base/file.h"
#include "city/saveload/runtime.h"
#include "city/saveload/state.h"
#include "game/errors.h"
#include "game/observation.h"
#include "game/state.h"

// Host reads and exception unwinding for complete Open/Load callers.

typedef struct CityFileExceptionFrame {
  jmp_buf jump;
  CityFileCleanup *cleanup;
  CityFileRuntime *file;
  struct CityFileExceptionFrame *previous;
} CityFileExceptionFrame;

struct CityLoadStream {
  FILE *stream;
  CityFileRuntime *owner;
};

// Unwind registered file cleanups to the active command frame and transfer control to its handler. Never returns; an
// unhandled exception or an exception during cleanup aborts instead of masquerading as EOF.
static _Noreturn void ThrowFileException(CityFileRuntime *owner) {
  // No EOF/status substitute for an unhandled exception or a second exception
  // from a destructor during unwinding (the original CRT terminates).
  if (owner->load_io.exception_frame == NULL || owner->load_io.unwinding) abort();
  owner->load_io.unwinding = 1;
  UnwindCityFileCleanups(owner->load_io.exception_frame->file,
      owner->load_io.exception_frame->cleanup);
  owner->load_io.unwinding = 0;
  longjmp(owner->load_io.exception_frame->jump, 1);
}

// Run a file action with a private scratch pool, cleanup boundary and exception frame. Context is borrowed; nested
// calls retain their parent's buffers, and errors unwind partial work before reporting without rolling City back.
void RunCityFileCommand(CityFileRuntime *file, CityFileAction action, void *context) {
  CityFileExceptionFrame frame;
  CityFileScratch scratch = {0};
  CityFileScratch *previous_scratch = file->scratch;
  CityMiscBuffer *previous_misc = file->misc;
  bool previous_misc_active = file->misc_active, previous_misc_owned = file->misc_owned;
  file->scratch = &scratch;
  file->misc = NULL;
  file->misc_active = file->misc_owned = false;
  frame.cleanup = CityFileCleanupTop(file);
  frame.file = file;
  frame.previous = file->load_io.exception_frame;
  file->load_io.exception_frame = &frame;
  if (setjmp(frame.jump) == 0) action(context);
  else {
    ReleaseCityMiscWorkspace(file);
    file->misc = previous_misc;
    file->misc_active = previous_misc_active;
    file->misc_owned = previous_misc_owned;
    file->scratch = previous_scratch;
    // Forward other file exceptions to the application command handler.
    // The transient exception and CRT dispatch belong to this host boundary;
    // Preserve the optional exception from a failed Open command.
    // This command's buffers leave scope after unwinding. Nested commands
    // never share or reset their parent's pool.
    ReportCityFileCommandFailure(file->game->city);
  }
  file->load_io.exception_frame = frame.previous;
  if (file->misc != previous_misc) ReleaseCityMiscWorkspace(file);
  file->misc = previous_misc;
  file->misc_active = previous_misc_active;
  file->misc_owned = previous_misc_owned;
  file->scratch = previous_scratch;
}

// Resolve a load path against the operation's retained directory without changing the process directory. Output
// receives the file helper's allocated native path on success; the caller releases it.
static bool ResolveLoadPath(const CityFileRuntime *owner, const char *path, char **native) {
  return ResolveFilePath(path, CityFileDirectory(owner), native);
}

// Resolve and open a path within the file operation's directory context. Returns an owned stream wrapper or NULL with
// the recorded open error; close it through CloseCityLoadStream before ending the operation.
CityLoadStream *OpenCityLoadStream(CityFileRuntime *owner, const char *path) {
  char *native = NULL;
  errno = 0;
  FILE *file = ResolveLoadPath(owner, path, &native) ? fopen(native, "rb") : NULL;
  int error = file != NULL ? 0 : errno != 0 ? errno : EINVAL;
  free(native);
  owner->load_io.open_error = error;
  if (file == NULL) {
    ObserveCityFileIO(owner, NATIVE_LOAD_OPEN, 0, 0, error, path, (u32)strlen(path));
    return NULL;
  }
  CityLoadStream *input = malloc(sizeof(*input));
  if (input == NULL) {
    fclose(file);
    SetGameError(owner->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    owner->load_io.open_error = ENOMEM;
    ObserveCityFileIO(owner, NATIVE_LOAD_OPEN, 0, 0, ENOMEM, path, (u32)strlen(path));
    return NULL;
  }
  *input = (CityLoadStream){file, owner};
  ++owner->load_io.load_stream_count;
  ObserveCityFileIO(owner, NATIVE_LOAD_OPEN, 0, 1, 0, path, (u32)strlen(path));
  return input;
}

// Fill the caller's file-exception record from the last load-open OS error. Owner and exception must be valid; this
// does not open a file, clear the stored error or display a dialog.
void FillCityLoadOpenError(const CityFileRuntime *owner, CityFileExceptionState *exception) {
  exception->cause = CITY_FILE_CAUSE_GENERIC;
  exception->os_error = owner->load_io.open_error;
}

// Read up to size bytes into caller storage and record the actual I/O outcome. Requires an open load stream and enough
// destination space; short EOF returns a count, while filesystem errors unwind through the active file exception
// frame.
u32 ReadCityLoadStream(CityLoadStream *input, void *destination, u32 size) {
  FILE *file = input->stream;
  errno = 0;
  u32 copied = (u32)fread(destination, 1, size, file);
  int error = ferror(file) ? errno != 0 ? errno : EIO : 0;
  // A short EOF read is an ordinary CFile result, not a filesystem exception.
  ObserveCityFileIO(input->owner, NATIVE_LOAD_READ, size, copied, error, destination, copied);
  if (error != 0) ThrowFileException(input->owner);
  return copied;
}

// Seek an open load stream and return its representable 32-bit position. OS errors and the UINT32_MAX sentinel throw
// through the active file frame; callers must not turn those into ordinary EOF.
u32 SeekCityLoadStream(CityLoadStream *file, int offset, int origin) {
  errno = 0;
  int seek = fseek(file->stream, offset, origin);
  long position = seek == 0 ? ftell(file->stream) : -1;
  int error = position < 0 ? errno != 0 ? errno : EIO : (u64)position > 0xFFFFFFFFu ? EOVERFLOW : 0;
  u32 result = position < 0 || (u64)position > 0xFFFFFFFFu ? 0xFFFFFFFFu : (u32)position;
  u8 input[8];
  u32 values[2] = {(u32)offset, (u32)origin};
  for (int index = 0; index < 8; ++index) input[index] = (u8)(values[index / 4] >> (8 * (index % 4)));
  ObserveCityFileIO(file->owner, NATIVE_LOAD_SEEK, 0, result, error, input, sizeof(input));
  // The sentinel throws even when the OS did not report an error.
  if (error != 0 || result == UINT32_MAX) ThrowFileException(file->owner);
  return result;
}

// Seek a preview using the throwing load-stream path and discard its position result. Requires an open load stream;
// zero is returned only after a successful seek, and failures unwind rather than returning an error status.
int SeekCityPreviewStream(CityLoadStream *stream, int offset, int origin) {
  SeekCityLoadStream(stream, offset, origin);
  return 0; // The CFile position result is discarded by the preview reader.
}
// Query the current load-stream position through the same observed seek path. Requires an open stream; seek failures
// throw and the returned bits retain the original signed projection.
int GetCityPreviewStreamPosition(CityLoadStream *stream) {
  return (int)(SeekCityLoadStream(stream, 0, SEEK_CUR));
}

// Close and release an owned load-stream wrapper, decrementing the operation's open-stream count. The wrapper is
// invalid after this call even if close fails and throws through the command frame.
void CloseCityLoadStream(CityLoadStream *input) {
  CityFileRuntime *owner = input->owner;
  errno = 0;
  bool result = fclose(input->stream) == 0;
  int error = result ? 0 : errno != 0 ? errno : EIO;
  --owner->load_io.load_stream_count;
  ObserveCityFileIO(owner, NATIVE_LOAD_CLOSE, 0, (u32)result, error, NULL, 0);
  free(input);
  if (error != 0) ThrowFileException(owner);
}

// Validate and replace the operation's relative-path directory context. Returns zero on success and -1 on failure,
// recording the I/O outcome; asset lookup and the process working directory remain separate.
int ChangeCityLoadDirectory(CityFileRuntime *owner, const char *path) {
  char *native = NULL;
  HostFileInfo info;
  errno = 0;
  bool success = ResolveLoadPath(owner, path, &native) && ReadHostFileInfo(native, &info) && info.directory;
  int error = success ? 0 : errno != 0 ? errno : EINVAL;
  if (success) SetCityFileDirectory(owner, native);
  free(native);
  // Keep resource lookup rooted at the app's asset directory. CFile uses this
  // separate OS working-directory context for relative city paths.
  ObserveCityFileIO(owner, NATIVE_LOAD_DIRECTORY, 0, success ? 0 : 0xFFFFFFFFu, error, path, (u32)strlen(path));
  return success ? 0 : -1;
}
