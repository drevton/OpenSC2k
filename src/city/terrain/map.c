#include "city/terrain/terrain.h"
#include "city/terrain/climate.h"

#include "city/maps/kinds.h"
#include "city/maps/map_types.h"
#include "city/maps/tables.h"

#include <stdlib.h>
#include <string.h>

// Terrain output storage, climate calibration and read-only appearance. Generation supplies the same five planes;
// file and live-City previews reuse this owner without allocating generation work or advancing City RNG.

// Release generated map storage and reset the output object. NULL is accepted; callers must not retain tile, climate
// or other views into the released backing.
void ReleaseCityTerrainMap(CityTerrainMap *map) {
  if (!map) return;
  free(map->storage);
  *map = (CityTerrainMap){0};
}

// Reuse the existing terrain/preview owner, with the same five map planes used by generation.
bool ResetCityTerrainMap(CityTerrainMap *map, int width, int height) {
  if (!IsSupportedMapAxis(width) || !IsSupportedMapAxis(height)) return false;
  int cells = width * height;
  size_t size = 5 * cells + TerrainSnowNoiseSize(width, height);
  void *storage = map->storage;
  if (!storage || map->width != width || map->height != height) {
    storage = calloc(1, size);
    if (!storage) return false;
    ReleaseCityTerrainMap(map);
  } else memset(storage, 0, size);
  *map = (CityTerrainMap){.storage = storage, .width = width, .height = height, .cell_count = cells,
      .altitude = storage, .terrain = (u8 *)storage + 2 * cells,
      .buildings = (u8 *)storage + 3 * cells, .flags = (u8 *)storage + 4 * cells};
  return true;
}

enum { kClimateHeightCount = CITY_ALTITUDE_LAND_MASK + 1 };

// Derive the non-desert ground climate near one valid generated-map tile. Requires initialized climate noise and
// desert threshold; reads bounded neighbors and does not update the map.
static int TerrainGreenClimate(const CityTerrainMap *map, TileIndex tile) {
  if (map->climate != CITY_TERRAIN_MIXED) return CLIMATE_TROPICAL;
  int x = tile % map->width, y = tile / map->width;
  int green = TerrainGrassPaletteAt(&map->snow, (CityPoint){x, y});
  if (!map->desert_limit) return green;
  // Desert coverage controls the dry regions, rather than being capped by
  // the temperature seed. Use the desert preset's tropical grass around those
  // regions, keeping the sandy border snow-free even in a cold region.
  // Two tiles beyond raw dry candidates plus their existing one-tile green rim
  // leave at least three grass tiles before any snow-capable Arctic blend.
  // Raw candidates include every desert tile without nested neighbor scans.
  enum { kDesertGrassMargin = 2 };
  for (int dy = -kDesertGrassMargin; dy <= kDesertGrassMargin; ++dy) {
    for (int dx = -kDesertGrassMargin; dx <= kDesertGrassMargin; ++dx) {
      int nx = x + dx, ny = y + dy;
      if (nx < 0 || ny < 0 || nx >= map->width || ny >= map->height) continue;
      if (!IsTileKindExposedWater(map->buildings[ny * map->width + nx]) &&
          TerrainClimateValue(&map->snow, (CityPoint){nx, ny}, true) < map->desert_limit)
        return CLIMATE_TROPICAL;
    }
  }
  return green;
}

// Like OpenTTD's CalculateCoverageLine, choose the nearest land quantile. Snow uses height relative to the smooth
// regional snow-line offset. Coverage must be zero through 100 and climate/height arrays initialized; returns a
// threshold without changing the generated map.
static int CalculateTerrainSnowLine(const CityTerrainMap *map, int coverage) {
  if (!coverage) return kClimateHeightCount;
  if (coverage == 100) return 0;
  int heights[kClimateHeightCount] = {0};
  CitySnowClimate offset = map->snow;
  offset.line = 0;
  for (int y = 0; y < map->height; ++y) for (int x = 0; x < map->width; ++x) {
    TileIndex tile = y * map->width + x;
    if (IsTileKindExposedWater(map->buildings[tile])) continue;
    // Both Arctic blends originate on the Arctic side. Avoid constructing
    // their neighbor-dependent appearance just to collect eligible heights.
    if (map->climate == CITY_TERRAIN_MIXED &&
        TerrainGreenClimate(map, tile) != CLIMATE_ARCTIC) continue;
    int height = (int)(map->altitude[tile] & CITY_ALTITUDE_LAND_MASK) -
        TerrainSnowLineAt(&offset, (CityPoint){x, y});
    ++heights[IntClamp(height, 0, kClimateHeightCount - 1)];
  }
  int goal = (map->cell_count - map->water_tiles) * coverage / 100, current = 0;
  int best_line = kClimateHeightCount, best_score = goal;
  for (int height = kClimateHeightCount - 1; height >= 0; --height) {
    current += heights[height];
    int score = goal - current;
    if (abs(score) < abs(best_score)) {
      best_score = score;
      best_line = height;
    }
  }
  return best_line;
}

