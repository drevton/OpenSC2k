#include "city/infrastructure/spans.h"

#include "city/infrastructure/lines.h"
#include "city/maps/terrain.h"
#include "city/infrastructure/surface_lines.h"
#include "city/infrastructure/bridges.h"
#include "city/infrastructure/power_lines.h"
#include "city/engine/commands.h"
#include "city/maps/layers.h"
#include "city/maps/map.h"
#include "city/presentation.h"
#include "city/saveload/map_format.h"
#include "city/state.h"
#include <assert.h>

const u16 kSpanCostPerCell[CITY_SPAN_STYLE_COUNT] = {
  [CITY_SPAN_POWER] = 10,
  [CITY_SPAN_RAIL] = 75,
  [CITY_SPAN_CAUSEWAY] = 25,
  [CITY_SPAN_RAISING_BRIDGE] = 50,
  [CITY_SPAN_SUSPENSION] = 75,
  [CITY_SPAN_HIGHWAY] = 200,
  [CITY_SPAN_REINFORCED_HIGHWAY] = 300,
  [CITY_SPAN_WOOD] = 20,
  [CITY_SPAN_CONCRETE] = 28,
  [CITY_SPAN_ARCH_STEEL] = 36,
  [CITY_SPAN_SUSPENSION_CONCRETE] = 42,
  [CITY_SPAN_SUSPENSION_STEEL] = 46,
  [CITY_SPAN_SUSPENSION_YELLOW] = 48,
  [CITY_SPAN_CANTILEVER_STEEL] = 56,
  [CITY_SPAN_CANTILEVER_BROWN] = 58,
  [CITY_SPAN_CANTILEVER_RED] = 62,
  [CITY_SPAN_GIRDER_STEEL] = 60,
  [CITY_SPAN_TUBULAR_STEEL] = 64,
  [CITY_SPAN_TUBULAR_YELLOW] = 95,
  [CITY_SPAN_TUBULAR_SILICON] = 128,
};

// Direction offsets from City.geography.rotation, shared by native and legacy shoreline searches.
static const i8 kSpanDirectionPreference[4] = {0, 2, 1, -1};

// Water kinds identify the crossing; water height supplies its level. Terrain codes describe the bed or shoreline, not
// a different kind of bridge.
static int NativeSpanBankStyles(const City *city, TileIndex tile, int water, int direction) {
  assert(IsMapTileValid(city, tile));
  const TileInfo *tp = &city->tile[tile];
  int height = ReadCityLandHeight(tp), shape = ReadTerrainCode(tp);
  bool inclined_bank = height == water && shape == ((direction - 1) & 3) + 1;
  if (HasTileWater(tp) ||
      (shape != CITY_TERRAIN_FLAT && !inclined_bank) || height < water || height > water + 1 ||
      ReadCityZoneKind(tp) == ZONE_MILITARY) return 0;
  int kind = tp->kind;
  if (IsTileKindUnbuilt(kind)) return CITY_SPAN_STYLE_POWER_MASK | CITY_SPAN_STYLE_RAIL_MASK |
      CITY_SPAN_STYLE_ROAD_MASK;
  if (IsTileKindPlainPowerLine(kind) && ReadCityStraightLineAxis(city, tile) == 0)
    return CITY_SPAN_STYLE_POWER_MASK;
  // A rising or supported head needs a straight road/track. Junctions are
  // permitted only on banks already level with the deck.
  if (height == water && IsCityLineBendOrJunction(city, tile)) return 0;
  if (kind == TK_ROAD) return CITY_SPAN_STYLE_ROAD_MASK;
  if (kind == TK_RAIL && (shape || FindCityRailTransition(city, tile) < 0))
    return CITY_SPAN_STYLE_RAIL_MASK;
  return 0;
}

