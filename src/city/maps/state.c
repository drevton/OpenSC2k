// TileInfo landscape state and rebuilding (zero-based bits):
// flags[1..2] is a permanent region interpreted by City's terrain_climate: mixed preset 0 temperate, 1 arctic,
// 2 tropical, 3 desert; desert preset 0/3 tropical, 1 desert, 2 rainforest. Uniform presets ignore these bits.
// terrain[0..3] stores current ground cover, including 1..4 snow coverage and 11/12 half/full desert on dry tiles.
// These cover states, neighboring regions and the City preset derive climate appearance; there is no climate byte.
// Landscape visits use clear-ground zone[5..7] as recovery timing or tree zone[5..7] as growth, never both.
// Family terrain[4..7], shared heights/slope, utility flags, overlays and underground follow their module contracts
// and tile_storage.h. Recounting reads them without introducing another per-tile encoding.

#include "city/infrastructure/bridges.h"
#include "city/infrastructure/highways.h"
#include "city/maps/ground.h"
#include "city/maps/trees.h"
#include "city/maps/water.h"
#include "city/maps/tile_storage.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "city/state.h"
#include "city/engine/simulation.h"
#include "city/growth/growth.h"
#include "city/infrastructure/infrastructure.h"
#include "city/maps/layers.h"
#include "city/maps/tables.h"
#include "city/maps/map.h"
#include "city/tools/tools.h"
#include "city/saveload/file.h"
#include "city/saveload/map_format.h"
#include "game/state.h"
#include "audio/state.h"
#include "assets/state.h"
#include "city/terrain/terrain.h"
#include "city/maps/map_types.h"


// State

// Recount tile IDs after loading a city, except zone category 7. Requires a complete native map with the supported
// cell alignment; replaces counters and excludes military zoning as specified.
void RecountCityTileStatistics(City *city) {
  CityStatistics *statistics = &city->statistics;
  const TileInfo *tiles = city->tile;
  // Independent counters avoid one serial increment chain on mostly empty maps.
  // Every supported map has a cell count divisible by four.
  u32 counts[4][TK_COUNT] = {0};
  for (TileIndex tile = 0; tile < city->map_cell_count; tile += 4) {
    for (int lane = 0; lane < 4; ++lane)
      if (ReadCityZoneKind(&tiles[tile + lane]) != ZONE_MILITARY)
        ++counts[lane][CityTileStatisticAt(city, tile + lane)];
    for (int lane = 0; lane < 4; ++lane) {
      int under = ReadCityBridgeUnderpassKind(&tiles[tile + lane]);
      if (under) ++counts[lane][CityTileStatisticsKind(city, under)];
    }
  }
  for (int kind = 0; kind < TK_COUNT; ++kind)
    statistics->count[kind] = counts[0][kind] + counts[1][kind] + counts[2][kind] + counts[3][kind];
}

// Compare two u32 source-order keys for sorting in ascending traversal order. Both arguments must point to readable
// keys; returns negative, zero or positive without subtracting unsigned values.
static int CompareWaterSourceOrder(const void *left, const void *right) {
  u32 a = *(const u32 *)left, b = *(const u32 *)right;
  return (a > b) - (a < b);
}

// Decode a linear traversal position into coordinates for the selected map rotation. Position must fit the map and
// direction must be a supported rotation; no map or cursor state changes.
static CityPoint WaterSourceAtPosition(const City *city, int position, int direction) {
  int width = city->map_width, height = city->map_height;
  int x = direction & 1 ? position % width : position / height;
  int y = direction & 1 ? position / width : position % height;
  if (direction == CITY_MAP_ROTATION_90 || direction == CITY_MAP_ROTATION_180) x = width - 1 - x;
  if (direction == CITY_MAP_ROTATION_180 || direction == CITY_MAP_ROTATION_270) y = height - 1 - y;
  return (CityPoint){x, y};
}

