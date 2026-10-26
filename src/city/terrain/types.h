#ifndef SC2K_CITY_TERRAIN_TYPES_H
#define SC2K_CITY_TERRAIN_TYPES_H

#include "city/types.h"

// Terrain settings and prepared records; generation and climate operations have separate interfaces.

// Climate presets are shared by live City state, terrain controls and file metadata.
typedef enum CityTerrainClimate {
  CITY_TERRAIN_NORMAL, CITY_TERRAIN_DESERT, CITY_TERRAIN_SNOWY, CITY_TERRAIN_MIXED,
  CITY_TERRAIN_SIMCITY, CITY_TERRAIN_CLIMATE_COUNT
} CityTerrainClimate;

// The owner supplies temperature and moisture backing in one allocation. points addresses
// TerrainSnowNoiseSize bytes; moisture and stride are bound by InitializeTerrainSnowNoise.
// Neither this descriptor nor the sampling operations allocate or release that backing.
typedef struct {
  u32 seed;
  u8 line, variation;
  u16 stride;
  u8 *points;
  u8 *moisture;
} CitySnowClimate;
enum { CITY_SNOW_LINE_DISABLED = 32, CITY_SNOW_LINE_VARIATION = 2 };

// Terrain

// New terrain sessions selected from startup use these original slider values.
enum {
  CITY_TERRAIN_DEFAULT_SIZE = 128, // Each zero generation axis uses this size, independently of feature spacing.
  CITY_TERRAIN_DEFAULT_HILLS = 12,
  CITY_TERRAIN_DEFAULT_WATER = 5,
  CITY_TERRAIN_DEFAULT_TREES = 15
};

// The terrain controls also use this boundary when encoding feature choices.
enum { kTerrainFeatureCount = 18 };
enum {
  kOcean = 1 << 0, kRiver = 1 << 1, kMeander = 1 << 2, kDelta = 1 << 3,
  kCrossing = 1 << 4, kBranch = 1 << 5, kRejoin = 1 << 6, kPlateau = 1 << 7,
  kRidge = 1 << 8, kValley = 1 << 9, kRolling = 1 << 10, kBasin = 1 << 11,
  kCanyon = 1 << 12, kCliffs = 1 << 13, kLake = 1 << 14, kLakes = 1 << 15,
  kSalty = 1 << 16, kRandomCoast = 1 << 17
};
typedef enum CityTerrainSmoothness {
  CITY_TERRAIN_VERY_SMOOTH, CITY_TERRAIN_SMOOTH, CITY_TERRAIN_ROUGH, CITY_TERRAIN_VERY_ROUGH,
  CITY_TERRAIN_SMOOTHNESS_COUNT
} CityTerrainSmoothness;
typedef enum CityTerrainVariety {
  CITY_TERRAIN_VARIETY_NONE, CITY_TERRAIN_VARIETY_LOW, CITY_TERRAIN_VARIETY_MEDIUM, CITY_TERRAIN_VARIETY_HIGH,
  CITY_TERRAIN_VARIETY_COUNT
} CityTerrainVariety;
typedef enum CityTerrainSmallRivers {
  CITY_TERRAIN_SMALL_RIVERS_NONE, CITY_TERRAIN_SMALL_RIVERS_FEW, CITY_TERRAIN_SMALL_RIVERS_MEDIUM,
  CITY_TERRAIN_SMALL_RIVERS_MANY, CITY_TERRAIN_SMALL_RIVERS_COUNT
} CityTerrainSmallRivers;
// Zero keeps the generator's seeded orientation.
enum {
  CITY_TERRAIN_COAST_NORTH = 1, CITY_TERRAIN_COAST_EAST = 2,
  CITY_TERRAIN_COAST_SOUTH = 4, CITY_TERRAIN_COAST_WEST = 8, CITY_TERRAIN_COAST_ALL = 15
};
typedef struct CityTerrainSettings {
  u32 seed, features;
  int hills, water, trees;
  int width, height;
  u8 smoothness, variety, small_rivers;
  u8 climate;
  u8 snow_coverage, desert_coverage;
  u8 coast_sides;
} CityTerrainSettings;
typedef struct CityTerrainMap {
  void *storage;
  u16 *altitude;
  u8 *terrain, *buildings, *flags;
  int width, height, cell_count;
  u32 process_state, game_state, ocean, river;
  int water_level, water_tiles, tree_tiles, minimum_height, maximum_height;
  CitySnowClimate snow;
  int desert_limit;
  u8 climate;
  bool legacy;
} CityTerrainMap;

#endif
