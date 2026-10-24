#include "city/events/events.h"
#include "city/maps/labels.h"
#include "city/infrastructure/utilities.h"
#include "game/observation.h"
#include "game/state.h"
#include "city/infrastructure/lines.h"

#include "city/infrastructure/networks.h"
#include "city/infrastructure/spans.h"
#include "city/infrastructure/types.h"
#include "city/saveload/map_format.h"
#include "city/infrastructure/surface_lines.h"
#include "city/infrastructure/bridges.h"
#include "city/infrastructure/highways.h"
#include "city/infrastructure/power_lines.h"
#include "city/engine/commands.h"
#include "city/maps/layers.h"
#include "city/presentation.h"
#include "city/state.h"
#include "city/tools/tools.h"

const u8 kLineTerrainOverrideBySlope[16] = {
  0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0
};

const u8 kLineTileOffsetByConnectionMask[16] = {
  0, 0, 1, 6, 0, 0, 7, 11, 1, 9, 1, 10, 8, 13, 12, 14
};

// Surface reconnectors retain the complete stock terrain bank, including full-width offsets.
const u16 kLineTileOffsetByTerrain[CITY_TERRAIN_BANK_48] = {
  0, 2, 3, 4, 5, 0, 0, 0, 1, 1, 257, 257, 257, 257, 257, 1,
  1, 1, 257, 257, 257, 257, 257, 1, 1, 1, 257, 257, 257, 257, 257, 1,
  257, 257, 257, 257, 257, 257, 257, 257, 257, 257, 0, 0, 257, 256, 256, 257
};

const u8 kLineHorizontalConnectionAllowed[256] = {
  1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 0, 1,
  0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 21, 21, 20, 1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 5, 23, 22
};

const u8 kLineTerrainElevationDelta[16] = {
  0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 0, 0
};

const u16 kInfrastructureTerrainByDirection[64] = {
  0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 256, 256, 1, 1, 256, 256, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 2, 1, 2, 1, 2, 3, 2, 3, 4, 3, 4, 3, 4, 1, 4, 1, 0, 0, 1, 6, 0, 0, 7, 11, 1, 9, 1, 10
};

const u8 kLineTerrainDirectionAllowed[64] = {
  0, 0, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 1, 0, 2, 0, 1, 0
};

const u8 kLineVerticalConnectionAllowed[256] = {
  1, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
  1, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 21, 21, 20, 1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 5, 23, 22, 6, 7, 8, 9, 10, 11, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 19, 0, 0, 17, 18, 0, 0, 0, 0, 0, 12, 13, 24, 0, 0, 25, 0, 14, 15, 15, 15,
  15, 16, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 60, 0, 0, 0,
};

const i8 kLineYStep[CITY_LINE_DIRECTION_COUNT] = {
  [CITY_LINE_WEST] = 0, [CITY_LINE_SOUTH] = 1, [CITY_LINE_EAST] = 0, [CITY_LINE_NORTH] = -1,
};
const i8 kLineXStep[CITY_LINE_DIRECTION_COUNT] = {
  [CITY_LINE_WEST] = -1, [CITY_LINE_SOUTH] = 0, [CITY_LINE_EAST] = 1, [CITY_LINE_NORTH] = 0,
};

// Choose the dominant direction of an infrastructure line. Returns a cardinal direction along the dominant axis,
// preferring horizontal on equal displacement.
int CityLineDirection(int x, int y, int end_x, int end_y) {
  int dy = y - end_y;
  int dx = x - end_x;
  if (dy < 0)
    dy = -dy;
  if (dx < 0)
    dx = -dx;
  if (dx >= dy)
    return end_x < x ? CITY_LINE_WEST : CITY_LINE_EAST;
  return end_y >= y ? CITY_LINE_SOUTH : CITY_LINE_NORTH;
}

const CityInfrastructureDefinition kCityInfrastructure[CITY_INFRASTRUCTURE_COUNT] = {
  [CITY_INFRASTRUCTURE_ROAD] = {CITY_PLACEMENT_ROAD, 10},
  [CITY_INFRASTRUCTURE_RAIL] = {CITY_PLACEMENT_RAIL, 25},
  [CITY_INFRASTRUCTURE_POWER] = {CITY_PLACEMENT_POWER, 2},
  [CITY_INFRASTRUCTURE_PIPE] = {CITY_PLACEMENT_PIPE, 3},
  [CITY_INFRASTRUCTURE_SUBWAY] = {CITY_PLACEMENT_SUBWAY, 100},
  [CITY_INFRASTRUCTURE_HIGHWAY] = {CITY_PLACEMENT_ROAD, 25},
};

// Decode admitted command options for a valid infrastructure family. Endpoint and price are resolved values;
// no UI selection, City mutation, allocation or RNG is involved.
CityConstructionChoices ReadCityLineCommandChoices(CityInfrastructureKind kind, TileIndex end, int options) {
  CityConstructionChoices choices = {.span = CITY_SPAN_CANCEL};
  switch (kind) {
  case CITY_INFRASTRUCTURE_ROAD:
    choices.supplied = true;
    choices.span = DecodeCityRoadSpanChoice((options >> 8) & 255);
    choices.border = options & CITY_LINE_ACCEPT_BORDER;
    break;
  case CITY_INFRASTRUCTURE_RAIL:
    choices.supplied = true;
    choices.span = options & CITY_LINE_CANCEL_SPAN ? CITY_SPAN_CANCEL : CITY_SPAN_RAIL;
    choices.border = options & CITY_LINE_ACCEPT_BORDER;
    break;
  case CITY_INFRASTRUCTURE_POWER:
    choices.supplied = true;
    choices.span = options & CITY_LINE_CANCEL_SPAN ? CITY_SPAN_CANCEL : CITY_SPAN_POWER;
    break;
  case CITY_INFRASTRUCTURE_HIGHWAY:
    choices.supplied = true;
    choices.span = (options & 255) == CITY_ROAD_SPAN_CANCEL ? CITY_SPAN_CANCEL : options & 255;
    choices.border = options & 256;
    break;
  default:
    break; // Pipes and subway have no span or border choices.
  }
  choices.endpoint_supplied = true;
  choices.end = end;
  choices.unit_cost = kCityInfrastructure[kind].unit_cost;
  return choices;
}

