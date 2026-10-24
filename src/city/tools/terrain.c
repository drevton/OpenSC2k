#include "game/errors.h"
#include "city/infrastructure/lines.h"
#include "city/infrastructure/networks.h"
#include "city/maps/terrain.h"
#include "city/maps/ground.h"
#include "city/maps/tile_queue.h"
#include "city/tools/tools.h"
#include "city/engine/commands.h"
#include "city/infrastructure/power_lines.h"
#include "city/maps/layers.h"
#include "city/maps/map.h"
#include "city/maps/water.h"
#include "city/maps/tables.h"
#include "city/presentation.h"
#include "city/state.h"
#include <assert.h>
#include <string.h>

// Protect existing structures during terrain edits. TEST validates the Boolean option without editing City.
i64 CmdSetTerrainProtection(City *city, u32 flags, int enabled, int reserved1, int reserved2) {
  if ((enabled != 0 && enabled != 1) || reserved1 || reserved2)
    return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) return 0;
  city->protect_terrain_structures = enabled != 0;
  return 1;
}

// Return the first cardinal neighbor below the valid starting tile's land elevation. Out-of-map neighbors are skipped
// and no lower neighbor returns INVALID_TILE; this does not mark or edit terrain.
TileIndex FindLowerTerrainNeighbor(const City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  int elevation = ReadCityLandHeight(&city->tile[tile]);
  for (int direction = 0; direction < 4; ++direction) {
    int next_y = point.y + kLineYStep[direction];
    int next_x = point.x + kLineXStep[direction];
    TileIndex next = MapTileXYOrInvalid(city, next_x, next_y);
    if (next != INVALID_TILE) {
      if (ReadCityLandHeight(&city->tile[next]) < elevation) return next;
    }
  }
  return INVALID_TILE;
}

// Raising is allowed only below height 31 and with no lower neighbor. Coordinates must be valid; returns only local
// eligibility and neither marks the recursive raise nor checks treasury.
bool CanRaiseTerrainCell(const City *city, int x, int y) {
  TileIndex tile = MapTileXY(city, x, y);
  int elevation = ReadCityLandHeight(&city->tile[tile]);
  return elevation != 31 && FindLowerTerrainNeighbor(city, tile) == INVALID_TILE;
}

// Match the structures removed by RecalculateTerrainCell. The command owns the reason; presentation never has to
// inspect a potentially changed error tile.
static int TerrainObstructionError(const City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile));
  int kind = ReadTile(city, tile);
  if (ReadCityTileClasses(kind) & (TCLASS_RAIL | TCLASS_RAIL_BRIDGE))
    return CITY_COMMAND_COST_REMOVE_RAIL;
  if (IsTileKindPlainPowerLine(kind)) return CITY_COMMAND_COST_REMOVE_POWER_LINE;
  if (kind == TK_ROAD || IsTileKindBridge(kind) ||
      IsTileKindTunnel(kind) || IsTileKindGroundCrossing(kind) ||
      kind == TK_HIGHWAY_ENTRANCE || IsTileKindMultiHighway(kind))
    return CITY_COMMAND_COST_REMOVE_ROAD;
  if (!IsTileKindUnbuilt(kind)) return CITY_COMMAND_COST_REMOVE_BUILDING;
  if (ReadUnderground(city, tile) != TUK_CLEAR) return CITY_COMMAND_COST_REMOVE_UNDERGROUND;
  return CITY_COMMAND_COST_OK;
}

// Native terrain stores the minimum height and raised-corner shape in each tile.
// Vertices are shared: north (x,y), east (x,y+1), south (x+1,y+1), west (x+1,y).
// A one-level edit can propagate at most the 5-bit height range. Keep admission
// and execution on the same bounded plan, without map-sized scratch storage.
enum { kTerrainEditRadius = 33, kTerrainEditWidth = 67,
    kTerrainEditCapacity = kTerrainEditWidth * kTerrainEditWidth };
static const int kTerrainCornerX[4] = {0, 0, 1, 1};
static const int kTerrainCornerY[4] = {0, 1, 1, 0};

typedef struct {
  int origin_x, origin_y;
  u8 heights[kTerrainEditCapacity]; // 255 borrows the unchanged map vertex.
  bool queued[kTerrainEditCapacity];
  bool tiles[kTerrainEditCapacity];
  int queue[kTerrainEditCapacity];
  int read, write, pending;
  CityRect vertices;
} TerrainEditPlan;

// Map world vertex coordinates into the bounded local terrain plan. Plan origin must be initialized; returns -1
// outside its fixed neighborhood and does not allocate or grow it.
static int TerrainEditIndex(const TerrainEditPlan *plan, int x, int y) {
  int lx = x - plan->origin_x + kTerrainEditRadius, ly = y - plan->origin_y + kTerrainEditRadius;
  return lx >= 0 && lx < kTerrainEditWidth && ly >= 0 && ly < kTerrainEditWidth ?
      ly * kTerrainEditWidth + lx : -1;
}

