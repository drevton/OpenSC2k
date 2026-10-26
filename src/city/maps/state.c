// TileInfo landscape state and rebuilding (zero-based bits):
// flags[1..2] is a permanent region interpreted by City's terrain_climate: mixed preset 0 temperate, 1 arctic,
// 2 tropical, 3 desert; desert preset 0/3 tropical, 1 desert, 2 rainforest. Uniform presets ignore these bits.
// terrain[0..3] stores current ground cover, including 1..4 snow coverage and 11/12 half/full desert on dry tiles.
// These cover states, neighboring regions and the City preset derive climate appearance; there is no climate byte.
// Landscape visits use clear-ground zone[5..7] as recovery timing or tree zone[5..7] as growth, never both.
// Family terrain[4..7], shared heights/slope, utility flags, overlays and underground follow their module contracts
// and tile_storage.h. Recounting reads them without introducing another per-tile encoding.

#include "city/maps/map.h"
#include "city/terrain/climate.h"
#include "city/maps/environment.h"

#include "city/engine/clock.h"
#include "city/infrastructure/utilities.h"
#include "city/infrastructure/power_lines.h"
#include "city/infrastructure/transport.h"
#include "city/maps/terrain.h"
#include "city/saveload/map_format.h"
#include "city/infrastructure/bridges.h"
#include "city/maps/ground.h"
#include "city/maps/buildings.h"
#include "city/maps/trees.h"
#include "city/maps/water.h"
#include "city/maps/module.h"
#include "city/state.h"
#include "city/presentation.h"
#include "game/errors.h"
#include "city/terrain/terrain.h"
#include <assert.h>
#include <string.h>

// Map changes

// Mark a changed tile and notify Drawing so each viewport can damage its old or new extent. Tile must be valid or
// INVALID_TILE, which is ignored; other indices assert, and disabled/full-map tracking needs no extra notification.
void InvalidateTile(City *city, TileIndex tile) {
  if (tile == INVALID_TILE) return;
  assert(tile < city->map_cell_count);
  SimulationMapChanges *changes = &city->map_changes;
  if (!changes->enabled || changes->full) return;
  InvalidateCityInformation(city->game, CITY_UPDATE_MAP);
  changes->tiles_changed = true;
  PublishCityMapTileDamage(city, tile);
}

// Invalidate a rectangle beginning at a valid tile, clipping its right and bottom edges to the map. INVALID_TILE is
// ignored; dimensions must describe the intended nonnegative area, and each included tile uses InvalidateTile.
void InvalidateTileRange(City *city, TileIndex start, int width, int height) {
  if (start == INVALID_TILE) return;
  assert(start < city->map_cell_count);
  if (!city->map_changes.enabled || city->map_changes.full) return;
  width = IntMin(width, city->map_width - MapTileX(city, start));
  height = IntMin(height, city->map_height - MapTileY(city, start));
  for (int y = 0; y < height; ++y) {
    TileIndex row = start + y * city->map_width;
    for (int x = 0; x < width; ++x) InvalidateTile(city, row + x);
  }
}

// Record one coarse-layer change for viewport-specific redraw. Layer must be a supported CityMapChangeLayer; disabled
// tracking leaves damage state unchanged.
void InvalidateMapLayer(City *city, CityMapChangeLayer layer) {
  if (city->map_changes.enabled) {
    city->map_changes.invalidated_layers |= 1 << layer;
    InvalidateCityInformation(city->game, CITY_UPDATE_MAP | CITY_UPDATE_UTILITIES);
  }
}

// Mark the whole map dirty when damage tracking is enabled. This supersedes later per-tile work; use it only for
// changes whose drawing impact is actually map-wide.
void InvalidateMap(City *city) {
  if (city->map_changes.enabled) {
    city->map_changes.full = true;
    InvalidateCityInformation(city->game, CITY_UPDATE_MAP);
  }
}

// Clear accumulated tile, layer and full-map changes, preserving the enabled policy. Presentation acknowledges
// a published batch after every view; City also uses this reset when replacing map storage.
void ClearSimulationMapChanges(SimulationMapChanges *changes) {
  changes->tiles_changed = false;
  changes->invalidated_layers = 0;
  changes->full = false;
}

// State