// Search native level-water geometry for compatible banks. Requires a valid origin and CityInfrastructureDirection;
// returns a span with an optional notice code, without building or charging.
static CityInfrastructureSpan FindNativeWaterSpan(const City *city, TileIndex origin, int direction) {
  CityInfrastructureSpan span = {.start = origin, .end = INVALID_TILE, .error_tile = INVALID_TILE, .direction = direction,
      .error = CITY_NOTICE_NEEDS_OPPOSITE_SHORE};
  int dx = kLineXStep[direction], dy = kLineYStep[direction];
  TileIndex start = GetRelativeTileOrInvalid(city, origin, -dx, -dy);
  if (start == INVALID_TILE) return span;
  int water = ReadCityWaterHeight(&city->tile[origin]);
  span.start = start;
  int styles = NativeSpanBankStyles(city, span.start, water, direction);
  if (!styles) return span;
  span.length = 1;
  CityPoint point = MapTilePt(city, start);
  for (;;) {
    point.x += dx;
    point.y += dy;
    TileIndex tile = MapTileXYOrInvalid(city, point.x, point.y);
    if (tile == INVALID_TILE) return span;
    ++span.length;
    CityTileGeometry geometry = ReadCityTileGeometry(city, tile);
    if (geometry.water == CITY_WATER_NONE) {
      styles &= NativeSpanBankStyles(city, tile, water, (direction + 2) & 3);
      if (!styles) { span.error = CITY_NOTICE_CROSSING_BLOCKED; return span; }
      span.styles = ((CityInfrastructureSpanStyles(span) & ~CITY_SPAN_STYLE_ROAD_MASK) |
          CityNativeRoadBridgeStyles(span.length)) & styles;
      span.end = tile;
      span.error = 0;
      return span;
    }
    if (!IsTileKindExposedWater(city->tile[tile].kind)) {
      span.error = CITY_NOTICE_CROSSING_BLOCKED;
      return span;
    }
    if (geometry.water_height != water ||
        (geometry.water_surface != CITY_WATER_SURFACE_LEVEL && geometry.water_surface != CITY_WATER_SURFACE_SHORE)) {
      span.error = CITY_NOTICE_SHORELINE_TOO_STEEP;
      return span;
    }
  }
}

// Read-only shoreline scan shared by placement and its hover preview.
bool IsCityInfrastructureSpanWater(const City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile));
  return HasTileWater(&city->tile[tile]) &&
      ((city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) ||
          ReadLegacyTerrainCode(city, tile) < CITY_TERRAIN_BANK_END);
}

// Find a supported crossing from a candidate tile and direction, optionally choosing an admitted direction. Invalid
// origins return an unavailable span; search is read-only and retains notice codes for its caller.
CityInfrastructureSpan FindCityInfrastructureSpan(const City *city, TileIndex origin_tile, int direction,
    bool choose_direction) {
  CityInfrastructureSpan span = {.start = origin_tile, .end = INVALID_TILE, .error_tile = INVALID_TILE,
      .direction = direction};
  if (!IsMapTileValid(city, origin_tile)) return span;
  static const u8 kAllowedDirections[16] = {CONN_NONE, CONN_SOUTH, CONN_EAST,
      CONN_NORTH, CONN_WEST, CONN_SE, CONN_NE, CONN_NW,
      CONN_WS};
  if ((city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) &&
      HasTileWater(&city->tile[origin_tile])) {
    for (int candidate = 0; candidate < (choose_direction ? 4 : 1); ++candidate) {
      int axis = choose_direction ? (city->geography.rotation + kSpanDirectionPreference[candidate]) & 3 : direction;
      span = FindNativeWaterSpan(city, origin_tile, axis);
      if (span.end != INVALID_TILE) return span;
    }
    return span;
  }
  int terrain = ReadLegacyTerrainCode(city, origin_tile);
  if (terrain < CITY_TERRAIN_BANK_32 || terrain >= CITY_TERRAIN_BANK_END)
    return span;
  int directions = kAllowedDirections[terrain & CITY_TERRAIN_SHAPE_MASK];
  if (directions == 0)
    return span;
  if (choose_direction) {
    for (int candidate = 0; candidate < 4; ++candidate) {
      direction = (city->geography.rotation + kSpanDirectionPreference[candidate]) & 3;
      if (directions & (1 << direction))
        break;
      if (candidate == 3)
        return span;
    }
  }
  CityPoint origin_point = MapTilePt(city, origin_tile);
  int end_y = origin_point.y;
  int end_x = origin_point.x;
  TileIndex end_index = origin_tile;
  int distance = 0;
  for (;;) {
    int kind = city->tile[end_index].kind;
    if (distance != 0 && kind != TK_CLEAR && !IsTileKindExposedWater(kind)) {
      span.error = CITY_NOTICE_CROSSING_BLOCKED;
      return span;
    }
    end_y += kLineYStep[direction];
    end_x += kLineXStep[direction];
    end_index = MapTileXYOrInvalid(city, end_x, end_y);
    if (end_index == INVALID_TILE) {
      span.error = CITY_NOTICE_NEEDS_OPPOSITE_SHORE;
      return span;
    }
    ++distance;
    int next_terrain = ReadLegacyTerrainCode(city, end_index);
    if (next_terrain < CITY_TERRAIN_WATER_FIRST || next_terrain >= CITY_TERRAIN_BANK_END)
      break;
  }
  span.end = MapTileXY(city, end_x - kLineXStep[direction], end_y - kLineYStep[direction]);
  span.direction = direction;
  span.length = distance;
  return span;
}

