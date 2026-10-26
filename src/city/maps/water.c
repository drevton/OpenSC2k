// TileInfo storage for exposed/occupied water (zero-based bits):
// kind carries water presence: TK_WATER and TK_WATER_BANK can spread; TK_RIVER stays confined.
// Wet power lines, bridge middles, hydro, piers and marinas retain their own water-class kind; no TF_WATER exists.
// terrain[0..3] on a wet occupant is the underlying exposed kind minus TK_WATER: 0 water, 1 river, 2 bank.
// Exposed water stores that form in kind instead. Banks use terrain[0..3] for ground recovery (0 or 7..10).
// terrain[4..7] belongs to the occupant (wire connections, bridge metadata or building facing/links), not water.
// altitude[0..4] is bed height; altitude[5..9] is water height, both in map levels.
// zone[4] selects the bed slope in zone[0..3]; without it, the tile is flat and the low nibble can hold zoning.
// Rapids are derived from river+slope at water level, not stored as another water-form field.
// flags[0] selects salt versus fresh water.
// Banks use zone[5..7] for their modulo-eight ground recovery counter. Other shared fields follow tile_storage.h.

#include "city/maps/water.h"

#include "drawing/maps/map_sprites.h"
#include "city/infrastructure/lines.h"
#include "city/saveload/map_format.h"
#include "city/engine/random.h"
#include "city/infrastructure/power_lines.h"
#include "city/maps/terrain.h"
#include "city/maps/tables.h"
#include "city/maps/ground.h"

#include <assert.h>

#include "audio/types.h"
#include "city/engine/commands.h"
#include "city/events/events.h"
#include "city/maps/layers.h"
#include "city/maps/tile_storage.h"
#include "city/maps/module.h"
#include "city/maps/map.h"
#include "city/presentation.h"
#include "city/state.h"
#include "city/tools/tools.h"
#include "drawing/maps/map_terrain.h"
#include "drawing/maps/map_camera.h"
#include "game/errors.h"

// Flooding and channel grading share the north/east/south/west edge order.
static const i8 kTerrainChannelNeighborY[4] = {-1, 0, 1, 0};

static const i8 kTerrainChannelNeighborX[4] = {0, 1, 0, -1};

// Dry/wet forms share their occupant metadata. Unsupported submerged occupants become exposed water.
int CityTileKindForWater(int kind, bool water) {
  if (IsTileKindShoreTree(kind)) return water ? kind : CityTreeTile(CityTreeCount(kind));
  switch (kind) {
  case TK_CLEAR: return water ? TK_WATER : TK_CLEAR;
  case TK_WATER: case TK_RIVER: case TK_WATER_BANK: return water ? kind : TK_CLEAR;
  case TK_POWER_LINE: case TK_POWER_LINE_WATER: return water ? TK_POWER_LINE_WATER : TK_POWER_LINE;
  case TK_POWER_LINE_TOWER: case TK_POWER_LINE_TOWER_WATER:
    return water ? TK_POWER_LINE_TOWER_WATER : TK_POWER_LINE_TOWER;
  case TK_ROAD_BRIDGE_SPAN: case TK_ROAD_BRIDGE_WATER: return water ? TK_ROAD_BRIDGE_WATER : TK_ROAD_BRIDGE_SPAN;
  case TK_RAIL_BRIDGE_SPAN: case TK_RAIL_BRIDGE_WATER: return water ? TK_RAIL_BRIDGE_WATER : TK_RAIL_BRIDGE_SPAN;
  case TK_HIGHWAY_BRIDGE_SPAN: case TK_HIGHWAY_BRIDGE_WATER:
    return water ? TK_HIGHWAY_BRIDGE_WATER : TK_HIGHWAY_BRIDGE_SPAN;
  case TK_HYDRO_POWER: case TK_PIER: case TK_MARINA: return water ? kind : TK_CLEAR;
  default: return water ? TK_WATER : kind;
  }
}

// Test whether a valid tile belongs to saltwater coastline or touches one. Reads only the bounded neighborhood and
// does not change water flags or elevation.
static bool UsesRaisedCoast(const City *city, TileIndex tile) {
  if (ReadTileWaterKind(&city->tile[tile]) == CITY_WATER_SALT) return true;
  for (int direction = 0; direction < 8; ++direction) {
    TileIndex neighbor = GetRelativeTileOrInvalid(city, tile,
        kTerrainNeighborXOffsets[direction], kTerrainNeighborYOffsets[direction]);
    if (neighbor != INVALID_TILE && ReadTileWaterKind(&city->tile[neighbor]) == CITY_WATER_SALT)
      return true;
  }
  return false;
}