// Convert the completed water scan totals to percentage coverage and treatment capacity. Call only after
// source/component traversal; zero demand produces full percentage coverage.
static void FinishWaterNetwork(City *city) {
  u32 total = city->infrastructure.water_total;
  int statistic = city->statistics.count[TK_WATER_TREATMENT_PLANT];
  city->infrastructure.capacity = (u32)(2000 * (statistic / 4)) >= city->infrastructure.water_percent;
  city->infrastructure.water_percent = total ? 100 * city->infrastructure.water_percent / total : 100;
}

// Advance the section clearing, ordered source scan and connected-component water distribution. Budget controls
// incremental progress and may be consumed by a whole section; false retains the City's cursor and true finalizes
// coverage.
bool StepCityWaterNetwork(City *city, int budget) {
  CitySimulationWork *work = &city->simulation_work;
  const int cells = city->map_cell_count, width = city->map_width;
  if (work->stage == 0) {
    if (!work->cursor) {
      InvalidateMapLayer(city, CITY_MAP_CHANGE_FLAGS);
      InvalidateMapLayer(city, CITY_MAP_CHANGE_POLICE_COVERAGE);
      city->infrastructure.water_percent = city->infrastructure.water_total = 0;
    }
    const int sections = cells / CITY_MAP_SECTION_CELL_COUNT;
    while (budget > 0 && work->cursor < sections) {
      ClearCityUtilitySection(city, work->cursor++, true);
      budget -= CITY_MAP_SECTION_CELL_COUNT;
    }
    if (work->cursor != sections) return false;
    work->stage = 1;
    work->cursor = 0;
  }
  int direction = city->geography.rotation;
  while (budget > 0) {
    if (work->stage == 2) {
      if (!StepCityWaterComponent(city, &work->network, &budget)) return false;
      work->stage = 1;
    } else if (work->cursor < cells && direction < CITY_MAP_ROTATION_COUNT) {
      int position = work->cursor++;
      --budget;
      CityPoint point = WaterSourceAtPosition(city, position, direction);
      int x = point.x, y = point.y;
      TileIndex cell = y * width + x;
      const TileInfo *tp = &city->tile[cell];
      int tile = tp->kind;
      if ((tile == TK_WATER_PUMP || tile == TK_DESALINATION_PLANT) &&
          (tp->flags & TF_POWERED) && !(tp->flags & TF_WATERED)) {
        BeginCityUtilityTraversal(city, &work->network, x, y);
        work->stage = 2;
      }
    } else {
      FinishWaterNetwork(city);
      return true;
    }
  }
  return false;
}

// Use existing bounded workspace to visit sparse water sources in stock traversal order. Returns false when that
// workspace cannot represent the scan, allowing the ordinary traversal path; it does not allocate another map plane.
static bool ScanWaterSourcesInOrder(City *city, int direction) {
  if (!city->water_source_order) return false;
  // Bounded source-order workspace is disjoint from the route FIFO. Find
  // sparse sources contiguously, then retain the stored simulation order.
  u32 *sources = city->water_source_order;
  int count = 0, capacity = IntMin(city->map_cell_count / 2, CITY_WATER_SOURCE_SORT_LIMIT);
  for (TileIndex tile = 0; tile < city->map_cell_count; ++tile) {
    const TileInfo *tp = &city->tile[tile];
    if (tp->kind != TK_WATER_PUMP && tp->kind != TK_DESALINATION_PLANT) continue;
    if (!(tp->flags & TF_POWERED)) continue;
    // An unusually source-dense map uses the original scan without allocation.
    if (count == capacity) return false;
    CityPoint tile_point = MapTilePt(city, tile);
    if (direction == CITY_MAP_ROTATION_90 || direction == CITY_MAP_ROTATION_180)
      tile_point.x = city->map_width - 1 - tile_point.x;
    if (direction == CITY_MAP_ROTATION_180 || direction == CITY_MAP_ROTATION_270)
      tile_point.y = city->map_height - 1 - tile_point.y;
    sources[count++] = direction & 1 ? tile_point.y * city->map_width + tile_point.x :
        tile_point.x * city->map_height + tile_point.y;
  }
  if (count > 1) qsort(sources, count, sizeof(*sources), CompareWaterSourceOrder);
  for (int i = 0; i < count; ++i) {
    CityPoint point = WaterSourceAtPosition(city, sources[i], direction);
    int x = point.x, y = point.y;
    if (!(city->tile[y * city->map_width + x].flags & TF_WATERED))
      ScanCityWaterComponent(city, x, y);
  }
  return true;
}

