// TileInfo storage for exposed/occupied water (zero-based bits):
// kind carries water presence: TK_WATER spreads, TK_RIVER stays confined, TK_WATER_BANK selects shore behavior.
// Wet power lines, bridge middles, hydro, piers and marinas retain their own water-class kind; no TF_WATER exists.
// terrain[0..3] on a wet occupant is the underlying exposed kind minus TK_WATER: 0 water, 1 river, 2 bank.
// Exposed water stores that form in kind instead. Banks use terrain[0..3] for ground recovery (0 or 7..10).
// terrain[4..7] belongs to the occupant (wire connections, bridge metadata or building facing/links), not water.
// altitude[0..4] is bed height; altitude[5..9] is water height, both in map levels. zone[4] selects the bed slope in
// zone[0..3]; without it, the tile is flat and the low nibble can hold zoning. Rapids are derived from river+slope
// at water level, not stored as another water-form field. flags[0] selects salt versus fresh water.
// Banks use zone[5..7] for their modulo-eight ground recovery counter. Other shared fields follow tile_storage.h.

#include "city/infrastructure/power_lines.h"
#include "city/maps/water.h"
#include "city/maps/ground.h"

#include <assert.h>

#include "audio/audio.h"
#include "city/engine/commands.h"
#include "city/engine/simulation.h"
#include "city/events/events.h"
#include "city/infrastructure/infrastructure.h"
#include "city/maps/layers.h"
#include "city/maps/module.h"
#include "city/presentation.h"
#include "city/state.h"
#include "city/tools/tools.h"
#include "drawing/maps/map_terrain.h"
#include "game/errors.h"
#include "game/state.h"

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

// Drain one admitted exposed water tile to bare ground without changing its bed or slope.
// Neighboring open water can flood it again.
void ClearCityExposedWater(City *city, TileIndex tile_index) {
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

// Reclaim low clear ground during a landscape visit, retaining its bed and slope. Cardinal neighbors must reach a
// shared edge; confined rivers cannot flood out of their channel. This allocates nothing and consumes no RNG.
bool FloodCityClearTile(City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile) && city->tile[tile].kind == TK_CLEAR);
  static const i8 kNeighborX[4] = {0, 1, 0, -1}, kNeighborY[4] = {-1, 0, 1, 0};
  TileInfo *tp = &city->tile[tile];
  int land = ReadCityLandHeight(tp), water = -1, salt = 0;
  for (int direction = 0; direction < 4; ++direction) {
    TileIndex next = GetRelativeTileOrInvalid(city, tile, kNeighborX[direction], kNeighborY[direction]);
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
  SetCityTerrainWater(city, tile, water, ReadTerrainCode(tp), false);
  tp->flags = (tp->flags & ~TF_SALTWATER) | salt;
  RefreshCityTileClimates(city, tile);
  CityPoint point = MapTilePt(city, tile);
  int left = IntMax(0, point.x - 1), top = IntMax(0, point.y - 1);
  InvalidateTileRange(city, MapTileXY(city, left, top), point.x + 2 - left, point.y + 2 - top);
  return true;
}

static const i8 kTerrainChannelNeighborY[4] = {-1, 0, 1, 0};

static const i8 kTerrainChannelNeighborX[4] = {0, 1, 0, -1};

static const i8 kTerrainChannelTurns[4] = {0, 1, 3, 2};