// Update water level and bed shape, preserving river behavior independently of salinity.
// A raised river bed becomes dry unless keep_rapids retains an existing sloped river surface.
// The caller invalidates old/new extents; kind changes also update tile statistics.
void SetCityTerrainWater(City *city, TileIndex tile, int water_height, int land_shape, bool keep_rapids) {
  assert(IsMapTileValid(city, tile));
  TileInfo *tp = &city->tile[tile];
  int altitude = tp->altitude, land = ReadCityLandHeight(tp);
  if (land_shape == CITY_TERRAIN_RAISE_LEVEL) land_shape = CITY_TERRAIN_FLAT;
  CityWaterKind water = ReadTileWaterKind(tp);
  int ground = ReadCityWaterGroundKind(tp);
  bool river = ground == TK_RIVER;
  bool coast = water != CITY_WATER_NONE ? water == CITY_WATER_SALT : UsesRaisedCoast(city, tile);
  bool was_wet = water != CITY_WATER_NONE;
  int bank_ground = IsTileKindShoreBank(tp->kind) ? ReadGroundKind(tp) : CITY_GROUND_GRASS;
  bool changed_bank = tp->kind != TK_WATER_BANK || ReadTerrainCode(tp) != land_shape ||
      ReadCityWaterHeight(tp) != water_height;
  bool was_rapids = keep_rapids && river && ReadTerrainCode(tp) && land == ReadCityWaterHeight(tp);
  bool wet = land < water_height || (land == water_height && (!land_shape || !river || was_rapids));
  if (!wet) {
    tp->flags &= ~TF_SALTWATER;
    WriteTerrainCode(tp, land_shape);
    ground = TK_CLEAR;
  } else {
    ground = river ? TK_RIVER : land == water_height && land_shape ? TK_WATER_BANK : TK_WATER;
    tp->flags = (tp->flags & ~TF_SALTWATER) | (coast ? TF_SALTWATER : 0);
  }
  int kind = CityTileKindForWater(tp->kind, wet);
  if (IsTileKindShoreTree(kind) && ground != TK_WATER_BANK) kind = ground;
  if (IsTileKindExposedWater(kind)) kind = ground;
  if (kind != tp->kind) ReplaceCityTileAndStatistics(city, tile, kind);
  if (wet) {
    // Replacing a dry building clears its old construction-stage bits, which
    // share this field. Install the water plane after that category change.
    tp->altitude = (altitude & CITY_ALTITUDE_CLEAR_WATER) | water_height << CITY_ALTITUDE_WATER_SHIFT;
    WriteTerrainCode(tp, land_shape);
    tp->terrain = (tp->terrain & 240) | (IsTileKindShoreBank(kind) ? bank_ground :
        IsTileKindExposedWater(kind) ? 0 : ground - TK_WATER);
    if (kind == TK_WATER_BANK && changed_bank) BeginCityBareGroundRecovery(city, tp);
  } else if (was_wet && (kind == TK_CLEAR || IsTileKindTree(kind))) {
    tp->altitude &= CITY_ALTITUDE_CLEAR_WATER;
    if (kind == TK_CLEAR) BeginCityBareGroundRecovery(city, tp);
  }
}

// A shoreline's low edge or corner becomes ordinary slope when newly created
// dry land meets it at water level. Its raised, landward edge remains a coast.
void ReclaimCityShorelineBanks(City *city, TileIndex land) {
  const TileInfo *ground = &city->tile[land];
  if (HasTileWater(ground)) return;
  CityTileGeometry geometry = ReadCityTileGeometry(city, land);
  int raised = kTerrainRaisedCorners[geometry.land_shape];
  bool changed = false;
  for (int direction = 0; direction < 8; ++direction) {
    int dx = kTerrainNeighborXOffsets[direction], dy = kTerrainNeighborYOffsets[direction];
    TileIndex neighbor = GetRelativeTileOrInvalid(city, land, dx, dy);
    if (neighbor == INVALID_TILE || !IsTileKindShoreBank(city->tile[neighbor].kind)) continue;
    CityTileGeometry bank = ReadCityTileGeometry(city, neighbor);
    if (bank.land_height != bank.water_height || !bank.land_shape) continue;
    int bank_raised = kTerrainRaisedCorners[bank.land_shape];
    bool touches = false;
    for (int corner = 0; corner < 4; ++corner) {
      if (geometry.land_height + ((raised >> corner) & 1) > bank.water_height) continue;
      for (int other = 0; other < 4; ++other)
        if (kTerrainCornerX[corner] == dx + kTerrainCornerX[other] &&
            kTerrainCornerY[corner] == dy + kTerrainCornerY[other] &&
            !(bank_raised & (1 << other))) touches = true;
    }
    if (!touches) continue;
    InvalidateTile(city, neighbor);
    int kind = city->tile[neighbor].kind;
    ReplaceCityTileAndStatistics(city, neighbor,
        IsTileKindShoreTree(kind) ? CityTreeTile(CityTreeCount(kind)) : TK_CLEAR);
    TileInfo *tp = &city->tile[neighbor];
    tp->altitude &= CITY_ALTITUDE_CLEAR_WATER;
    tp->flags &= ~TF_SALTWATER;
    RefreshCityTileClimates(city, neighbor);
    BeginCityBareGroundRecovery(city, tp);
    changed = true;
  }
  if (changed) {
    CityPoint point = MapTilePt(city, land);
    int left = IntMax(0, point.x - 2), top = IntMax(0, point.y - 2);
    InvalidateTileRange(city, MapTileXY(city, left, top),
        IntMin(city->map_width, point.x + 3) - left, IntMin(city->map_height, point.y + 3) - top);
  }
}