// Desert uses the regional moisture field, never elevation. Include the green neighbor margin in the quantile so
// coverage remains close to the chosen target. Coverage must be zero through 100 and moisture data initialized;
// returns an integer threshold without advancing RNG.
static int CalculateTerrainDesertLimit(const CityTerrainMap *map, int coverage) {
  if (!coverage) return 0;
  if (coverage == 100) return 256;
  int moisture[256] = {0};
  for (int y = 0; y < map->height; ++y) for (int x = 0; x < map->width; ++x) {
    TileIndex tile = y * map->width + x;
    if (IsTileKindExposedWater(map->buildings[tile])) continue;
    int value = TerrainClimateValue(&map->snow, (CityPoint){x, y}, true);
    bool excluded = false;
    for (int direction = 0; direction < 8; ++direction) {
      int nx = x + kClockwiseNeighborX[direction], ny = y + kClockwiseNeighborY[direction];
      if (nx < 0 || ny < 0 || nx >= map->width || ny >= map->height) continue;
      excluded |= IsTileKindExposedWater(map->buildings[ny * map->width + nx]);
      value = IntMax(value, TerrainClimateValue(&map->snow, (CityPoint){nx, ny}, true));
    }
    if (!excluded) ++moisture[value];
  }
  int goal = (map->cell_count - map->water_tiles) * coverage / 100, current = 0;
  int best_limit = 0, best_score = goal;
  for (int value = 0; value < 256; ++value) {
    current += moisture[value];
    int score = goal - current;
    if (abs(score) < abs(best_score)) {
      best_score = score;
      best_limit = value + 1;
    }
  }
  return best_limit;
}

// A one-tile green margin separates desert from water and moist regions. Tile must index the supplied generated map;
// reads the bounded moisture/water neighborhood without modifying climate or ground.
static bool TerrainTileIsDesert(const CityTerrainMap *map, TileIndex tile) {
  if (IsTileKindExposedWater(map->buildings[tile]) ||
      TerrainClimateValue(&map->snow, (CityPoint){tile % map->width, tile / map->width}, true) >=
          map->desert_limit) return false;
  int x = tile % map->width, y = tile / map->width;
  for (int direction = 0; direction < 8; ++direction) {
    int nx = x + kClockwiseNeighborX[direction], ny = y + kClockwiseNeighborY[direction];
    if (nx < 0 || nx >= map->width || ny < 0 || ny >= map->height) continue;
    TileIndex neighbor = ny * map->width + nx;
    if (IsTileKindExposedWater(map->buildings[neighbor]) ||
        TerrainClimateValue(&map->snow, (CityPoint){nx, ny}, true) >= map->desert_limit) return false;
  }
  return true;
}

// Choose a mixed grass-climate appearance where neighboring climate regions meet. Tile must be valid and green an
// admitted base climate; this does not generate new noise or change climate storage.
static int TerrainGrassTransition(const CityTerrainMap *map, TileIndex tile, int green) {
  int x = tile % map->width, y = tile / map->width;
  bool temperate = false, tropical = false;
  for (int i = 0; i < 8; i += 2) {
    int nx = x + kClockwiseNeighborX[i], ny = y + kClockwiseNeighborY[i];
    if (nx < 0 || ny < 0 || nx >= map->width || ny >= map->height) continue;
    TileIndex neighbor = ny * map->width + nx;
    if (IsTileKindExposedWater(map->buildings[neighbor]) || TerrainTileIsDesert(map, neighbor)) continue;
    int adjacent = TerrainGreenClimate(map, neighbor);
    temperate |= adjacent == CLIMATE_TEMPERATE;
    tropical |= adjacent == CLIMATE_TROPICAL;
  }
  // Only one side contributes the half blend, making a one-tile border.
  if (green == CLIMATE_ARCTIC) {
    if (temperate) return CLIMATE_TEMPERATE_ARCTIC;
    if (tropical) return CLIMATE_ARCTIC_TROPICAL;
  }
  if (green == CLIMATE_TROPICAL && temperate) return CLIMATE_TEMPERATE_TROPICAL;
  return green;
}