// Quote a resolved infrastructure line and any eligible adjoining span using the execution traversal. Validates
// endpoints and family, writes planned_span only when supplied, and changes neither City, treasury nor RNG.
CityCommandCost GetCityInfrastructureCost(const City *city, TileIndex start, TileIndex end,
    CityInfrastructureKind kind, int span_styles, CityInfrastructureSpan *planned_span) {
  if (planned_span) *planned_span = (CityInfrastructureSpan){.end = INVALID_TILE, .error_tile = INVALID_TILE};
  CityCommandCost quote = {.error = CITY_COMMAND_COST_INVALID, .error_tile = INVALID_TILE};
  if (!IsMapTileValid(city, start) || !IsMapTileValid(city, end) ||
      kind < 0 || kind >= CITY_INFRASTRUCTURE_COUNT) return quote;
  int unit_cost = kCityInfrastructure[kind].unit_cost;
  quote.error = CITY_COMMAND_COST_BLOCKED;
  bool highway = kind == CITY_INFRASTRUCTURE_HIGHWAY;
  if (highway && IsCityCompactHighwayLine(city, start, end)) {
    CityCompactHighwayPlan compact;
    PlanCityCompactHighway(city, start, end, -1, &compact);
    if (planned_span) *planned_span = compact.span;
    int style = CITY_SPAN_HIGHWAY;
    if (span_styles >= 0) {
      style = CITY_SPAN_STYLE_COUNT;
      for (int choice = CITY_SPAN_HIGHWAY; choice <= CITY_SPAN_REINFORCED_HIGHWAY; ++choice)
        if (span_styles & (1 << choice)) { style = choice; break; }
    }
    i64 cost = CityCompactHighwayCost(&compact, style);
    return (CityCommandCost){.cost = cost < 0 ? 0 : cost,
        .error = cost < 0 ? (int)cost : CITY_COMMAND_COST_OK, .error_tile = cost < 0 ? start : INVALID_TILE};
  }
  bool road = kind == CITY_INFRASTRUCTURE_ROAD;
  bool rail = kind == CITY_INFRASTRUCTURE_RAIL;
  bool power = kind == CITY_INFRASTRUCTURE_POWER;
  int styles = highway ? CITY_SPAN_STYLES_ALL : road ? CITY_SPAN_STYLE_ROAD_MASK :
      rail ? CITY_SPAN_STYLE_RAIL_MASK : CITY_SPAN_STYLE_POWER_MASK;
  if (span_styles >= 0) styles &= span_styles;
  CityInfrastructureSpan span = {.end = INVALID_TILE, .error_tile = INVALID_TILE};
  CityPoint start_point = MapTilePt(city, start);
  if (highway) {
    TileIndex anchor = CityHighwayPlacementAnchor(city, start);
    if (anchor == INVALID_TILE) return quote;
    start_point = MapTilePt(city, anchor);
    if (start_point.x + 1 >= city->map_width || start_point.y + 1 >= city->map_height) return quote;
    bool water = false;
    for (int dy = 0; dy < 2; ++dy)
      for (int dx = 0; dx < 2; ++dx)
        water |= HasTileWater(&city->tile[MapTileXY(city, start_point.x + dx, start_point.y + dy)]);
    if (water) {
      span = FindCityHighwaySpan(city, start, city->geography.rotation, true);
      if (planned_span) *planned_span = span;
      if (span.end == INVALID_TILE) return quote;
    }
  } else if ((road || rail || power) && IsCityInfrastructureSpanWater(city, start)) {
    span = FindCityInfrastructureSpan(city, start, city->geography.rotation, true);
    if (planned_span) *planned_span = span;
    if (span.end == INVALID_TILE) return quote;
  }
  if (span.end != INVALID_TILE) {
    quote.cost = CityInfrastructureSpanCost(span, styles);
  } else {
    CityConstructionState plan = {.stage = kCityInfrastructure[kind].stage};
    quote.cost = CalculateCityLineCost(city, start, end, &plan, unit_cost, highway);
    if (quote.cost < 0) return quote;
    int step = highway ? 2 : 1;
    int follow_x = plan.endpoint_x + step * kLineXStep[plan.direction];
    int follow_y = plan.endpoint_y + step * kLineYStep[plan.direction];
    TileIndex follow = MapTileXYOrInvalid(city, follow_x, follow_y);
    if ((road || rail || power || highway) &&
        (!highway || quote.cost != 0) && follow != INVALID_TILE) {
      bool water = IsCityInfrastructureSpanWater(city, follow);
      if (highway && IsMapCoordinateValid(city, follow_x + 1, follow_y + 1))
        for (int dy = 0; dy < 2; ++dy)
          for (int dx = 0; dx < 2; ++dx)
            water |= HasTileWater(&city->tile[MapTileXY(city, follow_x + dx, follow_y + dy)]);
      if (styles && water) {
        span = highway ? FindCityHighwaySpan(city, follow, plan.direction, false) :
            FindCityInfrastructureSpan(city, follow, plan.direction, false);
        if (planned_span) *planned_span = span;
        i64 span_cost = CityInfrastructureSpanCost(span, styles);
        if (span_cost >= 0) quote.cost += span_cost;
      }
    }
  }
  if (quote.cost < 0) return quote;
  quote.error = CITY_COMMAND_COST_OK;
  return quote;
}

