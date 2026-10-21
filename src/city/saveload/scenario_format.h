#ifndef SC2K_FILE_CITY_SCENARIO_FORMAT_H
#define SC2K_FILE_CITY_SCENARIO_FORMAT_H

#include "city/engine/city_types.h"

enum { CITY_SCENARIO_RECORD_SIZE = 52, CITY_MISSION_RECORD_SIZE = 48 };

// Canonical little-endian memory record; SCEN's field swapping stays in its loader.
void EncodeCityScenarioMetadata(const CityScenarioMetadata *state, u8 record[CITY_SCENARIO_RECORD_SIZE]);
void DecodeCityScenarioMetadata(CityScenarioMetadata *state, const u8 record[CITY_SCENARIO_RECORD_SIZE]);

#endif
