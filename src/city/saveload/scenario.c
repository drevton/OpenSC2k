#include "city/saveload/runtime.h"
#include "city/disasters/disasters.h"
#include "city/saveload/types.h"
#include "city/presentation.h"
#include "game/state.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "base/byte_order.h"
#include "city/saveload/scenario_format.h"
#include "city/saveload/city_file_internal.h"
#include "city/saveload/services.h"
#include "city/saveload/legacy_text.h"
#include "city/saveload/file.h"
#include "base/text.h"
#include "city/state.h"

// Scenario

static const CityScenarioWordField k_scenario_word_fields[] = {
  {0}, {4}, {30}, {32}, {48}, {50}
};

static const CityScenarioWordField k_scenario_dword_fields[] = {
  {6}, {10}, {14}, {18}, {22}, {26}, {34}, {38}, {42}
};

// Close the scenario input captured by a registered cleanup record. Context must remain alive until cleanup; this
// follows the stream's real close/unwind path rather than suppressing errors.
static void CloseScenarioMetadataInput(void *context) {
  void **input = context;
  if (*input != NULL) CityFileCloseStream(*input);
}

// Load a city, read a version-128 SCEN block, and activate the mission. Owns its opened stream and registered cleanup;
// false retains load failure state and must not activate a partially decoded mission.
bool LoadCityScenarioMetadata(City *city, const char *path) {
  void *input = NULL;
  i32 result = 0;
  CityFileCleanup cleanup = {CloseScenarioMetadataInput, &input, NULL};
  PushCityFileCleanup(city->game->file, &cleanup);
  city->mission.active = LoadCityFile(city, path);
  if (city->mission.active == 0) {
    goto done;
  }
  input = CityFileOpenStream(city->game->file, path);
  if (input == NULL) {
    ReportCityChunkError(city->game->file, CITY_NOTICE_ERROR_OPENING_FILE, 1);
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
    ReportCityChunkError(city->game->file, CITY_NOTICE_ERROR_EXTRACTING_SCENARIO_RESOURCE, 1);
    goto done; // CStdioFile destructor follows EndWaitCursor.
  }
  for (size_t index = 0; index < sizeof(k_scenario_word_fields) / sizeof(k_scenario_word_fields[0]);
       ++index) {
    int offset = k_scenario_word_fields[index].offset;
    WriteLE16(metadata + offset, ReadBE16(metadata + offset));
  }
  for (size_t index = 0; index < sizeof(k_scenario_dword_fields) / sizeof(k_scenario_dword_fields[0]);
       ++index) {
    int offset = k_scenario_dword_fields[index].offset;
    WriteLE32(metadata + offset, ReadBE32(metadata + offset));
  }
  DecodeCityScenarioMetadata(scenario_metadata, metadata);
  void *completed_input = input;
  input = NULL; // Close consumes the wrapper even when its OS close throws.
  CityFileCloseStream(completed_input);
  city->disasters.pending_type = scenario_metadata->disaster_type;
  SetDisasterLocation(&city->disasters.location, scenario_metadata->disaster_x, scenario_metadata->disaster_y);
  result = 1;
done:
  PopCityFileCleanup(city->game->file, &cleanup);
  CloseScenarioMetadataInput(&input);
  return result;
}

// Scenario Format

typedef struct {
  u16 state_offset;
  u8 record_offset;
  u8 width;
  u8 count;
} ScenarioField;

#define FIELD(member, record_offset, width, count) \
    {offsetof(CityScenarioMetadata, member), record_offset, width, count}
static const ScenarioField fields[] = {
  FIELD(disaster_type, 0, 2, 1),
  FIELD(disaster_y, 2, 1, 1),
  FIELD(disaster_x, 3, 1, 1),
  FIELD(criteria.remaining_ticks, 4, 2, 1),
  FIELD(criteria.minimum_population, 6, 4, 1),
  FIELD(criteria.minimum_sector_metric, 10, 4, 3),
  FIELD(criteria.minimum_net_funds, 22, 4, 1),
  FIELD(criteria.minimum_city_metric, 26, 4, 1),
  FIELD(criteria.minimum_service_metric, 30, 2, 2),
  FIELD(criteria.maximum_environment_metric, 34, 4, 3),
  FIELD(criteria.statistic_index, 46, 1, 2),
  FIELD(criteria.minimum_statistic, 48, 2, 2),
};
#undef FIELD

// Encode mission/scenario state into one fixed stock record. Record must hold CITY_SCENARIO_RECORD_SIZE bytes; numbers
// use the stock format's explicit byte order and state remains unchanged.
void EncodeCityScenarioMetadata(const CityScenarioMetadata *state, u8 record[CITY_SCENARIO_RECORD_SIZE]) {
  for (int field_index = 0; field_index < sizeof(fields) / sizeof(fields[0]); ++field_index) {
    const ScenarioField *field = &fields[field_index];
    for (int index = 0; index < field->count; ++index) {
      const u8 *source = (const u8 *)state + field->state_offset + index * field->width;
      u32 value = 0;
      if (field->width == 4) {
        memcpy(&value, source, sizeof(value));
      } else if (field->width == 2) {
        u16 word;
        memcpy(&word, source, sizeof(word));
        value = word;
      } else {
        value = *source;
      }
      for (int byte = 0; byte < field->width; ++byte)
        record[field->record_offset + index * field->width + byte] = value >> (8 * byte);
    }
  }
}

