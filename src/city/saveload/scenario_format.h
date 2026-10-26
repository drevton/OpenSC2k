#ifndef SC2K_FILE_CITY_SCENARIO_FORMAT_H
#define SC2K_FILE_CITY_SCENARIO_FORMAT_H

#include "city/engine/city_types.h"

enum { CITY_SCENARIO_RECORD_SIZE = 52, CITY_MISSION_RECORD_SIZE = 48 };

// Encode/decode one complete canonical little-endian record without file I/O or mission activation.
// Record buffers hold CITY_SCENARIO_RECORD_SIZE bytes. These operations do not allocate or retain the buffers.
void EncodeCityScenarioMetadata(const CityScenarioMetadata *state, u8 record[CITY_SCENARIO_RECORD_SIZE]);
void DecodeCityScenarioMetadata(CityScenarioMetadata *state, const u8 record[CITY_SCENARIO_RECORD_SIZE]);

// Convert one complete stock SCEN record to canonical byte order in place; byte fields stay unchanged.
// The loader seeds the buffer from live metadata first and converts it only after a successful tagged read.
void ConvertStockCityScenarioRecord(u8 record[CITY_SCENARIO_RECORD_SIZE]);

#endif