// Scan the map and traverse transport networks during phase 20. Requires initialized water layers and exclusive route
// workspace; completes water distribution and stores final coverage metrics.
void ScanCityMapPhase20(City *city) {
  InvalidateMapLayer(city, CITY_MAP_CHANGE_FLAGS);
  InvalidateMapLayer(city, CITY_MAP_CHANGE_POLICE_COVERAGE);
  city->infrastructure.water_percent = 0;
  city->infrastructure.water_total = 0;
  for (int section = 0; section < city->map_cell_count / CITY_MAP_SECTION_CELL_COUNT; ++section)
    ClearCityUtilitySection(city, section, true);
  // Keep the stock source order: limited water is assigned in traversal order.
  int direction = city->geography.rotation;
  if (direction < CITY_MAP_ROTATION_COUNT && !ScanWaterSourcesInOrder(city, direction)) {
    for (int position = 0; position < city->map_cell_count; ++position) {
      CityPoint point = WaterSourceAtPosition(city, position, direction);
      int x = point.x, y = point.y;
      const TileInfo *tp = &city->tile[y * city->map_width + x];
      int tile = tp->kind;
      if ((tile == TK_WATER_PUMP || tile == TK_DESALINATION_PLANT) &&
          (tp->flags & TF_POWERED) &&
          !(tp->flags & TF_WATERED))
        ScanCityWaterComponent(city, x, y);
    }
  }
  FinishWaterNetwork(city);
}

// Recount industry border connections from the current surface and underground tiles. Requires normalized native
// storage; replaces the cached connection count rather than creating connections.
void RecountCityIndustryConnections(City *city) {
  int count = 0;
  for (TileIndex tile = 0; tile < city->map_cell_count; ++tile) {
    const TileInfo *tp = &city->tile[tile];
    if (tp->overlay != CITY_OVERLAY_BORDER_CONNECTION) continue;
    int surface = tp->kind;
    if (surface == TK_RAIL ||
        surface == TK_ROAD_RAIL_CROSSING || surface == TK_RAIL_POWER_CROSSING ||
        IsTileKindStraightHighway(surface) ||
        IsTileKindMultiHighway(surface)) ++count;
  }
  city->infrastructure.service_bonus_a = count;
}

// Rebuild power, transport, zone and bond state after reading a city. Call only after successful decoding/conversion;
// rebuilds derived counts/networks instead of applying a second import over partial data.
void RebuildLoadedCityMapState(City *city) {
  city->infrastructure.service_bonus_b = 0;
  if (city->fixes & (1 << CITY_FIX_LOADED_CONNECTIONS))
    RecountCityIndustryConnections(city);
  for (int y = 0; y < city->map_height; ++y)
    for (int x = 0; x < city->map_width; ++x) {
      const TileIndex cell_index = MapTileXY(city, x, y);
      const TileInfo *tp = &city->tile[cell_index];
      if (tp->overlay != CITY_OVERLAY_BORDER_CONNECTION)
        continue;
      int tile = tp->kind;
      if (IsTileKindRoadRoute(tile))
        ++city->infrastructure.service_bonus_b;
    }
  city->infrastructure.counter_a = 0;
  city->infrastructure.counter_b = 0;
  city->infrastructure.counter_c = 0;
  ScanPowerGridPhase(city);
  ScanCityMapPhase20(city);
  city->indicators.zone_count = 0;
  for (int y = 0; y < city->map_height; ++y)
    for (int x = 0; x < city->map_width; ++x) {
      const TileIndex cell_index = MapTileXY(city, x, y);
      const TileInfo *tp = &city->tile[cell_index];
      int scratch_x = x / 4;
      int scratch_y = y / 4;
      int value = ReadScratch(city, scratch_x, scratch_y);
      int companion = ReadScratch(city, scratch_x, scratch_y + city->map_height / 4);
      int tile = tp->kind;
      int flags = tp->flags;
      if (tile != TK_CLEAR && !IsTileKindExposedWater(tile)) {
        if (tile == TK_LARGE_PARK)
          value += 40;
        else if (IsTileKindTreeOrPark(tile))
          value += 20;
        else if (IsTileKindBareGround(tile))
          value -= 20;
      } else if (IsTileKindExposedWater(tile)) {
        value += 12;
        companion += 12;
      } else {
        value += 4;
      }
      if (IsTileKindTransportOrBuilding(tile) || ReadCityZoneKind(tp) != ZONE_NONE) {
        city->tile[MapTileXY(city, x >> 1, y >> 1)].flags |= TF_VISITED;
        ++city->indicators.zone_count;
      }
      if (flags & TF_WATERED) {
        value += 4;
        companion += 4;
      }
      int terrain_code = ReadTerrainCode(tp);
      if (terrain_code != CITY_TERRAIN_FLAT &&
          !HasTileWater(tp)) value += 12;
      WriteScratch(city, scratch_x, scratch_y, value);
      WriteScratch(city, scratch_x, scratch_y + city->map_height / 4, companion);
    }
  city->disasters.event_cleanup_ticks = 0;
  city->indicators.bond_rate_total = 0;
  city->disasters.event_decay_ticks = 0;
  int count = (i32)city->indicators.bond_count;
  for (int index = 0; index < count; ++index)
    city->indicators.bond_rate_total += city->economic_tables.bond_rates.rate[index];
}