// Read a staged vertex height or borrow the unchanged map vertex when no edit is staged. Requires initialized plan
// markers and native geometry; neither plan nor map is modified.
static int TerrainEditHeight(const City *city, const TerrainEditPlan *plan, int x, int y) {
  int index = TerrainEditIndex(plan, x, y);
  return index >= 0 && plan->heights[index] != 255 ? plan->heights[index] : ReadCityTerrainVertexHeight(city, x, y);
}

// Stage an interior plan vertex height from zero through 30 and queue it once for propagation. Returns false outside
// the supported neighborhood/height range; success expands vertex bounds without editing City tiles.
static bool SetTerrainEditHeight(TerrainEditPlan *plan, int x, int y, int height) {
  int index = TerrainEditIndex(plan, x, y);
  if (index < 0 || index % kTerrainEditWidth == 0 || index % kTerrainEditWidth == kTerrainEditWidth - 1 ||
      index / kTerrainEditWidth == 0 || index / kTerrainEditWidth == kTerrainEditWidth - 1 ||
      height < 0 || height > 30) return false;
  plan->heights[index] = height;
  plan->vertices = UnionCityRects(plan->vertices, (CityRect){x, y, x + 1, y + 1});
  if (!plan->queued[index]) {
    plan->queued[index] = true;
    plan->queue[plan->write] = index;
    plan->write = (plan->write + 1) % kTerrainEditCapacity;
    ++plan->pending;
  }
  return true;
}

typedef struct {
  int low, high, mask, shape;
} TerrainTileCorners;

// Decode a tile's four staged-or-live vertex heights into extrema, corner mask and supported slope. Plan and City must
// describe the same edit; shape -1 marks an unsupported saddle or height difference.
static TerrainTileCorners ReadPlannedTerrainCorners(const City *city, const TerrainEditPlan *plan, int x, int y) {
  TerrainTileCorners result = {.low = 31};
  int corners[4];
  for (int corner = 0; corner < 4; ++corner) {
    corners[corner] = TerrainEditHeight(city, plan, x + kTerrainCornerX[corner], y + kTerrainCornerY[corner]);
    result.low = IntMin(result.low, corners[corner]);
    result.high = IntMax(result.high, corners[corner]);
  }
  for (int corner = 0; corner < 4; ++corner)
    if (corners[corner] > result.low) result.mask |= 1 << corner;
  result.shape = result.high - result.low > 1 || result.mask == 5 || result.mask == 10 ? -1 :
      kTerrainShapeByHigherNeighbors[result.mask];
  return result;
}

// Check whether any staged corner actually differs from the tile's live vertex heights. Requires a prepared plan and
// valid tile coordinates; this read-only comparison does not commit or dirty the tile.
static bool TerrainEditChangesTile(const City *city, const TerrainEditPlan *plan, int x, int y) {
  for (int corner = 0; corner < 4; ++corner) {
    int vx = x + kTerrainCornerX[corner], vy = y + kTerrainCornerY[corner];
    int index = TerrainEditIndex(plan, vx, vy);
    if (index >= 0 && plan->heights[index] != 255 && plan->heights[index] != ReadCityTerrainVertexHeight(city, vx, vy))
      return true;
  }
  return false;
}