// Drain an admitted exposed water tile after the caller pays and invalidates its old extents. Preserve bed/slope;
// update kind, counters, salinity, climate and damage. Neighboring open water can flood it again.
static void ClearCityExposedWater(City *city, TileIndex tile_index) {
  ReplaceCityTileAndStatistics(city, tile_index, TK_CLEAR);
  TileInfo *tp = &city->tile[tile_index];
  tp->flags &= ~TF_SALTWATER;
  tp->altitude &= CITY_ALTITUDE_CLEAR_WATER;
  RefreshCityTileClimates(city, tile_index);
  BeginCityBareGroundRecovery(city, tp);
  ReclaimCityShorelineBanks(city, tile_index);
  CityPoint point = MapTilePt(city, tile_index);
  int left = IntMax(0, point.x - 1), top = IntMax(0, point.y - 1);
  InvalidateTileRange(city, MapTileXY(city, left, top), point.x + 2 - left, point.y + 2 - top);
}

// Convert a dry tile to freshwater river at its bed elevation, preserving its slope.
void CreateCityRiverTile(City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile));
  TileInfo *tp = &city->tile[tile];
  if (HasTileWater(tp)) return;
  int shape = ReadTerrainCode(tp), water = ReadCityLandHeight(tp);
  ReplaceCityTileAndStatistics(city, tile, TK_CLEAR);
  // A new river starts at bed level. Its kind retains channel behavior on both
  // flat stretches and rapids, independently of the open-water datum.
  tp->altitude = (tp->altitude & CITY_ALTITUDE_CLEAR_WATER) | water << CITY_ALTITUDE_WATER_SHIFT;
  tp->flags &= ~TF_SALTWATER;
  tp->kind = TK_RIVER;
  SetCityTerrainWater(city, tile, water, shape, true);
  CityPoint point = MapTilePt(city, tile);
  int left = IntMax(0, point.x - 1), top = IntMax(0, point.y - 1);
  InvalidateTileRange(city, MapTileXY(city, left, top), point.x + 2 - left, point.y + 2 - top);
  RefreshCityPowerConnections(city, tile);
  RefreshCityTileClimates(city, tile);
}

// Reclaim low clear ground during a landscape visit, retaining its bed and slope. Cardinal neighbors must reach a
// shared edge; rivers cannot spread. The City rule gates flat land only, so shore slopes remain eligible.
// This allocates nothing and consumes no RNG.
bool FloodCityClearTile(City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile) && city->tile[tile].kind == TK_CLEAR);
  TileInfo *tp = &city->tile[tile];
  int shape = ReadTerrainCode(tp);
  if (!shape && !(city->fixes & (1ull << CITY_FIX_WATER_SPREADING))) return false;
  int land = ReadCityLandHeight(tp), water = -1, salt = 0;
  CityPoint point = MapTilePt(city, tile);
  for (int direction = 0; direction < 4; ++direction) {
    TileIndex next = MapTileXYOrInvalid(city, point.x + kTerrainChannelNeighborX[direction],
        point.y + kTerrainChannelNeighborY[direction]);
    if (next == INVALID_TILE) continue;
    const TileInfo *neighbor = &city->tile[next];
    int ground = ReadCityWaterGroundKind(neighbor);
    if (ground != TK_WATER && ground != TK_WATER_BANK) continue;
    int height = ReadCityWaterHeight(neighbor);
    if (height < land || height < water || CityTerrainEdgeHeight(tp, neighbor, direction) > height) continue;
    salt = height > water ? neighbor->flags & TF_SALTWATER : salt | (neighbor->flags & TF_SALTWATER);
    water = height;
  }
  if (water < 0) return false;
  InvalidateTile(city, tile);
  SetCityTerrainWater(city, tile, water, shape, false);
  tp->flags = (tp->flags & ~TF_SALTWATER) | salt;
  RefreshCityTileClimates(city, tile);
  int left = IntMax(0, point.x - 1), top = IntMax(0, point.y - 1);
  InvalidateTileRange(city, MapTileXY(city, left, top), point.x + 2 - left, point.y + 2 - top);
  return true;
}

static const i8 kTerrainChannelTurns[4] = {0, 1, 3, 2};