// Changes

// Native landscape follows OpenTTD's shuffled tile loop (landscape.cpp), eight-visit grass counter (clear_cmd.cpp), and
// coordinate-staggered tree loop (tree_cmd.cpp).
static CityPoint ClimatePosition(const City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile));
  CityPoint position = MapTilePt(city, tile);
  int width = city->map_width, height = city->map_height;
  // The map arrays rotate. Sample the fixed weather pattern in the original
  // coordinates so rotating a map cannot move its snow boundary.
  for (int rotation = city->geography.rotation; rotation > 0; --rotation) {
    position = (CityPoint){position.y, width - 1 - position.x};
    int previous_width = width;
    width = height; height = previous_width;
  }
  return position;
}

// The two permanent bits are interpreted by the City's landscape preset and survive every occupant change.
int ReadCityTileBaseClimate(const City *city, const TileInfo *tp) {
  int region = (tp->flags & TF_REGION_MASK) >> 1;
  if (city->terrain_climate == CITY_TERRAIN_MIXED) {
    static const u8 kMixed[] = {CLIMATE_TEMPERATE, CLIMATE_ARCTIC, CLIMATE_TROPICAL, CLIMATE_DESERT_100};
    return kMixed[region];
  }
  if (city->terrain_climate == CITY_TERRAIN_DESERT) {
    static const u8 kDesert[] = {CLIMATE_TROPICAL, CLIMATE_DESERT_100, CLIMATE_RAINFOREST, CLIMATE_TROPICAL};
    return kDesert[region];
  }
  return city->terrain_climate == CITY_TERRAIN_NORMAL ? CLIMATE_TEMPERATE :
      city->terrain_climate == CITY_TERRAIN_SNOWY ? CLIMATE_ARCTIC : CLIMATE_SIMCITY;
}

void WriteCityTileRegion(City *city, TileInfo *tp, int climate) {
  int base = TerrainBaseClimate(climate), region = 0;
  if (city->terrain_climate == CITY_TERRAIN_MIXED)
    region = base >= CLIMATE_DESERT_50 && base <= CLIMATE_DESERT_100 ? 3 :
        base == CLIMATE_ARCTIC || base == CLIMATE_TEMPERATE_ARCTIC || base == CLIMATE_ARCTIC_TROPICAL ? 1 :
        base == CLIMATE_TROPICAL || base == CLIMATE_TEMPERATE_TROPICAL ? 2 : 0;
  else if (city->terrain_climate == CITY_TERRAIN_DESERT)
    region = base >= CLIMATE_DESERT_50 && base <= CLIMATE_DESERT_100 ? 1 : base == CLIMATE_RAINFOREST ? 2 : 0;
  tp->flags = (tp->flags & ~TF_REGION_MASK) | region << 1;
}