// Build a bounded height edit and propagate slope constraints in caller-owned workspace. Requires a valid
// tile and admitted tool/options; returns charge/error data without changing map, treasury, route queue or RNG.
static CityCommandCost PlanNativeTerrainEdit(const City *city, TileIndex tile, bool raise, int tool, int options,
    int target_height, bool level, TerrainEditPlan *plan) {
  CityCommandCost quote = {.error = CITY_COMMAND_COST_BLOCKED, .error_tile = INVALID_TILE};
  memset(plan, 0, sizeof(*plan));
  memset(plan->heights, 255, sizeof(plan->heights));
  CityPoint origin = MapTilePt(city, tile);
  plan->origin_x = origin.x;
  plan->origin_y = origin.y;
  plan->vertices = (CityRect){plan->origin_x, plan->origin_y, plan->origin_x + 1, plan->origin_y + 1};
  bool point = target_height < 0 && (options & CITY_TERRAIN_POINT_EDIT) &&
      (tool == CITY_DEMOLISH_RAISE || tool == CITY_DEMOLISH_LOWER);
  int base = city->tile[tile].altitude & CITY_ALTITUDE_LAND_MASK;
  for (int corner = 0; corner < (point ? 1 : 4); ++corner) {
    int x = plan->origin_x + kTerrainCornerX[corner], y = plan->origin_y + kTerrainCornerY[corner];
    int height = ReadCityTerrainVertexHeight(city, x, y);
    int next = target_height < 0 ? height + (raise ? 1 : -1) : level ? target_height : height + target_height - base;
    // A sloped diamond can still be flattened when its low corners are at zero.
    if (next == height || (target_height < 0 && !raise && height == 0)) continue;
    if (!SetTerrainEditHeight(plan, x, y, next)) return quote;
  }
  if (!plan->pending) {
    quote.error = target_height < 0 ? CITY_COMMAND_COST_ALREADY_AT_SEA_LEVEL : CITY_COMMAND_COST_OK;
    return quote;
  }
  while (plan->pending) {
    int index = plan->queue[plan->read];
    plan->read = (plan->read + 1) % kTerrainEditCapacity;
    --plan->pending;
    plan->queued[index] = false;
    int x = plan->origin_x + index % kTerrainEditWidth - kTerrainEditRadius;
    int y = plan->origin_y + index / kTerrainEditWidth - kTerrainEditRadius;
    int height = plan->heights[index];
    // SC2 has no steep two-level slopes: constrain all vertices sharing a tile.
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        int nx = x + dx, ny = y + dy;
        if ((!dx && !dy) || nx < 0 || ny < 0 || nx > city->map_width || ny > city->map_height) continue;
        int neighbor = TerrainEditHeight(city, plan, nx, ny);
        if ((raise && neighbor < height - 1) || (!raise && neighbor > height + 1))
          if (!SetTerrainEditHeight(plan, nx, ny, height + (raise ? -1 : 1))) return quote;
      }
    }
    // Opposite raised corners have no SC2 sprite. Extend the edit monotonically
    // to the next corner rather than encoding an unsupported saddle.
    for (int corner = 0; corner < 4; ++corner) {
      int tx = x - kTerrainCornerX[corner], ty = y - kTerrainCornerY[corner];
      if (!IsMapCoordinateValid(city, tx, ty)) continue;
      TerrainTileCorners corners = ReadPlannedTerrainCorners(city, plan, tx, ty);
      if (corners.high - corners.low == 1 && (corners.mask == 5 || corners.mask == 10)) {
        int c = 0;
        while (((corners.mask >> c) & 1) == raise) ++c;
        if (!SetTerrainEditHeight(plan, tx + kTerrainCornerX[c], ty + kTerrainCornerY[c],
            raise ? corners.high : corners.low))
          return quote;
      }
    }
  }
  int changes = 0;
  for (int y = plan->vertices.top; y < plan->vertices.bottom; ++y)
    for (int x = plan->vertices.left; x < plan->vertices.right; ++x) {
      int index = TerrainEditIndex(plan, x, y);
      if (plan->heights[index] == 255) continue;
      int difference = plan->heights[index] - ReadCityTerrainVertexHeight(city, x, y);
      if (difference) changes += target_height < 0 ? 1 : difference < 0 ? -difference : difference;
    }
  for (int y = IntMax(0, plan->vertices.top - 1); y < IntMin(city->map_height, plan->vertices.bottom); ++y) {
    for (int x = IntMax(0, plan->vertices.left - 1); x < IntMin(city->map_width, plan->vertices.right); ++x) {
      if (!TerrainEditChangesTile(city, plan, x, y)) continue;
      TileIndex affected = MapTileXY(city, x, y);
      TerrainTileCorners corners = ReadPlannedTerrainCorners(city, plan, x, y);
      if (corners.shape < 0 || ReadCityZoneKind(&city->tile[affected]) == ZONE_MILITARY) return quote;
      const TileInfo *tp = &city->tile[affected];
      if (corners.low == ReadCityLandHeight(tp) && corners.shape == ReadTerrainCode(tp)) continue;
      plan->tiles[TerrainEditIndex(plan, x, y)] = true;
      if (city->protect_terrain_structures) {
        int error = TerrainObstructionError(city, affected);
        if (error) return (CityCommandCost){.error = error, .error_tile = affected};
      }
    }
  }
  quote.cost = (i64)(point ? changes : (changes + 3) / 4) * (raise ? 25 : kCityToolCosts[tool]);
  quote.error = CITY_COMMAND_COST_OK;
  return quote;
}

