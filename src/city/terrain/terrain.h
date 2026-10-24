#ifndef SC2K_TERRAIN_H
#define SC2K_TERRAIN_H

#include "city/types.h"

// Terrain

// New terrain sessions selected from startup use these original slider values.
enum {
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
typedef enum CityTerrainClimate {
  CITY_TERRAIN_NORMAL, CITY_TERRAIN_DESERT, CITY_TERRAIN_SNOWY, CITY_TERRAIN_MIXED,
  CITY_TERRAIN_SIMCITY, CITY_TERRAIN_CLIMATE_COUNT
} CityTerrainClimate;
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
typedef struct {
  u32 seed;
  u8 line, variation;
  u16 stride;
  u8 *points;
  u8 *moisture;
} CitySnowClimate;
enum { CITY_SNOW_LINE_DISABLED = 32, CITY_SNOW_LINE_VARIATION = 2 };
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
bool PrepareCityTerrainMap(CityTerrainMap *map, int width, int height);
void FinishCityTerrainMap(CityTerrainMap *map, const CityTerrainSettings *settings);
// Prepare preview backing, copy/downsample live terrain and finish derived metadata. City is read-only.
// False means preview backing could not be prepared.
bool ReadCityTerrainPreview(const City *city, CityTerrainMap *map, const CityTerrainSettings *settings);
void ReleaseCityTerrainMap(CityTerrainMap *map);
u32 DisabledCityTerrainFeatures(u32 features);
void ToggleCityTerrainFeature(CityTerrainSettings *settings, u32 feature);
bool AreCityTerrainSettingsValid(const CityTerrainSettings *settings);
bool GenerateTerrainMap(const CityTerrainSettings *settings, CityTerrainMap *map);
int CityTerrainTileClimate(const CityTerrainMap *map, TileIndex tile);
int TerrainSnowLineAt(const CitySnowClimate *snow, CityPoint position);
int TerrainSnowNoiseSize(int width, int height);
void InitializeTerrainSnowNoise(CitySnowClimate *snow, int width, int height);
int TerrainBaseClimate(int climate);
bool TerrainClimateSupportsSnow(int climate);
int TerrainSnowClimate(int grass, int height_above_line);

#endif