// Apply the selected water level across the complete edit map, normalizing terrain and removing submerged tree/park
// tiles. Requires bound layers and a supported height; this is a map-wide edit rather than a local placement.
void ApplyTerrainWaterLevel(City *city, int height) {
  for (int y = 0; y < city->map_height; ++y) {
    TileIndex tile = MapTileXY(city, 0, y);
    for (int x = 0; x < city->map_width; ++x, ++tile) {
      TileInfo *tp = &city->tile[tile];
      // Raising/lowering the open-water datum never changes a confined river.
      if (ReadCityWaterGroundKind(tp) == TK_RIVER) continue;
      int altitude = tp->altitude;
      int elevation = ReadCityLandHeight(tp);
      if (city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) {
        SetCityTerrainWater(city, tile, height, ReadTerrainCode(tp), true);
        continue;
      }
      if (height <= elevation) {
        if (HasTileWater(tp)) {
          int terrain_code = ReadLegacyTerrainCode(city, tile);
          if (terrain_code < CITY_TERRAIN_BANK_48) {
            // A permanently submerged building lost its stage. Receding water
            // starts it at zero, rather than interpreting water height as stage.
            int water_height = IsTileKindBuilding(tp->kind) ? 0 : height;
            tp->altitude = (altitude & CITY_ALTITUDE_CLEAR_WATER) | (water_height *
                CITY_ALTITUDE_WATER_STEP);
            WriteLegacyCityTerrainCode(city, tile, (u8)(terrain_code & CITY_TERRAIN_SHAPE_MASK));
          } else {
            WriteLegacyCityTerrainCode(city, tile, ReadTerrainCode(tp));
          }
        }
        continue;
      }

      int terrain_code = ReadLegacyTerrainCode(city, tile);
      tp->altitude = (altitude & CITY_ALTITUDE_CLEAR_WATER) | (height *
          CITY_ALTITUDE_WATER_STEP);
      int kind = tp->kind;
      if (IsTileKindTreeOrPark(kind))
        ReplaceCityTileAndStatistics(city, tile, TK_CLEAR);
      tp->kind = TK_WATER;
      if (terrain_code < CITY_TERRAIN_BANK_48) {
        int terrain = terrain_code & CITY_TERRAIN_SHAPE_MASK;
        int slope =
            elevation + 1 >= height && terrain_code != CITY_TERRAIN_FLAT ? CITY_TERRAIN_BANK_32 :
                CITY_TERRAIN_WATER_FIRST;
        WriteLegacyCityTerrainCode(city, tile, (u8)(terrain + slope));
      } else {
        WriteLegacyCityTerrainCode(city, tile, CITY_TERRAIN_WATER_FIRST);
      }
    }
  }
  if (city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE))
    for (TileIndex tile = 0; tile < (u32)city->map_cell_count; ++tile) ReclaimCityShorelineBanks(city, tile);
  InitializeCityPowerConnections(city);
  RecountCityTileStatistics(city);
}

// Move the terrain editor's water level by one step and apply it across the map when within bounds. Direction must be
// -1 or 1; an applied edit refreshes the map and restores the cursor, while a boundary attempt only sets busy.
void AdjustCityTerrainWaterLevel(City *city, int direction) {
  assert(direction == -1 || direction == 1);
  SetCityCursor(city, CITY_CURSOR_BUSY, CITY_CURSOR_SELECT);
  if (direction > 0 ? city->geography.edit_height <= 30 : city->geography.edit_height >= 1) {
    city->geography.edit_height += direction;
    ApplyTerrainWaterLevel(city, city->geography.edit_height);
    RefreshCityMapTiles(city);
    SetCityCursor(city, CITY_CURSOR_ARROW, CITY_CURSOR_SELECT);
  }
}

