// TileInfo storage for native road/rail/highway bridges (zero-based bits):
// kind selects the deck family and head/middle. Dry middle groups encode the underpass in kind's low two bits:
// 0 clear, 1 rail, 2 road, 3 power. Separate *_BRIDGE_WATER kinds mean wet middles without an underpass.
// Heads are always dry. terrain[4] is span axis (0 X/east-west, 1 Y/north-south); head terrain[5] selects the
// low-index end (0) or high-index end (1). Middle terrain[5] has no head-facing meaning; terrain[6] is unused.
// Highway terrain[7] selects the second parallel lane; it is not the building/highway footprint continuation bit.
// terrain[0..3] is ground/coverage for dry middles, or water kind minus TK_WATER for wet middles.
// Head altitude[5..8] holds BridgeStyle for road/rail or HighwayBridgeStyle for highways; altitude[9] holds the
// highway alternating-piece phase. Middle altitude[5..9] remains the underlying water-height field, meaningful as
// height only on wet middles; deck height comes from heads.
// altitude[0..4] remains land/bed height. zone[4] selects underlying slope versus zoning in zone[0..3]; zone[5..7]
// has no bridge payload. Shared flags/overlay, tunnels and underground retain their tile_storage.h meanings.

#include "city/infrastructure/bridges.h"

#include <assert.h>
#include <stdlib.h>

#include "assets/state.h"
#include "audio/audio.h"
#include "city/engine/commands.h"
#include "city/events/events.h"
#include "city/infrastructure/infrastructure.h"
#include "city/infrastructure/power_lines.h"
#include "city/maps/ground.h"
#include "city/maps/layers.h"
#include "city/maps/module.h"
#include "city/maps/water.h"
#include "city/presentation.h"
#include "city/state.h"
#include "city/tools/tools.h"
#include "drawing/maps/map_terrain.h"
#include "drawing/maps/tables.h"
#include "game/state.h"

// Shared bridge policy, indexed by the saved road/rail style. Length bounds count deck tiles, excluding heads.
// Geometry and palette are authored artwork indexes, not saved styles or chooser values.
typedef struct BridgeDefinition {
  u16 maximum_deck;
  u8 minimum_deck, choice, geometry, palette;
} BridgeDefinition;

static const BridgeDefinition kBridgeDefinitions[BRIDGE_STYLE_COUNT] = {
  //                                    maximum, minimum, chooser value, geometry, palette
  [BRIDGE_STYLE_WOOD]                = {65535, 0, CITY_SPAN_WOOD,                 0, 0},
  [BRIDGE_STYLE_CONCRETE]            = {    2, 0, CITY_SPAN_CONCRETE,             1, 1},
  [BRIDGE_STYLE_ARCH_STEEL]          = {    5, 0, CITY_SPAN_ARCH_STEEL,           2, 2},
  [BRIDGE_STYLE_SUSPENSION_CONCRETE] = {   10, 2, CITY_SPAN_SUSPENSION_CONCRETE,  3, 3},
  [BRIDGE_STYLE_SUSPENSION_STEEL]    = {65535, 3, CITY_SPAN_SUSPENSION_STEEL,     3, 4},
  [BRIDGE_STYLE_SUSPENSION_YELLOW]   = {65535, 3, CITY_SPAN_SUSPENSION_YELLOW,    3, 5},
  [BRIDGE_STYLE_CANTILEVER_STEEL]    = {    7, 3, CITY_SPAN_CANTILEVER_STEEL,     4, 6},
  [BRIDGE_STYLE_CANTILEVER_BROWN]    = {    8, 3, CITY_SPAN_CANTILEVER_BROWN,     4, 7},
  [BRIDGE_STYLE_CANTILEVER_RED]      = {    9, 3, CITY_SPAN_CANTILEVER_RED,       4, 8},
  [BRIDGE_STYLE_GIRDER_STEEL]        = {    2, 0, CITY_SPAN_GIRDER_STEEL,         5, 9},
  [BRIDGE_STYLE_TUBULAR_STEEL]       = {65535, 2, CITY_SPAN_TUBULAR_STEEL,       6, 10},
  [BRIDGE_STYLE_TUBULAR_YELLOW]      = {65535, 2, CITY_SPAN_TUBULAR_YELLOW,      6, 11},
  [BRIDGE_STYLE_TUBULAR_SILICON]     = {65535, 2, CITY_SPAN_TUBULAR_SILICON,     6, 12},
  [BRIDGE_STYLE_DRAWBRIDGE]          = {    9, 3, CITY_SPAN_RAISING_BRIDGE,       1, 1},
};

BridgeStyle CityBridgeStyleFromChoice(int choice) {
  // This conversion is used only after placement admission, not while drawing or traversing the map.
  assert(choice > CITY_SPAN_POWER && choice < CITY_SPAN_STYLE_COUNT);
  static const u8 kStyleByChoice[CITY_SPAN_STYLE_COUNT] = {
    [CITY_SPAN_RAIL] = BRIDGE_STYLE_CANTILEVER_STEEL,
    [CITY_SPAN_CAUSEWAY] = BRIDGE_STYLE_CONCRETE,
    [CITY_SPAN_RAISING_BRIDGE] = BRIDGE_STYLE_DRAWBRIDGE,
    [CITY_SPAN_SUSPENSION] = BRIDGE_STYLE_SUSPENSION_STEEL,
    [CITY_SPAN_HIGHWAY] = BRIDGE_STYLE_CONCRETE,
    [CITY_SPAN_REINFORCED_HIGHWAY] = BRIDGE_STYLE_CONCRETE,
    [CITY_SPAN_WOOD] = BRIDGE_STYLE_WOOD,
    [CITY_SPAN_CONCRETE] = BRIDGE_STYLE_CONCRETE,
    [CITY_SPAN_ARCH_STEEL] = BRIDGE_STYLE_ARCH_STEEL,
    [CITY_SPAN_SUSPENSION_CONCRETE] = BRIDGE_STYLE_SUSPENSION_CONCRETE,
    [CITY_SPAN_SUSPENSION_STEEL] = BRIDGE_STYLE_SUSPENSION_STEEL,
    [CITY_SPAN_SUSPENSION_YELLOW] = BRIDGE_STYLE_SUSPENSION_YELLOW,
    [CITY_SPAN_CANTILEVER_STEEL] = BRIDGE_STYLE_CANTILEVER_STEEL,
    [CITY_SPAN_CANTILEVER_BROWN] = BRIDGE_STYLE_CANTILEVER_BROWN,
    [CITY_SPAN_CANTILEVER_RED] = BRIDGE_STYLE_CANTILEVER_RED,
    [CITY_SPAN_GIRDER_STEEL] = BRIDGE_STYLE_GIRDER_STEEL,
    [CITY_SPAN_TUBULAR_STEEL] = BRIDGE_STYLE_TUBULAR_STEEL,
    [CITY_SPAN_TUBULAR_YELLOW] = BRIDGE_STYLE_TUBULAR_YELLOW,
    [CITY_SPAN_TUBULAR_SILICON] = BRIDGE_STYLE_TUBULAR_SILICON,
  };
  return kStyleByChoice[choice];
}