// Prefer the alternate direction on sloped terrain. Direction and terrain shape must be supported decoded values;
// returns the traversal choice without changing the plan.
int CityLineTerrainDirection(int x, int y, int end_x, int end_y) {
  int dy = y - end_y;
  int dx = x - end_x;
  if (dy < 0)
    dy = -dy;
  if (dx < 0)
    dx = -dx;
  if (dx >= dy) {
    if (dy <= 0)
      return end_x < x ? CITY_LINE_WEST : CITY_LINE_EAST;
    return end_y >= y ? CITY_LINE_SOUTH : CITY_LINE_NORTH;
  }
  if (dx <= 0)
    return end_y >= y ? CITY_LINE_SOUTH : CITY_LINE_NORTH;
  return end_x < x ? CITY_LINE_WEST : CITY_LINE_EAST;
}

// Reusing infrastructure is a successful, free placement when the repair is enabled. Requires a valid tile and
// matching placement stage; false includes disabled reuse and execution still owns charging/placement.
bool CityLineCellAlreadyBuilt(const City *city, TileIndex tile, int stage) {
  if (!(city->fixes & (1ull << CITY_FIX_REUSE_INFRASTRUCTURE))) return false;
  const TileInfo *tp = &city->tile[tile];
  if (stage == CITY_PLACEMENT_PIPE) {
    int underground = tp->underground & CITY_UNDERGROUND_KIND_MASK;
    return underground >= TUK_PIPE_FIRST && underground <= TUK_SUBWAY_NS_PIPE_EW;
  }
  if (IsTileKindBridge(tp->kind)) {
    int under = ReadCityBridgeUnderpass(tp);
    return (stage == CITY_PLACEMENT_ROAD && under == CITY_BRIDGE_UNDERPASS_ROAD) ||
        (stage == CITY_PLACEMENT_RAIL && under == CITY_BRIDGE_UNDERPASS_RAIL);
  }
  if (stage == CITY_PLACEMENT_ROAD) return tp->kind == TK_ROAD;
  if (stage == CITY_PLACEMENT_RAIL) return tp->kind == TK_RAIL &&
      (kLineTerrainOverrideBySlope[ReadTerrainCode(tp)] ||
          FindCityRailTransition(city, tile) < 0);
  return false;
}

// Validate the starting cell and prepare line endpoints and bounds. Requires writable initialized stage state and
// candidate endpoints; returns false when no admitted starting route can be prepared.
bool PrepareCityLine(const City *city, CityConstructionState *state, int x, int y, int end_x, int end_y) {
  const TileIndex tile_index = MapTileXY(city, x, y);
  const TileInfo *tp = &city->tile[tile_index];
  int kind = tp->kind;
  int direction = CityLineDirection(x, y, end_x, end_y);
  if (state->stage <= CITY_PLACEMENT_POWER && IsTileKindBridge(kind)) {
    if (x == end_x && y == end_y)
      direction = ReadCityBridgeAxis(tp) ? CITY_LINE_EAST : CITY_LINE_SOUTH;
    if (state->stage != CITY_PLACEMENT_ROAD && state->stage != CITY_PLACEMENT_RAIL) return false;
    if (!CanPlaceCityBridgeUnderpass(city, tile_index,
        state->stage == CITY_PLACEMENT_ROAD ? TK_ROAD : TK_RAIL, direction & 1))
      return false;
  } else if (state->stage <= CITY_PLACEMENT_POWER) {
    int terrain_code = ReadLegacyTerrainCode(city, tile_index);
    if (((city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) &&
            HasTileWater(tp)) ||
        kind == TK_HIGHWAY_ENTRANCE || IsTileKindMultiHighway(kind) ||
        kind == TK_SUBWAY_TO_RAIL || IsTileKindBuilding(kind) ||
        (terrain_code >= CITY_TERRAIN_WATER_FIRST && terrain_code < CITY_TERRAIN_BANK_32) || kind == TK_PARK)
      return false;
  }
  if (state->stage != CITY_PLACEMENT_PIPE && state->stage != CITY_PLACEMENT_SUBWAY &&
      kind == TK_RADIOACTIVE_WASTE)
    return false;
  if (state->stage != CITY_PLACEMENT_ALLOW_MILITARY &&
      ReadCityZoneKind(tp) == ZONE_MILITARY)
    return false;
  state->endpoint_y = end_y;
  state->endpoint_x = end_x;
  state->direction = direction;
  int terrain_code = ReadLegacyTerrainCode(city, tile_index);
  if (!CityLineCellAlreadyBuilt(city, tile_index, state->stage) &&
      terrain_code != CITY_TERRAIN_FLAT && terrain_code <= CITY_TERRAIN_SHAPE_4) {
    int terrain = 4 * (terrain_code & CITY_TERRAIN_SHAPE_MASK);
    if (kLineTerrainDirectionAllowed[state->direction + terrain] != 0) {
      state->direction = CityLineTerrainDirection(x, y, state->endpoint_x, state->endpoint_y);
      if (kLineTerrainDirectionAllowed[state->direction + terrain] != 0) {
        state->endpoint_y = y;
        state->endpoint_x = x;
      }
    }
  }
  state->min_y = IntMin(end_y, y);
  state->max_y = IntMax(end_y, y);
  state->min_x = IntMin(end_x, x);
  state->max_x = IntMax(end_x, x);
  return true;
}