// Commit an admitted bounded plan after resolving borrowed vertices. Every affected tile invalidates its old and new
// extents; water height, salinity and climate use the existing native terrain rules.
static void CommitTerrainEditPlan(City *city, TerrainEditPlan *plan, int options) {
  CityRect affected_bounds = {IntMax(0, plan->vertices.left - 1), IntMax(0, plan->vertices.top - 1),
      IntMin(city->map_width, plan->vertices.right), IntMin(city->map_height, plan->vertices.bottom)};
  for (int y = affected_bounds.top; y < affected_bounds.bottom; ++y) {
    for (int x = affected_bounds.left; x < affected_bounds.right; ++x) {
      if (!plan->tiles[TerrainEditIndex(plan, x, y)]) continue;
      TileIndex affected = MapTileXY(city, x, y);
      InvalidateTile(city, affected);
    }
  }
  // Resolve all borrowed vertices before writing incident tiles; otherwise later
  // tiles would read partially updated geometry from the same plan.
  for (int y = affected_bounds.top; y <= affected_bounds.bottom; ++y)
    for (int x = affected_bounds.left; x <= affected_bounds.right; ++x) {
      int index = TerrainEditIndex(plan, x, y);
      if (index >= 0 && plan->heights[index] == 255) plan->heights[index] = ReadCityTerrainVertexHeight(city, x, y);
    }
  for (int y = affected_bounds.top; y < affected_bounds.bottom; ++y) {
    for (int x = affected_bounds.left; x < affected_bounds.right; ++x) {
      if (!plan->tiles[TerrainEditIndex(plan, x, y)]) continue;
      TileIndex affected = MapTileXY(city, x, y);
      TerrainTileCorners corners = ReadPlannedTerrainCorners(city, plan, x, y);
      TileInfo *tp = &city->tile[affected];
      bool wet = HasTileWater(tp);
      int water_height = wet ? ReadCityWaterHeight(tp) : 0;
      int kind = tp->kind;
      if (!IsTileKindUnbuilt(kind)) BulldozeCityTile(city, affected, true);
      if (kind != TK_RADIOACTIVE_WASTE) ReplaceCityTileAndStatistics(city, affected,
          ReadCityWaterGroundKind(tp));
      SetUndergroundTile(city, affected, TUK_CLEAR);
      WriteTileZoneByte(tp, ReadTileZoneByte(tp) & CITY_ZONE_METADATA_MASK);
      tp->altitude = (tp->altitude & CITY_ALTITUDE_CLEAR_LAND) | corners.low;
      // Editing the bed must not move an existing lake or river to sea level.
      int water = wet ? water_height : city->geography.edit_height;
      if ((options & CITY_TERRAIN_NO_SEAWATER) &&
          !wet) WriteTerrainCode(tp, corners.shape);
      else SetCityTerrainWater(city, affected, water, corners.shape, false);
      RefreshCityPowerConnections(city, affected);
      RefreshCityTileClimates(city, affected);
      InvalidateTile(city, affected);
    }
  }
  // Bank conversion must see the completed edit, not another tile's staged
  // geometry or water level from the middle of this commit.
  for (int y = affected_bounds.top; y < affected_bounds.bottom; ++y)
    for (int x = affected_bounds.left; x < affected_bounds.right; ++x)
      if (plan->tiles[TerrainEditIndex(plan, x, y)]) ReclaimCityShorelineBanks(city, MapTileXY(city, x, y));
}

// Plan and commit a native terrain edit without payment. The shared command validates
// its price; rejected geometry reports the original blocker and leaves the map uncommitted.
i64 EditCityTerrainCell(City *city, TileIndex tile, bool raise, int tool) {
  TerrainEditPlan plan;
  CityCommandCost quote = PlanNativeTerrainEdit(city, tile, raise, tool,
      city->command_choices.terrain_options, -1, false, &plan);
  if (quote.error) {
    ReportCityCommandBlocker(city, quote.error_tile);
    return quote.error;
  }
  CommitTerrainEditPlan(city, &plan, city->command_choices.terrain_options);
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  return true;
}

// Set all four tile corners to an explicit elevation, or translate their existing shape to that elevation. Both
// commands price and execute the same bounded plan; TEST never changes terrain, treasury, RNG or presentation.
static i64 SetCityTerrainTileHeight(City *city, u32 flags, TileIndex tile, int height, int options, bool level) {
  if (!IsMapTileValid(city, tile) || height < 0 || height > 30 || (options & ~CITY_TERRAIN_NO_SEAWATER))
    return CITY_COMMAND_COST_INVALID;
  if (!level && city->settings.timer_mode != CITY_TIMER_STOPPED) return CITY_COMMAND_COST_BLOCKED;
  int base = city->tile[tile].altitude & CITY_ALTITUDE_LAND_MASK;
  bool raise = height > base;
  TerrainEditPlan plan;
  CityCommandCost quote = PlanNativeTerrainEdit(city, tile, raise, CITY_DEMOLISH_LEVEL, options, height, level, &plan);
  if (flags & CITY_COMMAND_TEST) return CityCommandPriceResult(quote);
  if (quote.error) {
    ReportCityCommandBlocker(city, quote.error_tile);
    return quote.error;
  }
  CommitTerrainEditPlan(city, &plan, options);
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  return 1;
}

i64 CmdLevelTerrainTile(City *city, u32 flags, TileIndex tile, int height, int options) {
  return SetCityTerrainTileHeight(city, flags, tile, height, options, true);
}

i64 CmdStretchTerrainTile(City *city, u32 flags, TileIndex tile, int height, int options) {
  return SetCityTerrainTileHeight(city, flags, tile, height, options, false);
}

// Raise one terrain tile or move it one step toward an explicit target height. Validates the action's payload; TEST
// quotes the corresponding edit and execution preserves the zero-step and signed-error contracts.
i64 CmdRaiseOrStepTerrain(City *city, u32 flags, TileIndex tile,
    bool raise, int options, int terrain_options, int reserved) {
  if ((terrain_options & ~CITY_TERRAIN_OPTIONS_MASK) || reserved)
    return CITY_COMMAND_COST_INVALID;
  int invalid = CITY_COMMAND_COST_INVALID;
  int tool = raise ? options : (options & CITY_TERRAIN_COMMAND_TOOL_MASK) >> CITY_TERRAIN_COMMAND_TOOL_SHIFT;
  if (!IsMapTileValid(city, tile) ||
      tool < CITY_DEMOLISH_CLEAR || tool > CITY_DEMOLISH_DEZONE ||
      (!raise && (options & ~CITY_TERRAIN_COMMAND_STEP_MASK) != 0))
    return invalid;
  if (flags & CITY_COMMAND_TEST) {
    if (!raise) {
      int height = ReadCityLandHeight(&city->tile[tile]);
      int target = options & CITY_TERRAIN_COMMAND_HEIGHT_MASK;
      if (height == target) return 0;
      raise = height < target;
    }
    return CityCommandPriceResult(GetCityTerrainCost(city, tile, raise, tool, terrain_options));
  }
  ClearGameError(city->game);
  if (city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) {
    if (!raise) {
      int height = ReadCityLandHeight(&city->tile[tile]);
      int target = options & CITY_TERRAIN_COMMAND_HEIGHT_MASK;
      if (height == target) return 1;
      raise = height < target;
    }
    return EditCityTerrainCell(city, tile, raise, tool);
  }
  return raise ? RaiseCityTerrainCell(city, tile, tool) :
      StepCityTerrainTowardHeight(city, tile, options & CITY_TERRAIN_COMMAND_HEIGHT_MASK, tool);
}