// Decode one complete stock scenario record into caller-owned mission metadata. Input must hold
// CITY_SCENARIO_RECORD_SIZE readable bytes; this does not activate a mission or read a file itself.
void DecodeCityScenarioMetadata(CityScenarioMetadata *state, const u8 record[CITY_SCENARIO_RECORD_SIZE]) {
  for (int field_index = 0; field_index < sizeof(fields) / sizeof(fields[0]); ++field_index) {
    const ScenarioField *field = &fields[field_index];
    for (int index = 0; index < field->count; ++index) {
      u32 value = 0;
      for (int byte = 0; byte < field->width; ++byte)
        value |= (u32)record[field->record_offset + index * field->width + byte] << (8 * byte);
      u8 *destination = (u8 *)state + field->state_offset + index * field->width;
      if (field->width == 4) {
        memcpy(destination, &value, sizeof(value));
      } else if (field->width == 2) {
        u16 word = value;
        memcpy(destination, &word, sizeof(word));
      } else {
        *destination = value;
      }
    }
  }
}

// Preview

// Open a scenario input through the active file runtime's load-stream service. Returns an owned wrapper or NULL;
// matching CityFileCloseStream and operation cleanup retain the usual observed I/O/error behavior.
void *CityFileOpenStream(CityFileRuntime *file, const char *path) {
  return OpenCityLoadStream(file, path);
}

// Close a scenario/load stream through the shared throwing close service. Stream must be an open wrapper from the
// matching file runtime and becomes invalid even when close throws.
void CityFileCloseStream(void *stream) {
  CloseCityLoadStream(stream);
}

// Original raw tagged-chunk reader; FILE supplies the excluded CFile services. Output must hold capacity bytes;
// returns NULL for rejected/missing data and restores the initial stream position through its backend's seek path.
static u8 *ReadTaggedChunk(CityFileInput *input, const char *tag, int version, u8 *output, size_t capacity) {
  int initial = CityFileInputPosition(input);
  if (initial == -1)
    return NULL;
  char header[5] = {0};
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
    if (strcmp(header, tag) != 0) {
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
    if (ReadLE32(word) != (u32)(int)version) {
      if (CityFileInputSeek(input, (int)bytes, SEEK_CUR) != 0)
        break;
      continue;
    }
    if (strncmp(tag, "TEXT", 4) == 0) {
      if ((int)bytes < 0)
        break; // A signed-negative byte count bypasses the stock output cap.
      if ((int)bytes > CITY_SCENARIO_INSTRUCTION_BYTES)
        bytes = CITY_SCENARIO_INSTRUCTION_BYTES;
      if ((size_t)bytes + 1 <= capacity && CityFileInputRead(input, output, bytes) == bytes) {
        output[bytes] = 0;
        result = output;
      }
      break;
    }
    if (strncmp(tag, "SCEN", 4) == 0 || strncmp(tag, "PICT", 4) == 0) {
      if ((int)bytes < 0)
        break;
      if (strncmp(tag, "PICT", 4) == 0 && (int)bytes > 4300)
        bytes = 4300;
      if ((size_t)bytes <= capacity) {
        if (CityFileInputRead(input, output, bytes) == bytes) result = output;
      } else if (strncmp(tag, "SCEN", 4) == 0) {
        // Retain the existing defined SCEN overflow replacement: accept the
        // first destination-sized prefix, consume the tail and keep adjacent
        // guest fields untouched. Ordinary stock-sized reads stay identical.
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

// Find a versioned stock chunk through a borrowed stdio input and decode it into caller storage. Tag must provide four
// bytes and output enough space for the admitted chunk; returns the payload pointer or NULL.
u8 *ReadCityFileChunkByTag(FILE *input, const char *tag, int version, u8 *output) {
  CityFileInput file = {.kind = CITY_FILE_INPUT_STDIO, .stream = input};
  return ReadTaggedChunk(&file, tag, version, output, SIZE_MAX);
}
// Find and decode a tagged chunk through an already opened observed-load stream. Output must provide capacity writable
// bytes; returns its payload pointer or NULL and propagates throwing backend errors.
u8 *CityFileReadTaggedStream(void *input, const char *tag, int version, u8 *output, size_t capacity) {
  CityFileInput file = {.kind = CITY_FILE_INPUT_PLATFORM, .stream = input};
  return ReadTaggedChunk(&file, tag, version, output, capacity);
}

// Canvas previews use bounded buffers and do not mutate City state. Picture/text must provide their stated capacities;
// returns a read-only preview result and preserves backend seek/error semantics.
bool ReadCityPreview(FILE *stream, u8 *picture, size_t picture_capacity, char *description, size_t text_capacity) {
  CityFileInput input = {.kind = CITY_FILE_INPUT_STDIO, .stream = stream};
  bool found = ReadTaggedChunk(&input, "PICT", CITY_SCENARIO_PREVIEW_VERSION, picture, picture_capacity) != NULL;
  ReadTaggedChunk(&input, "TEXT", CITY_SCENARIO_PREVIEW_VERSION, (u8 *)description, text_capacity);
  if (text_capacity != 0) description[text_capacity - 1] = 0;
  return found;
}

// Scenario Import
// Host presentation of the original scenario menu's version-129 instruction block.

// Read scenario instructions; the host owns the file and canvas lifetimes. Output must provide 769 bytes; opens/closes
// its own input and returns false without changing live City state on invalid data.
bool ReadCityScenarioInstructions(const char *path, char text[769]) {
  text[0] = '\0';
  FILE *input = fopen(path, "rb");
  if (input == NULL)
    return false;
  u8 *result = ReadCityFileChunkByTag(input, "TEXT", 129, (u8 *)text);
  fclose(input);
  if (result == NULL)
    return false;
  for (i32 index = 0; text[index] != '\0'; ++index)
    if (text[index] == '\r' || text[index] == '\t')
      text[index] = ' ';
  DecodeCityScenarioText(text, 769);
  return text[0] != '\0';
}
