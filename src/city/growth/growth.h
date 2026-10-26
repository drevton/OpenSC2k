#ifndef SC2K_DOMAIN_SRC_CITY_GROWTH_GROWTH_H
#define SC2K_DOMAIN_SRC_CITY_GROWTH_GROWTH_H

#include "city/types.h"

// Tables

// Fixed growth-selection lookups are independent of the reference executable.
extern const u8 kGrowthBaseTable[21];
extern const u8 kTileScanMaskTable[4];

// Zoning

// Visit one interleaved quarter of the map for zoning growth, infrastructure maintenance and facility activity.
// Requires complete native tiles. Offsets are 0..3; first/count are multiples of 32 quarter-grid positions.
// Batches preserve row/section/RNG order.
void ScanCityTileBatch(City *city, int quarter_x, int quarter_y,
    int first, int count, bool large_city_effect);
void ScanQuarterCityTiles(City *city, int quarter_x, int quarter_y);

void GrowActiveZoneTiles(City *city, TileIndex tile_index, int size, i32 rotated_layout);
void GrowZoneFromTraffic(City *city, TileIndex tile_index, int size, u8 zone);
bool ReplaceSpecialZoneTile(City *city, TileIndex tile, int replacement, int zone_kind);

#endif