// Lower one admitted terrain tile with the explicit charge/tool contract. Rejects invalid tile/payload; TEST quotes
// the selected charging behavior and execution preserves uncharged lowering semantics.
i64 CmdLowerTerrainTile(City *city, u32 flags, TileIndex tile,
    int options, int terrain_options, int reserved) {
  if ((terrain_options & ~CITY_TERRAIN_OPTIONS_MASK) || reserved)
    return CITY_COMMAND_COST_INVALID;
  int invalid = CITY_COMMAND_COST_INVALID;
  int tool = (options & CITY_TERRAIN_COMMAND_TOOL_MASK) >> CITY_TERRAIN_COMMAND_TOOL_SHIFT;
  if (!IsMapTileValid(city, tile) ||
      (options & ~CITY_TERRAIN_COMMAND_LOWER_MASK) != 0 || tool > CITY_DEMOLISH_DEZONE)
    return invalid;
  if (flags & CITY_COMMAND_TEST) {
    CityCommandCost quote = GetCityTerrainCost(city, tile, false, tool, terrain_options);
    if (quote.error) return CityCommandPriceResult(quote);
    return options & CITY_TERRAIN_COMMAND_CHARGE ? quote.cost : 0;
  }
  ClearGameError(city->game);
  if (city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) return EditCityTerrainCell(city, tile, false, tool);
  return LowerCityTerrainCell(city, tile, (options & CITY_TERRAIN_COMMAND_CHARGE) != 0, tool);
}

