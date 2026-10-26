#ifndef SC2K_CITY_INFRASTRUCTURE_UTILITIES_H
#define SC2K_CITY_INFRASTRUCTURE_UTILITIES_H

#include "city/infrastructure/types.h"

// Read-only supply for a valid water-pump tile, in simulation units.
int CityWaterPumpSupply(const City *city, TileIndex tile);

// Whole-pass scans require initialized map/utility layers and exclusive use of the route queue and visited flags.
// They complete supply/distribution and publish the resulting totals without leaving scheduled work pending.
// Map dimensions and backing remain stable throughout the pass; these scans do not run reentrant presentation.
void ScanPowerGridPhase(City *city);
void ScanCityWaterNetwork(City *city);

// Advance a scheduled scan using the caller's retained work and budget. Initialize work to zero for a new scan;
// false leaves it suspended, true means the scan's totals are final. Do not overlap scans or other route-queue users.
// Finish the work before map edits/replacement. Reset counts sections; source discovery counts scan positions,
// with water positions following the selected rotation. Component work retains the next source position unchanged.
bool StepCityPowerGrid(City *city, CityUtilityScan *work, int budget);
bool StepCityWaterNetwork(City *city, CityUtilityScan *work, int budget);

#endif