// Apply the selected water level across the complete edit map, normalizing terrain and removing submerged tree/park
// tiles. Requires bound layers and a supported height; this is a map-wide edit rather than a local placement.
void ApplyTerrainWaterLevel(City *city, int height) {
  for (int y = 0; y < city->map_height; ++y) {
    for (int x = 0; x < city->map_width; ++x) {
      const TileIndex cell_index = MapTileXY(city, x, y);
      TileInfo *tp = &city->tile[cell_index];
      // Raising/lowering the open-water datum never changes a confined river.
      if (ReadCityWaterGroundKind(tp) == TK_RIVER) continue;
      int altitude = tp->altitude;
      int elevation = ReadCityLandHeight(tp);
      if (city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE)) {
        SetCityTerrainWater(city, cell_index, height, ReadTerrainCode(tp), true);
        continue;
      }
      if (height <= elevation) {
        if (HasTileWater(tp)) {
          int terrain_code = ReadLegacyTerrainCode(city, cell_index);
          if (terrain_code < CITY_TERRAIN_BANK_48) {
            // A permanently submerged building lost its stage. Receding water
            // starts it at zero, rather than interpreting water height as stage.
            int water_height = IsTileKindBuilding(tp->kind) ? 0 : height;
            tp->altitude = (altitude & CITY_ALTITUDE_CLEAR_WATER) | (water_height *
                CITY_ALTITUDE_WATER_STEP);
            WriteLegacyCityTerrainCode(city, cell_index, (u8)(terrain_code & CITY_TERRAIN_SHAPE_MASK));
          } else {
            WriteLegacyCityTerrainCode(city, cell_index, ReadTerrainCode(tp));
          }
        }
        continue;
      }

      int terrain_code = ReadLegacyTerrainCode(city, cell_index);
      tp->altitude = (altitude & CITY_ALTITUDE_CLEAR_WATER) | (height *
          CITY_ALTITUDE_WATER_STEP);
      int tile = tp->kind;
      if (IsTileKindTreeOrPark(tile))
        ReplaceCityTileAndStatistics(city, cell_index, TK_CLEAR);
      tp->kind = TK_WATER;
      if (terrain_code < CITY_TERRAIN_BANK_48) {
        int terrain = terrain_code & CITY_TERRAIN_SHAPE_MASK;
        int slope =
            elevation + 1 >= height && terrain_code != CITY_TERRAIN_FLAT ? CITY_TERRAIN_BANK_32 :
                CITY_TERRAIN_WATER_FIRST;
        WriteLegacyCityTerrainCode(city, cell_index, (u8)(terrain + slope));
      } else {
        WriteLegacyCityTerrainCode(city, cell_index, CITY_TERRAIN_WATER_FIRST);
      }
    }
  }
  if (city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE))
    for (TileIndex tile = 0; tile < (u32)city->map_cell_count; ++tile) ReclaimCityShorelineBanks(city, tile);
  InitializeCityPowerConnections(city);
  RecountCityTileStatistics(city);
}

// Move the terrain editor's water level by one step and apply it across the map when within bounds. Direction must be
// -1 or 1; an applied edit refreshes the map and restores the cursor, while a boundary attempt only sets busy.
void AdjustCityTerrainWaterLevel(City *city, int direction) {
  assert(direction == -1 || direction == 1);
  SetCityCursor(city, CITY_CURSOR_BUSY, 0);
  if (direction > 0 ? city->geography.edit_height <= 30 : city->geography.edit_height >= 1) {
    city->geography.edit_height += direction;
    ApplyTerrainWaterLevel(city, city->geography.edit_height);
    RefreshCityMapTiles(city);
    SetCityCursor(city, CITY_CURSOR_ARROW, 0);
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
  if ((city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE)) &&
      !HasTileWater(start) && ReadTerrainCode(start) > CITY_TERRAIN_SHAPE_4) return;
  RegradeCityTerrainCell(city, tile_index);
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
      if ((city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE)) &&
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
    RegradeCityTerrainCell(city, tile_index);
    heading += kTerrainChannelTurns[choice];
    if (Sc2kCrtRandRange(city, 3) != 0)
      heading = (heading + 2 * Sc2kCrtRand(city) + 1) & 3;
  }
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
      (!(city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE)) || ReadTerrainCode(tp) <= CITY_TERRAIN_SHAPE_4) &&
      IsTileKindLandscape(tp->kind) && tp->kind != TK_RADIOACTIVE_WASTE;
}

// Quote raw canal construction over eligible tiles in an inclusive rectangle. Invalid endpoints are rejected;
// the query neither applies monetary policy nor consumes RNG.
CityCommandCost GetCityCanalRectangleCost(const City *city, TileIndex start, TileIndex end) {
  CityCommandCost quote = {.error = CITY_COMMAND_COST_INVALID, .error_tile = INVALID_TILE};
  if (!IsMapTileValid(city, start) || !IsMapTileValid(city, end)) return quote;
  CityRect bounds = MapTileRectangle(city, start, end);
  for (int y = bounds.top; y < bounds.bottom; ++y)
    for (int x = bounds.left; x < bounds.right; ++x)
      if (CanPlaceCityCanal(city, MapTileXY(city, x, y)))
        quote.cost += CITY_CANAL_COST;
  quote.error = !quote.cost ? CITY_COMMAND_COST_BLOCKED : CITY_COMMAND_COST_OK;
  return quote;
}