// Coverage is stored as ground state. Region supplies only the permanent grass/tree family.
int ReadTileClimate(const City *city, const TileInfo *tp) {
  int base = ReadCityTileBaseClimate(city, tp);
  if (HasTileWater(tp)) return base == CLIMATE_DESERT_100 ? CLIMATE_TROPICAL : base;
  int ground = ReadGroundKind(tp);
  if (ground >= CITY_GROUND_SNOW_FIRST && ground <= CITY_GROUND_SNOW_LAST)
    return CLIMATE_ARCTIC + ground;
  if (ground == CITY_GROUND_DESERT_HALF) return CLIMATE_DESERT_50;
  if (ground == CITY_GROUND_DESERT_FULL) return CLIMATE_DESERT_100;
  return base == CLIMATE_DESERT_100 ? CLIMATE_TROPICAL : base;
}

// Blend artwork is selected from bounded permanent-region neighbors, independently of their occupants.
int ReadCityTileClimate(const City *city, TileIndex tile) {
  const TileInfo *tp = &city->tile[tile];
  int appearance = ReadTileClimate(city, tp), base = ReadCityTileBaseClimate(city, tp);
  if (city->terrain_climate != CITY_TERRAIN_MIXED || base == CLIMATE_DESERT_100) return appearance;
  bool temperate = false, tropical = false;
  for (int d = 0; d < 8; d += 2) {
    TileIndex next = GetRelativeTileOrInvalid(city, tile, kTerrainNeighborXOffsets[d], kTerrainNeighborYOffsets[d]);
    if (next == INVALID_TILE) continue;
    int adjacent = ReadCityTileBaseClimate(city, &city->tile[next]);
    temperate |= adjacent == CLIMATE_TEMPERATE;
    tropical |= adjacent == CLIMATE_TROPICAL;
  }
  int blend = base == CLIMATE_ARCTIC ?
      (temperate ? CLIMATE_TEMPERATE_ARCTIC : tropical ? CLIMATE_ARCTIC_TROPICAL : base) :
      base == CLIMATE_TROPICAL && temperate ? CLIMATE_TEMPERATE_TROPICAL : base;
  int snow = ReadTileSnowCoverage(tp);
  if (snow) return snow <= 2 && blend == CLIMATE_TEMPERATE_ARCTIC ? CLIMATE_SNOW_TEMPERATE_ARCTIC_50 :
      snow <= 2 && blend == CLIMATE_ARCTIC_TROPICAL ? CLIMATE_SNOW_ARCTIC_TROPICAL_50 : appearance;
  return appearance == base ? blend : appearance;
}

// Move stored coverage toward its current height/region target. Terrain-editor edits can settle it immediately.
void RefreshCityTileGroundCover(City *city, TileIndex tile, bool immediate) {
  TileInfo *tp = &city->tile[tile];
  if (HasTileWater(tp) && !IsTileKindShoreTree(tp->kind)) return;
  int ground = ReadGroundKind(tp), base = ReadCityTileBaseClimate(city, tp), target = 0;
  // Clearing and exposed beds must finish their bare phase before climate
  // coverage can settle, including immediate terrain-editor refreshes.
  if (ground >= CITY_GROUND_RECOVERY_FIRST && ground <= CITY_GROUND_RECOVERY_GRASS_FIRST) return;
  if (ground == CITY_TRANSPORT_GROUND_PAVED && IsTileKindGroundTransport(tp->kind)) return;
  if (base == CLIMATE_ARCTIC && city->snow.line != CITY_SNOW_LINE_DISABLED)
    target = IntClamp(ReadCityLandHeight(tp) - TerrainSnowLineAt(&city->snow, ClimatePosition(city, tile)) + 1, 0, 4);
  if (base == CLIMATE_DESERT_100) target = CITY_GROUND_DESERT_FULL;
  else if (city->terrain_climate == CITY_TERRAIN_DESERT || city->terrain_climate == CITY_TERRAIN_MIXED) {
    for (int d = 0; d < 8; d += 2) {
      TileIndex next = GetRelativeTileOrInvalid(city, tile, kTerrainNeighborXOffsets[d], kTerrainNeighborYOffsets[d]);
      if (next != INVALID_TILE && ReadCityTileBaseClimate(city, &city->tile[next]) == CLIMATE_DESERT_100) {
        target = CITY_GROUND_DESERT_HALF;
        break;
      }
    }
  }
  bool snow = ground >= CITY_GROUND_SNOW_FIRST && ground <= CITY_GROUND_SNOW_LAST;
  bool desert = ground == CITY_GROUND_DESERT_HALF || ground == CITY_GROUND_DESERT_FULL;
  int next = ground;
  if (target >= CITY_GROUND_DESERT_HALF) next = target;
  else if (target) {
    int amount = snow ? ground : 0;
    next = immediate ? target : amount < target ? amount + 1 : amount > target ? amount - 1 : amount;
  } else if (snow) next = immediate || ground == CITY_GROUND_SNOW_FIRST ? CITY_GROUND_GRASS : ground - 1;
  else if (desert) next = CITY_GROUND_GRASS;
  if (next == ground) return;
  InvalidateTile(city, tile);
  WriteGroundKind(tp, next);
  InvalidateTile(city, tile);
}