// Decode the independent surface line beneath a native bridge-middle record. Requires a readable tile; other kinds
// return CITY_BRIDGE_UNDERPASS_NONE.
SC2K_PRESERVE_MOST int ReadCityBridgeUnderpass(const TileInfo *tp) {
  if (IsTileKindBridgeMiddle(tp->kind) && !HasTileWater(tp)) return tp->kind & 3;
  return CITY_BRIDGE_UNDERPASS_NONE;
}

// Convert a bridge's underpass code to its native transport kind. Requires a readable tile; absent underpasses map to
// TK_CLEAR and no map state is modified.
int ReadCityBridgeUnderpassKind(const TileInfo *tp) {
  int under = ReadCityBridgeUnderpass(tp);
  if (under == CITY_BRIDGE_UNDERPASS_POWER) return TK_POWER_LINE;
  return !under ? TK_CLEAR : (under == CITY_BRIDGE_UNDERPASS_RAIL ? TK_RAIL :
      TK_ROAD);
}

// OpenTTD's limits count the water/deck tiles, excluding the two bridge heads. Retain SimCity economics; artwork
// choices are available independently of the calendar. Length counts deck/water cells as required by the style limits;
// returns a mask without affordability or calendar gating.
int CityNativeRoadBridgeStyles(int length) {
  int styles = 0, deck = length - 2;
  for (int style = 0; style < BRIDGE_STYLE_COUNT; ++style) {
    const BridgeDefinition *definition = &kBridgeDefinitions[style];
    if (deck >= definition->minimum_deck && deck <= definition->maximum_deck) styles |= 1 << definition->choice;
  }
  return styles;
}

// OpenTTD CalcBridgePiece: north, south, inner north/south, middle odd/even. North/south distances must describe an
// admitted span position; returns its head or alternating inner artwork selector.
int CityRoadBridgePiece(int north, int south) {
  if (north == 1) return 0;
  if (south == 1) return 1;
  if (north < south) return north & 1 ? 3 : 2;
  if (north > south) return south & 1 ? 2 : 3;
  return north & 1 ? 5 : 4;
}

// Decode the command's road-style field, including explicit native bridge styles and cancellation. Returns -2 for
// unsupported encodings; this does not select a UI answer or mutate construction state.
int DecodeCityRoadSpanChoice(int value) {
  if (value & CITY_ROAD_BRIDGE_COMMAND_FLAG) {
    int choice = value & ~CITY_ROAD_BRIDGE_COMMAND_FLAG;
    return choice >= CITY_ROAD_BRIDGE_STYLE_FIRST && choice < CITY_SPAN_STYLE_COUNT ? choice : -2;
  }
  return value == CITY_ROAD_SPAN_CANCEL ? CITY_SPAN_CANCEL :
      value <= CITY_ROAD_SPAN_DECORATED ? CITY_SPAN_CAUSEWAY + value : -2;
}

// Encode a supported resolved road span choice for a command payload. Choice must be a valid legacy/native style or
// cancellation; this is the inverse format projection and performs no admission.
int EncodeCityRoadSpanChoice(int choice) {
  return choice >= CITY_ROAD_BRIDGE_STYLE_FIRST ? CITY_ROAD_BRIDGE_COMMAND_FLAG | choice :
      choice == CITY_SPAN_CANCEL ? CITY_ROAD_SPAN_CANCEL : choice - CITY_SPAN_CAUSEWAY;
}

// Explicit endpoints permit native bridges above dry terrain and perpendicular roads/tracks. The packed water-height
// field is only borrowed on dry tiles. Invalid endpoints or unsupported geometry return an unavailable span with
// blocker data; City and RNG remain unchanged.
CityInfrastructureSpan FindCityBridgeSpan(const City *city, TileIndex start, TileIndex end, bool rail) {
  CityInfrastructureSpan span = {.start = start, .end = INVALID_TILE, .error_tile = INVALID_TILE,
      .error = CITY_NOTICE_CROSSING_BLOCKED};
  if (!(city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE)) ||
      !IsMapTileValid(city, start) || !IsMapTileValid(city, end)) return span;
  CityPoint start_point = MapTilePt(city, start);
  CityPoint end_point = MapTilePt(city, end);
  if (start == end) return span;
  if (start_point.x != end_point.x && start_point.y != end_point.y) {
    if (abs(start_point.x - end_point.x) >= abs(start_point.y - end_point.y)) end_point.y = start_point.y;
    else end_point.x = start_point.x;
    end = MapTileXY(city, end_point.x, end_point.y);
  }
  int axis = start_point.x == end_point.x;
  if (axis ? start_point.y > end_point.y : start_point.x > end_point.x) {
    TileIndex swap = start; start = end; end = swap;
  }
  span.start = start;
  span.direction = axis ? CITY_LINE_SOUTH : CITY_LINE_EAST;
  span.length = axis ? IntMax(start_point.y, end_point.y) - IntMin(start_point.y, end_point.y) + 1 :
      IntMax(start_point.x, end_point.x) - IntMin(start_point.x, end_point.x) + 1;
  if (span.length < 2) return span;
  int base = ReadCityLandHeight(&city->tile[start]);
  if (base >= 31) { span.error_tile = start; return span; }
  int family = rail ? TK_RAIL : TK_ROAD;
  for (int step = 0; step < span.length; ++step) {
    TileIndex tile = start + step * (axis ? city->map_width : 1);
    span.error_tile = tile;
    const TileInfo *tp = &city->tile[tile];
    int height = ReadCityLandHeight(tp), shape = ReadTerrainCode(tp);
    bool water = HasTileWater(tp);
    bool head = step == 0 || step == span.length - 1;
    int surface = tp->kind;
    int line_axis = ReadCityStraightLineAxis(city, tile, 0);
    bool straight = surface == family && line_axis == axis;
    bool perpendicular = IsTileKindPlainTransport(surface) && line_axis == !axis;
    int power_axis = axis ? CONN_EW : CONN_NS;
    bool power = IsTileKindPlainPowerLine(tp->kind) && CityPowerConnectionMask(tp) == power_axis;
    if (ReadCityZoneKind(tp) == ZONE_MILITARY || ReadOverlay(city, tile) ||
        tp->kind == TK_RADIOACTIVE_WASTE ||
        (!IsTileKindUnbuilt(tp->kind) && !straight && !perpendicular && !power)) return span;
    if (head) {
      if (water || shape || height != base ||
          (!IsTileKindUnbuilt(tp->kind) && !straight)) return span;
    } else {
      int maximum = height + (shape >= 16 ? 2 : shape != 0);
      if (maximum > base || (water && ReadCityWaterHeight(tp) > base)) return span;
      if (!IsTileKindUnbuilt(tp->kind) && (water || shape || straight)) return span;
    }
  }
  span.styles = CityNativeRoadBridgeStyles(span.length);
  TileIndex center = span.start + (span.length - 1) / 2 * (axis ? city->map_width : 1);
  if (rail || !HasTileWater(&city->tile[center])) span.styles &= ~CITY_SPAN_STYLE_RAISING_BRIDGE_MASK;
  span.end = end;
  span.error = 0;
  span.error_tile = INVALID_TILE;
  return span;
}