// Advance an infrastructure line, respecting terrain and existing tiles. Requires a prepared stage/end and valid
// cursor pointers; false stops traversal before an unsuitable next cell is placed.
bool AdvanceCityLine(const City *city, CityConstructionState *state, int *x, int *y) {
  if (*y == state->endpoint_y && *x == state->endpoint_x)
    return false;

  const TileIndex tile_index = MapTileXY(city, *x, *y);
  const TileInfo *tp = &city->tile[tile_index];
  bool under_bridge = IsTileKindBridge(tp->kind) &&
      (state->stage == CITY_PLACEMENT_ROAD || state->stage == CITY_PLACEMENT_RAIL);
  bool forced_direction = false;
  if (state->stage < CITY_PLACEMENT_SUBWAY) {
    forced_direction = ReadCityStraightLineAxis(city, tile_index, 0) == 1 - (state->direction & 1);
  } else {
    int underground = tp->underground & CITY_UNDERGROUND_KIND_MASK;
    if (underground != TUK_CLEAR) {
      int shape = underground + (state->direction & 1);
      forced_direction = shape == TUK_PIPE_NS || shape == TUK_SUBWAY_NS;
    }
  }
  bool already_built = CityLineCellAlreadyBuilt(city, tile_index, state->stage);
  if (already_built) forced_direction = false;
  if (under_bridge) forced_direction = true;

  int terrain_code = ReadLegacyTerrainCode(city, tile_index);
  bool underwater_step = false;
  int reuse_direction = CityLineDirection(*x, *y, state->endpoint_x, state->endpoint_y);
  int next_y = *y + kLineYStep[reuse_direction];
  int next_x = *x + kLineXStep[reuse_direction];
  TileIndex next_index = MapTileXY(city, next_x, next_y);
  const TileInfo *next_tp = &city->tile[next_index];
  bool existing_step = !under_bridge && already_built &&
      CityLineCellAlreadyBuilt(city, next_index, state->stage);
  if (existing_step) state->direction = reuse_direction;
  // Buried utilities can cross a lake bank in either direction. Keep the
  // stored bed geometry; surface slope admission is not a pipe-height model.
  if ((city->fixes & (1ull << CITY_FIX_BURIED_UTILITY_TERRAIN)) &&
      (state->stage == CITY_PLACEMENT_PIPE || state->stage == CITY_PLACEMENT_SUBWAY)) {
    underwater_step = HasTileWater(tp) || HasTileWater(next_tp);
    if (underwater_step) state->direction = reuse_direction;
  }
  if (!existing_step && !underwater_step &&
      (forced_direction || kLineTerrainOverrideBySlope[terrain_code & CITY_TERRAIN_SHAPE_MASK] != 0)) {
    int elevation = ReadCityLandHeight(tp);
    next_y = *y + kLineYStep[state->direction];
    next_x = *x + kLineXStep[state->direction];
    if (next_y < (i32)state->min_y || next_x < (i32)state->min_x
        || next_y > (i32)state->max_y || next_x > (i32)state->max_x)
      return false;
    next_index = MapTileXY(city, next_x, next_y);
    next_tp = &city->tile[next_index];
    int next_terrain_code = ReadLegacyTerrainCode(city, next_index);
    if (state->stage == CITY_PLACEMENT_RAIL && next_terrain_code != CITY_TERRAIN_FLAT &&
        ReadCityLandHeight(next_tp) != elevation)
      return false;
    if (next_terrain_code < CITY_TERRAIN_BANK_END &&
        kLineTerrainDirectionAllowed[4 * (next_terrain_code & CITY_TERRAIN_SHAPE_MASK) + state->direction] != 0)
      return false;
  } else if (!existing_step && !underwater_step) {
    int desired_elevation = kLineTerrainElevationDelta[terrain_code & CITY_TERRAIN_SHAPE_MASK] +
        ReadCityLandHeight(tp);
    int direction = reuse_direction;
    int next_terrain_code = ReadLegacyTerrainCode(city, next_index);
    int next_elevation = ReadCityLandHeight(next_tp);
    if ((next_terrain_code < CITY_TERRAIN_BANK_END &&
            kLineTerrainDirectionAllowed[4 * (next_terrain_code & CITY_TERRAIN_SHAPE_MASK) + direction] != 0) ||
        (kLineTerrainElevationDelta[next_terrain_code & CITY_TERRAIN_SHAPE_MASK] != 0 &&
            (next_elevation == desired_elevation || next_elevation - desired_elevation == -2)) ||
        (next_terrain_code == CITY_TERRAIN_FLAT && next_elevation - desired_elevation == -1)) {
      direction = CityLineTerrainDirection(*x, *y, state->endpoint_x, state->endpoint_y);
      next_y = *y + kLineYStep[direction];
      next_x = *x + kLineXStep[direction];
      next_index = MapTileXY(city, next_x, next_y);
      next_tp = &city->tile[next_index];
      next_terrain_code = ReadLegacyTerrainCode(city, next_index);
      next_elevation = ReadCityLandHeight(next_tp);
      if (next_terrain_code < CITY_TERRAIN_BANK_END &&
          kLineTerrainDirectionAllowed[4 * (next_terrain_code & CITY_TERRAIN_SHAPE_MASK) + direction] != 0)
        return false;
      if (kLineTerrainElevationDelta[next_terrain_code & CITY_TERRAIN_SHAPE_MASK] != 0 &&
          (next_elevation == desired_elevation || next_elevation - desired_elevation == -2))
        return false;
      if (next_terrain_code == CITY_TERRAIN_FLAT && next_elevation - desired_elevation == -1)
        return false;
    }
    if (state->stage == CITY_PLACEMENT_RAIL && state->direction != direction &&
        next_terrain_code != CITY_TERRAIN_FLAT)
      return false;
    state->direction = direction;
  }

  if (state->stage != CITY_PLACEMENT_ALLOW_MILITARY &&
      ReadCityZoneKind(next_tp) == ZONE_MILITARY)
    return false;
  if (state->stage < CITY_PLACEMENT_SUBWAY) {
    if (IsMapTileValid(city, next_index) &&
        IsCityInfrastructureSpanWater(city, next_index))
      return false;
    int kind = next_tp->kind;
    if (kind == TK_RADIOACTIVE_WASTE || kind == TK_PARK)
      return false;
    if (IsTileKindBridge(kind)) {
      if ((state->stage != CITY_PLACEMENT_ROAD && state->stage != CITY_PLACEMENT_RAIL) ||
          !CanPlaceCityBridgeUnderpass(city, next_index,
              state->stage == CITY_PLACEMENT_ROAD ? TK_ROAD : TK_RAIL,
              state->direction & 1)) return false;
    } else if (!IsTileKindUnbuilt(kind) && !CityLineCellAlreadyBuilt(city, next_index, state->stage)) {
      if (ReadCityStraightLineAxis(city, next_index, 0) != 1 - (state->direction & 1))
        return false;
    }
  } else {
    int tunnel_height =
        (next_tp->altitude & CITY_ALTITUDE_TUNNEL_MASK) >> CITY_ALTITUDE_TUNNEL_SHIFT;
    if (tunnel_height == 1 || tunnel_height == 2)
      return false;
    int underground = next_tp->underground & CITY_UNDERGROUND_KIND_MASK;
    if (underground != TUK_CLEAR && !CityLineCellAlreadyBuilt(city, next_index, state->stage)) {
      int shape = underground + (state->direction & 1);
      if (shape != TUK_PIPE_NS && shape != TUK_SUBWAY_NS)
        return false;
    }
  }
  *y = next_y;
  *x = next_x;
  return true;
}