void RefreshCityTileClimates(City *city, TileIndex tile) {
  if (!IsMapTileValid(city, tile)) return;
  for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
    TileIndex next = GetRelativeTileOrInvalid(city, tile, dx, dy);
    if (next == INVALID_TILE) continue;
    InvalidateTile(city, next);
    RefreshCityTileGroundCover(city, next, city->settings.timer_mode == CITY_TIMER_STOPPED);
    InvalidateTile(city, next);
  }
}

// Initialize landscape evolution and generated ground, tree and climate metadata. Terrain may be NULL for defaults;
// when supplied its dimensions and storage must match City, and enabled fixes determine which landscape effects apply.
bool ReadCityTerrainPreview(const City *city, CityTerrainMap *map, const CityTerrainSettings *settings) {
  int divisor = IntMax(1, IntMax(city->map_width, city->map_height) / 256);
  int width = IntMax(128, city->map_width / divisor), height = IntMax(128, city->map_height / divisor);
  if (!PrepareCityTerrainMap(map, width, height)) return false;
  for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
    TileIndex source = MapTileXY(city, x * city->map_width / width, y * city->map_height / height);
    TileIndex tile = y * width + x;
    const TileInfo *tp = &city->tile[source];
    CityTileGeometry geometry = ReadCityTileGeometry(city, source);
    bool water = geometry.water != CITY_WATER_NONE;
    map->altitude[tile] = geometry.land_height | (water ? geometry.water_height << CITY_ALTITUDE_WATER_SHIFT : 0);
    map->terrain[tile] = geometry.land_shape;
    map->buildings[tile] = water ? ReadCityWaterGroundKind(tp) : IsTileKindTree(tp->kind) ? tp->kind : TK_CLEAR;
    map->flags[tile] = water ? tp->flags & TF_SALTWATER : 0;
  }
  FinishCityTerrainMap(map, settings);
  return true;
}