// Axis, transport type and head facing are local metadata. Style lookup is reserved for drawing and save projection;
// routing reads the kind directly. Tile must be a valid stored bridge and side -1 or 1; INVALID_TILE means no
// matching adjacent bridge piece exists.
static TileIndex CityBridgeNeighbor(const City *city, TileIndex tile, int side) {
  const TileInfo *tp = &city->tile[tile];
  int axis = ReadCityBridgeAxis(tp);
  TileIndex neighbor = GetRelativeTileOrInvalid(city, tile, side * !axis, side * axis);
  if (neighbor == INVALID_TILE) return INVALID_TILE;
  const TileInfo *other = &city->tile[neighbor];
  if (!IsTileKindBridge(other->kind) ||
      CityBridgeHeadKind(other->kind) != CityBridgeHeadKind(tp->kind) ||
      ReadCityBridgeAxis(other) != axis ||
      (IsTileKindHighwayBridge(tp->kind) &&
          ((ReadTileZoneByte(other) ^ ReadTileZoneByte(tp)) & CITY_BRIDGE_HIGHWAY_RIGHT)))
    return INVALID_TILE;
  if (IsCityBridgeHead(tp) &&
      ((ReadTileZoneByte(tp) & CITY_BRIDGE_HEAD_FACING_MASK) ? side > 0 : side < 0)) return INVALID_TILE;
  if (IsCityBridgeHead(other) &&
      ((ReadTileZoneByte(other) & CITY_BRIDGE_HEAD_FACING_MASK) ? side < 0 : side > 0)) return INVALID_TILE;
  return neighbor;
}

// Decode the native bridge-head facing from a readable tile record. Non-head pieces use the routine's no-head value;
// no whole-span search or mutation occurs.
SC2K_PRESERVE_MOST int ReadCityBridgeHead(const TileInfo *tp) {
  if (!IsCityBridgeHead(tp)) return CITY_BRIDGE_HEAD_NONE;
  return ReadTileZoneByte(tp) & CITY_BRIDGE_HEAD_FACING_MASK ?
      CITY_BRIDGE_HEAD_SOUTH : CITY_BRIDGE_HEAD_NORTH;
}

// Walk matching bridge metadata toward one end of a valid bridge tile. Side must be -1 or 1; returns the terminal
// matching cell even when malformed metadata stops before a proper head.
TileIndex FindCityBridgeHead(const City *city, TileIndex origin, int side) {
  assert(IsMapTileValid(city, origin));
  assert(side == -1 || side == 1);
  TileIndex tile = origin;
  int head = side < 0 ? CITY_BRIDGE_HEAD_NORTH : CITY_BRIDGE_HEAD_SOUTH;
  while (ReadCityBridgeHead(&city->tile[tile]) != head) {
    TileIndex next = CityBridgeNeighbor(city, tile, side);
    if (next == INVALID_TILE) break;
    tile = next;
  }
  return tile;
}

// Validate both heads, style agreement and paired highway-lane geometry for a valid stored bridge cell. Returns a
// read-only predicate; malformed or incomplete spans are rejected without repairing map data.
bool ValidateCityBridgeSpan(const City *city, TileIndex tile) {
  TileIndex first = FindCityBridgeHead(city, tile, -1), last = FindCityBridgeHead(city, tile, 1);
  int axis = ReadCityBridgeAxis(&city->tile[tile]), stride = axis ? city->map_width : 1;
  bool highway = IsTileKindHighwayBridge(ReadTile(city, tile));
  int length = (last - first) / stride + 1;
  if (length < 2 || ReadCityBridgeHead(&city->tile[first]) != CITY_BRIDGE_HEAD_NORTH ||
      ReadCityBridgeHead(&city->tile[last]) != CITY_BRIDGE_HEAD_SOUTH ||
      ReadTileBridgeStyle(&city->tile[first]) != ReadTileBridgeStyle(&city->tile[last])) return false;
  if (highway) {
    int lane = (ReadTileZoneByte(&city->tile[tile]) & CITY_BRIDGE_HIGHWAY_RIGHT) != 0;
    if (length & 1) return false;
    TileIndex other = GetRelativeTileOrInvalid(city, tile,
        axis ? lane ? -1 : 1 : 0, axis ? 0 : lane ? -1 : 1);
    if (other == INVALID_TILE) return false;
    const TileInfo *tp = &city->tile[other];
    if (CityBridgeHeadKind(tp->kind) != TK_HIGHWAY_BRIDGE_HEAD ||
        ReadCityBridgeAxis(tp) != axis ||
        !((ReadTileZoneByte(tp) ^ ReadTileZoneByte(&city->tile[tile])) & CITY_BRIDGE_HIGHWAY_RIGHT) ||
        ReadCityBridgeHead(tp) != ReadCityBridgeHead(&city->tile[tile]) ||
        (IsTileKindBridgeHead(tp->kind) && ReadTileBridgeStyle(tp) != ReadTileBridgeStyle(&city->tile[tile])))
      return false;
    int across = axis ? 1 : city->map_width;
    if (FindCityBridgeHead(city, other, -1) != (lane ? first - across : first + across) ||
        FindCityBridgeHead(city, other, 1) != (lane ? last - across : last + across)) return false;
  }
  return true;
}

