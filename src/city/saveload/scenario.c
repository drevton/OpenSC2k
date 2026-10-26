#include "city/saveload/file.h"

#include "city/saveload/runtime.h"
#include "city/disasters/disasters.h"
#include "city/saveload/types.h"
#include "city/presentation.h"
#include "game/state.h"
#include <stdio.h>
#include <string.h>

#include "base/byte_order.h"
#include "city/saveload/scenario_format.h"
#include "city/saveload/summary.h"
#include "city/saveload/input.h"
#include "city/saveload/services.h"
#include "city/saveload/legacy_text.h"
#include "city/state.h"

// Close the scenario input captured by a registered cleanup record. Context must remain alive until cleanup; this
// follows the stream's real close/unwind path rather than suppressing errors.
static void CloseScenarioMetadataInput(void *context) {
  CityLoadStream **input = context;
  if (*input != NULL) CityFileCloseStream(*input);
}

// Load a city, then its version-128 SCEN block. A successful City load sets the mission flag even if metadata fails.
// Own the input through registered cleanup; only successful decoding and close reach the pending-disaster updates.
bool LoadCityScenarioMetadata(City *city, const char *path) {
  CityLoadStream *input = NULL;
  bool loaded = false;
  CityFileCleanup cleanup = {CloseScenarioMetadataInput, &input, NULL};
  PushCityFileCleanup(city->game->file, &cleanup);
  city->mission.active = LoadCityFile(city, path);
  if (!city->mission.active) goto done;
  input = CityFileOpenStream(city->game->file, path);
  if (input == NULL) {
    ReportCityFileError(city->game->file, CITY_NOTICE_ERROR_OPENING_FILE, CITY_FILE_ERROR_RESOURCE_ONLY);
    goto done;
  }
  CityScenarioMetadata *scenario_metadata = &city->mission.metadata;
  u8 metadata[CITY_SCENARIO_RECORD_SIZE];
  EncodeCityScenarioMetadata(scenario_metadata, metadata);
  // Preserve ordinary partial writes and the untouched short-record tail.
  // Oversized records retain the existing bounded-prefix replacement.
  if (CityFileReadTaggedStream(input, "SCEN", CITY_SCENARIO_METADATA_VERSION,
      metadata, sizeof(metadata)) == NULL) {
    DecodeCityScenarioMetadata(scenario_metadata, metadata);
    ReportCityFileError(city->game->file,
        CITY_NOTICE_ERROR_EXTRACTING_SCENARIO_RESOURCE, CITY_FILE_ERROR_RESOURCE_ONLY);
    goto done; // The registered cleanup closes the stream.
  }
  ConvertStockCityScenarioRecord(metadata);
  DecodeCityScenarioMetadata(scenario_metadata, metadata);
  CityLoadStream *completed_input = input;
  input = NULL; // Close consumes the wrapper even when its OS close throws.
  CityFileCloseStream(completed_input);
  city->disasters.pending_type = scenario_metadata->disaster_type;
  SetDisasterLocation(&city->disasters.location, scenario_metadata->disaster_x, scenario_metadata->disaster_y);
  loaded = true;
done:
  PopCityFileCleanup(city->game->file, &cleanup);
  CloseScenarioMetadataInput(&input);
  return loaded;
}

// Preview

// Open a scenario input through the active file runtime's load-stream service. Returns an owned wrapper or NULL;
// matching CityFileCloseStream and operation cleanup retain the usual observed I/O/error behavior.
CityLoadStream *CityFileOpenStream(CityFileRuntime *file, const char *path) {
  return OpenCityLoadStream(file, path);
}

// Close a scenario/load stream through the shared throwing close service. Stream must be an open wrapper from the
// matching file runtime and becomes invalid even when close throws.
void CityFileCloseStream(CityLoadStream *stream) {
  CloseCityLoadStream(stream);
}