// A one-level edit propagates through the 5-bit elevation range. Keep its temporary heights local to that
// neighborhood; a price query never writes the map or route queue. Returns cost and admission error without committing
// the edit or consuming simulation RNG.
CityCommandCost GetCityTerrainCost(const City *city, TileIndex tile, bool raise, int tool, int options) {
  CityCommandCost quote = {.error = CITY_COMMAND_COST_BLOCKED, .error_tile = INVALID_TILE};
  enum { kRadius = 32, kWidth = 2 * kRadius + 1, kCapacity = kWidth * kWidth };
  if (!IsMapTileValid(city, tile) || tool < CITY_DEMOLISH_CLEAR || tool > CITY_DEMOLISH_DEZONE) return quote;
  if (city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) {
    TerrainEditPlan plan;
    return PlanNativeTerrainEdit(city, tile, raise, tool, options, -1, false, &plan);
  }
  u8 heights[kCapacity] = {0};
  bool queued[kCapacity] = {0};
  int queue[kCapacity];
  CityPoint origin_point = MapTilePt(city, tile);
  int center = kRadius * kWidth + kRadius;
  int first_height = ReadCityLandHeight(&city->tile[tile]);
  if ((!raise && first_height == 0) || (raise && first_height >= 30)) return quote;
  heights[center] = first_height + (raise ? 1 : 0); // Stored height + 1 after a lower.
  queue[0] = center;
  queued[center] = true;
  int read = 0, write = 1, pending = 1, changes = raise ? 0 : 1;
  CityRect bounds = {origin_point.x, origin_point.y, origin_point.x + 1, origin_point.y + 1};
  static const int kNeighborX[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
  static const int kNeighborY[8] = {0, 1, 1, 1, 0, -1, -1, -1};
  static const int kHigherMask[8] = {3, 2, 6, 4, 12, 8, 9, 1};
  while (pending) {
    int index = queue[read];
    read = (read + 1) % kCapacity;
    --pending;
    queued[index] = false;
    int x = origin_point.x + index % kWidth - kRadius, y = origin_point.y + index / kWidth - kRadius;
    TileIndex current = MapTileXY(city, x, y);
    const TileInfo *tp = &city->tile[current];
    int level = raise ? ReadCityLandHeight(tp) : heights[index] - 1;
    if (raise) {
      if (tp->flags & TF_VISITED) continue;
      if (level >= 30 || ReadCityZoneKind(tp) == ZONE_MILITARY ||
          (tool == CITY_DEMOLISH_CLEAR && HasTileWater(tp))) return quote;
      ++changes;
    }
    bounds = UnionCityRects(bounds, (CityRect){x, y, x + 1, y + 1});
    if (raise && city->protect_terrain_structures) {
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          TileIndex affected = GetRelativeTileOrInvalid(city, current, dx, dy);
          if (affected == INVALID_TILE) continue;
          int error = TerrainObstructionError(city, affected);
          if (error) return (CityCommandCost){.error = error, .error_tile = affected};
        }
      }
    }
    int higher = 0;
    for (int direction = 0; direction < 8; ++direction) {
      int nx = x + kNeighborX[direction], ny = y + kNeighborY[direction];
      TileIndex neighbor = MapTileXYOrInvalid(city, nx, ny);
      if (neighbor == INVALID_TILE) continue;
      const TileInfo *np = &city->tile[neighbor];
      if (raise && (ReadCityZoneKind(np) == ZONE_MILITARY ||
          (tool == CITY_DEMOLISH_CLEAR && HasTileWater(np)))) return quote;
      int local_x = nx - origin_point.x + kRadius, local_y = ny - origin_point.y + kRadius;
      bool inside = local_x >= 0 && local_x < kWidth && local_y >= 0 && local_y < kWidth;
      int next = local_y * kWidth + local_x;
      int height = !raise && inside && heights[next] ? heights[next] - 1 :
          ReadCityLandHeight(np);
      if (height > level) higher |= kHigherMask[direction];
    }
    for (int direction = 0; direction < 8; ++direction) {
      if (raise && (direction & 1)) continue;
      int nx = x + kNeighborX[direction], ny = y + kNeighborY[direction];
      TileIndex neighbor = MapTileXYOrInvalid(city, nx, ny);
      if (neighbor == INVALID_TILE) continue;
      int local_x = nx - origin_point.x + kRadius, local_y = ny - origin_point.y + kRadius;
      bool inside = local_x >= 0 && local_x < kWidth && local_y >= 0 && local_y < kWidth;
      int next = local_y * kWidth + local_x;
      int height = !raise && inside && heights[next] ? heights[next] - 1 :
          ReadCityLandHeight(&city->tile[neighbor]);
      bool changes_neighbor = raise ? height < level : height > level + 1 || (height > level && higher == 15);
      if (!changes_neighbor) continue;
      // An invalid, discontinuous height map may propagate beyond the bounded edit.
      if (!inside) return quote;
      if (raise && heights[next]) continue;
      heights[next] = raise ? height + 1 : height;
      if (!raise) ++changes;
      if (!queued[next]) {
        queue[write] = next;
        write = (write + 1) % kCapacity;
        ++pending;
        queued[next] = true;
      }
    }
  }
  if (!raise && city->protect_terrain_structures) {
    for (int y = IntMax(0, bounds.top - 1); y < IntMin(city->map_height, bounds.bottom + 1); ++y) {
      for (int x = IntMax(0, bounds.left - 1); x < IntMin(city->map_width, bounds.right + 1); ++x) {
        TileIndex affected = MapTileXY(city, x, y);
        int error = TerrainObstructionError(city, affected);
        if (error) return (CityCommandCost){.error = error, .error_tile = affected};
      }
    }
  }
  quote.cost = (i64)changes * (raise ? 25 : kCityToolCosts[tool]);
  quote.error = CITY_COMMAND_COST_OK;
  return quote;
}