// Select a stable simulation counter slot from a stored native kind. Partial stock imports keep their source key;
// completed maps consolidate direction, style and appearance independently of stock artwork projection.
int CityTileStatisticsKey(const City *city, int kind) {
  if (!city->native_map_storage) return kind;
  if (IsTileKindShoreTree(kind)) return CityTreeTile(CityTreeCount(kind));
  switch (kind) {
  case TK_CLEAR: case TK_WATER: case TK_RIVER: case TK_WATER_BANK: return TSTAT_CLEAR;
  case TK_RUBBLE: return TSTAT_RUBBLE;
  case TK_PARK: return TSTAT_PARK;
  case TK_POWER_LINE: case TK_POWER_LINE_TOWER: return TSTAT_POWER_LINE;
  case TK_POWER_LINE_WATER: case TK_POWER_LINE_TOWER_WATER: return TSTAT_WATER_POWER_LINE;
  case TK_ROAD: return TSTAT_ROAD;
  case TK_RAIL: return TSTAT_RAIL;
  case TK_TUNNEL_ENTRANCE: return TSTAT_TUNNEL_ENTRANCE;
  case TK_ROAD_POWER_CROSSING: return TSTAT_ROAD_POWER_CROSSING;
  case TK_ROAD_RAIL_CROSSING: return TSTAT_ROAD_RAIL_CROSSING;
  case TK_RAIL_POWER_CROSSING: return TSTAT_RAIL_POWER_CROSSING;
  case TK_HIGHWAY_ENTRANCE: return TSTAT_HIGHWAY_ENTRANCE;
  case TK_HIGHWAY_INCLINE: return TSTAT_HIGHWAY_INCLINE;
  case TK_HIGHWAY_BEND: return TSTAT_HIGHWAY_BEND;
  case TK_HIGHWAY_JUNCTION: return TSTAT_HIGHWAY_JUNCTION;
  case TK_HIGHWAY: return TSTAT_HIGHWAY;
  case TK_HIGHWAY_RAIL: return TSTAT_HIGHWAY_RAIL;
  case TK_HIGHWAY_ROAD: return TSTAT_HIGHWAY_ROAD;
  case TK_HIGHWAY_POWER: return TSTAT_HIGHWAY_POWER;
  case TK_SUBWAY_TO_RAIL: return TSTAT_SUBWAY_TO_RAIL;
  default: break;
  }
  if (!IsTileKindBridge(kind)) return kind;
  switch (CityBridgeHeadKind(kind)) {
  case TK_ROAD_BRIDGE_HEAD:
    return IsTileKindBridgeHead(kind) ? TSTAT_ROAD_BRIDGE_HEAD : TSTAT_ROAD_BRIDGE;
  case TK_RAIL_BRIDGE_HEAD:
    return IsTileKindBridgeHead(kind) ? TSTAT_RAIL_BRIDGE_HEAD : TSTAT_RAIL_BRIDGE;
  default:
    return TSTAT_HIGHWAY_BRIDGE;
  }
}

// Surface counters include construction stage and occupied water power lines. Bridge underpasses are counted
// separately; this reads only the record, City import policy and immutable kind/building metadata.
int ReadCityTileStatisticsKey(const City *city, const TileInfo *tp) {
  if (city->native_map_storage && IsTileKindWaterPowerLine(tp->kind)) return TSTAT_WATER_POWER_LINE;
  int construction = CityBuildingConstructionKind(tp);
  return construction ? construction : CityTileStatisticsKey(city, tp->kind);
}

// Recount tile IDs after loading a city, except zone category 7. Requires a complete native map with the supported
// cell alignment; replaces counters and excludes military zoning as specified.
void RecountCityTileStatistics(City *city) {
  CityStatistics *statistics = &city->statistics;
  const TileInfo *tiles = city->tile;
  // Independent counters avoid one serial increment chain on mostly empty maps.
  // Every supported map has a cell count divisible by four.
  u32 counts[4][TK_COUNT] = {0};
  for (TileIndex tile = 0; tile < city->map_cell_count; tile += 4) {
    for (int lane = 0; lane < 4; ++lane) {
      const TileInfo *tp = &tiles[tile + lane];
      if (ReadCityZoneKind(tp) != ZONE_MILITARY)
        ++counts[lane][ReadCityTileStatisticsKey(city, tp)];
      int under = ReadCityBridgeUnderpassKind(tp);
      if (under) ++counts[lane][CityTileStatisticsKey(city, under)];
    }
  }
  for (int kind = 0; kind < TK_COUNT; ++kind)
    statistics->count[kind] = counts[0][kind] + counts[1][kind] + counts[2][kind] + counts[3][kind];
}

// Rebuild power, transport, zone and bond state after reading a city. Call only after successful decoding/conversion;
// rebuilds derived counts/networks instead of applying a second import over partial data.
void RebuildLoadedCityMapState(City *city) {
  city->infrastructure.service_bonus_b = 0;
  if (city->fixes & (1ull << CITY_FIX_LOADED_CONNECTIONS))
    RecountCityIndustryConnections(city);
  for (TileIndex tile = 0; tile < city->map_cell_count; ++tile) {
    const TileInfo *tp = &city->tile[tile];
    if (tp->overlay == CITY_OVERLAY_BORDER_CONNECTION && IsTileKindRoadRoute(tp->kind))
      ++city->infrastructure.service_bonus_b;
  }
  city->infrastructure.counter_a = 0;
  city->infrastructure.counter_b = 0;
  city->infrastructure.counter_c = 0;
  ScanPowerGridPhase(city);
  ScanCityWaterNetwork(city);
  AccumulateCityLandInfluence(city);
  city->disasters.event_cleanup_ticks = 0;
  city->indicators.bond_rate_total = 0;
  city->disasters.event_decay_ticks = 0;
  int count = (i32)city->indicators.bond_count;
  for (int index = 0; index < count; ++index)
    city->indicators.bond_rate_total += city->economic_tables.bond_rates.rate[index];
}