// Read a bridge's stored style from its head, locating that head when needed. Requires a valid bridge cell with a
// consistent span; the returned style is metadata, not a selected UI option.
int ReadCityBridgeStyle(const City *city, TileIndex tile) {
  TileIndex head = IsCityBridgeHead(&city->tile[tile]) ? tile : FindCityBridgeHead(city, tile, -1);
  return ReadTileBridgeStyle(&city->tile[head]);
}

// The inclusive endpoints are in coordinate order. Highway callers convert each lane separately, preserving its
// position across the bridge width. Endpoints must describe the admitted straight span and style; installs complete
// native metadata and invalidates old/new cell extents.
void StoreCityBridgeSpan(City *city, TileIndex first, TileIndex last, int head_kind, int style) {
  int axis = MapTileX(city, first) == MapTileX(city, last);
  int stride = axis ? city->map_width : 1;
  if (first > last) { TileIndex swap = first; first = last; last = swap; }
  int middle_kind = TK_ROAD_BRIDGE_SPAN + ((head_kind - TK_ROAD_BRIDGE_HEAD) << 2);
  for (TileIndex tile = first;; tile += stride) {
    TileInfo *tp = &city->tile[tile];
    int lane = head_kind == TK_HIGHWAY_BRIDGE_HEAD ?
        ((ReadTileZoneByte(tp) & (axis ? CITY_BUILDING_CONTINUE_X : CITY_BUILDING_CONTINUE_Y)) != 0) : 0;
    bool head = tile == first || tile == last;
    int under = ReadCityBridgeUnderpass(tp);
    ReplaceCityTileAndStatistics(city, tile, head ? head_kind :
        CityTileKindForWater(middle_kind + under, HasTileWater(tp)));
    WriteTileZoneByte(tp, (axis ? CITY_BRIDGE_AXIS_MASK : 0) |
        (head && tile == last ? CITY_BRIDGE_HEAD_FACING_MASK : 0) | (lane ? CITY_BRIDGE_HIGHWAY_RIGHT : 0));
    if (head) {
      tp->altitude &= CITY_ALTITUDE_CLEAR_WATER;
      WriteTileBridgeStyle(tp, style);
    }
    InvalidateTile(city, tile);
    if (tile == last) break;
  }
}

// Legacy 2x2 artwork groups are derived from distance to a head and lane. The bridge itself stores only one width bit,
// including on odd-grid spans.
int CityBridgeHighwayZone(const City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile));
  int axis = ReadCityBridgeAxis(&city->tile[tile]);
  TileIndex first = FindCityBridgeHead(city, tile, -1);
  int stride = axis ? city->map_width : 1;
  int distance = (tile - first) / stride;
  int lane = (ReadTileZoneByte(&city->tile[tile]) & CITY_BRIDGE_HIGHWAY_RIGHT) != 0;
  int along = distance & 1;
  return (axis << CITY_HIGHWAY_ORIENTATION_SHIFT) |
      ((axis ? lane : along) ? CITY_BUILDING_CONTINUE_X : 0) |
      ((axis ? along : lane) ? CITY_BUILDING_CONTINUE_Y : 0);
}

// Derive a valid highway bridge cell's alternating deck piece from head distance and stored phase. Requires consistent
// bridge metadata; returns an artwork selector without modifying the map.
int ReadCityHighwayBridgePiece(const City *city, TileIndex tile) {
  TileIndex first = FindCityBridgeHead(city, tile, -1);
  int distance = (tile - first) / (ReadCityBridgeAxis(&city->tile[tile]) ? city->map_width : 1);
  int phase = (city->tile[first].altitude & CITY_BRIDGE_HIGHWAY_PHASE_MASK) != 0;
  return ((distance / 2) & 1) ^ phase;
}

// Derive the bridge artwork piece from distances to its two stored ends. Requires a valid consistent span; no topology
// or style is inferred from pixels.
int ReadCityBridgePiece(const City *city, TileIndex tile) {
  TileIndex first = FindCityBridgeHead(city, tile, -1), last = FindCityBridgeHead(city, tile, 1);
  int stride = ReadCityBridgeAxis(&city->tile[tile]) ? city->map_width : 1;
  int north = (tile - first) / stride + !IsCityBridgeHead(&city->tile[first]);
  int south = (last - tile) / stride + !IsCityBridgeHead(&city->tile[last]);
  return CityRoadBridgePiece(north, south);
}

// Read a valid bridge's deck level, using its bank head when the dry-head payload is not stored. Requires consistent
// span metadata; returns geometry without editing borrowed altitude fields.
int ReadCityBridgeDeckHeight(const City *city, TileIndex tile) {
  // Bridge heads sit on their bank. Water-built bridges need not initialize
  // a borrowed water-height payload on dry heads, including saved SC2 bridges.
  if (IsCityBridgeHead(&city->tile[tile])) return ReadCityLandHeight(&city->tile[tile]) + 1;
  if (!HasTileWater(&city->tile[tile])) return ReadCityWaterHeight(&city->tile[tile]) + 1;
  TileIndex head = FindCityBridgeHead(city, tile, -1);
  if (IsCityBridgeHead(&city->tile[head])) return ReadCityLandHeight(&city->tile[head]) + 1;
  return ReadCityWaterHeight(&city->tile[tile]) + 1;
}

// Resolve the transport family available through a valid bridge tile in the supplied cardinal direction. Crossing the
// bridge axis reads its stored underpass; absent underpasses return TK_CLEAR and ordinary tiles retain their kind.
int CityBridgeTransportKind(const TileInfo *tp, int direction) {
  if (!IsTileKindBridge(tp->kind)) return tp->kind;
  int axis = ReadCityBridgeAxis(tp);
  if ((direction & 1) != axis) {
    int under = ReadCityBridgeUnderpass(tp);
    return under == CITY_BRIDGE_UNDERPASS_ROAD ? TK_ROAD :
        under == CITY_BRIDGE_UNDERPASS_RAIL ? TK_RAIL :
        under == CITY_BRIDGE_UNDERPASS_POWER ? TK_POWER_LINE : TK_CLEAR;
  }
  return IsTileKindHighwayBridge(tp->kind) ? TK_HIGHWAY :
      IsTileKindRailBridge(tp->kind) ? TK_RAIL : TK_ROAD;
}