void GenerateCityLandscape(City *city, const CityTerrainMap *terrain) {
  city->landscape_cursor = city->landscape_tick = city->landscape_random = 0;
  InitializeCityLandscape(city);
  if (!(city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE))) return;
  if (!(city->fixes & (1 << CITY_FIX_LANDSCAPE_DETAILS))) return;
  city->terrain_climate = terrain ? terrain->climate : CITY_TERRAIN_SIMCITY;
  city->desert_limit = terrain ? terrain->desert_limit : 0;
  city->snow.seed = terrain ? terrain->snow.seed : 0;
  city->snow.line = terrain ? terrain->snow.line : CITY_SNOW_LINE_DISABLED;
  city->snow.variation = terrain ? terrain->snow.variation : 0;
  InitializeTerrainSnowNoise(&city->snow, city->map_width, city->map_height);
  if (terrain) memcpy(city->snow.points, terrain->snow.points,
      TerrainSnowNoiseSize(city->map_width, city->map_height));
  // Generation replaces the entire landscape. Mark it once before updating
  // tree statistics, avoiding per-tree viewport damage callbacks.
  InvalidateMap(city);
  // Regeneration also replaces metadata left by an earlier landscape. Keep this
  // RNG separate from the original terrain generator and simulation streams.
  city->landscape_random = city->random.crt ^ city->random.event ^ 0x6D2B79F5;
  if (!city->landscape_random) city->landscape_random = 1;
  city->landscape_cursor = 1;
  city->landscape_tick = 0;
  TileIndex cells = city->map_cell_count;
  for (TileIndex tile = 0; tile < cells; ++tile) {
    TileInfo *tp = &city->tile[tile];
    if (!HasTileWater(tp)) WriteGroundKind(tp, CITY_GROUND_GRASS);
    WriteCityTileRegion(city, tp, terrain ? CityTerrainTileClimate(terrain, tile) : CLIMATE_SIMCITY);
    int kind = tp->kind;
    bool tree = IsTileKindTree(kind);
    if (kind == TK_CLEAR || IsTileKindExposedWater(kind) || tree) {
      WriteTileZoneByte(tp, ReadTileZoneByte(tp) & CITY_ZONE_KIND_MASK);
      if (tree) GenerateCityTree(city, tile);
    }
  }
  GenerateCityGround(city);
  for (TileIndex tile = 0; tile < cells; ++tile) RefreshCityTileGroundCover(city, tile, true);
}

// Apply the landscape visit appropriate to a valid tile's water, ground, transport or trees. May change recovery, age and
// species using landscape RNG; the traversal owns the visit order.
static void VisitCityLandscapeTile(City *city, TileIndex tile) {
  u32 classes = ReadCityTileClasses(city->tile[tile].kind);
  const TileModule *module = kCityTileModules[(classes & CITY_TILE_MODULE_MASK) >> CITY_TILE_MODULE_SHIFT];
  if (module->tick) module->tick(city, tile);
  else RefreshCityTileGroundCover(city, tile, false);
}

// Visit the next fixed share of the map using the supplied shuffle feedback polynomial. Retains cursor and tick in
// City; the polynomial must match the supported map-size traversal.
static void StepCityLandscape(City *city, u32 feedback) {
  int budget = city->map_cell_count >> 8;
  ++city->landscape_tick;
  if (!(city->landscape_tick & 255)) {
    VisitCityLandscapeTile(city, 0);
    --budget;
  }
  TileIndex tile = city->landscape_cursor;
  while (budget-- > 0) {
    VisitCityLandscapeTile(city, tile);
    tile = (tile >> 1) ^ ((tile & 1) ? feedback : 0);
  }
  city->landscape_cursor = tile;
  SeedCityTrees(city);
}

// Advance one deterministic landscape tick when native landscape is enabled. Selects the supported map-size traversal
// and reuses City state; it does not sample host time.
void AdvanceCityLandscape(City *city) {
  if (!(city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE))) return;
  static const u32 kLandscapeFeedback[] = {0xD8F, 0x1296, 0x2496, 0x4357, 0x8679, 0x1030E,
      0x206CD, 0x403FE, 0x807B8, 0x1004B2, 0x2006A8, 0x4004B2, 0x800B87};
  int bits = 0;
  while ((1 << bits) < city->map_cell_count) ++bits;
  u32 feedback = kLandscapeFeedback[bits - 12];
  // OpenTTD visits 1/256 of the map every 27 ms, whereas a normal-speed
  // City phase takes two 200 ms cadences. Keep the fractional tick in the
  // saved City clock: 27 phases advance exactly 400 landscape ticks. The
  // caller runs only when simulation advances, preserving pause and speed.
  enum { LANDSCAPE_TICK_MS = 27, NORMAL_PHASE_MS = 2 * CITY_CADENCE_INTERVAL_MS };
  int phase = city->clock.tick % LANDSCAPE_TICK_MS;
  int steps = (phase + 1) * NORMAL_PHASE_MS / LANDSCAPE_TICK_MS -
      phase * NORMAL_PHASE_MS / LANDSCAPE_TICK_MS;
  for (int step = 0; step < steps; ++step) StepCityLandscape(city, feedback);
}