// Invalidate the line preview and count terrain surcharge cells.
bool PrepareCityZonePreview(City *city, TileIndex start, TileIndex end) {
  CityPoint from = MapTilePt(city, start), to = MapTilePt(city, end);
  int x = from.x, y = from.y;
  if (PrepareCityLine(city, &city->construction, x, y, to.x, to.y) == 0) return false;
  city->construction.terrain_surcharge_count = 0;
  city->construction.cell_count = 0;
  do {
    TileIndex tile = MapTileXY(city, x, y);
    if (!CityLineCellAlreadyBuilt(city, tile, city->construction.stage)) {
      int terrain_code = ReadLegacyTerrainCode(city, tile);
      ++city->construction.cell_count;
      if (terrain_code < CITY_TERRAIN_BANK_48 &&
          kZoneTerrainSurchargeFlags[terrain_code & CITY_TERRAIN_SHAPE_MASK] != 0)
        ++city->construction.terrain_surcharge_count;
    }
    InvalidateTile(city, tile);
  } while (AdvanceCityLine(city, &city->construction, &x, &y) != 0);
  return true;
}

// Price the accepted route using exactly the traversal used to construct it. The final accepted cell is returned in
// state->endpoint_x/y for optional spans. Invalid endpoints return -1; state is caller-owned planning output and map,
// treasury and RNG remain unchanged.
i64 CalculateCityLineCost(const City *city, TileIndex start, TileIndex end, CityConstructionState *state, int unit_cost,
    bool highway) {
  if (!IsMapTileValid(city, start) || !IsMapTileValid(city, end)) return -1;
  CityPoint start_point = MapTilePt(city, start);
  CityPoint end_point = MapTilePt(city, end);
  state->cell_count = 0;
  state->terrain_surcharge_count = 0;
  bool preserve_terrain = (city->fixes & (1ull << CITY_FIX_BURIED_UTILITY_TERRAIN)) &&
      (state->stage == CITY_PLACEMENT_PIPE || state->stage == CITY_PLACEMENT_SUBWAY);
  if (highway) {
    TileIndex anchor = CityHighwayPlacementAnchor(city, start);
    if (anchor == INVALID_TILE) return -1;
    start_point = MapTilePt(city, anchor);
    end_point.x -= (end_point.x - start_point.x) & 1; end_point.y -= (end_point.y - start_point.y) & 1;
    if (start_point.x + 1 >= city->map_width || start_point.y + 1 >= city->map_height ||
        GetTransportSpanCompatibilityMask(city, anchor) == CITY_SPAN_COMPATIBILITY_INVALID) return -1;
    PrepareCityHighwayLine(city, state, start_point.x, start_point.y, end_point.x, end_point.y);
  } else if (!PrepareCityLine(city, state, start_point.x, start_point.y, end_point.x, end_point.y)) {
    return -1;
  }
  do {
    TileIndex tile = MapTileXY(city, start_point.x, start_point.y);
    if (highway) {
      for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx)
          if (!CityHighwayCellAlreadyBuilt(city, MapTileXY(city, start_point.x + dx, start_point.y + dy)))
            ++state->cell_count;
      continue;
    }
    if (CityLineCellAlreadyBuilt(city, tile, state->stage)) continue;
    ++state->cell_count;
    if (preserve_terrain) continue;
    int terrain = ReadLegacyTerrainCode(city, tile);
    if (terrain < CITY_TERRAIN_BANK_48 &&
        kZoneTerrainSurchargeFlags[terrain & CITY_TERRAIN_SHAPE_MASK]) ++state->terrain_surcharge_count;
  } while (highway ? AdvanceCityHighwayLine(city, state, &start_point.x, &start_point.y) :
      AdvanceCityLine(city, state, &start_point.x, &start_point.y));
  state->endpoint_x = start_point.x;
  state->endpoint_y = start_point.y;
  return (i64)unit_cost * state->cell_count + 25 * (i64)state->terrain_surcharge_count;
}