// Compute the traversable edge height for a valid tile and cardinal direction. Bridge heads expose bank height outward
// and deck height inward; perpendicular travel uses the underlying surface geometry.
int CityBridgeTransportEdgeHeight(const City *city, TileIndex tile, int direction) {
  const TileInfo *tp = &city->tile[tile];
  if (IsTileKindHighwayBridge(tp->kind)) return ReadCitySurfaceHeight(tp);
  if (!IsTileKindBridge(tp->kind) ||
      (direction & 1) != ReadCityBridgeAxis(tp)) return ReadCitySurfaceHeight(tp);
  int head = ReadCityBridgeHead(tp);
  bool negative = direction == CITY_LINE_WEST || direction == CITY_LINE_NORTH;
  if ((head == CITY_BRIDGE_HEAD_NORTH && negative) || (head == CITY_BRIDGE_HEAD_SOUTH && !negative))
    return ReadCityLandHeight(tp);
  return ReadCityBridgeDeckHeight(city, tile);
}

// Quote or construct a bridge between resolved endpoints with explicit family/style bits. Validates flags and
// geometry/style admission. TEST is read-only; execution updates terrain, metadata, statistics and targeted damage.
// Both return the cost; the shared command layer owns charging.
i64 CmdBuildCityBridge(City *city, u32 flags, TileIndex start, TileIndex end, int options) {
  if ((flags & ~CITY_COMMAND_TEST) || (options & ~(CITY_BRIDGE_COMMAND_RAIL | CITY_BRIDGE_COMMAND_STYLE_MASK)))
    return CITY_COMMAND_COST_INVALID;
  bool rail = (options & CITY_BRIDGE_COMMAND_RAIL) != 0;
  int choice = options & CITY_BRIDGE_COMMAND_STYLE_MASK;
  if ((choice < CITY_ROAD_BRIDGE_STYLE_FIRST && choice != CITY_SPAN_RAISING_BRIDGE) ||
      choice >= CITY_SPAN_STYLE_COUNT || (rail && choice == CITY_SPAN_RAISING_BRIDGE)) return CITY_COMMAND_COST_INVALID;
  if (IsMapTileValid(city, start) && start == end) {
    return CITY_COMMAND_COST_BRIDGE_SAME_SPOT;
  }
  CityInfrastructureSpan span = FindCityBridgeSpan(city, start, end, rail);
  if (span.end == INVALID_TILE || !(span.styles & (1 << choice))) {
    if (!(flags & CITY_COMMAND_TEST)) ReportCityCommandBlocker(city, span.error_tile);
    return CITY_COMMAND_COST_BLOCKED;
  }
  i64 cost = (i64)span.length * kSpanCostPerCell[choice];
  if (flags & CITY_COMMAND_TEST) return cost;
  int stride = span.direction == CITY_LINE_SOUTH ? city->map_width : 1;
  int axis = span.direction == CITY_LINE_SOUTH;
  int base = ReadCityLandHeight(&city->tile[span.start]);
  BridgeStyle style = CityBridgeStyleFromChoice(choice);
  for (int step = 0; step < span.length; ++step) {
    TileIndex tile = span.start + step * stride;
    TileInfo *tp = &city->tile[tile];
    int under = step && step < span.length - 1 && !IsTileKindUnbuilt(tp->kind) ?
        IsTileKindPlainPowerLine(tp->kind) ? CITY_BRIDGE_UNDERPASS_POWER :
            tp->kind == TK_ROAD ? CITY_BRIDGE_UNDERPASS_ROAD : CITY_BRIDGE_UNDERPASS_RAIL : 0;
    int head = step == 0 ? CITY_BRIDGE_HEAD_NORTH : step == span.length - 1 ? CITY_BRIDGE_HEAD_SOUTH : 0;
    int kind = rail ? (head ? TK_RAIL_BRIDGE_HEAD : TK_RAIL_BRIDGE_SPAN + under) :
        head ? TK_ROAD_BRIDGE_HEAD : TK_ROAD_BRIDGE_SPAN + under;
    if (!head) kind = CityTileKindForWater(kind, HasTileWater(tp));
    ReplaceCityTileAndStatistics(city, tile, kind);
    WriteTileZoneByte(tp, (axis ? CITY_BRIDGE_AXIS_MASK : 0) |
        (head == CITY_BRIDGE_HEAD_SOUTH ? CITY_BRIDGE_HEAD_FACING_MASK : 0));
    if (head) {
      tp->altitude &= CITY_ALTITUDE_CLEAR_WATER;
      WriteTileBridgeStyle(tp, style);
    }
    if (!head && !HasTileWater(tp))
      tp->altitude = (tp->altitude & CITY_ALTITUDE_CLEAR_WATER) | base << CITY_ALTITUDE_WATER_SHIFT;
    int under_kind = ReadCityBridgeUnderpassKind(tp);
    if (under_kind) ++city->statistics.count[CityTileStatisticsKind(city, under_kind)];
    InvalidateTile(city, tile);
  }
  for (int step = 0; step < span.length; ++step) {
    TileIndex tile = span.start + step * stride;
    RefreshAdjacentSurfaceConnections(city, tile);
  }
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  return cost;
}

// Clear an admitted bridge middle's underlying transport or ground cover. Preserve the bridge's family, lane, axis,
// deck height and water; the caller owns admission/payment and reconnects adjacent networks after this edit.
void ClearCityBridgeUnderpass(City *city, TileIndex tile_index) {
  TileInfo *tp = &city->tile[tile_index];
  int under = ReadCityBridgeUnderpass(tp);
  if (under) {
    --city->statistics.count[CityTileStatisticsKind(city, ReadCityBridgeUnderpassKind(tp))];
    // Keep the bridge family, axis, lane, deck height and ground intact.
    tp->kind -= under;
    if (under == CITY_BRIDGE_UNDERPASS_POWER) tp->flags &= ~(TF_POWERED | TF_POWERABLE);
    RefreshCityPowerConnections(city, tile_index);
  } else {
    // Bridge regrowth uses the phased landscape cycle; retain the zone metadata.
    WriteGroundKind(tp,
        CityClimateHasGroundCover(ReadTileClimate(city, tp)) ?
        CITY_GROUND_RECOVERY_FIRST : CITY_GROUND_RECOVERY_GRASS_FIRST);
  }
  InvalidateTile(city, tile_index);
  ShowCityDemolitionEffect(city, tile_index, false);
}

// Rebuild the bank terrain around a newly edited highway bridge shore. Requires a valid admitted shoreline location;
// this is a post-placement geometry effect and not an admission query.
static void RecalculateHighwayBridgeShore(City *city, TileIndex shore) {
  static const i8 kCornerX[4] = {0, 0, 1, 1};
  static const i8 kCornerY[4] = {0, 1, 1, 0};
  InvalidateTileRange(city, shore, 2, 2);
  for (int corner = 0; corner < 4; ++corner)
    RecalculateTerrainCell(city, GetRelativeTileOrInvalid(city, shore, kCornerX[corner], kCornerY[corner]));
  InvalidateTileRange(city, shore, 2, 2);
}

