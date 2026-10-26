#include "city/saveload/scenario_format.h"

#include <stddef.h>
#include <string.h>

#include "base/byte_order.h"

typedef struct {
  u16 state_offset;
  u8 record_offset;
  u8 width;
  u8 count;
} ScenarioField;

#define FIELD(member, record_offset, width, count) \
    {offsetof(CityScenarioMetadata, member), record_offset, width, count}
// One schema maps naturally aligned City fields to the fixed 52-byte record.
// Stock SCEN swaps its numeric fields; canonical records used by the native codecs are little-endian.
static const ScenarioField kScenarioFields[] = {
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

// Encode mission state into the canonical little-endian scenario record. Output must hold CITY_SCENARIO_RECORD_SIZE
// writable bytes; input state remains unchanged.
void EncodeCityScenarioMetadata(const CityScenarioMetadata *state, u8 record[CITY_SCENARIO_RECORD_SIZE]) {
  for (size_t field_index = 0; field_index < sizeof(kScenarioFields) / sizeof(kScenarioFields[0]); ++field_index) {
    const ScenarioField *field = &kScenarioFields[field_index];
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

// Decode one complete canonical little-endian scenario record into caller-owned mission metadata. Input must hold
// CITY_SCENARIO_RECORD_SIZE readable bytes; this does not activate a mission or read a file itself.
void DecodeCityScenarioMetadata(CityScenarioMetadata *state, const u8 record[CITY_SCENARIO_RECORD_SIZE]) {
  for (size_t field_index = 0; field_index < sizeof(kScenarioFields) / sizeof(kScenarioFields[0]); ++field_index) {
    const ScenarioField *field = &kScenarioFields[field_index];
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

// Convert the stock record's numeric fields to canonical byte order. Byte fields stay unchanged.
// The complete buffer is already seeded from City, including any tail untouched by a short successful read.
void ConvertStockCityScenarioRecord(u8 record[CITY_SCENARIO_RECORD_SIZE]) {
  for (size_t field_index = 0; field_index < sizeof(kScenarioFields) / sizeof(kScenarioFields[0]); ++field_index) {
    const ScenarioField *field = &kScenarioFields[field_index];
    for (int index = 0; index < field->count; ++index) {
      u8 *value = record + field->record_offset + index * field->width;
      if (field->width == 2) WriteLE16(value, ReadBE16(value));
      else if (field->width == 4) WriteLE32(value, ReadBE32(value));
    }
  }
}