// Grade a downhill channel from a valid tile, stopping at existing water or the map boundary. Length controls
// the maximum reach; writes terrain in place and the caller has no meaningful numeric result to consume.
void CarveTerrainWaterChannel(City *city, TileIndex tile_index, int length) {
  assert(IsMapTileValid(city, tile_index));
  CityPoint point = MapTilePt(city, tile_index);
  int heading = 1;
  int remaining = length;
  const TileInfo *start = &city->tile[tile_index];
  if ((city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) &&
      !HasTileWater(start) && ReadTerrainCode(start) > CITY_TERRAIN_SHAPE_4) return;
  CreateCityRiverTile(city, tile_index);
  while (remaining != 0) {
    --remaining;
    int elevation = ReadCityLandHeight(&city->tile[tile_index]);
    if (ReadCityTileGeometry(city, tile_index).water_surface == CITY_WATER_SURFACE_RAPIDS)
      ++elevation;
    int choice;
    int next_y = point.y, next_x = point.x;
    TileIndex next_tile = INVALID_TILE;
    for (choice = 0; choice < 4; ++choice) {
      int direction = ((u8)heading + (u8)kTerrainChannelTurns[choice]) & 3;
      next_y = point.y + kTerrainChannelNeighborY[direction];
      next_x = point.x + kTerrainChannelNeighborX[direction];
      next_tile = MapTileXYOrInvalid(city, next_x, next_y);
      if (next_tile == INVALID_TILE) return;
      const TileInfo *tp = &city->tile[next_tile];
      int next_elevation = ReadCityLandHeight(tp);
      if (next_elevation > elevation)
        continue;
      int terrain_code = ReadTerrainCode(tp);
      if ((city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) &&
          !HasTileWater(tp) && terrain_code > CITY_TERRAIN_SHAPE_4) continue;
      if (HasTileWater(tp))
        return;
      if (next_elevation < elevation || terrain_code == CITY_TERRAIN_FLAT)
        break;
    }
    if (choice == 4)
      return;
    point = (CityPoint){next_x, next_y};
    tile_index = next_tile;
    CreateCityRiverTile(city, tile_index);
    heading += kTerrainChannelTurns[choice];
    if (Sc2kCrtRandRange(city, 3) != 0)
      heading = (heading + 2 * Sc2kCrtRand(city) + 1) & 3;
  }
}

static bool CanPlaceStreamCell(const TileInfo *tp) {
  return IsTileKindUnbuilt(tp->kind) && tp->kind != TK_RADIOACTIVE_WASTE &&
      ReadCityZoneKind(tp) == ZONE_NONE && ReadTileUndergroundKind(tp) == TUK_CLEAR && !tp->overlay;
}

// Grow a freshwater channel downhill, favoring the current heading with occasional gentle turns. Admission only
// checks the source; execution consumes City RNG and stops at water, obstructions, a basin or the length limit.
i64 CmdPlaceStream(City *city, u32 flags, TileIndex tile, int length) {
  if (!IsMapTileValid(city, tile) || length < 1 || length > 256) return CITY_COMMAND_COST_INVALID;
  if (city->settings.timer_mode != CITY_TIMER_STOPPED || HasTileWater(&city->tile[tile]) ||
      !CanPlaceStreamCell(&city->tile[tile])) return CITY_COMMAND_COST_BLOCKED;
  if (flags & CITY_COMMAND_TEST) return 0;
  int heading = Sc2kCrtRandRange(city, 4);
  int reach = length / 2 + Sc2kCrtRandRange(city, length - length / 2 + 1);
  TileIndex previous = INVALID_TILE;
  int placed = 0;
  do {
    int height = ReadCityLandHeight(&city->tile[tile]);
    InvalidateTile(city, tile);
    CreateCityRiverTile(city, tile);
    ++placed;
    if (placed >= reach) break;
    if (Sc2kCrtRandRange(city, 5) == 0)
      heading = (heading + (Sc2kCrtRandRange(city, 2) ? 1 : 3)) & 3;
    static const int kTurns[4] = {0, 1, -1, 2};
    TileIndex next = INVALID_TILE;
    int next_heading = heading, lowest = height + 1;
    for (int turn = 0; turn < 4; ++turn) {
      int direction = (heading + kTurns[turn]) & 3;
      TileIndex candidate = GetRelativeTileOrInvalid(city, tile, kLineXStep[direction], kLineYStep[direction]);
      if (candidate == INVALID_TILE || candidate == previous || !CanPlaceStreamCell(&city->tile[candidate])) continue;
      CityTileGeometry geometry = ReadCityTileGeometry(city, candidate);
      int elevation = geometry.water != CITY_WATER_NONE ? geometry.surface_height : geometry.land_height;
      if (elevation >= lowest) continue;
      lowest = elevation;
      next = candidate;
      next_heading = direction;
    }
    if (next == INVALID_TILE || HasTileWater(&city->tile[next])) break;
    previous = tile;
    tile = next;
    heading = next_heading;
  } while (true);
  return placed;
}

// Check whether a valid tile can be converted to a canal. Uses native terrain, water and obstruction rules only; it
// does not charge, alter geometry or trigger presentation.
bool CanPlaceCityCanal(const City *city, TileIndex tile) {
  if (!IsMapTileValid(city, tile)) return false;
  const TileInfo *tp = &city->tile[tile];
  // Active events can expose virtual overlay IDs above the stored byte range.
  int overlay = ReadOverlay(city, tile);
  return !HasTileWater(tp) &&
      !(overlay >= CITY_OVERLAY_BORDER_CONNECTION && overlay < 256) &&
      (!(city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) || ReadTerrainCode(tp) <= CITY_TERRAIN_SHAPE_4) &&
      IsTileKindLandscape(tp->kind) && tp->kind != TK_RADIOACTIVE_WASTE;
}