// Remove the native bridge selected by a valid tile. Returns whether a supported bridge was removed;
// preserves its underlying water/ground and explicitly reconnects and invalidates affected cells.
bool RemoveCityNativeBridge(City *city, TileIndex origin, bool animate) {
  if (!IsTileKindBridge(city->tile[origin].kind)) return false;
  bool highway = IsTileKindHighwayBridge(ReadTile(city, origin));
  int axis = ReadCityBridgeAxis(&city->tile[origin]);
  int stride = axis ? city->map_width : 1;
  int across = axis ? 1 : city->map_width;
  if (highway && (ReadTileZoneByte(&city->tile[origin]) & CITY_BRIDGE_HIGHWAY_RIGHT)) origin -= across;
  TileIndex first[2], last[2];
  for (int lane = 0; lane < (highway ? 2 : 1); ++lane) {
    first[lane] = FindCityBridgeHead(city, origin + lane * across, -1);
    last[lane] = FindCityBridgeHead(city, origin + lane * across, 1);
  }
  // Stock graded the banks during construction and repairs them during demolition. Terrain-preserving bridges
  // leave the original slopes in place, so their removal must not reshape the banks or demolish nearby occupants.
  bool repair_banks = highway && !(city->fixes & (1 << CITY_FIX_HIGHWAY_TERRAIN));
  TileIndex far_shore = INVALID_TILE;
  if (repair_banks) {
    TileIndex near_shore = GetRelativeTileOrInvalid(city, first[0], -2 * !axis, -2 * axis);
    far_shore = GetRelativeTileOrInvalid(city, last[0], !axis, axis);
    RecalculateHighwayBridgeShore(city, near_shore);
  }
  if (animate) PlayCitySound(city, origin, CITY_SOUND_EXPLOSION);
  for (int lane = 0; lane < (highway ? 2 : 1); ++lane) for (TileIndex tile = first[lane];; tile += stride) {
    TileInfo *tp = &city->tile[tile];
    int under = HasTileWater(tp) ? ReadCityWaterGroundKind(tp) : ReadCityBridgeUnderpassKind(tp);
    if (animate) ShowCityDemolitionEffect(city, tile, false);
    ReplaceCityTileAndStatistics(city, tile, under);
    WriteTileZoneByte(tp, ZONE_NONE);
    if (!highway && !HasTileWater(tp)) tp->altitude = (tp->altitude & CITY_ALTITUDE_CLEAR_WATER) |
        ReadCityLandHeight(tp) << CITY_ALTITUDE_WATER_SHIFT;
    InvalidateTile(city, tile);
    if (tile == last[lane]) break;
  }
  if (repair_banks) RecalculateHighwayBridgeShore(city, far_shore);
  for (int lane = 0; lane < (highway ? 2 : 1); ++lane) for (TileIndex tile = first[lane];; tile += stride) {
    RefreshAdjacentSurfaceConnections(city, tile);
    if (tile == last[lane]) break;
  }
  return true;
}

// Check a valid native bridge tile for a compatible road or rail underpass. Reads bridge axis, ground and
// existing underpass state only; success does not reserve or write the underpass.
bool CanPlaceCityBridgeUnderpass(const City *city, TileIndex tile, int kind, int axis) {
  if (!IsMapTileValid(city, tile)) return false;
  const TileInfo *tp = &city->tile[tile];
  if (!IsTileKindBridgeMiddle(tp->kind) || (kind != TK_ROAD && kind != TK_RAIL)) return false;
  int under = ReadCityBridgeUnderpass(tp);
  int desired = kind == TK_ROAD ? CITY_BRIDGE_UNDERPASS_ROAD : CITY_BRIDGE_UNDERPASS_RAIL;
  if ((under && under != desired) || (axis >= 0 && axis == ReadCityBridgeAxis(tp)) ||
      ReadCityZoneKind(tp) == ZONE_MILITARY) return false;
  return !HasTileWater(tp) && !ReadTerrainCode(tp) &&
      ReadCityBridgeDeckHeight(city, tile) > ReadCityLandHeight(tp);
}

// Install a previously admitted surface line underneath a native bridge and update its statistics/topology. Tile must
// be valid and admission must already have checked the family and perpendicular axis; invalidation surrounds the
// category change.
void PlaceCityBridgeUnderpass(City *city, TileIndex tile, int kind) {
  if (!CanPlaceCityBridgeUnderpass(city, tile, kind, -1)) return;
  TileInfo *tp = &city->tile[tile];
  int under = ReadCityBridgeUnderpass(tp);
  if (under) return;
  InvalidateTile(city, tile);
  // The bridge family and all head/axis/width metadata stay intact. Only its
  // underlying category changes; the bridge's statistics kind is unchanged.
  tp->kind += kind == TK_ROAD ? CITY_BRIDGE_UNDERPASS_ROAD : CITY_BRIDGE_UNDERPASS_RAIL;
  if (ReadGroundKind(tp) >= CITY_GROUND_ROUGH) WriteGroundKind(tp,
      CITY_GROUND_GRASS);
  ++city->statistics.count[CityTileStatisticsKind(city, ReadCityBridgeUnderpassKind(tp))];
  RefreshAdjacentSurfaceConnections(city, tile);
  InvalidateTile(city, tile);
}

SC2K_PRESERVE_MOST int CityBridgeViewZone(const City *city, TileIndex tile) {
  const TileInfo *tp = &city->tile[tile];
  int zone = ReadTileZoneByte(tp), kind = tp->kind;
  int rotation = CityMapCameraRotation(city->game->drawing), axis = (zone & CITY_BRIDGE_AXIS_MASK) != 0;
  int toggles = (rotation & 1) * CITY_BRIDGE_AXIS_MASK;
  // Count turns beginning on each axis; only their parity affects these bits.
  if (IsTileKindBridgeHead(kind) && ((rotation + axis) & 2)) toggles |= CITY_BRIDGE_HEAD_FACING_MASK;
  if (IsTileKindHighwayBridge(kind) && ((rotation + !axis) & 2)) toggles |= CITY_BRIDGE_HIGHWAY_RIGHT;
  return zone ^ toggles;
}