// Read a versioned stock chunk through the selected borrowed input. Tag provides exactly four bytes;
// output must hold capacity writable bytes;
// returns NULL for rejected/missing data and restores the initial stream position through its backend's seek path.
static u8 *ReadTaggedChunk(CityFileInput *input, const char tag[4], int version, u8 *output, size_t capacity) {
  int initial = CityFileInputPosition(input);
  if (initial == -1)
    return NULL;
  char header[4] = {0};
  u8 word[4];
  u8 *result = NULL;
  if (CityFileInputRead(input, header, 4) != 4 || memcmp(header, "FORM", 4) != 0 ||
      CityFileInputRead(input, word, 4) != 4 || CityFileInputRead(input, header, 4) != 4 ||
      memcmp(header, "SCDH", 4) != 0)
    goto done;
  u32 position = 12;
  for (;;) {
    if (CityFileInputRead(input, header, 4) != 4)
      break;
    position += 4;
    if (memcmp(header, tag, sizeof(header)) != 0) {
      if (CityFileInputRead(input, word, 4) != 4)
        break;
      position += 4 + ReadBE32(word);
      if (CityFileInputSeek(input, (int)position, SEEK_SET) != 0)
        break;
      continue;
    }
    if (CityFileInputRead(input, word, 4) != 4)
      break;
    position += 4;
    u32 chunk_size = ReadBE32(word);
    if (chunk_size < 4)
      break; // Reject a missing version instead of reproducing stock buffer-overrun behavior.
    u32 bytes = chunk_size - 4;
    if (CityFileInputRead(input, word, 4) != 4)
      break;
    position += 4;
    if (ReadLE32(word) != (u32)version) {
      if (CityFileInputSeek(input, (int)bytes, SEEK_CUR) != 0)
        break;
      continue;
    }
    bool text = memcmp(tag, "TEXT", 4) == 0;
    bool scenario = memcmp(tag, "SCEN", 4) == 0;
    bool picture = memcmp(tag, "PICT", 4) == 0;
    if (text) {
      if (bytes > INT32_MAX)
        break; // A signed-negative byte count bypasses the stock output cap.
      if (bytes > CITY_SCENARIO_INSTRUCTION_BYTES)
        bytes = CITY_SCENARIO_INSTRUCTION_BYTES;
      if ((size_t)bytes + 1 <= capacity && CityFileInputRead(input, output, bytes) == bytes) {
        output[bytes] = 0;
        result = output;
      }
      break;
    }
    if (scenario || picture) {
      if (bytes > INT32_MAX)
        break;
      if (picture && bytes > CITY_SCENARIO_PICTURE_BYTES)
        bytes = CITY_SCENARIO_PICTURE_BYTES;
      if ((size_t)bytes <= capacity) {
        if (CityFileInputRead(input, output, bytes) == bytes) result = output;
      } else if (scenario) {
        // Retain the existing defined SCEN overflow replacement: accept the
        // first destination-sized prefix and consume the tail without writing past
        // the destination. Ordinary stock-sized reads stay identical.
        u8 discarded[256];
        u32 remaining = bytes - (u32)capacity;
        if (CityFileInputRead(input, output, (u32)capacity) != capacity) break;
        while (remaining != 0) {
          u32 count = remaining < sizeof(discarded) ? remaining : sizeof(discarded);
          if (CityFileInputRead(input, discarded, count) != count) break;
          remaining -= count;
        }
        if (remaining == 0) result = output;
      }
      break;
    }
  }
done:
  CityFileInputSeek(input, initial, SEEK_SET);
  return result;
}

// Find and decode a tagged chunk through an already opened observed-load stream. Output must provide capacity writable
// bytes; returns its payload pointer or NULL and propagates throwing backend errors.
u8 *CityFileReadTaggedStream(CityLoadStream *input, const char tag[4], int version, u8 *output, size_t capacity) {
  CityFileInput file = {.kind = CITY_FILE_INPUT_PLATFORM, .load_stream = input};
  return ReadTaggedChunk(&file, tag, version, output, capacity);
}

// Canvas previews use bounded buffers and do not mutate City state. Picture/text must provide their stated capacities;
// returns a read-only preview result and preserves backend seek/error semantics.
bool ReadCityPreview(FILE *stream, u8 *picture, size_t picture_capacity, char *description, size_t text_capacity) {
  CityFileInput input = {.kind = CITY_FILE_INPUT_STDIO, .stdio_stream = stream};
  bool found = ReadTaggedChunk(&input, "PICT", CITY_SCENARIO_PREVIEW_VERSION, picture, picture_capacity) != NULL;
  ReadTaggedChunk(&input, "TEXT", CITY_SCENARIO_PREVIEW_VERSION, (u8 *)description, text_capacity);
  if (text_capacity != 0) description[text_capacity - 1] = 0;
  return found;
}

// Scenario Import
// Host presentation of the original scenario menu's version-129 instruction block.

// Read scenario instructions; the host owns the file and canvas lifetimes. Output must provide 769 bytes; opens/closes
// its own input and returns false without changing live City state on invalid data.
bool ReadCityScenarioInstructions(const char *path, char text[CITY_SCENARIO_INSTRUCTION_BYTES + 1]) {
  text[0] = '\0';
  FILE *stream = fopen(path, "rb");
  if (stream == NULL)
    return false;
  CityFileInput input = {.kind = CITY_FILE_INPUT_STDIO, .stdio_stream = stream};
  u8 *result = ReadTaggedChunk(&input, "TEXT", CITY_SCENARIO_INSTRUCTIONS_VERSION, (u8 *)text,
      CITY_SCENARIO_INSTRUCTION_BYTES + 1);
  fclose(stream);
  if (result == NULL)
    return false;
  for (i32 index = 0; text[index] != '\0'; ++index)
    if (text[index] == '\r' || text[index] == '\t')
      text[index] = ' ';
  DecodeCityScenarioText(text, CITY_SCENARIO_INSTRUCTION_BYTES + 1);
  return text[0] != '\0';
}
