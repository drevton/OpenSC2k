#ifndef SC2K_DOMAIN_SRC_CITY_GROWTH_GROWTH_H
#define SC2K_DOMAIN_SRC_CITY_GROWTH_GROWTH_H

#include "city/maps/tile_storage.h"
#include "city/types.h"
#include "city/maps/map_types.h"

// Tables

typedef struct { i8 x, y; } TransportNeighborOffset;
typedef struct { TransportNeighborOffset offset[24]; } TransportNeighborOffsets;
extern const TransportNeighborOffsets kTransportNeighborOffsets;

// Fixed growth-selection lookups are independent of the reference executable.
enum { CITY_GROWTH_TRANSPORT_WEIGHT_COUNT = 515 };
extern const u8 kGrowthBaseTable[21];
extern const u8 kGrowthDivisorTable[21];
extern const u8 kGrowthCornerPatterns[16];
extern const u16 kZoneCompatibilityMask[16];
extern const u8 kTileScanMaskTable[4];
extern const i8 kSpecialZoneCorridorYStep[4];
extern const i8 kSpecialZoneCorridorXStep[4];
extern const u16 kTransportValueBySize[CITY_GROWTH_TRANSPORT_WEIGHT_COUNT];

// Zoning

// One valid tile zoned residential through seaport. May account population, grow, decline or spawn special-zone
// vehicles. The caller owns visit order and the shared large-city-effect decision; reserved zones are excluded.
void GrowCityZonedTile(City *city, TileIndex tile, bool large_city_effect_enabled);
// Select and place immediately; callers decide growth eligibility.
void PlaceGrowthTiles(City *city, TileIndex tile_index, int size, int growth_type);
void GrowActiveZoneTiles(City *city, TileIndex tile_index, int size, i32 rotated_layout);
void LargeCityTileEffect(City *city, TileIndex tile_index);
void GrowZoneFromTraffic(City *city, TileIndex tile_index, int size, u8 zone);
bool TileHasTransportAccess(City *city, TileIndex tile_index);
bool TileHasNearbyPower(City *city, TileIndex tile_index);
int SelectZone9Replacement(const CityTileCounts *counts);
int SelectZone8Replacement(const CityTileCounts *counts);
bool ReplaceSpecialZoneTile(City *city, TileIndex tile, int replacement, int zone_kind);

#endif