// Return a native deck cache slot after matching the piece to the camera direction.
static int BridgeDeckSprite(const City *city, TileIndex tile, int zoom, BridgeStyle style) {
  int piece = ReadCityBridgePiece(city, tile);
  int world_axis = ReadCityBridgeAxis(&city->tile[tile]);
  CityPoint along = CityMapCameraVector(CityMapCameraRotation(city->game->drawing), !world_axis, world_axis);
  static const u8 kReversePiece[] = {1, 0, 3, 2, 4, 5};
  if (along.x + along.y < 0 && piece < 6) piece = kReversePiece[piece];
  if (piece >= CITY_LANDSCAPE_BRIDGE_PIECES) piece = 4;
  int axis = (CityBridgeViewZone(city, tile) & CITY_BRIDGE_AXIS_MASK) != 0;
  return MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM - CITY_LANDSCAPE_GROUND_FIRST +
      (IsTileKindRailBridge(ReadTile(city, tile)) ? CITY_LANDSCAPE_RAIL_BRIDGE_FIRST :
          CITY_LANDSCAPE_ROAD_BRIDGE_FIRST) +
      (kBridgeDefinitions[style].geometry * CITY_LANDSCAPE_BRIDGE_PIECES + piece) * 2 + axis;
}

TileIndex CityDrawbridgeCenter(const City *city, TileIndex tile) {
  if (!IsMapTileValid(city, tile) || !IsTileKindBridge(city->tile[tile].kind) ||
      IsTileKindRailBridge(city->tile[tile].kind) || IsTileKindHighwayBridge(city->tile[tile].kind)) return INVALID_TILE;
  TileIndex first = FindCityBridgeHead(city, tile, -1);
  if (!IsCityBridgeHead(&city->tile[first]) || ReadTileBridgeStyle(&city->tile[first]) != BRIDGE_STYLE_DRAWBRIDGE)
    return INVALID_TILE;
  TileIndex last = FindCityBridgeHead(city, tile, 1);
  int stride = ReadCityBridgeAxis(&city->tile[first]) ? city->map_width : 1;
  int length = (last - first) / stride;
  return length >= 2 && IsCityBridgeHead(&city->tile[last]) ? first + length / 2 * stride : INVALID_TILE;
}

// Ship steering probes up to four tiles ahead, including diagonal approaches. Open before that probe reaches the leaf.
enum { kDrawbridgeShipRange = 4 };

static bool ShipNearDrawbridge(const City *city, TileIndex center, TileIndex ship) {
  if (ship == INVALID_TILE) return false;
  CityPoint a = MapTilePt(city, center), b = MapTilePt(city, ship);
  int dx = abs(a.x - b.x), dy = abs(a.y - b.y);
  return dx <= kDrawbridgeShipRange && dy <= kDrawbridgeShipRange;
}

static bool DrawbridgeHasShip(const City *city, TileIndex center, int excluded_slot) {
  for (int y = -kDrawbridgeShipRange; y <= kDrawbridgeShipRange; ++y) {
    for (int x = -kDrawbridgeShipRange; x <= kDrawbridgeShipRange; ++x) {
      TileIndex tile = GetRelativeTileOrInvalid(city, center, x, y);
      if (tile == INVALID_TILE) continue;
      for (int slot = FirstCityMapEventOnTile(city, tile); slot >= 0; slot = NextCityMapEventOnTile(city, slot))
        if (slot != excluded_slot && city->events.event[slot].type == CITY_MAP_EVENT_SHIP) return true;
    }
  }
  return false;
}

bool IsCityDrawbridgeOpen(const City *city, TileIndex tile) {
  return tile != INVALID_TILE && CityDrawbridgeCenter(city, tile) == tile && DrawbridgeHasShip(city, tile, -1);
}

bool CityBridgeAllowsBoat(const City *city, TileIndex tile) {
  const TileInfo *tp = &city->tile[tile];
  if (!IsTileKindBridgeMiddle(tp->kind)) return false;
  if (IsTileKindHighwayBridge(tp->kind)) return ReadCityHighwayBridgePiece(city, tile) != 0;
  return CityDrawbridgeCenter(city, tile) != tile || DrawbridgeHasShip(city, tile, -1);
}

void InvalidateCityDrawbridgesForShip(City *city, int slot, TileIndex previous, TileIndex current) {
  if (previous == current) return;
  TileIndex positions[2] = {previous, current};
  for (int pass = 0; pass < 2; ++pass) {
    if (positions[pass] == INVALID_TILE) continue;
    for (int dy = -kDrawbridgeShipRange; dy <= kDrawbridgeShipRange; ++dy) {
      for (int dx = -kDrawbridgeShipRange; dx <= kDrawbridgeShipRange; ++dx) {
        TileIndex tile = GetRelativeTileOrInvalid(city, positions[pass], dx, dy);
        if (tile == INVALID_TILE || !IsTileKindBridgeMiddle(city->tile[tile].kind)) continue;
        if (pass && previous != INVALID_TILE) {
          CityPoint a = MapTilePt(city, previous), b = MapTilePt(city, tile);
          if (abs(a.x - b.x) <= kDrawbridgeShipRange && abs(a.y - b.y) <= kDrawbridgeShipRange) continue;
        }
        if (CityDrawbridgeCenter(city, tile) != tile ||
            ShipNearDrawbridge(city, tile, previous) == ShipNearDrawbridge(city, tile, current) ||
            DrawbridgeHasShip(city, tile, slot)) continue;
        InvalidateTile(city, tile);
      }
    }
  }
}

void DrawCityBridgeDeckArtwork(City *city, TileIndex tile, int layout, int zoom, int screen_x, int baseline,
    BridgeStyle style) {
  Drawing *drawing = city->game->drawing;
  if (style == BRIDGE_STYLE_DRAWBRIDGE && CityDrawbridgeCenter(city, tile) == tile) {
    int leaf = DrawbridgeHasShip(city, tile, -1) ? CITY_MAP_TILE_SPRITE_RAISING_BRIDGE_OPEN :
        CITY_MAP_TILE_SPRITE_RAISING_BRIDGE_CLOSED;
    int sprite = MapSpriteZoomIndex(leaf, zoom);
    CityPoint extent = CityMapSpriteExtent(drawing, sprite);
    // The baked baseline is 32 source pixels above the model origin. Register that origin at the tile centre.
    DrawCityMapSpriteAtBaseline(drawing, sprite, screen_x + ((8 << zoom) - extent.x) / 2, baseline - (4 << zoom),
        (CityBridgeViewZone(city, tile) & CITY_BRIDGE_AXIS_MASK) != 0);
    return;
  }
  int sprite = layout + 500 * zoom;
  if (city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE)) {
    sprite = BridgeDeckSprite(city, tile, zoom, style);
    int palette = kBridgeDefinitions[style].palette;
    DrawCityMapPaletteSpriteAtBaseline(drawing, sprite, screen_x, baseline, SPRITE_OPAQUE_PALETTE,
        MAP_PALETTE_BRIDGE_FIRST + palette);
    int first = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM - CITY_LANDSCAPE_GROUND_FIRST;
    bool rail = IsTileKindRailBridge(city->tile[tile].kind);
    int piece = sprite - first - (rail ? CITY_LANDSCAPE_RAIL_BRIDGE_FIRST : CITY_LANDSCAPE_ROAD_BRIDGE_FIRST);
    int foreground = first + CITY_LANDSCAPE_BRIDGE_FRONT_FIRST + piece +
        (rail ? CITY_LANDSCAPE_BRIDGE_GEOMETRIES * CITY_LANDSCAPE_BRIDGE_PIECES * 2 : 0);
    DrawCityBridgeTrafficForeground(city, tile, foreground, screen_x, baseline, palette);
  } else {
    DrawCityMapSpriteAtBaseline(drawing, sprite, screen_x, baseline, CityMapViewTileFlip(city, tile));
  }
}