// Quote raw canal construction over eligible tiles in an inclusive rectangle. Invalid endpoints are rejected;
// the query neither applies monetary policy nor consumes RNG.
static CityCommandCost GetCityCanalRectangleCost(const City *city, TileIndex start, TileIndex end) {
  CityCommandCost quote = {.error = CITY_COMMAND_COST_INVALID, .error_tile = INVALID_TILE};
  if (!IsMapTileValid(city, start) || !IsMapTileValid(city, end)) return quote;
  CityRect bounds = MapTileRectangle(city, start, end);
  for (int y = bounds.top; y < bounds.bottom; ++y) {
    TileIndex tile = MapTileXY(city, bounds.left, y);
    for (int x = bounds.left; x < bounds.right; ++x, ++tile)
      if (CanPlaceCityCanal(city, tile)) quote.cost += CITY_CANAL_COST;
  }
  quote.error = !quote.cost ? CITY_COMMAND_COST_BLOCKED : CITY_COMMAND_COST_OK;
  return quote;
}

// Check canal admission once, then convert the tile and clear its zoning. Returns BLOCKED without effects or 1
// after applying the edit. Pricing and payment belong to the caller; recorded Game errors remain separate.
i64 ApplyWaterToolAtTile(City *city, TileIndex tile) {
  if (!CanPlaceCityCanal(city, tile)) return CITY_COMMAND_COST_BLOCKED;
  CreateCityRiverTile(city, tile);
  WriteCityZoneKind(&city->tile[tile], ZONE_NONE);
  return 1;
}

// Convert eligible cells in the resolved inclusive rectangle to canals. Rejects reserved options and invalid
// endpoints; TEST only quotes the work and execution preserves admission errors and per-cell charging.
i64 CmdBuildCanalsRect(City *city, u32 flags, TileIndex location, TileIndex end, int options) {
  int invalid = CITY_COMMAND_COST_INVALID;
  if (options || !(city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE))) return invalid;
  CityCommandCost quote = GetCityCanalRectangleCost(city, location, end);
  if (flags & CITY_COMMAND_TEST) return CityCommandPriceResult(quote);
  if (quote.error) return quote.error;
  i64 affordable = AffordableCityCommandCost(city, quote.cost);
  if (affordable < 0) return affordable;
  CityRect bounds = MapTileRectangle(city, location, end);
  ClearGameError(city->game);
  for (int y = bounds.top; y < bounds.bottom; ++y) {
    TileIndex tile = MapTileXY(city, bounds.left, y);
    for (int x = bounds.left; x < bounds.right; ++x, ++tile)
      if (ApplyWaterToolAtTile(city, tile) >= 0) DebitCityCommandCost(city, CITY_CANAL_COST);
  }
  PlayCitySound(city, location, CITY_SOUND_WATER);
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  return GetGameError(city->game) ? CITY_COMMAND_COST_REJECTED : 1;
}

// Adjust the shared terrain editing height by the resolved direction. Only -1 and 1 are admitted; TEST validates
// that direction without effects, while execution reports BLOCKED if the water level cannot change.
i64 CmdAdjustTerrainHeight(City *city, u32 flags, TileIndex location, int direction) {
  (void)location;
  if (direction != 1 && direction != -1) return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) return 0;
  int previous_height = city->geography.edit_height;
  AdjustCityTerrainWaterLevel(city, direction);
  return city->geography.edit_height != previous_height ? 1 : CITY_COMMAND_COST_BLOCKED;
}

// Carve a water channel from the resolved location and length using the original grading algorithm. Validates the
// command payload; TEST does not alter terrain and execution preserves the action's meaningful result.
i64 CmdCarveWaterChannel(City *city, u32 flags, TileIndex location, int parameter1) {
  int invalid = CITY_COMMAND_COST_INVALID;
  if (!IsMapTileValid(city, location) || parameter1 < 0 || parameter1 > 32767)
    return invalid;
  if (flags & CITY_COMMAND_TEST) return 0;
  ClearGameError(city->game);
  CarveTerrainWaterChannel(city, location, parameter1);
  return GetGameError(city->game) ? CITY_COMMAND_COST_REJECTED : 1;
}

// Apply a canal/water tile through the admitted single-cell terrain operation. Validates tile and reserved parameters;
// TEST uses CanPlaceCityCanal without RNG or map edits.
i64 CmdApplyWaterTile(City *city, u32 flags, TileIndex location,
    int parameter1) {
  int invalid = CITY_COMMAND_COST_INVALID;
  if (!IsMapTileValid(city, location) || parameter1 != 0)
    return invalid;
  if (flags & CITY_COMMAND_TEST) return CanPlaceCityCanal(city, location) ? 0 : CITY_COMMAND_COST_BLOCKED;
  ClearGameError(city->game);
  return ApplyWaterToolAtTile(city, location);
}

