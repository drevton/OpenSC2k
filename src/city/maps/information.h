#ifndef SC2K_CITY_MAPS_INFORMATION_H
#define SC2K_CITY_MAPS_INFORMATION_H

#include "city/maps/tile_storage.h"
#include "city/types.h"

typedef union CityStatistics CityStatistics;

// One-based land-use summary buckets; zero excludes construction/abandoned tiles.
typedef enum CityLandUseCategory {
  CITY_LAND_USE_NONE = 0, CITY_LAND_USE_TRANSPORTATION = 1, CITY_LAND_USE_POWER = 2,
  CITY_LAND_USE_WATER = 3, CITY_LAND_USE_RESIDENTIAL = 4, CITY_LAND_USE_COMMERCIAL = 5,
  CITY_LAND_USE_INDUSTRIAL = 6, CITY_LAND_USE_PORTS_AIRPORTS = 7, CITY_LAND_USE_EDUCATION = 8,
  CITY_LAND_USE_HEALTH_SAFETY = 9, CITY_LAND_USE_RECREATION = 10, CITY_LAND_USE_ARCOLOGIES = 11,
  CITY_LAND_USE_COUNT = 11,
} CityLandUseCategory;

typedef enum CityTileWaterSupply {
  CITY_TILE_WATER_SERVICE,
  CITY_TILE_WATER_PRODUCTION,
  CITY_TILE_WATER_STORAGE,
} CityTileWaterSupply;

// Read-only tile query values. UI owns the retained copy and its localized text/artwork.
typedef struct {
  TileIndex tile;
  TileInfo cell;
  int x, y, altitude_feet, traffic, water;
  u32 facility_years;
  bool has_facility_age;
  u8 density, density_history, traffic_density, land_value, crime, pollution, police, fire;
  u8 crime_rating, pollution_rating, water_supply;
  bool underwater, rocky, show_traffic, building_stage, show_services, powered, watered;
} CityTileInformation;

// Tile description groups used by presentation, independently of graphics resources.
// The road/rail crossing and highway families intentionally share labels.
typedef enum CityTileCategory {
  CITY_TILE_CATEGORY_CLEAR = 0, CITY_TILE_CATEGORY_RUBBLE = 1,
  CITY_TILE_CATEGORY_RADIOACTIVE_WASTE = 2, CITY_TILE_CATEGORY_TREES = 3,
  CITY_TILE_CATEGORY_PARK = 4, CITY_TILE_CATEGORY_POWER_LINES = 5,
  CITY_TILE_CATEGORY_ROAD = 6, CITY_TILE_CATEGORY_RAIL = 7,
  CITY_TILE_CATEGORY_TUNNEL = 8, CITY_TILE_CATEGORY_ROAD_CROSSING = 9,
  CITY_TILE_CATEGORY_RAIL_CROSSING = 10, CITY_TILE_CATEGORY_HIGHWAY = 11,
  CITY_TILE_CATEGORY_SUSPENSION_BRIDGE = 12, CITY_TILE_CATEGORY_BRIDGE = 13,
  CITY_TILE_CATEGORY_POWER_BRIDGE = 14, CITY_TILE_CATEGORY_HIGHWAY_ENTRANCE = 15,
  CITY_TILE_CATEGORY_HIGHWAY_MULTI = 16, CITY_TILE_CATEGORY_HIGHWAY_BRIDGE = 17,
  CITY_TILE_CATEGORY_SUBWAY_TO_RAIL = 18, CITY_TILE_CATEGORY_LOWER_CLASS_HOMES = 19,
  CITY_TILE_CATEGORY_MIDDLE_CLASS_HOMES = 20, CITY_TILE_CATEGORY_LUXURY_HOMES = 21,
  CITY_TILE_CATEGORY_COUNT = 22
} CityTileCategory;

// Classify a valid stored tile kind for the existing description slots; does not read artwork or modify City.
// Clear/exposed-water kinds share a category; UI selects their final water/terrain label from the captured facts.
int CityTileInformationCategory(int kind);

// Queries read valid tiles and retain no pointers into City or presentation owners.
CityTileInformation ReadCityTileInformation(const City *city, TileIndex tile);
// Summarize the compatibility-indexed counters without narrowing their native 32-bit values. Output is borrowed
// caller storage; construction and abandoned categories retain their existing exclusion rules.
// Output slot is category - 1; NONE has no slot. Percentages truncate, and an empty total leaves both arrays zero.
void CalculateCityLandUse(const CityStatistics *statistics, u32 acres[CITY_LAND_USE_COUNT],
    u16 percent[CITY_LAND_USE_COUNT]);

#endif