// Lower terrain, propagate neighbor reductions, and optionally charge. Stock stores y in RECT.left/right and x in
// RECT.top/bottom for this tile envelope. Requires a valid tile and admitted tool; charge selects treasury effects and
// partial recursive edits retain the original failure behavior.
i64 LowerCityTerrainCell(City *city, TileIndex tile_index, bool charge, int tool) {
  if (city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) {
    if (!charge) return EditCityTerrainCell(city, tile_index, false, tool);
    return DoCityCommand(city, CITY_COMMAND_LOWER_TERRAIN_TILE, 0, tile_index,
        CITY_TERRAIN_COMMAND_CHARGE | tool << CITY_TERRAIN_COMMAND_TOOL_SHIFT,
        city->command_choices.terrain_options, 0);
  }
  if (city->protect_terrain_structures) {
    CityCommandCost quote = GetCityTerrainCost(city, tile_index, false, tool, city->command_choices.terrain_options);
    if (quote.error) {
      ReportCityCommandBlocker(city, quote.error_tile);
      return quote.error;
    }
  }
  CityPoint tile_point = MapTilePt(city, tile_index);
  static const i8 neighbor_y[8] = {0, 1, 1, 1, 0, -1, -1, -1};
  static const i8 neighbor_x[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
  static const u8 neighbor_mask[8] = {3, 2, 6, 4, 12, 8, 9, 1};
  CityRect bounds = {tile_point.y, tile_point.x, tile_point.y + 1, tile_point.x + 1};
  i64 cost = charge ? AffordableCityCommandCost(city, kCityToolCosts[tool]) : 0;
  if (cost < 0) return cost;
  charge = cost != 0;
  int cell = ReadAltitudeWord(city, tile_index);
  if ((cell & CITY_ALTITUDE_LAND_MASK) == 0) {
    return CITY_COMMAND_COST_BLOCKED;
  }
  if (IsMapTileValid(city, tile_index))
    WriteAltitudeWord(city, tile_index, ((cell & CITY_ALTITUDE_LAND_MASK) - 1) | (cell & CITY_ALTITUDE_CLEAR_LAND));
  if (charge != 0)
    DebitCityCommandCost(city, cost);
  ResetCityTileQueueAndPush(city, tile_point.x, tile_point.y);
  while (city->route_queue_cursor.read != city->route_queue_cursor.write) {
    PointU16 point = DequeueCityTileCoordinate(city);
    int current_y = point.y;
    int current_x = point.x;
    TileIndex tile_index = MapTileXY(city, current_x, current_y);
    TileInfo *tp = &city->tile[tile_index];
    int elevation = ReadCityLandHeight(tp);
    if (IsMapTileValid(city, tile_index))
      WriteTileZoneByte(tp, ReadTileZoneByte(tp) & CITY_ZONE_METADATA_MASK);
    CityRect area = {IntMin(current_y, tile_point.y), IntMin(current_x, tile_point.x), IntMax(current_y, tile_point.y),
        IntMax(current_x, tile_point.x)};
    if (city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) {
      // Include every changed cell, also along the origin's row or column.
      bounds = UnionCityRects(bounds, (CityRect){current_y, current_x, current_y + 1, current_x + 1});
    } else if (area.left < area.right && area.top < area.bottom) {
      // Stock UnionRect ignores empty straight-axis extensions.
      bounds = UnionCityRects(bounds, area);
    }
    int higher_mask = 0;
    for (int direction = 0; direction < 8; ++direction) {
      int next_y = current_y + neighbor_y[direction];
      int next_x = current_x + neighbor_x[direction];
      TileIndex next = MapTileXYOrInvalid(city, next_x, next_y);
      if (next != INVALID_TILE &&
          ReadCityLandHeight(&city->tile[next]) > elevation)
        higher_mask |= neighbor_mask[direction];
    }
    for (int direction = 0; direction < 8; ++direction) {
      int next_y = current_y + neighbor_y[direction];
      int next_x = current_x + neighbor_x[direction];
      TileIndex next = MapTileXYOrInvalid(city, next_x, next_y);
      if (next != INVALID_TILE) {
        TileInfo *np = &city->tile[next];
        cell = np->altitude;
        int next_elevation = cell & CITY_ALTITUDE_LAND_MASK;
        if (next_elevation > elevation + 1 || (next_elevation > elevation && higher_mask == 15)) {
          if (charge != 0)
            DebitCityCommandCost(city, cost);
          InvalidateTile(city, next);
          PushCityTileCoordinate(city, next_x, next_y);
          WriteAltitudeWord(city, next, (next_elevation - 1) | (cell &
              CITY_ALTITUDE_CLEAR_LAND));
        }
      }
    }
  }
  if (city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) {
    for (int refresh_y = bounds.left - 1; refresh_y < bounds.right + 1; ++refresh_y) {
      for (int refresh_x = bounds.top - 1; refresh_x < bounds.bottom + 1; ++refresh_x) {
        TileIndex refreshed = MapTileXYOrInvalid(city, refresh_x, refresh_y);
        if (refreshed == INVALID_TILE) continue;
        InvalidateTile(city, refreshed);
        RecalculateTerrainCell(city, refreshed);
        InvalidateTile(city, refreshed);
      }
    }
  } else {
    for (int refresh_y = bounds.left - 1; refresh_y <= bounds.right + 1; ++refresh_y)
      for (int refresh_x = bounds.top - 1; refresh_x <= bounds.bottom + 1; ++refresh_x)
        RefreshTerrainNeighborhood(city, MapTileXYOrInvalid(city, refresh_x, refresh_y));
  }
  if (charge != 0) {
    if (city->indicators.cash < 0)
      city->indicators.cash = 0;
    InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  }
  return true;
}

// Coordinate deltas and absolute magnitudes determine cardinal direction. Returns -1 for no movement; otherwise
// returns the dominant-axis cardinal direction without changing any pointer state.
int ClassifyCardinalDragDirection(int x, int y, int previous_x, int previous_y) {
  if (y == previous_y && x == previous_x)
    return -1;
  int delta_y = y - previous_y;
  int delta_x = x - previous_x;
  int magnitude_y = delta_y < 0 ? -delta_y : delta_y;
  int magnitude_x = delta_x < 0 ? -delta_x : delta_x;
  if (magnitude_y <= magnitude_x)
    return delta_x <= 0 ? 0 : 2;
  return delta_y <= 0 ? 3 : 1;
}

// Check whether terrain can be raised and mark the affected lower neighbors. Invalid candidate tiles end recursion;
// origin identifies the initial edit for blocker reporting, and visitation marks belong to the matching raise pass.
i64 MarkTerrainRaisePath(City *city, TileIndex tile_index, bool allow_water, TileIndex origin) {
  if (!IsMapTileValid(city, tile_index)) return true;
  if (ReadCityZoneKind(&city->tile[tile_index]) == ZONE_MILITARY) {
    ReportCityCommandBlocker(city, tile_index != origin ? tile_index : INVALID_TILE);
    return CITY_COMMAND_COST_BLOCKED;
  }
  if (HasTileWater(&city->tile[tile_index]) && !allow_water) {
    ReportCityCommandBlocker(city, tile_index != origin ? tile_index : INVALID_TILE);
    return CITY_COMMAND_COST_BLOCKED;
  }
  if ((city->tile[tile_index].flags & TF_VISITED) != 0)
    return true;

  city->tile[tile_index].flags |= TF_VISITED;
  int level = ReadCityLandHeight(&city->tile[tile_index]);
  if (level >= 30) {
    ReportCityCommandBlocker(city, tile_index != origin ? tile_index : INVALID_TILE);
    return CITY_COMMAND_COST_BLOCKED;
  }

  for (int neighbor = 0; neighbor < 8; ++neighbor) {
    const TileIndex nearby_index = GetRelativeTileOrInvalid(city, tile_index, kTerrainNeighborXOffsets[neighbor],
        kTerrainNeighborYOffsets[neighbor]);
    if (nearby_index == INVALID_TILE) continue;
    const TileInfo *np = &city->tile[nearby_index];
    if (ReadCityZoneKind(np) == ZONE_MILITARY ||
        (HasTileWater(np) && !allow_water)) {
      city->tile[tile_index].flags &= ~TF_VISITED;
      ReportCityCommandBlocker(city, nearby_index != origin ? nearby_index : INVALID_TILE);
      return CITY_COMMAND_COST_BLOCKED;
    }
  }

  TileNeighbors neighbors = GetTileNeighbors(city, tile_index);
  for (int neighbor = 0; neighbor < neighbors.count; ++neighbor) {
    TileIndex nearby_index = neighbors.tiles[neighbor];
    if (ReadCityLandHeight(&city->tile[nearby_index]) >= level) continue;
    i64 result = MarkTerrainRaisePath(city, nearby_index, allow_water, origin);
    if (result < 0) {
      city->tile[tile_index].flags &= ~TF_VISITED;
      return result;
    }
  }
  return true;
}

// Raise the marked terrain cells, charging each successful elevation change. Invalid or unmarked tiles are ignored
// with the original completion value; charges only applied raises and preserves earlier effects on later funds
// failure.
i64 RaiseMarkedTerrain(City *city, TileIndex tile) {
  if (!IsMapTileValid(city, tile) || !(city->tile[tile].flags & TF_VISITED)) return 1;
  city->tile[tile].flags &= ~TF_VISITED;
  int altitude = ReadAltitudeWord(city, tile), level = ReadCityLandHeight(&city->tile[tile]);
  if (level >= 30) return 1;
  i64 result = 1;
  TileNeighbors neighbors = GetTileNeighbors(city, tile);
  for (int neighbor = 0; neighbor < neighbors.count; ++neighbor) {
    TileIndex nearby = neighbors.tiles[neighbor];
    if (ReadCityLandHeight(&city->tile[nearby]) >= level) continue;
    i64 raised = RaiseMarkedTerrain(city, nearby);
    if (raised < 0) result = raised;
  }
  int cost = 25;
  i64 affordable = AffordableCityCommandCost(city, cost);
  if (affordable < 0) return affordable;
  InvalidateTile(city, tile);
  DebitCityCommandCost(city, cost);
  WriteAltitudeWord(city, tile, (level + 1) | (altitude & CITY_ALTITUDE_CLEAR_LAND));
  WriteZoneByte(city, tile, ReadZoneByte(city, tile) & CITY_ZONE_METADATA_MASK);
  RebalanceAdjacentTerrainElevations(city, tile);
  RefreshTerrainNeighborhood(city, tile);
  return result;
}

// Validate the recursive raise, apply it and refresh city status.
i64 RaiseCityTerrainCell(City *city, TileIndex tile_index, int tool) {
  assert(IsMapTileValid(city, tile_index));
  if (city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE))
    return DoCityCommand(city, CITY_COMMAND_RAISE_TERRAIN_TILE, 0, tile_index, tool,
        city->command_choices.terrain_options, 0);
  if (city->protect_terrain_structures) {
    CityCommandCost quote = GetCityTerrainCost(city, tile_index, true, tool, city->command_choices.terrain_options);
    if (quote.error) {
      ReportCityCommandBlocker(city, quote.error_tile);
      return quote.error;
    }
  }
  i64 result = MarkTerrainRaisePath(city, tile_index, tool != CITY_DEMOLISH_CLEAR, tile_index);
  if (result < 0) return result;
  result = RaiseMarkedTerrain(city, tile_index);
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  return result;
}

// One charged raise/lower step toward the brush's starting elevation.
i64 StepCityTerrainTowardHeight(City *city, TileIndex tile_index, int target_height,
    int tool) {
  assert(IsMapTileValid(city, tile_index));
  int height = ReadCityLandHeight(&city->tile[tile_index]);
  if (height < target_height)
    return RaiseCityTerrainCell(city, tile_index, tool);
  if (height > target_height)
    return LowerCityTerrainCell(city, tile_index, true, tool);
  return true;
}
