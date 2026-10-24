#ifndef SC2K_CITY_MAPS_ENVIRONMENT_H
#define SC2K_CITY_MAPS_ENVIRONMENT_H

#include "city/types.h"

// Retained environment stage/cursor and city-center sums. Zero initializes the pollution-input stage.
// City owns this record for incremental work; whole-pass callers can keep it on the stack.
typedef struct CityEnvironmentScan {
  int row, stage, count, center_x, center_y;
  i64 sum_x, sum_y;
} CityEnvironmentScan;

// Environment passes require exclusive use of City scratch until complete. A zero-initialized scan starts a pass;
// retain it between bounded row budgets. False means work remains, true means the derived layers are complete.
bool StepCityEnvironment(City *city, CityEnvironmentScan *scan, int rows);
void UpdateCityEnvironmentMaps(City *city);

#endif