// Copy or downsample City terrain into caller-owned preview storage, then finish it using the supplied settings.
// City is read-only; returns false if preview backing cannot be prepared.
bool ReadCityTerrainPreview(const City *city, CityTerrainMap *map, const CityTerrainSettings *settings) {
  int divisor = IntMax(1, IntMax(city->map_width, city->map_height) / 256);
  int width = IntMax(128, city->map_width / divisor), height = IntMax(128, city->map_height / divisor);
  if (!ResetCityTerrainMap(map, width, height)) return false;
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

// Initialize imported native ground, climate, tree and power metadata when no landscape state exists. Reuses an
// existing nonzero landscape seed without rerandomizing trees; initialization may consume the independent landscape
// RNG and invalidates the map.
void InitializeCityLandscape(City *city) {
  if (city->landscape_random) {
    InitializeCityPowerConnections(city);
    return;
  }
  city->landscape_cursor = 1;
  city->landscape_tick = 0;
  city->landscape_random = city->random.crt ^ city->random.event ^ 0x6D2B79F5;
  if (!city->landscape_random) city->landscape_random = 1;
  TileIndex cells = city->map_cell_count;
  for (TileIndex tile = 0; tile < cells; ++tile) {
    TileInfo *tp = &city->tile[tile];
    int kind = tp->kind;
    if (kind == TK_CLEAR || IsTileKindExposedWater(kind) || IsTileKindTree(kind)) {
      tp->terrain &= 15;
      if (IsTileKindTree(kind)) {
        WriteTreeSpecies(tp, city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE) ?
            RandomCityTreeSpecies(city, ReadCityTileBaseClimate(city, tp)) : 0);
        WriteTreeGrowth(tp, CITY_TREE_GROWN);
      }
    }
  }
  InitializeCityPowerConnections(city);
  city->protect_terrain_structures = (city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) != 0;
  InvalidateMap(city);
}

// Finish generated ground, tree and climate metadata during terrain installation. Terrain is the caller's prepared
// map with dimensions/storage matching City; enabled fixes determine which landscape effects apply.
static void GenerateCityLandscape(City *city, const CityTerrainMap *terrain) {
  city->landscape_cursor = city->landscape_tick = city->landscape_random = 0;
  InitializeCityLandscape(city);
  if (!(city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE))) return;
  if (!(city->fixes & (1ull << CITY_FIX_LANDSCAPE_DETAILS))) return;
  city->terrain_climate = terrain->climate;
  city->desert_limit = terrain->desert_limit;
  city->snow.seed = terrain->snow.seed;
  city->snow.line = terrain->snow.line;
  city->snow.variation = terrain->snow.variation;
  InitializeTerrainSnowNoise(&city->snow, city->map_width, city->map_height);
  memcpy(city->snow.points, terrain->snow.points,
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
    WriteTileClimateRegion(tp, city->terrain_climate, CityTerrainTileClimate(terrain, tile));
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

// Install a prepared terrain map into the reset City's existing storage. Legacy conversion can fail after writes;
// success restores generator RNG state, statistics and climate before the caller resets the viewport.
bool InstallCityTerrainMap(City *city, const CityTerrainMap *map, const CityTerrainSettings *settings) {
  u32 cells = map->cell_count;
  for (TileIndex tile = 0; tile < cells; ++tile) {
    TileInfo *tp = &city->tile[tile];
    tp->altitude = map->altitude[tile];
    tp->zone = 0;
    tp->terrain = 0;
    if (map->legacy) tp->terrain = map->terrain[tile];
    else WriteTerrainCode(tp, map->terrain[tile]);
    tp->kind = map->legacy && !IsTileKindTree(map->buildings[tile]) ? TK_CLEAR : map->buildings[tile];
    tp->flags = map->flags[tile];
  }
  if (map->legacy) {
    BeginLegacyCityMapImport(city);
    if (!ConvertCityMapToNative(city)) {
      SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
      return false;
    }
  }
  RecountCityTileStatistics(city);
  city->random.crt = map->process_state;
  city->random.event = map->game_state;
  city->geography.rotation = 0;
  GenerateCityLandscape(city, map);
  city->geography.ocean = map->ocean;
  city->geography.river = map->river;
  city->geography.edit_height = map->water_level;
  city->geography.hills = settings->hills;
  city->geography.water = settings->water;
  city->geography.trees = settings->trees;
  return true;
}

// Native landscape follows OpenTTD's shuffled tile loop (landscape.cpp), eight-visit grass counter (clear_cmd.cpp), and
// coordinate-staggered tree loop (tree_cmd.cpp).
// Apply the landscape visit appropriate to a valid tile's water, ground, transport or trees. May change recovery, age and
// species using landscape RNG; the traversal owns the visit order.
static void VisitCityLandscapeTile(City *city, TileIndex tile) {
  const TileModule *module = TileModuleForClasses(ReadCityTileClasses(city->tile[tile].kind));
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
  if (!(city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE))) return;
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