static int ShoreClimate(const City *city, TileIndex tile) {
  int climate = ReadCityTileClimate(city, tile);
  if (climate == CLIMATE_SIMCITY) return climate;
  int snow = CityClimateSnowCoverage(climate) ? climate : 0, desert = 0;
  CityPoint point = MapTilePt(city, tile);
  int x = point.x, y = point.y;
  // Banks store the submerged base's climate. Borrow coverage from the
  // adjacent dry land so a lower bank does not leave a green coastal strip.
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      int nx = x + dx, ny = y + dy;
      if (!dx && !dy) continue;
      TileIndex neighbor = MapTileXYOrInvalid(city, nx, ny);
      if (neighbor == INVALID_TILE) continue;
      if (HasTileWater(&city->tile[neighbor])) continue;
      int adjacent = ReadCityTileClimate(city, neighbor);
      if (CityClimateSnowCoverage(adjacent) > CityClimateSnowCoverage(snow)) snow = adjacent;
      if (adjacent >= CLIMATE_DESERT_50 && adjacent <= CLIMATE_DESERT_100)
        desert = IntMax(desert, adjacent);
    }
  }
  if (snow) return snow;
  if (desert) return IntMax(desert,
      climate >= CLIMATE_DESERT_50 && climate <= CLIMATE_DESERT_100 ? climate : 0);
  return climate;
}

static int FlatWaterSprite(const City *city, TileIndex tile, int zoom) {
  // Fresh rivers and lakes can share the sea's height. Select their upstream
  // river layout by water kind rather than the editable terrain water datum.
  if (ReadTileWaterKind(&city->tile[tile]) == CITY_WATER_FRESH)
    return MAP_SLOPED_WATER_SPRITE_FIRST + zoom * MAP_SELECTION_SPRITE_SHAPES;
  return MapSpriteZoomIndex(CITY_SPRITE_CATALOG_FLAT_WATER, zoom);
}

// Select the same camera-relative water and bank layers for map drawing and retained tile previews.
MapRiverSprites ReadCityRiverSprites(const City *city, TileIndex tile, int zoom,
    const CityTileGeometry *geometry) {
  MapRiverSprites sprites = {0};
  if (!(city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE))) return sprites;
  int kind = city->tile[tile].kind;
  int first = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM - CITY_LANDSCAPE_GROUND_FIRST;
  if (!geometry->surface_shape && geometry->water == CITY_WATER_FRESH) {
    static const i8 kNeighborX[] = {-1, 0, 1, 0, -1, 1, 1, -1};
    static const i8 kNeighborY[] = {0, 1, 0, -1, 1, 1, -1, -1};
    int water = 0;
    // Diagonal land can need its own bank even when other sides also border land.
    for (int i = 0; i < 8; ++i) {
      TileIndex neighbor = CityMapViewNeighbor(city, tile, kNeighborX[i], kNeighborY[i]);
      if (neighbor == INVALID_TILE || HasTileWater(&city->tile[neighbor])) water |= 1 << i;
    }
    sprites.sprite[sprites.count++] = FlatWaterSprite(city, tile, zoom);
    if (water == UINT8_MAX) return sprites;
    int climate = ShoreClimate(city, tile);
    for (int piece = 0; piece < CITY_LANDSCAPE_RIVER_BANK_PIECES; ++piece) {
      bool draw;
      if (piece < 4) {
        draw = !(water & (1 << piece));
      } else {
        int corner = (piece - 4) & 3, sides = (1 << corner) | (1 << ((corner + 1) & 3));
        draw = piece < 8 ? !(water & sides) :
            (water & sides) == sides && !(water & (1 << (corner + 4)));
      }
      if (draw) sprites.sprite[sprites.count++] = first + CITY_LANDSCAPE_RIVER_BANK_FIRST +
          climate * CITY_LANDSCAPE_RIVER_BANK_PIECES + piece;
    }
  } else if ((IsTileKindExposedWater(kind) || kind == TK_HYDRO_POWER) &&
      geometry->water_surface == CITY_WATER_SURFACE_RAPIDS &&
      geometry->surface_shape >= 1 && geometry->surface_shape <= CITY_LANDSCAPE_RIVER_SLOPES) {
    int shape = geometry->surface_shape, climate = ShoreClimate(city, tile);
    sprites.sprite[sprites.count++] = CityWaterSprite(city, tile, zoom, geometry);
    for (int side = 0; side < CITY_LANDSCAPE_RIVER_SIDES; ++side) {
      int dx = shape & 1 ? side * 2 - 1 : 0;
      int dy = shape & 1 ? 0 : 1 - side * 2;
      TileIndex neighbor = CityMapViewNeighbor(city, tile, dx, dy);
      if (neighbor != INVALID_TILE && HasTileWater(&city->tile[neighbor])) continue;
      sprites.sprite[sprites.count++] = first + CITY_LANDSCAPE_RIVER_SIDE_FIRST +
          (climate * CITY_LANDSCAPE_RIVER_SLOPES + shape - 1) * CITY_LANDSCAPE_RIVER_SIDES + side;
    }
  }
  return sprites;
}