// Generate permanent ground independently of elevation and snow. Tile must index the supplied generated map; returns
// permanent climate independently of transient snow appearance.
static int TerrainTileGroundClimate(const CityTerrainMap *map, TileIndex tile) {
  if (map->climate == CITY_TERRAIN_SIMCITY) return CLIMATE_SIMCITY;
  if (map->climate == CITY_TERRAIN_NORMAL) return CLIMATE_TEMPERATE;
  if (map->climate == CITY_TERRAIN_SNOWY) return CLIMATE_ARCTIC;
  CityPoint position = {tile % map->width, tile / map->width};
  int green = TerrainGreenClimate(map, tile);
  if (map->climate == CITY_TERRAIN_DESERT &&
      TerrainClimateValue(&map->snow, position, true) >= 128)
    green = CLIMATE_RAINFOREST;
  if (!TerrainTileIsDesert(map, tile))
    return map->climate == CITY_TERRAIN_MIXED ? TerrainGrassTransition(map, tile, green) : green;
  // OpenTTD 15.3 NeighbourIsNormal / TileLoopClearDesert: half desert only
  // touches a non-desert cardinal neighbor; the interior uses full desert.
  // A narrow desert strip can therefore be entirely half-covered.
  int tx = tile % map->width, ty = tile / map->width;
  for (int i = 0; i < 8; i += 2) {
    int nx = tx + kClockwiseNeighborX[i], ny = ty + kClockwiseNeighborY[i];
    if (nx < 0 || nx >= map->width || ny < 0 || ny >= map->height) continue;
    TileIndex neighbor = ny * map->width + nx;
    if (!TerrainTileIsDesert(map, neighbor)) return CLIMATE_DESERT_50;
  }
  return CLIMATE_DESERT_100;
}

// Preview and City generation share the same cached appearance encoding. Tile must index the supplied generated map;
// returns its cached ground/snow appearance without allocating or modifying generation state.
int CityTerrainTileClimate(const CityTerrainMap *map, TileIndex tile) {
  int ground = TerrainTileGroundClimate(map, tile);
  if (!TerrainClimateSupportsSnow(ground) || map->snow.line == CITY_SNOW_LINE_DISABLED) return ground;
  CityPoint position = {tile % map->width, tile / map->width};
  int snow = (int)(map->altitude[tile] & CITY_ALTITUDE_LAND_MASK) - TerrainSnowLineAt(&map->snow, position);
  return TerrainSnowClimate(ground, snow);
}

// Finish imported or generated terrain using the selected climate, without generating new land or trees.
void FinishCityTerrainMap(CityTerrainMap *map, const CityTerrainSettings *settings) {
  map->climate = settings->climate;
  map->water_tiles = map->tree_tiles = 0;
  map->minimum_height = 31; map->maximum_height = 0;
  for (int tile = 0; tile < map->cell_count; ++tile) {
    int height = map->altitude[tile] & CITY_ALTITUDE_LAND_MASK;
    map->water_tiles += IsTileKindExposedWater(map->buildings[tile]);
    map->tree_tiles += map->buildings[tile] >= TK_TREE_FIRST && map->buildings[tile] <= TK_TREE_LAST;
    map->minimum_height = IntMin(map->minimum_height, height);
    map->maximum_height = IntMax(map->maximum_height, height);
  }
  map->snow = (CitySnowClimate){.seed = settings->seed ^ 0x6C8E9CF5, .line = CITY_SNOW_LINE_DISABLED,
      .points = (u8 *)map->storage + 5 * map->cell_count};
  InitializeTerrainSnowNoise(&map->snow, map->width, map->height);
  map->desert_limit = 0;
  if (settings->climate == CITY_TERRAIN_DESERT || settings->climate == CITY_TERRAIN_MIXED)
    map->desert_limit = CalculateTerrainDesertLimit(map, settings->desert_coverage);
  if (settings->climate == CITY_TERRAIN_SNOWY || settings->climate == CITY_TERRAIN_MIXED) {
    map->snow.variation = settings->snow_coverage && settings->snow_coverage < 100 ? CITY_SNOW_LINE_VARIATION : 0;
    map->snow.line = CalculateTerrainSnowLine(map, settings->snow_coverage);
  }
}