// Resolve a supplied endpoint using the same planner as interactive dragging. Keep any planning writes on failure;
// only a successful route replaces the live cost and returns the reached tile.
TileIndex PrepareCityLineEndpoint(City *city, TileIndex start, TileIndex end, int unit_cost, bool highway) {
  i64 cost = CalculateCityLineCost(city, start, end, &city->construction, unit_cost, highway);
  if (cost < 0) return INVALID_TILE;
  city->construction.cost = cost;
  return MapTileXY(city, city->construction.endpoint_x, city->construction.endpoint_y);
}

// Find the neighbor connection tile reached by a prepared line. Requires valid endpoints and construction geometry;
// INVALID_TILE means the line does not offer a supported border connection.
TileIndex FindCityBorderConnection(const City *city, TileIndex start, TileIndex end, int direction, bool highway) {
  CityPoint end_point = MapTilePt(city, end);
  int step = highway ? 2 : 1;
  bool single_edge = start == end &&
      (end_point.x == 0 || end_point.y == 0 || end_point.x == city->map_width - step ||
          end_point.y == city->map_height - step);
  if (IsMapCoordinateValid(city, end_point.x + step * kLineXStep[direction],
      end_point.y + step * kLineYStep[direction]) && !single_edge) return INVALID_TILE;
  // The original highway command checks the starting block's overlay.
  return ReadOverlay(city, highway ? start : end) == CITY_OVERLAY_BORDER_CONNECTION ? INVALID_TILE : end;
}

// Quote the additional neighbor connection for a resolved infrastructure family. Endpoints and kind must follow line
// admission; returns the connection charge without recording a border connection or charging treasury.
int GetCityLineConnectionCost(const City *city, TileIndex start, TileIndex end, CityInfrastructureKind kind) {
  bool highway = kind == CITY_INFRASTRUCTURE_HIGHWAY;
  bool road = kind == CITY_INFRASTRUCTURE_ROAD;
  bool rail = kind == CITY_INFRASTRUCTURE_RAIL;
  if ((!road && !rail && !highway) || !IsMapTileValid(city, start) || !IsMapTileValid(city, end)) return 0;
  if (HasTileWater(&city->tile[start])) return 0;
  CityConstructionState plan = {.stage = rail ? CITY_PLACEMENT_RAIL : CITY_PLACEMENT_ROAD};
  int unit_cost = kCityInfrastructure[kind].unit_cost;
  i64 cost = CalculateCityLineCost(city, start, end, &plan, unit_cost, highway);
  int connection_cost = road ? 1000 : 1500;
  if (cost <= 0) return 0;
  if (highway) start = CityHighwayPlacementAnchor(city, start);
  TileIndex endpoint = MapTileXY(city, plan.endpoint_x, plan.endpoint_y);
  return FindCityBorderConnection(city, start, endpoint, plan.direction, highway) == INVALID_TILE ? 0 : connection_cost;
}

// Advance before placing each cell: advancement updates live construction state consumed by terrain preparation and
// the individual line placers. Requires paid/prepared construction state and a valid current cursor; returns the
// reached tile after applying admitted cells.
static TileIndex PlaceCityLineCells(City *city, TileIndex tile, TileIndex endpoint, void (*place)(City *, TileIndex)) {
  CityPoint start = MapTilePt(city, tile), end = MapTilePt(city, endpoint);
  PrepareCityLine(city, &city->construction, start.x, start.y, end.x, end.y);
  CityPoint next = start;
  while (AdvanceCityLine(city, &city->construction, &next.x, &next.y)) {
    PrepareInfrastructureTerrainCell(city, tile);
    place(city, tile);
    tile += (next.y - start.y) * city->map_width + next.x - start.x;
    start = next;
  }
  PrepareInfrastructureTerrainCell(city, tile);
  place(city, tile);
  return tile;
}

// Validate the payment before debiting cash; reused lines retain their distinct result. Requires the prepared line's
// live construction state; negative errors leave payment uncommitted and success debits the actual charge once.
i64 PayCityLineCost(City *city, bool allow_reuse) {
  int cost = city->construction.cost;
  if (!cost && allow_reuse && (city->fixes & (1ull << CITY_FIX_REUSE_INFRASTRUCTURE))) {
    city->construction.direction = CITY_LINE_WEST;
    return CITY_LINE_PAYMENT_REUSED;
  }
  if (!cost) return CITY_COMMAND_COST_BLOCKED;
  i64 payment = PayCityCommandCost(city, cost);
  if (payment < 0) return payment;
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  return CITY_LINE_PAYMENT_PAID;
}