void DrawCityBridgeUnderpass(City *city, TileIndex tile, int kind, int zoom, int screen_x, int ground_y) {
  int axis = ReadCityBridgeAxis(&city->tile[tile]);
  int layout = kind == TK_POWER_LINE ? CityMapPowerLayout(axis ? CONN_EW : CONN_NS, CITY_TERRAIN_FLAT) :
      kCityMapKindSprites[kind] + !axis;
  Drawing *drawing = city->game->drawing;
  int sprite = CityMapCameraSprite(city->game->resources, layout, CityMapCameraRotation(drawing)) + 500 * zoom;
  DrawCityMapSpriteAtBaseline(drawing, sprite, screen_x, ground_y, 0);
}

bool DrawCityBridgeGround(City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry) {
  DrawCityMapTerrainTile(city, tile, zoom, -1, screen_x, ground_y, geometry);
  int under = ReadCityBridgeUnderpassKind(&city->tile[tile]);
  if (under) DrawCityBridgeUnderpass(city, tile, under, zoom, screen_x, ground_y);
  return true;
}

// Bridge ground and the elevated deck occupy separate planes. Prepared heads
// include the approach structure; terrain and perpendicular underpasses stay below.
static bool DrawBridgeTile(City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *tile_geometry) {
  Drawing *drawing = city->game->drawing;
  const TileInfo *tp = &city->tile[tile];
  if (!(city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE)) || !IsTileKindBridge(tp->kind) ||
      IsTileKindHighwayBridge(tp->kind)) return false;
  BridgeStyle style = ReadCityBridgeStyle(city, tile);
  CityTileGeometry geometry = *tile_geometry;
  int first = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM - CITY_LANDSCAPE_GROUND_FIRST;
  if (geometry.water == CITY_WATER_NONE && IsCityMapGroundPass(drawing)) {
    DrawCityMapTerrainSkirt(city, tile, zoom, screen_x, ground_y, geometry.land_height);
    if (geometry.land_shape > 0 && geometry.land_shape < CITY_TERRAIN_SHAPE_13)
      DrawCityMapTerrainFace(city, tile, zoom, first + CITY_LANDSCAPE_TERRAIN_FACE_FIRST + geometry.land_shape,
          screen_x, ground_y, CityMapSlopeFaceSides(city, tile, geometry.land_shape, geometry.land_height));
    DrawCityMapSpriteAtBaseline(drawing, CityGroundSpriteForShape(city, tile, zoom, geometry.land_shape),
        screen_x, ground_y, 0);
  } else {
    DrawCityMapTerrainTile(city, tile, zoom, -1, screen_x, ground_y, &geometry);
  }
  int zone = CityBridgeViewZone(city, tile);
  int axis = (zone & CITY_BRIDGE_AXIS_MASK) != 0, head = IsCityBridgeHead(tp) ?
      (zone & CITY_BRIDGE_HEAD_FACING_MASK ?
          CITY_BRIDGE_HEAD_SOUTH : CITY_BRIDGE_HEAD_NORTH) : CITY_BRIDGE_HEAD_NONE;
  if (head) {
    DrawCityMapPlacementTile(city, tile);
    int shape = axis ? (head == CITY_BRIDGE_HEAD_NORTH ? 3 : 1) :
        (head == CITY_BRIDGE_HEAD_NORTH ? 4 : 2);
    int form = (IsTileKindRailBridge(tp->kind) ? CITY_LANDSCAPE_BRIDGE_HEAD_FORMS : 0) + (style != BRIDGE_STYLE_WOOD);
    // A stored head connects bank height to a deck one level higher. Its underlying ground slope does not turn
    // that approach into a level platform; road traffic uses the same rising surface.
    int sprite = first + CITY_LANDSCAPE_BRIDGE_HEAD_FIRST + form * CITY_LANDSCAPE_BRIDGE_HEAD_VIEWS +
        shape - 1;
    int palette_style = kBridgeDefinitions[style].palette;
    DrawCityMapPaletteSpriteAtBaseline(drawing, sprite, screen_x, ground_y, SPRITE_OPAQUE_PALETTE,
        MAP_PALETTE_BRIDGE_FIRST + palette_style);
    DrawCityBridgeTrafficForeground(city, tile, sprite + CITY_LANDSCAPE_BRIDGE_HEAD_FRONT_FIRST -
        CITY_LANDSCAPE_BRIDGE_HEAD_FIRST, screen_x, ground_y, palette_style);
  } else {
    int under = ReadCityBridgeUnderpassKind(tp);
    bool power = under == TK_POWER_LINE;
    if (under && (power || IsCityMapGroundPass(drawing)))
      DrawCityBridgeUnderpass(city, tile, under, zoom, screen_x, ground_y);
    DrawCityMapPlacementTile(city, tile);
    int baseline = ground_y - (ReadCityBridgeDeckHeight(city, tile) - 1 - geometry.surface_height) *
        CityMapHeightStep(zoom);
    DrawCityBridgeDeckArtwork(city, tile, CityMapViewTileSpriteIndex(city, tile), zoom, screen_x, baseline, style);
  }
  return true;
}

void TickCityBridgeTile(City *city, TileIndex tile) {
  RefreshCityTileGroundCover(city, tile, false);
  if (HasTileWater(&city->tile[tile])) return;
  CityPoint point = MapTilePt(city, tile);
  u32 cycle = 11 * point.x + 9 * point.y + (city->landscape_tick >> 8);
  GrowCityGround(city, tile, cycle, false);
}

const TileModule kCityBridgeModule = {
  .tick = TickCityBridgeTile, .draw = DrawBridgeTile, .draw_ground = DrawCityBridgeGround,
};