// Return a span's explicit style mask or the stock length-based style choices. Span must contain initialized
// length/style data; this does not select a style or open a selector.
int CityInfrastructureSpanStyles(CityInfrastructureSpan span) {
  if (span.styles) return span.styles;
  int available = CITY_SPAN_STYLE_SHORT_MASK;
  if (span.length >= 5 && span.length <= 11) available = CITY_SPAN_STYLE_WITH_RAISING_MASK;
  if (span.length >= 7) available |= CITY_SPAN_STYLE_SUSPENSION_MASK;
  return available;
}

// Return the cheapest admitted style charge for an initialized span and allowed-style mask. Returns -1 when no valid
// end or matching style exists; performs no affordability check or payment.
i64 CityInfrastructureSpanCost(CityInfrastructureSpan span, int styles) {
  if (span.end == INVALID_TILE) return -1;
  int available = CityInfrastructureSpanStyles(span);
  i64 cost = -1;
  for (int style = CITY_SPAN_POWER; style < CITY_SPAN_STYLE_COUNT; ++style)
    if (available & styles & (1 << style)) {
      i64 price = (i64)span.length * kSpanCostPerCell[style];
      if (cost < 0 || price < cost) cost = price;
    }
  return cost;
}

// Place a bridge or tunnel across a contiguous terrain span.
i64 PlaceInfrastructureSpan(City *city, TileIndex origin_tile, int styles, int choose_direction) {
  assert(IsMapTileValid(city, origin_tile));
  CityInfrastructureSpan span = FindCityInfrastructureSpan(city, origin_tile,
      city->construction.direction, choose_direction != 0);
  if (span.end == INVALID_TILE) {
    return span.error ? -span.error : CITY_COMMAND_COST_BLOCKED;
  }
  bool dry_banks = span.start != origin_tile;
  TileIndex tile_index = span.start;
  int direction = span.direction;
  int distance = span.length;
  int available = CityInfrastructureSpanStyles(span);
  int choice = SelectInfrastructureSpan(city, distance, (available & styles));
  if (choice == CITY_SPAN_CANCEL) {
    CancelCityCommandFeedback(city);
    return CITY_COMMAND_COST_BLOCKED;
  }
  bool native_road = choice >= CITY_ROAD_BRIDGE_STYLE_FIRST;
  bool ground_approaches = dry_banks && choice != CITY_SPAN_POWER &&
      (city->fixes & (1ull << CITY_FIX_DRY_BRIDGE_APPROACHES));
  u32 cost = (u32)distance * kSpanCostPerCell[choice];
  i64 payment = PayCityCommandCost(city, cost);
  if (payment < 0) return payment;
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  if (dry_banks) InvalidateTile(city, tile_index);
  if (!dry_banks && ReadLegacyTerrainCode(city, tile_index) < CITY_TERRAIN_BANK_48) {
    int altitude = city->tile[tile_index].altitude;
    city->tile[tile_index].altitude = ((altitude & CITY_ALTITUDE_LAND_MASK) + 1) | (altitude &
        CITY_ALTITUDE_CLEAR_LAND);
  }
  if (!ground_approaches) WriteLegacyCityTerrainCode(city, tile_index, dry_banks &&
      ReadCityLandHeight(&city->tile[tile_index]) > ReadCityWaterHeight(&city->tile[origin_tile]) ?
      CITY_TERRAIN_FLAT : ((direction + 1) & 3) + 1);
  if (IsTileKindExposedWater(city->tile[tile_index].kind))
    ReplaceCityTileAndStatistics(city, tile_index, TK_CLEAR);
  if (choice == CITY_SPAN_POWER)
    PlaceCityPowerLineCell(city, tile_index);
  else if (choice == CITY_SPAN_RAIL)
    PlaceCityRailCell(city, tile_index);
  else if ((choice >= CITY_SPAN_CAUSEWAY && choice <= CITY_SPAN_SUSPENSION) || native_road)
    PlaceCityRoadCell(city, tile_index);
  int stride = kLineYStep[direction] * city->map_width + kLineXStep[direction];
  for (int position = 1; position < distance - 1; ++position) {
    tile_index += stride;
    if (choice == CITY_SPAN_POWER) {
      city->tile[tile_index].flags |= TF_POWERABLE;
      ReplaceCityTileAndStatistics(city, tile_index, TK_POWER_LINE_WATER);
      WriteTransportConnections(&city->tile[tile_index],
          direction & 1 ? CONN_NS : CONN_EW);
      RefreshCityPowerConnections(city, tile_index);
    }
    InvalidateTile(city, tile_index);
  }
  TileIndex end_index = tile_index + stride;
  if (dry_banks) InvalidateTile(city, end_index);
  if (!dry_banks && ReadLegacyTerrainCode(city, end_index) < CITY_TERRAIN_BANK_48) {
    int altitude = city->tile[end_index].altitude;
    city->tile[end_index].altitude = ((altitude & CITY_ALTITUDE_LAND_MASK) + 1) | (altitude &
        CITY_ALTITUDE_CLEAR_LAND);
  }
  if (!ground_approaches) WriteLegacyCityTerrainCode(city, end_index, dry_banks &&
      ReadCityLandHeight(&city->tile[end_index]) > ReadCityWaterHeight(&city->tile[origin_tile]) ?
      CITY_TERRAIN_FLAT : ((direction - 1) & 3) + 1);
  if (IsTileKindExposedWater(city->tile[end_index].kind))
    ReplaceCityTileAndStatistics(city, end_index, TK_CLEAR);
  if (choice == CITY_SPAN_POWER)
    PlaceCityPowerLineCell(city, end_index);
  else if (choice == CITY_SPAN_RAIL)
    PlaceCityRailCell(city, end_index);
  else if ((choice >= CITY_SPAN_CAUSEWAY && choice <= CITY_SPAN_SUSPENSION) || native_road)
    PlaceCityRoadCell(city, end_index);
  if (choice != CITY_SPAN_POWER) {
    BridgeStyle style = CityBridgeStyleFromChoice(choice);
    StoreCityBridgeSpan(city, span.start, end_index,
        choice == CITY_SPAN_RAIL ? TK_RAIL_BRIDGE_HEAD : TK_ROAD_BRIDGE_HEAD, style);
  }
  if (dry_banks) {
    for (int bank = 0; bank < 2; ++bank) {
      TileIndex tile = bank ? span.end : span.start;
      if (choice == CITY_SPAN_RAIL) UpdateRailConnections(city, tile);
      else if (choice == CITY_SPAN_POWER) UpdatePowerLineConnections(city, tile);
      else UpdateRoadConnections(city, tile);
      InvalidateTile(city, tile);
    }
  }
  return true;
}

// Tool-specific masks for the span selector. Returns the result of PlaceInfrastructureSpan unchanged; its input and
// side-effect contract applies.
i64 PlaceRoadSpan(City *city, TileIndex origin_tile, int choose_direction) {
  return PlaceInfrastructureSpan(city, origin_tile, CITY_SPAN_STYLE_ROAD_MASK, choose_direction);
}

// Place a span through the shared selector constrained to railway styles. Requires prepared construction direction and
// a valid start; preserves PlaceInfrastructureSpan's signed result and continuation behavior.
i64 PlaceRailSpan(City *city, TileIndex origin_tile, int choose_direction) {
  return PlaceInfrastructureSpan(city, origin_tile, CITY_SPAN_STYLE_RAIL_MASK, choose_direction);
}

// Place a span through the shared selector constrained to power-line styles. Requires prepared construction direction
// and a valid start; preserves cancellation, charging and signed-error behavior.
i64 PlacePowerLineSpan(City *city, TileIndex origin_tile, int choose_direction) {
  return PlaceInfrastructureSpan(city, origin_tile, CITY_SPAN_STYLE_POWER_MASK, choose_direction);
}