// Draw water and adjoining riverbanks, including slope edges beneath hydro plants.
// The selected water surface stays intact; painting never changes the tile or water behavior.
bool DrawCityWaterBank(const City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry) {
  MapRiverSprites sprites = ReadCityRiverSprites(city, tile, zoom, geometry);
  if (sprites.count <= 1) return false;
  for (int i = 0; i < sprites.count; ++i)
    DrawCityMapSpriteAtBaseline(city->drawing, sprites.sprite[i], screen_x, ground_y, 0);
  return true;
}

static bool DrawCityRiverTile(const City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *tile_geometry) {
  Drawing *drawing = city->drawing;
  CityTileGeometry geometry = *tile_geometry;
  if (!(city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) || !IsTileKindExposedWater(city->tile[tile].kind) ||
      geometry.water_surface != CITY_WATER_SURFACE_RAPIDS) return false;
  int shape = geometry.surface_shape;
  if (shape < 1 || shape > CITY_LANDSCAPE_RIVER_SLOPES) return false;
  if (!IsCityMapGroundPass(drawing)) {
    DrawCityMapPlacementTile(city, tile);
    return true;
  }
  DrawCityMapTerrainSkirt(city, tile, zoom, screen_x, ground_y, geometry.land_height);
  int face = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM - CITY_LANDSCAPE_GROUND_FIRST +
      CITY_LANDSCAPE_TERRAIN_FACE_FIRST + shape;
  DrawCityMapTerrainFace(city, tile, zoom, face, screen_x, ground_y,
      CityMapSlopeFaceSides(city, tile, shape, geometry.land_height));
  MapRiverSprites sprites = ReadCityRiverSprites(city, tile, zoom, &geometry);
  for (int i = 0; i < sprites.count; ++i)
    DrawCityMapSpriteAtBaseline(drawing, sprites.sprite[i], screen_x, ground_y, 0);
  DrawCityMapPlacementTile(city, tile);
  return true;
}

// Select the wet surface from camera geometry, including retained water below infrastructure and hydro spillways.
int CityWaterSprite(const City *city, TileIndex tile, int zoom, const CityTileGeometry *geometry) {
  if (city->tile[tile].kind == TK_HYDRO_POWER &&
      geometry->land_shape >= CITY_TERRAIN_SHAPE_1 && geometry->land_shape <= CITY_TERRAIN_SHAPE_4)
    return MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM - CITY_LANDSCAPE_GROUND_FIRST +
        CITY_LANDSCAPE_HYDRO_WATER_FIRST + geometry->land_shape - 1;
  if (geometry->water_surface == CITY_WATER_SURFACE_RAPIDS)
    return MAP_SLOPED_WATER_SPRITE_FIRST + zoom * MAP_SELECTION_SPRITE_SHAPES + geometry->surface_shape;
  if (geometry->water_surface == CITY_WATER_SURFACE_SHORE) {
    int ground = ReadGroundKind(&city->tile[tile]);
    if (ground >= CITY_GROUND_RECOVERY_FIRST && ground <= CITY_GROUND_RECOVERY_LAST) {
      int stage = IntMax(0, ground - CITY_GROUND_RECOVERY_GRASS_FIRST);
      return MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM -
          CITY_LANDSCAPE_GROUND_FIRST + CITY_LANDSCAPE_COAST_RECOVERY_FIRST +
          stage * CITY_LANDSCAPE_COAST_SHAPES + geometry->surface_shape;
    }
    int native = CITY_LANDSCAPE_COAST_FIRST +
        ShoreClimate(city, tile) * CITY_LANDSCAPE_COAST_SHAPES + geometry->surface_shape;
    return MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM +
        native - CITY_LANDSCAPE_GROUND_FIRST;
  }
  return FlatWaterSprite(city, tile, zoom);
}

static void TickCityWaterTile(City *city, TileIndex tile) {
  if (city->tile[tile].kind == TK_WATER_BANK) AdvanceCityGroundRecovery(city, tile);
}

static i64 ClearWaterTile(City *city, u32 flags, TileIndex tile) {
  if (flags & CITY_COMMAND_TEST) {
    const TileInfo *tp = &city->tile[tile];
    return ReadCityWaterHeight(tp) > ReadCityLandHeight(tp) ? CITY_COMMAND_COST_CLEAR_DEEP_WATER : CITY_COMMAND_COST_OK;
  }
  ClearCityExposedWater(city, tile);
  return 1;
}

const TileModule kCityWaterModule = {
  .clear = ClearWaterTile, .tick = TickCityWaterTile, .draw = DrawCityRiverTile,
};