// Collect or use the prepared endpoint, charge the line and place its underground cells. Construction stage and unit
// cost must match the caller's pipe/subway family; signed errors and cancellation are preserved.
static i64 PlaceCityUndergroundLine(City *city, TileIndex origin_tile, bool subway) {
  int group = subway ? CITY_TOOL_GROUP_RAIL : CITY_TOOL_GROUP_WATER;
  void (*place)(City *, TileIndex) = subway ? PlaceCitySubwayCell : PlaceCityWaterPipeCell;
  city->construction.stage = subway ? CITY_PLACEMENT_SUBWAY : CITY_PLACEMENT_PIPE;
  TileIndex endpoint = city->command_choices.endpoint_supplied ?
      PrepareCityLineEndpoint(city, origin_tile, city->command_choices.end, city->command_choices.unit_cost, false) :
      TrackCitySelectedInfrastructureLine(city->game, origin_tile, group);
  if (endpoint == INVALID_TILE)
    return CITY_COMMAND_COST_BLOCKED;
  i64 payment = PayCityLineCost(city, !subway);
  if (payment < 0) return payment;
  if (payment == CITY_LINE_PAYMENT_REUSED) return true;
  PlaceCityLineCells(city, origin_tile, endpoint, place);
  city->construction.direction = CITY_LINE_WEST;
  return true;
}

// Build pipes from resolved choices or a retained drag. The shared underground path owns payment and reconnection.
i64 PlaceCityWaterPipe(City *city, TileIndex origin_tile) {
  return PlaceCityUndergroundLine(city, origin_tile, false);
}

// Build subway from resolved choices or a retained drag, using the same live construction/payment state.
i64 PlaceCitySubway(City *city, TileIndex origin_tile) {
  return PlaceCityUndergroundLine(city, origin_tile, true);
}

// Road and rail retain their entry points and choices, sharing the same placement/continuation algorithm and its live
// construction state. Requires a valid origin and matching road/rail family; signed errors, cancellation and nested
// span callbacks retain their original order.
static i64 PlaceCitySurfaceLine(City *city, TileIndex origin_tile, bool rail) {
  int group = rail ? CITY_TOOL_GROUP_RAIL : CITY_TOOL_GROUP_ROAD;
  int connection_cost = rail ? 1500 : 1000;
  i64 (*place_span)(City *, TileIndex, int) = rail ? PlaceRailSpan : PlaceRoadSpan;
  void (*place)(City *, TileIndex) = rail ? PlaceCityRailCell : PlaceCityRoadCell;
  void (*reconnect)(City *, TileIndex) = rail ? UpdateRailConnections : UpdateRoadConnections;
  if (IsMapTileValid(city, origin_tile) && IsCityInfrastructureSpanWater(city, origin_tile)) {
    city->construction.direction = city->geography.rotation;
    return place_span(city, origin_tile, 1);
  }
  city->construction.stage = rail ? CITY_PLACEMENT_RAIL : CITY_PLACEMENT_ROAD;
  TileIndex endpoint = city->command_choices.endpoint_supplied ?
      PrepareCityLineEndpoint(city, origin_tile, city->command_choices.end, city->command_choices.unit_cost, false) :
      TrackCitySelectedInfrastructureLine(city->game, origin_tile, group);
  if (endpoint == INVALID_TILE)
    return CITY_COMMAND_COST_BLOCKED;
  if (IsTileKindBridge(city->tile[origin_tile].kind)) {
    CityPoint origin_point = MapTilePt(city, origin_tile), end_point = MapTilePt(city, endpoint);
    if (!PrepareCityLine(city, &city->construction, origin_point.x, origin_point.y, end_point.x, end_point.y))
      return CITY_COMMAND_COST_BLOCKED;
  }
  bool extend_bridge = false;
  if (!city->construction.cost) {
    CityInfrastructureSpan span;
    GetCityInfrastructureCost(city, origin_tile, endpoint,
        rail ? CITY_INFRASTRUCTURE_RAIL : CITY_INFRASTRUCTURE_ROAD, -1, &span);
    extend_bridge = span.end != INVALID_TILE;
  }
  if (!extend_bridge && city->construction.cost == 0 && (city->fixes & (1ull << CITY_FIX_REUSE_INFRASTRUCTURE))) {
    city->construction.direction = CITY_LINE_WEST;
    return true;
  }
  if (!extend_bridge && !city->construction.cost) return CITY_COMMAND_COST_BLOCKED;
  i64 payment = PayCityCommandCost(city, city->construction.cost);
  if (payment < 0) return payment;
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  const TileIndex end_index = PlaceCityLineCells(city, origin_tile, endpoint, place);
  int direction = city->construction.direction;
  TileIndex follow_index = GetRelativeTileOrInvalid(city, end_index,
      kLineXStep[direction], kLineYStep[direction]);
  if (follow_index != INVALID_TILE) {
    if (IsCityInfrastructureSpanWater(city, follow_index))
      place_span(city, follow_index, 0);
  }
  if (FindCityBorderConnection(city, origin_tile, end_index, direction, false) != INVALID_TILE &&
      AffordableCityCommandCost(city, connection_cost) >= 0 &&
      ChooseCityConnectionQuestion(city, rail ? CITY_BORDER_CONNECTION_RAIL : CITY_BORDER_CONNECTION_ROAD,
          connection_cost) == CITY_DIALOG_YES) {
    DebitCityCommandCost(city, connection_cost);
    WriteOverlay(city, end_index, CITY_OVERLAY_BORDER_CONNECTION);
    reconnect(city, end_index);
    if (rail) {
      ++city->infrastructure.service_bonus_a;
    } else ++city->infrastructure.service_bonus_b;
    InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  }
  city->construction.direction = CITY_LINE_WEST;
  return true;
}

// Build a road through the shared surface path, including spans and an admitted border connection.
i64 PlaceCityRoad(City *city, TileIndex origin_tile) {
  return PlaceCitySurfaceLine(city, origin_tile, false);
}