// Apply the same canal admission used by previews and rectangle commands. Regrading edits terrain/water/statistics;
// this single-cell operation leaves pricing and payment to its caller.
i64 ApplyWaterToolAtTile(City *city, TileIndex tile) {
  if (!CanPlaceCityCanal(city, tile)) return CITY_COMMAND_COST_BLOCKED;
  RegradeCityTerrainCell(city, tile);
  WriteTileZoneByte(&city->tile[tile], ReadTileZoneByte(&city->tile[tile]) & CITY_ZONE_METADATA_MASK);
  return true;
}

// Convert eligible cells in the resolved inclusive rectangle to canals. Rejects reserved options and invalid
// endpoints; TEST only quotes the work and execution preserves admission errors and per-cell charging.
i64 CmdBuildCanalsRect(City *city, u32 flags, TileIndex location, TileIndex end, int options) {
  int invalid = CITY_COMMAND_COST_INVALID;
  if (options || !(city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE))) return invalid;
  CityCommandCost quote = GetCityCanalRectangleCost(city, location, end);
  if (flags & CITY_COMMAND_TEST) return CityCommandPriceResult(quote);
  if (quote.error) return quote.error;
  i64 affordable = AffordableCityCommandCost(city, quote.cost);
  if (affordable < 0) return affordable;
  CityRect bounds = MapTileRectangle(city, location, end);
  ClearGameError(city->game);
  i64 failure = 0;
  for (int y = bounds.top; y < bounds.bottom; ++y)
    for (int x = bounds.left; x < bounds.right; ++x) {
      TileIndex tile = MapTileXY(city, x, y);
      if (!CanPlaceCityCanal(city, tile)) continue;
      i64 result = ApplyWaterToolAtTile(city, tile);
      if (result < 0) failure = result;
      else DebitCityCommandCost(city, CITY_CANAL_COST);
    }
  PlayCitySound(city, location, CITY_SOUND_WATER);
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  return failure ? failure : GetGameError(city->game) ? CITY_COMMAND_COST_REJECTED : 1;
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
  return CITY_SPRITE_CATALOG_FLAT_WATER + CITY_SPRITE_CATALOG_MEDIUM_BASE * zoom;
}

// Select the same camera-relative water and bank layers for map drawing and retained tile previews.
CityRiverSprites ReadCityRiverSprites(const City *city, TileIndex tile, int zoom,
    const CityTileGeometry *geometry) {
  CityRiverSprites sprites = {0};
  if (!(city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE))) return sprites;
  int kind = ReadTile(city, tile);
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
bool DrawCityWaterBank(City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry) {
  CityRiverSprites sprites = ReadCityRiverSprites(city, tile, zoom, geometry);
  if (sprites.count <= 1) return false;
  for (int i = 0; i < sprites.count; ++i)
    DrawCityMapSpriteAtBaseline(city->game->drawing, sprites.sprite[i], screen_x, ground_y, 0);
  return true;
}

static bool DrawCityRiverTile(City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *tile_geometry) {
  Drawing *drawing = city->game->drawing;
  CityTileGeometry geometry = *tile_geometry;
  if (!(city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE)) || !IsTileKindExposedWater(ReadTile(city, tile)) ||
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
  CityRiverSprites sprites = ReadCityRiverSprites(city, tile, zoom, &geometry);
  for (int i = 0; i < sprites.count; ++i)
    DrawCityMapSpriteAtBaseline(drawing, sprites.sprite[i], screen_x, ground_y, 0);
  DrawCityMapPlacementTile(city, tile);
  return true;
}

// Select the wet surface from camera geometry, including retained water below infrastructure and hydro spillways.
int CityWaterSprite(const City *city, TileIndex tile, int zoom, const CityTileGeometry *geometry) {
  if (ReadTile(city, tile) == TK_HYDRO_POWER &&
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
  if (city->tile[tile].kind == TK_WATER_BANK) GrowCityGround(city, tile, 0, false);
}

const TileModule kCityWaterModule = {.tick = TickCityWaterTile, .draw = DrawCityRiverTile};
