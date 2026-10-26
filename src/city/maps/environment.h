#ifndef SC2K_CITY_MAPS_ENVIRONMENT_H
#define SC2K_CITY_MAPS_ENVIRONMENT_H

#include "city/types.h"

// Full scan state lives in map_types.h; callers retain it across bounded row budgets.
typedef struct CityEnvironmentScan CityEnvironmentScan;

// Add land and industrial influence to the existing quarter-resolution scratch fields and replace zone_count.
// Requires complete native tiles and exclusive scratch ownership. Retains visited flags and visits tiles in map
// order, including reads affected by earlier coarse visited writes. Does not run the full environment pipeline.
void AccumulateCityLandInfluence(City *city);

// Environment passes require complete native tiles and exclusive use of City scratch until complete.
// A zero-initialized scan starts a pass; retain it between bounded row budgets.
// False means work remains, true means the derived layers are complete.
bool StepCityEnvironment(City *city, CityEnvironmentScan *scan, int rows);
void UpdateCityEnvironmentMaps(City *city);

#endif