// Build rail through the shared surface path, retaining its own span and border connection policy.
i64 PlaceCityRail(City *city, TileIndex origin_tile) {
  return PlaceCitySurfaceLine(city, origin_tile, true);
}

// Build a power line from resolved choices or a retained drag. Water spans keep their separate admission/charging path.
i64 PlaceCityPowerLine(City *city, TileIndex origin_tile) {
  if (IsMapTileValid(city, origin_tile) && IsCityInfrastructureSpanWater(city, origin_tile)) {
    city->construction.direction = city->geography.rotation;
    return PlacePowerLineSpan(city, origin_tile, 1);
  }
  city->construction.stage = CITY_PLACEMENT_POWER;
  TileIndex endpoint = city->command_choices.endpoint_supplied ?
      PrepareCityLineEndpoint(city, origin_tile, city->command_choices.end, city->command_choices.unit_cost, false) :
      TrackInfrastructureLinePlacement(city->game, origin_tile,
          kCityToolCosts[CITY_TOOL_GROUP_POWER * CITY_TOOL_VARIANT_CAPACITY + CITY_POWER_LINES]);
  if (endpoint == INVALID_TILE)
    return CITY_COMMAND_COST_BLOCKED;
  if (!city->construction.cost) return CITY_COMMAND_COST_BLOCKED;
  i64 payment = PayCityCommandCost(city, city->construction.cost);
  if (payment < 0) return payment;
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  TileIndex end_index = PlaceCityLineCells(city, origin_tile, endpoint, PlaceCityPowerLineCell);
  TileIndex follow_index = GetRelativeTileOrInvalid(city, end_index,
      kLineXStep[city->construction.direction], kLineYStep[city->construction.direction]);
  if (follow_index != INVALID_TILE) {
    if (IsCityInfrastructureSpanWater(city, follow_index))
      PlacePowerLineSpan(city, follow_index, 0);
  }
  city->construction.direction = CITY_LINE_WEST;
  return true;
}

// Place the selected line family from existing resolved choices or retained input. Payment and cell traversal
// remain in the family's placement routine. Only diagnostic observers may supply a missing pointer release.
i64 PlaceCityInfrastructureLine(City *city, CityInfrastructureKind kind, TileIndex tile, TileIndex end) {
  const GameObservation *observer = city->game->observation;
  if (observer && observer->line_endpoint_input && !city->command_choices.endpoint_supplied && !city->zone_drag_input)
    return observer->line_endpoint_input(city, kind, tile, end);
  switch (kind) {
  case CITY_INFRASTRUCTURE_HIGHWAY:
    return IsCityCompactHighwayLine(city, tile, end) ? PlaceCityTransportLine(city, tile) :
        PlaceCityLegacyTransportLine(city, tile);
  case CITY_INFRASTRUCTURE_PIPE:
    return PlaceCityWaterPipe(city, tile);
  case CITY_INFRASTRUCTURE_SUBWAY:
    return PlaceCitySubway(city, tile);
  case CITY_INFRASTRUCTURE_ROAD:
    return PlaceCityRoad(city, tile);
  case CITY_INFRASTRUCTURE_RAIL:
    return PlaceCityRail(city, tile);
  case CITY_INFRASTRUCTURE_POWER:
    return PlaceCityPowerLine(city, tile);
  default:
    return CITY_COMMAND_COST_INVALID;
  }
}

// Construct a road, rail, power, pipe or subway line from its resolved family and options.
// TEST is read-only; execution uses supplied span/border choices without consulting UI tools.
i64 CmdPlaceInfrastructureLine(City *city, u32 flags, TileIndex start,
    CityInfrastructureKind kind, TileIndex end, int options, int reserved) {
  if (!IsMapTileValid(city, start) || !IsMapTileValid(city, end) || reserved) return CITY_COMMAND_COST_INVALID;
  int allowed = 0;
  switch (kind) {
  case CITY_INFRASTRUCTURE_ROAD:
    allowed = CITY_LINE_ACCEPT_BORDER | 65280;
    if (DecodeCityRoadSpanChoice((options >> 8) & 255) < CITY_SPAN_CANCEL) return CITY_COMMAND_COST_INVALID;
    break;
  case CITY_INFRASTRUCTURE_RAIL:
    allowed = CITY_LINE_ACCEPT_BORDER | CITY_LINE_CANCEL_SPAN;
    break;
  case CITY_INFRASTRUCTURE_POWER:
    allowed = CITY_LINE_CANCEL_SPAN;
    break;
  case CITY_INFRASTRUCTURE_PIPE:
  case CITY_INFRASTRUCTURE_SUBWAY:
    break;
  default:
    return CITY_COMMAND_COST_INVALID;
  }
  if (options & ~allowed) return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) {
    CityConstructionChoices choices = ReadCityLineCommandChoices(kind, end, options);
    int styles = choices.supplied ? choices.span < 0 ? 0 : 1 << choices.span : -1;
    CityCommandCost quote = GetCityInfrastructureCost(city, start, end, kind, styles, NULL);
    if (quote.error) return CityCommandPriceResult(quote);
    if (choices.border) quote.cost += GetCityLineConnectionCost(city, start, end, kind);
    return quote.cost;
  }
  i64 placed = PlaceCityInfrastructureLine(city, kind, start, end);
  if (placed > 0 && city->indicators.population < 50000) {
    if (kind == CITY_INFRASTRUCTURE_POWER) ScanPowerGridPhase(city);
    if (kind == CITY_INFRASTRUCTURE_PIPE) ScanCityWaterNetwork(city);
  }
  return placed;
}
