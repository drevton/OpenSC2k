// TileInfo storage for land highways and ramps (zero-based bits; bridges use bridges.c's contract):
// kind distinguishes straight highway, rail/road/power undercrossing, incline, bend, junction and entrance ramp.
// terrain[4..5] is orientation 0..3; bit 4 selects X/east-west (0) versus Y/north-south (1), while bit 5
// distinguishes opposite facings where the shape needs it. Straight-axis queries ignore bit 5.
// Highway footprint terrain[6] links toward its anchor in -X, terrain[7] in -Y; both are clear at the anchor.
// Entrance ramps are one tile: terrain[6] is mirror instead of a footprint link, terrain[7] is not used.
// terrain[0..3] retains ground/coverage, including paving for a road/rail beneath the highway. These land kinds
// are dry; water spans use distinct native bridge kinds. zone[4] selects slope versus zoning in zone[0..3];
// zone[5..7] has no highway payload. Land/tunnel heights, flags, overlays and underground follow tile_storage.h.

#include "city/infrastructure/highways.h"

#include "city/events/events.h"
#include "drawing/maps/map_sprites.h"
#include "city/maps/labels.h"
#include "city/infrastructure/lines.h"
#include "city/infrastructure/networks.h"
#include "city/infrastructure/spans.h"
#include "city/infrastructure/surface_lines.h"
#include "audio/types.h"
#include "city/infrastructure/bridges.h"
#include "city/infrastructure/types.h"
#include "city/maps/terrain.h"
#include "city/infrastructure/power_lines.h"
#include "city/engine/commands.h"
// Highway planning, placement, junctions, approaches, footprint metadata and drawing.
#include "assets/state.h"
#include "city/maps/module.h"
#include "drawing/maps/map_terrain.h"
#include "drawing/maps/map_camera.h"
#include "drawing/maps/map_view.h"
#include "drawing/maps/tables.h"

#include <assert.h>
#include <stdlib.h>

#include "city/maps/layers.h"
#include "city/maps/water.h"
#include "city/maps/map.h"
#include "city/maps/tables.h"
#include "city/presentation.h"
#include "city/saveload/map_format.h"
#include "city/state.h"
#include "city/tools/tools.h"

static CityInfrastructureSpan FindCityNativeHighwaySpan(const City *city, TileIndex origin, int direction,
    bool choose_direction);
static i64 PlaceCityHighwayOnramp(City *city, TileIndex tile);

// The complete two-by-two highway footprint must fit on the map before it can start a water span.
static const u8 kTransportTerrainClass[256] = {
  0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1,
};

static const u8 kTransportSpanNorthClass[18] = {
  0, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0
};

static const u8 kTransportSpanSouthClass[18] = {
  0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0
};

static const u8 kTransportSpanDirectionByMask[16] = {
  2, 2, 3, 8, 2, 2, 9, 12, 3, 11, 3, 12, 10, 12, 12, 12
};

static const u8 kTransportSpanDirectionByComplement[16] = {
  2, 2, 3, 3, 2, 2, 3, 2, 3, 3, 3, 2, 3, 2, 2, 2
};

bool CityHighwayFootprintHasWater(const City *city, TileIndex anchor) {
  if (!IsMapTileValid(city, anchor)) return false;
  CityPoint anchor_point = MapTilePt(city, anchor);
  if (!IsMapCoordinateValid(city, anchor_point.x + 1, anchor_point.y + 1)) return false;
  for (int dy = 0; dy < 2; ++dy)
    for (int dx = 0; dx < 2; ++dx)
      if (HasTileWater(&city->tile[MapTileXY(city, anchor_point.x + dx, anchor_point.y + dy)])) return true;
  return false;
}

// Resolve a valid highway cell's continuation metadata to its top-left footprint tile. Returns INVALID_TILE if the
// required offset leaves the map; bridge lane metadata is decoded before applying the offset.
TileIndex CityHighwayAnchor(const City *city, TileIndex tile) {
  const TileInfo *tp = &city->tile[tile];
  int metadata = IsTileKindHighwayBridge(tp->kind) ? CityBridgeHighwayZone(city, tile) : tp->terrain;
  return GetRelativeTileOrInvalid(city, tile,
      -((metadata & CITY_BUILDING_CONTINUE_X) != 0), -((metadata & CITY_BUILDING_CONTINUE_Y) != 0));
}

// Lane changes stay inside a footprint. Separate straight spans join only at their ends, with the same explicit axis
// and carriageway alignment. From/to must be adjacent valid tiles and direction their cardinal edge; false rejects
// misaligned separate carriageways.
bool CityHighwaysCanJoin(const City *city, TileIndex from, TileIndex to, int direction) {
  if (!IsTileKindHighwayAxis(city->tile[from].kind) ||
      !IsTileKindHighwayAxis(city->tile[to].kind)) return true;
  TileIndex first = CityHighwayAnchor(city, from), second = CityHighwayAnchor(city, to);
  if (first == INVALID_TILE || second == INVALID_TILE) return false;
  if (first == second) return true;
  int axis = ReadCityHighwayAxis(&city->tile[from]);
  if (axis != ReadCityHighwayAxis(&city->tile[to]) || axis != (direction & 1)) return false;
  return axis == CITY_HIGHWAY_AXIS_Y ? MapTileX(city, first) == MapTileX(city, second) :
      MapTileY(city, first) == MapTileY(city, second);
}

// Choose the placement anchor for existing highway footprints or a new highway start. Requires a valid tile;
// compact-highway policy determines whether a new start is snapped to the legacy grid.
TileIndex CityHighwayPlacementAnchor(const City *city, TileIndex tile) {
  int kind = city->tile[tile].kind;
  if (IsTileKindHighwayFootprint(kind) || IsTileKindHighwayBridge(kind)) return CityHighwayAnchor(city, tile);
  if (city->fixes & (1ull << CITY_FIX_COMPACT_HIGHWAYS)) return tile;
  CityPoint point = MapTilePt(city, tile);
  return MapTileXY(city, point.x & ~1, point.y & ~1);
}

// Resolve a valid highway tile's footprint rectangle from City's continuation links and neighbouring kinds.
// A continuation outside the map returns empty bounds; the returned rectangle contains no owned storage.
CityRect CityHighwayFootprintBounds(const City *city, TileIndex tile) {
  bool bridge = IsTileKindHighwayBridge(city->tile[tile].kind);
  int links = bridge ? CityBridgeHighwayZone(city, tile) : city->tile[tile].terrain;
  CityPoint point = MapTilePt(city, tile);
  int dx = (links & CITY_BUILDING_CONTINUE_X) != 0, dy = (links & CITY_BUILDING_CONTINUE_Y) != 0;
  int x = point.x - dx, y = point.y - dy;
  int width = 1, height = 1;
  if (x < 0 || y < 0) return (CityRect){0};
  if (bridge) return (CityRect){x, y, x + 2, y + 2};
  TileIndex anchor = tile - dx - dy * city->map_width;
  if (x + 1 < city->map_width) {
    TileIndex right = anchor + 1;
    const TileInfo *tp = &city->tile[right];
    if (IsTileKindHighwayFootprint(tp->kind) &&
        (tp->terrain & CITY_BUILDING_CONTINUATION_MASK) == CITY_BUILDING_CONTINUE_X) width = 2;
  }
  if (y + 1 < city->map_height) {
    TileIndex down = anchor + city->map_width;
    const TileInfo *tp = &city->tile[down];
    if (IsTileKindHighwayFootprint(tp->kind) &&
        (tp->terrain & CITY_BUILDING_CONTINUATION_MASK) == CITY_BUILDING_CONTINUE_Y) height = 2;
  }
  return (CityRect){x, y, x + width, y + height};
}

// Test a physical corner of a valid native highway footprint. Straight footprints use their actual bounds;
// bends, junctions, inclines and bridge lanes select one corner from their continuation links.
bool IsCityHighwayFootprintCorner(const City *city, TileIndex tile, int corner) {
  int kind = city->tile[tile].kind;
  if (IsTileKindStraightHighwayFamily(kind)) {
    CityRect bounds = CityHighwayFootprintBounds(city, tile);
    bool bottom = corner == CITY_BUILDING_CORNER_BOTTOM_LEFT || corner == CITY_BUILDING_CORNER_BOTTOM_RIGHT;
    CityPoint point = MapTilePt(city, tile);
    return point.x == (corner & 2 ? bounds.right - 1 : bounds.left) &&
        point.y == (bottom ? bounds.bottom - 1 : bounds.top);
  }
  int metadata = IsTileKindHighwayBridge(kind) ? CityBridgeHighwayZone(city, tile) : ReadZoneByte(city, tile);
  int links = metadata >> 6;
  return (((links & 1) << 1) | ((links ^ (links >> 1)) & 1)) == corner;
}

// Decode a valid native highway footprint into rotation-relative CityFootprintCornerMask bits. Straight
// footprints can expose several corners; bridge lanes and other shapes select one from their own metadata.
SC2K_PRESERVE_MOST int CityHighwayCornerMask(const City *city, TileIndex tile) {
  int kind = city->tile[tile].kind;
  if (IsTileKindStraightHighwayFamily(kind)) {
    CityRect bounds = CityHighwayFootprintBounds(city, tile);
    CityPoint point = MapTilePt(city, tile);
    int mask = 0;
    int rotation = city->geography.rotation;
    if (point.x == bounds.left && point.y == bounds.top) mask |= CITY_FOOTPRINT_CORNER_NW << rotation;
    if (point.x == bounds.left && point.y == bounds.bottom - 1)
      mask |= CITY_FOOTPRINT_CORNER_NW << ((rotation + 1) & 3);
    if (point.x == bounds.right - 1 &&
        point.y == bounds.bottom - 1) mask |= CITY_FOOTPRINT_CORNER_NW << ((rotation + 2) & 3);
    if (point.x == bounds.right - 1 && point.y == bounds.top)
      mask |= CITY_FOOTPRINT_CORNER_NW << ((rotation + 3) & 3);
    return mask;
  }
  int metadata = IsTileKindHighwayBridge(kind) ? CityBridgeHighwayZone(city, tile) : city->tile[tile].terrain;
  // Packed continuation codes 00/01/10/11 map to clockwise corners 0/3/1/2.
  int links = metadata >> 6;
  int corner = ((links & 1) << 1) | ((links ^ (links >> 1)) & 1);
  return CITY_FOOTPRINT_CORNER_NW << ((city->geography.rotation + corner) & 3);
}

// Install continuation links for a complete two-by-two highway footprint while preserving zoning and orientation.
// Top-left and all four cells must be valid; the caller owns tile replacement and drawing invalidation.
void InitializeCityHighwayFootprint(City *city, TileIndex top_left) {
  for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
    TileIndex tile = top_left + dy * city->map_width + dx;
    TileInfo *tp = &city->tile[tile];
    WriteTileZoneByte(tp, ReadCityZoneKind(tp) |
        (tp->terrain & CITY_HIGHWAY_ORIENTATION_MASK) |
        (dx ? CITY_BUILDING_CONTINUE_X : 0) | (dy ? CITY_BUILDING_CONTINUE_Y : 0));
  }
}

// Place one highway tile, preserving perpendicular lines and recording its axis.
void UpdateCityHighwayTile(City *city, TileIndex tile_index, int shape) {
  assert(IsMapTileValid(city, tile_index));
  InvalidateTile(city, tile_index);
  int previous_kind = city->tile[tile_index].kind;
  int kind = TK_HIGHWAY;
  bool power_line = IsTileKindPlainPowerLine(previous_kind);
  if ((power_line || IsTileKindPlainTransport(previous_kind)) &&
      ReadCityStraightLineAxis(city, tile_index) >= 0) {
    if (power_line) kind = TK_HIGHWAY_POWER;
    else if (previous_kind == TK_ROAD) kind = TK_HIGHWAY_ROAD;
    else if (previous_kind == TK_RAIL) kind = TK_HIGHWAY_RAIL;
  }
  ReplaceCityTileAndStatistics(city, tile_index, kind);
  WriteCityHighwayOrientation(&city->tile[tile_index], shape);
  RefreshCityPowerConnections(city, tile_index);
}

// Place or refresh a 2x2 highway footprint, grading its ground when the fix is disabled. Requires footprint_shape >= 0
// && footprint_shape < CITY_TRANSPORT_FOOTPRINT_BRIDGE_13; violations are programming errors.
void UpdateCityHighwayFootprint(City *city, TileIndex tile_index, int footprint_shape) {
  // Bridge selectors are queries only; span construction owns bridge kinds.
  assert(footprint_shape >= 0 && footprint_shape < CITY_TRANSPORT_FOOTPRINT_BRIDGE_13);
  const TileIndex tile_east = tile_index + 1;
  const TileIndex tile_south = tile_index + city->map_width;
  const TileIndex tile_south_east = tile_south + 1;
  const TileIndex cells[4] = {tile_index, tile_south, tile_east, tile_south_east};
  static const i8 corner_y[4] = {0, 1, 1, 0};
  static const i8 corner_x[4] = {0, 0, 1, 1};
  bool preserve_ground = (city->fixes & (1ull << CITY_FIX_HIGHWAY_TERRAIN)) != 0;
  InvalidateTileRange(city, tile_index, 2, 2);
  int maximum = GetMaximumTerrainHeightIn2x2(city, tile_index);
  if (footprint_shape < CITY_TRANSPORT_FOOTPRINT_GRADED_4) {
    if (!preserve_ground && ReadCityLandHeight(&city->tile[tile_index]) < maximum)
      WriteLegacyCityTerrainCode(city, tile_index, 13);
    if (!preserve_ground && ReadCityLandHeight(&city->tile[tile_east]) < maximum)
      WriteLegacyCityTerrainCode(city, tile_east, 13);
    if (!preserve_ground && ReadCityLandHeight(&city->tile[tile_south_east]) < maximum)
      WriteLegacyCityTerrainCode(city, tile_south_east, 13);
    if (!preserve_ground && ReadCityLandHeight(&city->tile[tile_south]) < maximum)
      WriteLegacyCityTerrainCode(city, tile_south, 13);

    // Later cells retain their original precedence when choosing the axis.
    for (int corner = 0; corner < 4; ++corner) {
      int axis = ReadCityStraightLineAxis(city, cells[corner]);
      if (city->tile[cells[corner]].kind == TK_HIGHWAY) continue;
      if (axis == 0)
        footprint_shape = CITY_TRANSPORT_FOOTPRINT_FORCE_EW;
      else if (axis == 1)
        footprint_shape = CITY_TRANSPORT_FOOTPRINT_FORCE_NS;
    }

    int shape = footprint_shape & 1;
    UpdateCityHighwayTile(city, tile_index, shape);
    UpdateCityHighwayTile(city, tile_south, shape);
    UpdateCityHighwayTile(city, tile_south_east, shape);
    UpdateCityHighwayTile(city, tile_east, shape);
    InitializeCityHighwayFootprint(city, tile_index);
    InvalidateTileRange(city, tile_index, 2, 2);
    return;
  }

  if (!preserve_ground && footprint_shape < CITY_TRANSPORT_FOOTPRINT_MULTI_8) {
    int northwest = city->tile[tile_index].altitude;
    int southwest = city->tile[tile_south].altitude;
    int southeast = city->tile[tile_south_east].altitude;
    int northeast = city->tile[tile_east].altitude;
    if ((northwest & CITY_ALTITUDE_LAND_MASK) != maximum || (southwest & CITY_ALTITUDE_LAND_MASK) != maximum ||
        (southeast & CITY_ALTITUDE_LAND_MASK) != maximum || (northeast & CITY_ALTITUDE_LAND_MASK) != maximum) {
      int level = maximum - 1;
      city->tile[tile_index].altitude = level | (northwest & CITY_ALTITUDE_CLEAR_LAND);
      city->tile[tile_south].altitude = level | (southwest & CITY_ALTITUDE_CLEAR_LAND);
      city->tile[tile_south_east].altitude = level | (southeast & CITY_ALTITUDE_CLEAR_LAND);
      city->tile[tile_east].altitude = level | (northeast & CITY_ALTITUDE_CLEAR_LAND);
    }
    static const u8 kGradedTerrain[4][4] = {
        {13, 1, 13, 1}, {13, 13, 2, 2}, {3, 13, 3, 13}, {4, 4, 13, 13}};
    int grade = footprint_shape - CITY_TRANSPORT_FOOTPRINT_GRADED_4;
    for (int corner = 0; corner < 4; ++corner)
      WriteLegacyCityTerrainCode(city, cells[corner], kGradedTerrain[grade][corner]);
  } else if (!preserve_ground) {
    for (int corner = 0; corner < 4; ++corner) {
      TileIndex cell_index = tile_index + corner_y[corner] * city->map_width + corner_x[corner];
      const TileInfo *tp = &city->tile[cell_index];
      if (!IsFlatDryTile(tp) || ReadCityLandHeight(tp) < maximum)
        WriteLegacyCityTerrainCode(city, cell_index, 13);
    }
  }

  for (int dy = 0; dy < 2; ++dy)
    for (int dx = 0; dx < 2; ++dx) {
      TileIndex tile = tile_index + dy * city->map_width + dx;
      int kind = CityHighwayFootprintKind(footprint_shape);
      ReplaceCityTileAndStatistics(city, tile, kind);
      WriteCityHighwayOrientation(&city->tile[tile], CityHighwayFootprintOrientation(footprint_shape));
    }

  InitializeCityHighwayFootprint(city, tile_index);
  InvalidateTileRange(city, tile_index, 2, 2);
}

// Clear edited zone-state bits and reconnect nearby transport spans.
void ClearZoneStateAndUpdateTransportSpanNeighbors(City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile));
  if (GetTransportSpanCompatibilityMask(city, tile) == CITY_SPAN_COMPATIBILITY_INVALID)
    return;

  InvalidateTileRange(city, tile, 2, 2);
  for (int dy = 0; dy < 2; ++dy) {
    for (int dx = 0; dx < 2; ++dx) {
      TileIndex cell_index = tile + dy * city->map_width + dx;
      TileInfo *tp = &city->tile[cell_index];
      WriteCityZoneKind(tp, ZONE_NONE);
    }
  }

  InvalidateTileRange(city, tile, 2, 2);

  int direction = ResolveTransportSpanDirection(city, tile);
  if (direction >= 0)
    UpdateCityHighwayFootprint(city, tile, direction);

  static const i8 neighbor_y[4] = {-2, 2, 0, 0};
  static const i8 neighbor_x[4] = {0, 0, -2, 2};
  for (int neighbor = 0; neighbor < 4; ++neighbor) {
    TileIndex nearby = GetRelativeTileOrInvalid(city, tile, neighbor_x[neighbor], neighbor_y[neighbor]);
    if (ClassifyTransportSpanTile(city, nearby) < CITY_TRANSPORT_FOOTPRINT_FORCE_NS)
      continue;
    direction = ResolveTransportSpanDirection(city, nearby);
    if (direction >= 0)
      UpdateCityHighwayFootprint(city, nearby, direction);
  }

  direction = ResolveTransportSpanDirection(city, tile);
  if (direction >= 0)
    UpdateCityHighwayFootprint(city, tile, direction);
}

// A highway edge uses the water plane or an aligned land grade; -1 rejects unsupported slopes. Placement and
// reconnection share this admission rule. Geometry and direction must be decoded supported values; the query has no
// terrain or connection side effects.
static int CityHighwayGradeEdgeHeight(CityTileGeometry geometry, int direction) {
  static const u8 kRaisedEdges[] = {0, CITY_LINE_NORTH, CITY_LINE_WEST, CITY_LINE_SOUTH, CITY_LINE_EAST};
  if (geometry.water != CITY_WATER_NONE) return geometry.surface_height;
  int shape = geometry.land_shape;
  if ((shape > CITY_TERRAIN_SHAPE_4 && shape != CITY_TERRAIN_SHAPE_13) ||
      (shape && shape <= CITY_TERRAIN_SHAPE_4 && (shape & 1) != (direction & 1))) return -1;
  return geometry.land_height + (shape == CITY_TERRAIN_SHAPE_13) +
      (shape >= CITY_TERRAIN_SHAPE_1 && shape <= CITY_TERRAIN_SHAPE_4 && direction == kRaisedEdges[shape]);
}

// Both carriageways must cross a placement boundary together. Parallel sides may touch without joining; a one-cell
// stagger at an end is not a connection. Bounds must be an admitted half-open placement rectangle; false means an
// adjoining carriageway would be split or misaligned.
static bool CityHighwayEdgesAlign(const City *city, CityRect bounds) {
  for (int direction = 0; direction < 4; ++direction) {
    int axis = direction & 1;
    int begin = axis ? bounds.left : bounds.top;
    int end = axis ? bounds.right : bounds.bottom;
    for (int offset = begin; offset < end; ++offset) {
      int x = axis ? offset : direction == CITY_LINE_WEST ? bounds.left - 1 : bounds.right;
      int y = axis ? direction == CITY_LINE_NORTH ? bounds.top - 1 : bounds.bottom : offset;
      TileIndex tile = MapTileXYOrInvalid(city, x, y);
      if (tile == INVALID_TILE) continue;
      const TileInfo *tp = &city->tile[tile];
      int kind = tp->kind, across;
      if (IsTileKindBridge(kind) && IsTileKindHighwayBridge(kind)) {
        if (ReadCityBridgeAxis(tp) != axis) continue;
        across = (axis ? x : y) - ((tp->terrain & CITY_BRIDGE_HIGHWAY_RIGHT) != 0);
      } else {
        if (!IsTileKindHighwayFootprint(kind) ||
            (IsTileKindHighwayAxis(kind) && ReadCityHighwayAxis(tp) != axis)) continue;
        TileIndex anchor = CityHighwayAnchor(city, tile);
        if (anchor == INVALID_TILE) return false;
        across = axis ? MapTileX(city, anchor) : MapTileY(city, anchor);
      }
      if (across < begin || across + 2 > end) return false;
    }
  }
  return true;
}

// A bend can combine whole straight segments, but must never cut a footprint, erase a surface crossing or reserve land
// outside its two-by-two artwork. Out-of-map two-by-two footprints return false; fill selects whether eligible clear
// cells may participate in the prospective junction.
static bool CanFormCityHighwayJunction(const City *city, int x, int y, bool fill) {
  TileIndex anchor = MapTileXYOrInvalid(city, x, y);
  if (anchor == INVALID_TILE || !IsMapCoordinateValid(city, x + 1, y + 1)) return false;
  int height = ReadCityLandHeight(&city->tile[anchor]);
  for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
    TileIndex tile = anchor + dy * city->map_width + dx;
    const TileInfo *tp = &city->tile[tile];
    int kind = tp->kind;
    if (HasTileWater(tp) || ReadTerrainCode(tp) || ReadCityLandHeight(tp) != height ||
        ReadCityZoneKind(tp) == ZONE_MILITARY) return false;
    if (kind == TK_HIGHWAY || IsTileKindHighwayBendOrJunction(kind)) {
      CityRect bounds = CityHighwayFootprintBounds(city, tile);
      if (bounds.left < x || bounds.top < y || bounds.right > x + 2 || bounds.bottom > y + 2) return false;
    } else if (!fill || !IsTileKindUnbuilt(kind) || kind == TK_RADIOACTIVE_WASTE) {
      return false;
    }
  }
  return CityHighwayEdgesAlign(city, (CityRect){x, y, x + 2, y + 2});
}

// Both carriageways must meet the flat junction at its edge height. Slopes remain connected at either end, including
// older shared incline footprints. Coordinates must identify an admitted flat two-by-two junction; returns its edge
// bitmask without writing neighbors.
static int CityHighwayJunctionConnections(const City *city, int x, int y) {
  int height = ReadCityLandHeight(&city->tile[MapTileXY(city, x, y)]), mask = 0;
  for (int direction = 0; direction < 4; ++direction) {
    bool connected = true;
    for (int lane = 0; lane < 2; ++lane) {
      int nx = x + (direction & 1 ? lane : direction == CITY_LINE_WEST ? -1 : 2);
      int ny = y + (direction & 1 ? direction == CITY_LINE_NORTH ? -1 : 2 : lane);
      TileIndex tile = MapTileXYOrInvalid(city, nx, ny);
      if (tile == INVALID_TILE) { connected = false; break; }
      const TileInfo *tp = &city->tile[tile];
      int kind = tp->kind;
      bool bridge = IsTileKindBridge(kind) && IsTileKindHighwayBridge(kind);
      bool straight = bridge || IsTileKindHighwayAxis(kind);
      if (!straight && !IsTileKindHighwayBendOrJunction(kind)) {
        connected = false; break;
      }
      CityTileGeometry geometry = ReadCityTileGeometry(city, tile);
      int edge_height = bridge ? CityBridgeTransportEdgeHeight(city, tile, (direction + 2) & 3) :
          CityHighwayGradeEdgeHeight(geometry, (direction + 2) & 3);
      int axis = bridge ? ReadCityBridgeAxis(tp) : straight ?
          ReadCityHighwayAxis(tp) : -1;
      if (edge_height != height || (straight && axis != (direction & 1))) { connected = false; break; }
      if (bridge) {
        int across = ((direction & 1) ? nx : ny) -
            ((tp->terrain & CITY_BRIDGE_HIGHWAY_RIGHT) != 0);
        if (across != ((direction & 1) ? x : y)) { connected = false; break; }
        continue;
      }
      CityRect bounds = CityHighwayFootprintBounds(city, tile);
      if (direction & 1 ? bounds.left != x || bounds.right != x + 2 :
          bounds.top != y || bounds.bottom != y + 2) { connected = false; break; }
    }
    if (connected) mask |= 1 << direction;
  }
  return mask;
}

// Recompute compact-highway junction artwork in the bounded neighborhood of an edited tile. Requires a valid tile
// after its fields are written; disabled compact-highway behavior leaves connections alone.
static void RefreshCityHighwayConnections(City *city, TileIndex tile) {
  if (!(city->fixes & (1ull << CITY_FIX_COMPACT_HIGHWAYS))) return;
  CityPoint point = MapTilePt(city, tile);
  for (int y = point.y - 2; y <= point.y + 1; ++y) for (int x = point.x - 2; x <= point.x + 1; ++x) {
    if (!CanFormCityHighwayJunction(city, x, y, false)) continue;
    int shape = kTransportSpanDirectionByMask[CityHighwayJunctionConnections(city, x, y)];
    if (shape < CITY_TRANSPORT_FOOTPRINT_MULTI_8) continue;
    int kind = CityHighwayFootprintKind(shape);
    TileIndex anchor = MapTileXY(city, x, y);
    const TileInfo *tp = &city->tile[anchor];
    if (tp->kind == kind &&
        ReadCityHighwayOrientation(tp) == CityHighwayFootprintOrientation(shape)) continue;
    UpdateCityHighwayFootprint(city, anchor, shape);
  }
}

// Straight highways occupy one cell along their axis and two across it. Bends and junctions reserve the existing
// two-by-two artwork footprint. Endpoints must be valid; false also selects the legacy-width route when compact
// highways are disabled.
SC2K_PRESERVE_MOST bool IsCityCompactHighwayLine(const City *city, TileIndex start, TileIndex end) {
  if (!(city->fixes & (1ull << CITY_FIX_COMPACT_HIGHWAYS))) return false;
  CityPoint from = MapTilePt(city, start), to = MapTilePt(city, end);
  // Turning needs at least one 2x2 step on both axes. Ignore one-cell jitter
  // on an otherwise straight drag, without projecting every drag onto an axis.
  return abs(to.x - from.x) < 2 || abs(to.y - from.y) < 2;
}

// Build a caller-owned plan for an admitted compact highway line. Invalid endpoints or unsupported geometry return
// false; plan captures costs and span choices without applying tiles or charging treasury.
bool PlanCityCompactHighway(const City *city, TileIndex start, TileIndex end, int orientation,
    CityCompactHighwayPlan *plan) {
  *plan = (CityCompactHighwayPlan){.start = start, .cost = -1,
      .span = {.start = INVALID_TILE, .end = INVALID_TILE, .error_tile = INVALID_TILE}};
  if (!IsMapTileValid(city, start) || !IsMapTileValid(city, end) ||
      !IsCityCompactHighwayLine(city, start, end)) return false;
  CityPoint start_point = MapTilePt(city, start);
  CityPoint end_point = MapTilePt(city, end);
  int start_kind = city->tile[start].kind;
  // A straight drag chooses its dominant axis from the original start.
  // Diagonal routes use the 2x2 bend planner before reaching this path.
  int axis = start != end ? CityLineDirection(start_point.x, start_point.y, end_point.x, end_point.y) & 1 :
      orientation >= 0 ? orientation & 1 : CITY_HIGHWAY_AXIS_X;
  if (start == end) {
    if (IsTileKindHighwayAxis(start_kind)) {
      axis = ReadCityHighwayAxis(&city->tile[start]);
    } else {
      int score[2] = {0};
      for (int direction = 0; direction < 4; ++direction) {
        TileIndex next = GetRelativeTileOrInvalid(city, start, kLineXStep[direction], kLineYStep[direction]);
        if (next == INVALID_TILE) continue;
        const TileInfo *tp = &city->tile[next];
        int kind = tp->kind, travel_axis = direction & 1;
        if (IsTileKindHighwayAxis(kind))
          score[travel_axis] += 1 + (travel_axis == ReadCityHighwayAxis(tp));
        else if (IsTileKindHighwayBendOrJunction(kind))
          score[travel_axis] += 2;
      }
      if (score[0] != score[1]) axis = score[1] > score[0];
    }
  }
  if (IsTileKindHighwayFootprint(start_kind)) {
    TileIndex anchor = CityHighwayAnchor(city, start);
    if (anchor == INVALID_TILE) return true;
    CityPoint anchor_point = MapTilePt(city, anchor);
    // Align the two lanes without snapping along an existing 2x2 segment.
    if (IsTileKindStraightHighwayFamily(start_kind)) {
      if (axis) start_point.x = anchor_point.x; else start_point.y = anchor_point.y;
    } else {
      start_point = anchor_point;
    }
    if (start == end) end_point = start_point;
  } else {
    for (int side = -1; side <= 1; side += 2) {
      TileIndex next = GetRelativeTileOrInvalid(city, start, axis ? 0 : side, axis ? side : 0);
      if (next == INVALID_TILE) continue;
      const TileInfo *tp = &city->tile[next];
      int kind = tp->kind;
      if (!IsTileKindHighwayFootprint(kind)) continue;
      TileIndex anchor = CityHighwayAnchor(city, next);
      if (anchor == INVALID_TILE) continue;
      CityPoint anchor_point = MapTilePt(city, anchor);
      if (IsTileKindHighwayAxis(kind) && ReadCityHighwayAxis(tp) == axis) {
        if (axis) start_point.x = end_point.x = anchor_point.x;
        else start_point.y = end_point.y = anchor_point.y;
        break;
      }
      bool aligned = false;
      for (int offset = 0; offset <= 1; ++offset) {
        int cx = axis ? start_point.x - offset : anchor_point.x;
        int cy = axis ? anchor_point.y : start_point.y - offset;
        if (!CanFormCityHighwayJunction(city, cx, cy, false)) continue;
        if (axis) start_point.x = end_point.x = cx; else start_point.y = end_point.y = cy;
        aligned = true;
        break;
      }
      if (aligned) break;
    }
  }
  // Only the endpoint is projected; lane alignment above is relative to start.
  if (axis) end_point.x = start_point.x; else end_point.y = start_point.y;
  plan->start = MapTileXY(city, start_point.x, start_point.y);
  plan->axis = axis;
  int width = axis ? 2 : 1, height = axis ? 1 : 2;
  if (start == end) {
    for (int offset = 0; offset <= 1; ++offset) {
      int cx = start_point.x - (axis ? 0 : offset), cy = start_point.y - (axis ? offset : 0);
      if (!CanFormCityHighwayJunction(city, cx, cy, true)) continue;
      int shape = kTransportSpanDirectionByMask[CityHighwayJunctionConnections(city, cx, cy)];
      if (shape < CITY_TRANSPORT_FOOTPRINT_MULTI_8) continue;
      start_point.x = end_point.x = cx; start_point.y = end_point.y = cy;
      plan->start = MapTileXY(city, start_point.x, start_point.y);
      width = height = 2;
      plan->shape = shape;
      break;
    }
  }
  plan->direction = axis ? end_point.y < start_point.y ? CITY_LINE_NORTH : CITY_LINE_SOUTH :
      end_point.x < start_point.x ? CITY_LINE_WEST : CITY_LINE_EAST;
  int length = (axis ? abs(end_point.y - start_point.y) : abs(end_point.x - start_point.x)) + 1;
  CityRect placement = {IntMin(start_point.x, end_point.x), IntMin(start_point.y, end_point.y),
      IntMax(start_point.x, end_point.x) + width, IntMax(start_point.y, end_point.y) + height};
  bool reverse = axis ? end_point.y < start_point.y : end_point.x < start_point.x;
  int sx = start_point.x, sy = start_point.y, origin = axis ? start_point.y : start_point.x;
  int required_length = 0, accepted_length = 0;
  int previous_height = -1;
  i64 cost = 0;
  for (int segment = 0; segment < length; ++segment) {
    bool blocked = false, incomplete = false;
    int segment_required = required_length;
    int entry_height = -1, exit_height = -1;
    for (int dy = 0; dy < height; ++dy) for (int dx = 0; dx < width; ++dx) {
      TileIndex tile = MapTileXYOrInvalid(city, start_point.x + dx, start_point.y + dy);
      if (tile == INVALID_TILE) { blocked = true; continue; }
      CityTileGeometry geometry = ReadCityTileGeometry(city, tile);
      const TileInfo *tp = &city->tile[tile];
      if (geometry.water != CITY_WATER_NONE) {
        int bx = start_point.x - (reverse && !axis), by = start_point.y - (reverse && axis);
        TileIndex bank = MapTileXYOrInvalid(city, bx, by);
        if (bank == INVALID_TILE || !IsMapCoordinateValid(city, bx + 1, by + 1)) goto invalid_overlap;
        plan->span = FindCityNativeHighwaySpan(city, bank, plan->direction, start == end);
        if (plan->span.end == INVALID_TILE) goto invalid_overlap;
        if (!accepted_length) plan->direction = plan->span.direction;
        plan->land_cost = accepted_length ? plan->cost : 0;
        plan->cost = plan->land_cost + CityInfrastructureSpanCost(plan->span, plan->span.styles);
        CityPoint start_position = MapTilePt(city, plan->span.start);
        int ex = start_position.x + 2 * (plan->span.length - 1) * kLineXStep[plan->span.direction];
        int ey = start_position.y + 2 * (plan->span.length - 1) * kLineYStep[plan->span.direction];
        plan->water = (CityRect){IntMin(start_position.x, ex), IntMin(start_position.y, ey),
            IntMax(start_position.x, ex) + 2, IntMax(start_position.y, ey) + 2};
        goto finish_plan;
      }
      int shape = geometry.land_shape;
      int near = CityHighwayGradeEdgeHeight(geometry, (plan->direction + 2) & 3);
      int far = CityHighwayGradeEdgeHeight(geometry, plan->direction);
      if (near < 0 || (entry_height >= 0 && (entry_height != near || exit_height != far))) goto invalid_overlap;
      entry_height = near; exit_height = far;
      int kind = tp->kind;
      if (shape && shape <= CITY_TERRAIN_SHAPE_4 && !IsTileKindUnbuilt(kind) &&
          kind != TK_HIGHWAY &&
          !IsTileKindHighwayIncline(kind)) goto invalid_overlap;
      if (IsTileKindHighwayFootprint(kind)) {
        if (!plan->shape) {
          bool junction = IsTileKindHighwayBendOrJunction(kind);
          bool reshape = junction || ReadCityHighwayAxis(tp) != axis;
          CityRect bounds = CityHighwayFootprintBounds(city, tile);
          if (reshape) {
            if (!junction && kind != TK_HIGHWAY) goto invalid_overlap;
            int far = axis ? (reverse ? bounds.top : bounds.bottom - 1) :
                reverse ? bounds.left : bounds.right - 1;
            segment_required = IntMax(segment_required, (far - origin) * (reverse ? -1 : 1) + 1);
          } else {
            // Unchanged straights may extend along the line, but not across it.
            if (axis) { bounds.top = start_point.y; bounds.bottom = start_point.y + height; }
            else { bounds.left = start_point.x; bounds.right = start_point.x + width; }
          }
          if (bounds.left < placement.left || bounds.right > placement.right ||
              bounds.top < placement.top || bounds.bottom > placement.bottom) incomplete = true;
        }
        if (!CityHighwayCellAlreadyBuilt(city, tile)) cost += 25;
      } else if (IsTileKindUnbuilt(kind) && kind != TK_RADIOACTIVE_WASTE) {
        cost += 25;
      } else if (ReadCityStraightLineAxis(city, tile) == 1 - axis) {
        cost += 25;
      } else {
        blocked = true;
      }
      if (ReadCityZoneKind(tp) == ZONE_MILITARY) blocked = true;
    }
    if (blocked) break;
    if (incomplete) goto invalid_overlap;
    if (previous_height >= 0 && entry_height != previous_height) goto invalid_overlap;
    previous_height = exit_height;
    required_length = segment_required;
    plan->land = (CityRect){IntMin(sx, start_point.x), IntMin(sy, start_point.y),
        IntMax(sx, start_point.x) + width, IntMax(sy, start_point.y) + height};
    accepted_length = segment + 1;
    plan->cost = cost;
    start_point.x += kLineXStep[plan->direction]; start_point.y += kLineYStep[plan->direction];
  }
  plan->land_cost = plan->cost;
finish_plan:
  // An obstacle must not stop construction halfway through a reshaped piece.
  if (accepted_length < required_length) goto invalid_overlap;
  if (accepted_length && !CityHighwayEdgesAlign(city, plan->land)) goto invalid_overlap;
  return true;
invalid_overlap:
  plan->cost = -1;
  return true;
}

// Apply an admitted retained-command preview to the live construction record. The caller has selected compact
// planning or admitted the legacy footprint; terrain, treasury and RNG remain unchanged. Damage is immediate.
void UpdateCityHighwayPreview(City *city, TileIndex start, TileIndex end, const CityCompactHighwayPlan *compact) {
  if (compact) {
    city->construction.terrain_surcharge_count = 0;
    city->construction.cell_count = compact->cost >= 0 ? compact->cost / 25 : 0;
    const CityRect areas[] = {compact->land, compact->water};
    for (int part = 0; part < 2; ++part) {
      CityRect bounds = areas[part];
      InvalidateTileRange(city, MapTileXYOrInvalid(city, bounds.left, bounds.top),
          bounds.right - bounds.left, bounds.bottom - bounds.top);
    }
  } else {
    CityPoint from = MapTilePt(city, start), to = MapTilePt(city, end);
    int x = from.x, y = from.y;
    PrepareCityHighwayLine(city, &city->construction, x, y, to.x, to.y);
    city->construction.terrain_surcharge_count = 0;
    city->construction.cell_count = 0;
    do {
      for (int cell = 0; cell < 4; ++cell) {
        int cell_y = y + (cell & 1);
        int cell_x = x + (cell >> 1);
        TileIndex tile = MapTileXY(city, cell_x, cell_y);
        if (!CityHighwayCellAlreadyBuilt(city, tile)) ++city->construction.cell_count;
      }
      InvalidateTileRange(city, MapTileXY(city, x, y), 2, 2);
    } while (AdvanceCityHighwayLine(city, &city->construction, &x, &y));
  }
  city->construction.direction = 0;
}

// Compute the charge for a prepared compact-highway plan and selected span style. Plan must come from the matching
// planner; returns a negative admission error for an unavailable style without changing City.
i64 CityCompactHighwayCost(const CityCompactHighwayPlan *plan, int span_style) {
  if (plan->cost < 0) return CITY_COMMAND_COST_BLOCKED;
  if (plan->span.end != INVALID_TILE) {
    if ((span_style != CITY_SPAN_HIGHWAY && span_style != CITY_SPAN_REINFORCED_HIGHWAY) ||
        !(plan->span.styles & (1 << span_style))) return CITY_COMMAND_COST_BLOCKED;
    return plan->land_cost + (i64)plan->span.length * kSpanCostPerCell[span_style];
  }
  return plan->cost;
}

// Quote or build a compact highway using resolved endpoints, orientation and span options. Rejects unsupported input
// and invalid plans; CITY_COMMAND_TEST leaves City/RNG untouched and execution returns the actual signed
// placement result.
static i64 CmdBuildCityCompactHighway(City *city, u32 flags, TileIndex start, TileIndex end,
    int orientation, int span_style) {
  CityCompactHighwayPlan plan;
  if ((flags & ~CITY_COMMAND_TEST) || !PlanCityCompactHighway(city, start, end, orientation, &plan))
    return CITY_COMMAND_COST_BLOCKED;
  int priced_style = span_style == CITY_SPAN_CANCEL && (flags & CITY_COMMAND_TEST) ? CITY_SPAN_HIGHWAY : span_style;
  i64 cost = CityCompactHighwayCost(&plan, priced_style);
  if (cost < 0) return cost;
  if (flags & CITY_COMMAND_TEST) return cost;
  city->construction.cost = cost;
  city->construction.direction = plan.direction;
  if (plan.shape) {
    CityRect bounds = plan.land;
    TileIndex anchor = MapTileXY(city, bounds.left, bounds.top);
    int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    UpdateCityHighwayFootprint(city, anchor, plan.shape);
    for (int dy = 0; dy < height; ++dy) for (int dx = 0; dx < width; ++dx) {
      TileIndex tile = anchor + dy * city->map_width + dx;
      TileInfo *tp = &city->tile[tile];
      WriteTileZoneByte(tp, (tp->terrain & CITY_HIGHWAY_ORIENTATION_MASK) |
          (dx ? CITY_BUILDING_CONTINUE_X : 0) | (dy ? CITY_BUILDING_CONTINUE_Y : 0));
    }
    InvalidateTileRange(city, anchor, width, height);
  } else {
    const CityRect areas[] = {plan.land, plan.water};
    for (int part = 0; part < 2; ++part) {
      CityRect bounds = areas[part];
      int axis = part ? plan.span.direction & 1 : plan.axis;
      for (int y = bounds.top; y < bounds.bottom; ++y) for (int x = bounds.left; x < bounds.right; ++x) {
        TileIndex tile = MapTileXY(city, x, y);
        TileInfo *tp = &city->tile[tile];
        int kind = tp->kind;
        if (!part && IsTileKindHighwayAxis(kind) &&
            ReadCityHighwayAxis(tp) == axis) continue;
        InvalidateTile(city, tile);
        UpdateCityHighwayTile(city, tile, axis);
        WriteTileZoneByte(tp, (axis << CITY_HIGHWAY_ORIENTATION_SHIFT) |
            (axis && x > bounds.left ? CITY_BUILDING_CONTINUE_X : 0) |
            (!axis && y > bounds.top ? CITY_BUILDING_CONTINUE_Y : 0));
        InvalidateTile(city, tile);
      }
    }
  }
  for (int y = plan.land.top; y < plan.land.bottom; ++y) {
    TileIndex first = y * city->map_width + plan.land.left;
    for (TileIndex tile = first, end = first + plan.land.right - plan.land.left; tile < end; ++tile)
      RefreshAdjacentSurfaceConnections(city, tile);
  }
  if (plan.span.end != INVALID_TILE) {
    CityRect bounds = plan.water;
    int axis = plan.span.direction & 1;
    if (span_style == CITY_SPAN_REINFORCED_HIGHWAY)
      for (int lane = 0; lane < 2; ++lane) {
        TileIndex first = MapTileXY(city, bounds.left + (axis ? lane : 0), bounds.top + (axis ? 0 : lane));
        TileIndex last = MapTileXY(city, bounds.right - (axis ? 2 - lane : 1),
            bounds.bottom - (axis ? 1 : 2 - lane));
        StoreCityBridgeSpan(city, first, last, TK_HIGHWAY_BRIDGE_HEAD, HIGHWAY_BRIDGE_STYLE_REINFORCED);
      }
    RefreshCityHighwayConnections(city, plan.span.start);
    RefreshCityHighwayConnections(city, plan.span.end);
  }
  for (int y = plan.land.top; y < plan.land.bottom; ++y)
    for (int x = plan.land.left; x < plan.land.right; ++x)
      RefreshCityHighwayConnections(city, MapTileXY(city, x, y));
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  return cost;
}

// Place a highway line with explicit span, neighbor-connection and orientation choices. Rejects unsupported payload
// bits and endpoints; TEST quotes geometry and affordability without construction or UI selection reads.
i64 CmdPlaceTransportLine(City *city, u32 flags, TileIndex start,
    TileIndex end, int options, int reserved) {
  (void)reserved;
  int invalid = CITY_COMMAND_COST_INVALID;
  int span_style = options & 255;
  if (!IsMapTileValid(city, start) || !IsMapTileValid(city, end) ||
      options < 0 || (options & ~CITY_HIGHWAY_COMMAND_MASK) != 0 ||
      (options >> CITY_HIGHWAY_FACING_SHIFT) > 4 || span_style > CITY_ROAD_SPAN_CANCEL)
    return invalid;
  int orientation = (options >> CITY_HIGHWAY_FACING_SHIFT) - 1;
  if (IsCityCompactHighwayLine(city, start, end)) {
    i64 result = CmdBuildCityCompactHighway(city, flags, start, end, orientation,
        span_style == CITY_ROAD_SPAN_CANCEL ? CITY_SPAN_CANCEL : span_style);
    if (flags & CITY_COMMAND_TEST) return result;
    return result < 0 ? result : 1;
  }
  if (flags & CITY_COMMAND_TEST) {
    int styles = span_style == CITY_ROAD_SPAN_CANCEL ? 0 : 1 << span_style;
    CityCommandCost quote = GetCityInfrastructureCost(city, start, end, CITY_INFRASTRUCTURE_HIGHWAY, styles, NULL);
    if (options & 256) quote.cost += GetCityLineConnectionCost(city, start, end, CITY_INFRASTRUCTURE_HIGHWAY);
    return CityCommandPriceResult(quote);
  }

  return PlaceCityInfrastructureLine(city, CITY_INFRASTRUCTURE_HIGHWAY, start, end);
}

// Connect an eligible road cell to its neighboring highway and charge the onramp price. Rejects unused parameters and
// unsuitable tiles; TEST performs admission and affordability only.
i64 CmdPlaceHighwayOnramp(City *city, u32 flags, TileIndex tile,
    int reserved) {
  int invalid = CITY_COMMAND_COST_INVALID;
  if (!IsMapTileValid(city, tile) || reserved != 0)
    return invalid;
  if (flags & CITY_COMMAND_TEST)
    return CanPlaceCityOnramp(city, tile) ? CITY_ONRAMP_COST :
        CITY_COMMAND_COST_BLOCKED;
  return PlaceCityHighwayOnramp(city, tile);
}

// Advance the transport-line cursor after checking span, height, and water state. Requires initialized route state and
// valid cursor pointers; false means the traversal is finished or the next step is not admitted.
bool AdvanceCityHighwayLine(const City *city, CityConstructionState *state, int *x, int *y) {
  int current_y = *y;
  int current_x = *x;
  if (current_y == state->endpoint_y && current_x == state->endpoint_x)
    return false;

  bool fixed_direction = false;
  TileIndex current_tile = MapTileXYOrInvalid(city, current_x, current_y);
  int compatibility = GetTransportSpanCompatibilityMask(city, current_tile);
  if (compatibility == CITY_SPAN_COMPATIBILITY_INVALID)
    return false;
  if (compatibility == CITY_SPAN_COMPATIBILITY_ALL_DIRECTIONS) {
    const TileIndex footprint[] = {current_tile, current_tile + city->map_width,
        current_tile + city->map_width + 1, current_tile + 1};
    for (int cell = 0; cell < 4; ++cell) {
      TileIndex tile_index = footprint[cell];
      // Existing highway cells become part of the junction; they do not
      // constrain the new highway to the surface crossing orientation.
      if (CityHighwayCellAlreadyBuilt(city, tile_index)) continue;
      int tile = city->tile[tile_index].kind;
      int axis = ReadCityStraightLineAxis(city, tile_index);
      // A straight east/west power line does not constrain this cursor,
      // whether or not its tile carries a tower.
      if (IsTileKindPlainPowerLine(tile) ? axis != 0 : !IsTileKindLandscape(tile)) {
        bool tunnel = tile == TK_TUNNEL_ENTRANCE &&
            ReadTileVariant(&city->tile[tile_index]) + (state->direction & 1) == 1;
        if (axis != 1 - (state->direction & 1) && !tunnel) return false;
        fixed_direction = true;
      }
    }
  } else {
    fixed_direction = true;
  }

  int current_height = GetMaximumTerrainHeightIn2x2(city, current_tile);
  int next_y;
  int next_x;
  if (fixed_direction) {
    int direction = state->direction;
    next_y = current_y + 2 * kLineYStep[direction];
    next_x = current_x + 2 * kLineXStep[direction];
    if ((i32)state->min_y > next_y || (i32)state->min_x > next_x
        || (i32)state->max_y < next_y || (i32)state->max_x < next_x)
      return false;
    TileIndex next_tile = GetRelativeTileOrInvalid(city, current_tile, next_x - current_x, next_y - current_y);
    compatibility = GetTransportSpanCompatibilityMask(city, next_tile);
    int next_height = compatibility == CITY_SPAN_COMPATIBILITY_INVALID ? current_height :
        GetMaximumTerrainHeightIn2x2(city, next_tile);
    if (compatibility == CITY_SPAN_COMPATIBILITY_INVALID || current_height + 1 < next_height ||
        current_height - 1 > next_height)
      return false;
  } else {
    int direction = CityLineDirection(current_x, current_y, state->endpoint_x, state->endpoint_y);
    next_y = current_y + 2 * kLineYStep[direction];
    next_x = current_x + 2 * kLineXStep[direction];
    TileIndex next_tile = GetRelativeTileOrInvalid(city, current_tile, next_x - current_x, next_y - current_y);
    compatibility = GetTransportSpanCompatibilityMask(city, next_tile);
    int next_height = compatibility == CITY_SPAN_COMPATIBILITY_INVALID ? current_height :
        GetMaximumTerrainHeightIn2x2(city, next_tile);
    if (compatibility == CITY_SPAN_COMPATIBILITY_INVALID || current_height + 1 < next_height ||
        current_height - 1 > next_height) {
      direction = CityLineTerrainDirection(current_x, current_y, state->endpoint_x, state->endpoint_y);
      next_y = current_y + 2 * kLineYStep[direction];
      next_x = current_x + 2 * kLineXStep[direction];
      next_tile = GetRelativeTileOrInvalid(city, current_tile, next_x - current_x, next_y - current_y);
      compatibility = GetTransportSpanCompatibilityMask(city, next_tile);
      next_height = compatibility == CITY_SPAN_COMPATIBILITY_INVALID ? current_height :
          GetMaximumTerrainHeightIn2x2(city, next_tile);
      if (compatibility == CITY_SPAN_COMPATIBILITY_INVALID || current_height + 1 < next_height ||
          current_height - 1 > next_height)
        return false;
    }
    state->direction = direction;
  }

  for (int dy = 0; dy < 2; ++dy)
    for (int dx = 0; dx < 2; ++dx) {
      TileIndex next = MapTileXYOrInvalid(city, next_x + dx, next_y + dy);
      if (next != INVALID_TILE && HasTileWater(&city->tile[next]))
        return false;
    }

  for (int dy = 0; dy < 2; ++dy)
    for (int dx = 0; dx < 2; ++dx) {
      int tile_y = next_y + dy;
      int tile_x = next_x + dx;
      TileIndex tile = MapTileXY(city, tile_x, tile_y);
      if (city->tile[tile].kind == TK_RAIL &&
          ReadCityStraightLineAxis(city, tile) == (state->direction & 1))
        return false;
    }

  *y = next_y;
  *x = next_x;
  return true;
}

// Initialize endpoint, direction, and inclusive bounds for a transport line. Endpoints must be valid and state
// writable; initializes its inclusive bounds and current direction without placing cells.
void PrepareCityHighwayLine(const City *city, CityConstructionState *state, int x, int y, int end_x, int end_y) {
  state->endpoint_y = end_y - ((end_y - y) & 1);
  state->endpoint_x = end_x - ((end_x - x) & 1);
  state->direction = CityLineDirection(x, y, end_x, end_y);
  if (GetTransportSpanCompatibilityMask(city, MapTileXYOrInvalid(city, x, y)) == CITY_SPAN_COMPATIBILITY_INVALID) {
    state->endpoint_y = y;
    state->endpoint_x = x;
  }
  state->min_y = IntMin(end_y, y);
  state->max_y = IntMax(end_y, y);
  state->min_x = IntMin(x, end_x);
  state->max_x = IntMax(x, end_x);
}

// Test whether a proposed highway cell can reuse the existing footprint at no charge. Coordinates and construction
// state must describe the current route; this is a read-only admission check.
bool CityHighwayCellAlreadyBuilt(const City *city, TileIndex tile) {
  if (!(city->fixes & (1ull << CITY_FIX_REUSE_INFRASTRUCTURE))) return false;
  int surface = city->tile[tile].kind;
  return IsTileKindHighwayFootprint(surface);
}

// Each category occupies one nibble when four cells are combined with weights 1, 2, 4 and 8.
typedef enum CitySpanTerrainClass {
  CITY_SPAN_TERRAIN_SHORE = 1,
  CITY_SPAN_TERRAIN_WATER = 1 << 4,
  CITY_SPAN_TERRAIN_STEEP = 1 << 8,
  CITY_SPAN_TERRAIN_LAND = 1 << 12
} CitySpanTerrainClass;

// Classify decoded geometry for the legacy highway span search. Dry slopes and rapids are too steep; the result
// is a scan category, not a stored terrain code. Only the shape matters on dry land.
static CitySpanTerrainClass ReadHighwaySpanTerrainClass(const City *city, TileIndex tile) {
  CityTileGeometry geometry = ReadCityTileGeometry(city, tile);
  if (geometry.water == CITY_WATER_NONE)
    return geometry.land_shape == CITY_TERRAIN_FLAT ? CITY_SPAN_TERRAIN_LAND : CITY_SPAN_TERRAIN_STEEP;
  if (geometry.water_surface == CITY_WATER_SURFACE_RAPIDS) return CITY_SPAN_TERRAIN_STEEP;
  return geometry.water_surface == CITY_WATER_SURFACE_SHORE ? CITY_SPAN_TERRAIN_SHORE : CITY_SPAN_TERRAIN_WATER;
}

// Stock two-by-two shoreline classification and span search. Returns an unavailable span for rejected starts/banks;
// search reads native geometry without charging or reserving construction.
CityInfrastructureSpan FindCityHighwaySpan(const City *city, TileIndex origin_tile, int direction, bool choose_direction) {
  CityInfrastructureSpan span = {.start = origin_tile, .end = INVALID_TILE, .error_tile = INVALID_TILE,
      .direction = direction};
  if (!IsMapTileValid(city, origin_tile)) return span;
  static const u16 kDirectionMaskByTerrainClass[16] = {0, 6, 12, 4, 9, 0, 8, 12, 3, 2, 0, 6, 1, 3, 9, 0};
  TileIndex highway_start = CityHighwayPlacementAnchor(city, origin_tile);
  if (highway_start == INVALID_TILE) return span;
  CityPoint start_point = MapTilePt(city, highway_start);
  const TileIndex start_index = highway_start;
  const TileIndex start_east = MapTileXY(city, start_point.x + 1, start_point.y);
  const TileIndex start_south = MapTileXY(city, start_point.x, start_point.y + 1);
  const TileIndex start_southeast = MapTileXY(city, start_point.x + 1, start_point.y + 1);
  if (!IsTileKindLandscape(city->tile[start_index].kind) ||
      !IsTileKindLandscape(city->tile[start_east].kind) ||
      !IsTileKindLandscape(city->tile[start_south].kind) ||
      !IsTileKindLandscape(city->tile[start_southeast].kind)) {
    span.error = CITY_NOTICE_SHORELINE_NEEDS_CLEARING;
    return span;
  }

  direction &= 3;
  if (choose_direction) {
    int terrain_class = ReadHighwaySpanTerrainClass(city, start_index) +
        2 * ReadHighwaySpanTerrainClass(city, start_south) +
        4 * ReadHighwaySpanTerrainClass(city, start_southeast) +
        8 * ReadHighwaySpanTerrainClass(city, start_east);
    if ((terrain_class & 0x0F00) != 0) {
      span.error = CITY_NOTICE_SHORELINE_TOO_STEEP;
      return span;
    }
    if ((terrain_class & 0xFF00) != 0)
      terrain_class >>= 12;
    int directions = kDirectionMaskByTerrainClass[terrain_class & 15];
    static const i8 kDirectionPreference[4] = {0, 2, 1, -1};
    int candidate;
    for (candidate = 0; candidate < 4; ++candidate) {
      direction = (city->geography.rotation + kDirectionPreference[candidate]) & 3;
      if ((directions & (1 << direction)) != 0)
        break;
    }
    if (candidate == 4)
      return span;
  }

  int span_steps = 0;
  int span_y = start_point.y;
  int span_x = start_point.x;
  int step_y = kLineYStep[direction];
  int step_x = kLineXStep[direction];
  for (;;) {
    span_y += 2 * step_y;
    span_x += 2 * step_x;
    ++span_steps;
    const TileIndex span_index = MapTileXYOrInvalid(city, span_x, span_y);
    if (span_index == INVALID_TILE) {
      span.error = CITY_NOTICE_CANNOT_CROSS_OCEAN;
      return span;
    }
    const TileIndex span_east = MapTileXY(city, span_x + 1, span_y);
    const TileIndex span_south = MapTileXY(city, span_x, span_y + 1);
    const TileIndex span_southeast = MapTileXY(city, span_x + 1, span_y + 1);
    if (!IsTileKindLandscape(city->tile[span_index].kind) ||
        !IsTileKindLandscape(city->tile[span_east].kind) ||
        !IsTileKindLandscape(city->tile[span_south].kind) ||
        !IsTileKindLandscape(city->tile[span_southeast].kind)) {
      span.error = CITY_NOTICE_SPAN_BLOCKED_BY_CROSSING;
      return span;
    }

    int terrain_class = ReadHighwaySpanTerrainClass(city, span_index) +
        2 * ReadHighwaySpanTerrainClass(city, span_south) +
        4 * ReadHighwaySpanTerrainClass(city, span_southeast) +
        8 * ReadHighwaySpanTerrainClass(city, span_east);
    if ((terrain_class & 0x0F00) != 0) {
      span.error = CITY_NOTICE_OPPOSITE_SHORE_TOO_STEEP;
      return span;
    }
    if ((terrain_class & 0x00FF) != 0)
      continue;

    terrain_class = CITY_SPAN_STYLE_HIGHWAY_MASK;
    if (span_steps >= 3) {
      int start_height = GetMaximumTerrainHeightIn2x2(city, start_index);
      TileIndex near_tile = GetRelativeTileOrInvalid(city, start_index, -2 * step_x, -2 * step_y);
      if (near_tile != INVALID_TILE && GetRelativeTileOrInvalid(city, near_tile, 1, 1) != INVALID_TILE &&
          IsTileKindLandscape(city->tile[near_tile].kind) &&
          IsTileKindLandscape(city->tile[near_tile + 1].kind) &&
          IsTileKindLandscape(city->tile[near_tile + city->map_width].kind) &&
          IsTileKindLandscape(city->tile[near_tile + city->map_width + 1].kind)) {
        int near_height = GetMaximumTerrainHeightIn2x2(city, near_tile);
        int near_mask = GetTransportSpanCompatibilityMask(city, near_tile);
        if (near_mask != CITY_SPAN_COMPATIBILITY_INVALID && start_height <= near_height &&
            near_height <= start_height + 1 && (start_height != near_height || (near_mask & (1 << direction)) != 0)) {
          TileIndex far_tile = GetRelativeTileOrInvalid(city, start_index,
              2 * span_steps * step_x, 2 * span_steps * step_y);
          if (far_tile != INVALID_TILE && GetRelativeTileOrInvalid(city, far_tile, 1, 1) != INVALID_TILE &&
              IsTileKindLandscape(city->tile[far_tile].kind) &&
              IsTileKindLandscape(city->tile[far_tile + 1].kind) &&
              IsTileKindLandscape(city->tile[far_tile + city->map_width].kind) &&
              IsTileKindLandscape(city->tile[far_tile + city->map_width + 1].kind)) {
            int far_height = GetMaximumTerrainHeightIn2x2(city, far_tile);
            int far_mask = GetTransportSpanCompatibilityMask(city, far_tile);
            if (far_mask != CITY_SPAN_COMPATIBILITY_INVALID && start_height <= far_height &&
                far_height <= start_height + 1 &&
                (start_height != far_height || (far_mask & (1 << ((direction + 2) & 3))) != 0))
              terrain_class = CITY_SPAN_STYLE_HIGHWAY_BOTH_MASK;
          }
        }
      }
    }

    span.end = span_index;
    span.length = span_steps;
    span.direction = direction;
    span.styles = terrain_class;
    return span;
  }
}

// Native spans preserve their selected axis and accept aligned existing bank highways. The prepared reinforced deck
// still groups longitudinal cells in pairs. Requires explicit supported axis/options and initialized native map data;
// returns its plan/error without changing City.
static CityInfrastructureSpan FindCityNativeHighwaySpan(const City *city, TileIndex origin, int direction,
    bool choose_direction) {
  CityInfrastructureSpan span = {.start = origin, .end = INVALID_TILE, .error_tile = INVALID_TILE, .direction = direction};
  if (!IsMapTileValid(city, origin)) return span;
  CityPoint origin_point = MapTilePt(city, origin);
  if (choose_direction) {
    for (int candidate = 0; candidate < 4; ++candidate) {
      int next_direction = (direction + candidate) & 3;
      int axis = next_direction & 1;
      int bx = origin_point.x - kLineXStep[next_direction] + (next_direction == CITY_LINE_WEST);
      int by = origin_point.y - kLineYStep[next_direction] + (next_direction == CITY_LINE_NORTH);
      bool bank = true;
      for (int lane = 0; lane < 2; ++lane) {
        int nx = bx + (axis ? lane : 0), ny = by + (axis ? 0 : lane);
        TileIndex bank_tile = MapTileXYOrInvalid(city, nx, ny);
        if (bank_tile == INVALID_TILE || HasTileWater(&city->tile[bank_tile])) {
          bank = false; break;
        }
      }
      if (!bank) continue;
      span = FindCityNativeHighwaySpan(city, origin, next_direction, false);
      if (span.end != INVALID_TILE) return span;
    }
    return span;
  }
  int axis = direction & 1, water_height = -1, maximum_ground = 0, previous_height = -1;
  bool level_deck = true;
  int fx = origin_point.x + (direction == CITY_LINE_WEST), fy = origin_point.y + (direction == CITY_LINE_NORTH);
  for (int count = 0;; ++count) {
    int cx = fx + count * kLineXStep[direction], cy = fy + count * kLineYStep[direction];
    bool dry = true;
    int entry_height = -1, exit_height = -1;
    for (int lane = 0; lane < 2; ++lane) {
      int nx = cx + (axis ? lane : 0), ny = cy + (axis ? 0 : lane);
      TileIndex tile = MapTileXYOrInvalid(city, nx, ny);
      if (tile == INVALID_TILE) { span.error = CITY_NOTICE_CANNOT_CROSS_OCEAN; return span; }
      CityTileGeometry geometry = ReadCityTileGeometry(city, tile);
      const TileInfo *tp = &city->tile[tile];
      int kind = tp->kind;
      if (ReadCityZoneKind(tp) == ZONE_MILITARY ||
          kind == TK_RADIOACTIVE_WASTE || (!IsTileKindUnbuilt(kind) &&
              (kind != TK_HIGHWAY || ReadCityHighwayAxis(tp) != axis ||
                  geometry.water != CITY_WATER_NONE))) {
        span.error = CITY_NOTICE_SPAN_BLOCKED_BY_CROSSING;
        return span;
      }
      maximum_ground = IntMax(maximum_ground, geometry.land_height + (geometry.land_shape != 0));
      int near = CityHighwayGradeEdgeHeight(geometry, (direction + 2) & 3);
      int far = CityHighwayGradeEdgeHeight(geometry, direction);
      if (near < 0 || (entry_height >= 0 && (near != entry_height || far != exit_height))) return span;
      entry_height = near; exit_height = far;
      if (near != far) level_deck = false;
      if (geometry.water == CITY_WATER_NONE) continue;
      dry = false;
      if (geometry.water_surface == CITY_WATER_SURFACE_RAPIDS ||
          (water_height >= 0 && geometry.water_height != water_height)) {
        span.error = CITY_NOTICE_SHORELINE_TOO_STEEP;
        return span;
      }
      water_height = geometry.water_height;
    }
    if ((previous_height >= 0 && entry_height != previous_height) ||
        (water_height >= 0 && maximum_ground > water_height + 1)) {
      span.error = CITY_NOTICE_OPPOSITE_SHORE_TOO_STEEP;
      return span;
    }
    if (!dry || water_height < 0 || (count & 1)) { previous_height = exit_height; continue; }
    if (!count) return span;
    int ex = origin_point.x + (count - 2) * kLineXStep[direction];
    int ey = origin_point.y + (count - 2) * kLineYStep[direction];
    if (!CityHighwayEdgesAlign(city, (CityRect){IntMin(origin_point.x, ex), IntMin(origin_point.y, ey),
        IntMax(origin_point.x, ex) + 2, IntMax(origin_point.y, ey) + 2})) return span;
    span.end = MapTileXY(city, cx, cy);
    span.length = count / 2;
    span.styles = count >= 6 && level_deck ? CITY_SPAN_STYLE_HIGHWAY_BOTH_MASK : CITY_SPAN_STYLE_HIGHWAY_MASK;
    return span;
  }
}

// Validate and apply a two-lane highway crossing with an explicit direction-selection policy.
i64 PlaceCityHighwaySpan(City *city, TileIndex origin_tile, bool choose_direction) {
  assert(IsMapTileValid(city, origin_tile));
  CityInfrastructureSpan span = FindCityHighwaySpan(city, origin_tile, city->construction.direction, choose_direction);
  if (span.end == INVALID_TILE) {
    return span.error ? -span.error : CITY_COMMAND_COST_BLOCKED;
  }
  TileIndex highway_start = CityHighwayPlacementAnchor(city, origin_tile);
  if (highway_start == INVALID_TILE) return CITY_COMMAND_COST_BLOCKED;
  int direction = span.direction, span_steps = span.length, styles = span.styles;
  int step_y = kLineYStep[direction], step_x = kLineXStep[direction];
  static const i8 kFootprintX[4] = {0, 1, 1, 0};
  static const i8 kFootprintY[4] = {0, 0, 1, 1};
  int style = SelectInfrastructureSpan(city, span_steps, styles);
  if (style == CITY_SPAN_CANCEL) {
    CancelCityCommandFeedback(city);
    return CITY_COMMAND_COST_BLOCKED;
  }
  i64 affordable = AffordableCityCommandCost(city, (i64)span_steps * kSpanCostPerCell[style]);
  if (affordable < 0) return affordable;

  if (style == CITY_SPAN_REINFORCED_HIGHWAY) {
    int start_height = GetMaximumTerrainHeightIn2x2(city, highway_start);
    TileIndex near_tile = GetRelativeTileOrInvalid(city, highway_start, -2 * step_x, -2 * step_y);
    int near_height = GetMaximumTerrainHeightIn2x2(city, near_tile);
    int terrain_kind = start_height - near_height == -1 ? (direction & 1) : (((direction + 1) & 3) + 4);
    UpdateCityHighwayFootprint(city, near_tile, terrain_kind);
    TileIndex far_tile = GetRelativeTileOrInvalid(city, highway_start, 2 * span_steps * step_x, 2 * span_steps * step_y);
    int far_height = GetMaximumTerrainHeightIn2x2(city, far_tile);
    terrain_kind = start_height - far_height == -1 ? (direction & 1) : (((direction - 1) & 3) + 4);
    UpdateCityHighwayFootprint(city, far_tile, terrain_kind);
  }

  int stride = 2 * (step_y * city->map_width + step_x);
  TileIndex segment_tile = highway_start;
  for (int segment = 0; segment < span_steps; ++segment, segment_tile += stride) {
    if (style == CITY_SPAN_HIGHWAY || style == CITY_SPAN_REINFORCED_HIGHWAY) {
      InvalidateTileRange(city, segment_tile, 2, 2);
      for (int corner = 0; corner < 4; ++corner) {
        const TileIndex cell_index = segment_tile + kFootprintY[corner] * city->map_width + kFootprintX[corner];
        ReplaceCityTileAndStatistics(city, cell_index, TK_HIGHWAY);
        WriteCityHighwayOrientation(&city->tile[cell_index], direction & 1);
      }
      InitializeCityHighwayFootprint(city, segment_tile);
      InvalidateTileRange(city, segment_tile, 2, 2);
    }
  }
  if (style == CITY_SPAN_HIGHWAY || style == CITY_SPAN_REINFORCED_HIGHWAY) {
    for (int lane = 0; lane < 2; ++lane) {
      int axis = direction & 1;
      TileIndex end = highway_start + (span_steps - 1) * stride;
      int lane_offset = axis ? lane : lane * city->map_width;
      TileIndex first = IntMin(highway_start, end) + lane_offset;
      TileIndex last = IntMax(highway_start, end) + lane_offset + (axis ? city->map_width : 1);
      StoreCityBridgeSpan(city, first, last,
          TK_HIGHWAY_BRIDGE_HEAD, HIGHWAY_BRIDGE_STYLE_STANDARD);
    }
  }
  return true;
}

// The native frontend and retained pointer adapters enter the same command.
static i64 PlaceCityNativeTransportLine(City *city, TileIndex origin_tile) {
  assert(IsMapTileValid(city, origin_tile));
  TileIndex end = city->command_choices.endpoint_supplied ?
      PrepareCityLineEndpoint(city, origin_tile, city->command_choices.end, 25, true) :
      TrackCityMapZonePlacementDrag(city->game, origin_tile, 25);
  CityCompactHighwayPlan plan;
  if (!PlanCityCompactHighway(city, origin_tile, end, -1, &plan) || plan.cost < 0) {
    return CITY_COMMAND_COST_BLOCKED;
  }
  int style = plan.span.end == INVALID_TILE ? CITY_SPAN_CANCEL :
      SelectInfrastructureSpan(city, plan.span.length, plan.span.styles);
  if (!PlanCityCompactHighway(city, origin_tile, end, -1, &plan)) return CITY_COMMAND_COST_BLOCKED;
  i64 cost = CityCompactHighwayCost(&plan, style);
  if (cost < 0) return cost;
  i64 payment = PayCityCommandCost(city, cost);
  if (payment < 0) return payment;
  i64 result = CmdBuildCityCompactHighway(city, 0, origin_tile, end, -1, style);
  return result < 0 ? result : 1;
}

// Place a legacy 2x2 transport line, continuing along the shoreline where needed.
i64 PlaceCityLegacyTransportLine(City *city, TileIndex origin_tile) {
  assert(IsMapTileValid(city, origin_tile));
  TileIndex highway_start = CityHighwayPlacementAnchor(city, origin_tile);
  if (highway_start == INVALID_TILE) return CITY_COMMAND_COST_BLOCKED;
  CityPoint start_point = MapTilePt(city, highway_start);
  if (CityHighwayFootprintHasWater(city, highway_start))
    return PlaceCityHighwaySpan(city, highway_start, true);
  if (GetTransportSpanCompatibilityMask(city, highway_start) == CITY_SPAN_COMPATIBILITY_INVALID)
    return CITY_COMMAND_COST_BLOCKED;

  TileIndex endpoint = city->command_choices.endpoint_supplied ?
      PrepareCityLineEndpoint(city, highway_start, city->command_choices.end, 25, true) :
      TrackCityMapZonePlacementDrag(city->game, highway_start, 25);
  if (endpoint == INVALID_TILE)
    return CITY_COMMAND_COST_BLOCKED;
  CityPoint end_point = MapTilePt(city, endpoint);
  i64 payment = PayCityLineCost(city, true);
  if (payment < 0) return payment;
  if (payment == CITY_LINE_PAYMENT_REUSED) return true;

  PrepareCityHighwayLine(city, &city->construction, start_point.x, start_point.y, end_point.x, end_point.y);
  int current_y = start_point.y;
  int current_x = start_point.x;
  int next_y = start_point.y;
  int next_x = start_point.x;
  TileIndex current_tile = highway_start;
  while (AdvanceCityHighwayLine(city, &city->construction, &next_x, &next_y)) {
    ClearZoneStateAndUpdateTransportSpanNeighbors(city, current_tile);
    current_y = next_y;
    current_x = next_x;
    current_tile = MapTileXY(city, current_x, current_y);
  }
  ClearZoneStateAndUpdateTransportSpanNeighbors(city, current_tile);

  int direction = city->construction.direction;
  int follow_y = current_y + 2 * kLineYStep[direction];
  int follow_x = current_x + 2 * kLineXStep[direction];
  TileIndex follow = MapTileXYOrInvalid(city, follow_x, follow_y);
  if (CityHighwayFootprintHasWater(city, follow))
    return PlaceCityHighwaySpan(city, follow, false);

  if (FindCityBorderConnection(city, highway_start, current_tile, direction, true) != INVALID_TILE &&
      AffordableCityCommandCost(city, 1500) >= 0 &&
      ChooseCityConnectionQuestion(city, CITY_BORDER_CONNECTION_HIGHWAY_OR_TUNNEL, 1500) == CITY_DIALOG_YES) {
    DebitCityCommandCost(city, 1500);
    WriteOverlay(city, current_tile, CITY_OVERLAY_BORDER_CONNECTION);
    ++city->infrastructure.service_bonus_a;
    InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
    RefreshCityMapTiles(city);
  }
  city->construction.direction = CITY_LINE_WEST;
  return true;
}

// Placement mode is selected once; native failures never fall through to stock placement.
i64 PlaceCityTransportLine(City *city, TileIndex origin_tile) {
  assert(IsMapTileValid(city, origin_tile));
  return city->fixes & (1ull << CITY_FIX_COMPACT_HIGHWAYS) ? PlaceCityNativeTransportLine(city, origin_tile) :
      PlaceCityLegacyTransportLine(city, origin_tile);
}

// Classify a two-cell transport span and its orientation. Requires a complete valid two-by-two footprint; returns the
// stock compatibility class without altering native records.
CityTransportFootprint ClassifyTransportSpanTile(const City *city, TileIndex tile) {
  if (tile == INVALID_TILE) return CITY_TRANSPORT_FOOTPRINT_ROTATION_0;
  const TileInfo *tp = &city->tile[tile];
  int kind = tp->kind;
  if (IsTileKindHighwayFootprint(kind) || IsTileKindHighwayBridge(kind)) {
    TileIndex anchor = CityHighwayAnchor(city, tile);
    if (anchor == INVALID_TILE) return CITY_TRANSPORT_FOOTPRINT_NONE;
    tile = anchor;
    tp = &city->tile[tile];
    kind = tp->kind;
  }
  if (IsTileKindHighwayBridge(kind)) return ReadCityBridgeAxis(tp) ?
      CITY_TRANSPORT_FOOTPRINT_ROTATION_1 : CITY_TRANSPORT_FOOTPRINT_ROTATION_0;
  if (!IsTileKindHighwayFootprint(kind))
    return CITY_TRANSPORT_FOOTPRINT_NONE;
  if (IsTileKindStraightHighwayFamily(kind) ||
      ReadFootprintCorners(city, tile) == CITY_FOOTPRINT_CORNERS_ALL) {
    if (IsTileKindStraightHighwayFamily(kind) && kind != TK_HIGHWAY)
      return CITY_TRANSPORT_FOOTPRINT_EW - ReadCityHighwayAxis(tp);
    if (HasTileWater(tp))
      return CITY_TRANSPORT_FOOTPRINT_BRIDGE_13 + ReadCityHighwayAxis(tp);
    CityRect bounds = CityHighwayFootprintBounds(city, tile);
    if (bounds.right - bounds.left != bounds.bottom - bounds.top)
      return CITY_TRANSPORT_FOOTPRINT_FORCE_EW - ReadCityHighwayAxis(tp);
    if (GetRelativeTileOrInvalid(city, tile, 1, 1) == INVALID_TILE)
      return CITY_TRANSPORT_FOOTPRINT_NONE;
    const TileIndex neighbors[] = {tile + city->map_width, tile + city->map_width + 1, tile + 1};
    int adjacent = 0;
    for (int i = 0; i < 3; ++i) {
      TileIndex neighbor = neighbors[i];
      const TileInfo *np = &city->tile[neighbor];
      adjacent = np->kind;
      if (IsTileKindStraightHighwayFamily(adjacent) && adjacent != TK_HIGHWAY)
        return CITY_TRANSPORT_FOOTPRINT_EW - ReadCityHighwayAxis(np);
      if (HasTileWater(np))
        return CITY_TRANSPORT_FOOTPRINT_BRIDGE_13 + ReadCityHighwayAxis(np);
    }
    if (adjacent == TK_HIGHWAY)
      return CITY_TRANSPORT_FOOTPRINT_FORCE_EW - ReadCityHighwayAxis(&city->tile[neighbors[2]]);
    return CITY_TRANSPORT_FOOTPRINT_NONE;
  }
  return ReadCityHighwayFootprintShape(tp);
}

// Return the greatest terrain level in a 2x2 cell footprint. Requires IsMapTileValid(city, tile) &&
// GetRelativeTileOrInvalid(city, tile, 1, 1) != INVALID_TILE; violations are programming errors.
int GetMaximumTerrainHeightIn2x2(const City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile) && GetRelativeTileOrInvalid(city, tile, 1, 1) != INVALID_TILE);
  const TileIndex cells[] = {tile, tile + city->map_width, tile + city->map_width + 1, tile + 1};
  int maximum = 0;
  for (int corner = 0; corner < 4; ++corner) {
    TileIndex cell = cells[corner];
    const TileInfo *tp = &city->tile[cell];
    int level = ReadCityLandHeight(tp);
    // Sloped or wet tiles contribute one level above their stored land height.
    if (!IsFlatDryTile(tp)) ++level;
    maximum = IntMax(maximum, level);
  }
  return maximum;
}

// Return the directions compatible with a transport 2x2 footprint. Requires valid two-by-two geometry; returns a
// direction mask and does not select or place a span.
int GetTransportSpanCompatibilityMask(const City *city, TileIndex tile_index) {
  if (tile_index == INVALID_TILE || GetRelativeTileOrInvalid(city, tile_index, 1, 1) == INVALID_TILE)
    return CITY_SPAN_COMPATIBILITY_INVALID;
  CityPoint point = MapTilePt(city, tile_index);
  int x = point.x, y = point.y;
  const TileIndex cells[] = {tile_index, tile_index + 1, tile_index + city->map_width,
      tile_index + city->map_width + 1};
  int corner_group[CITY_TRANSPORT_TERRAIN_CLASS_COUNT] = {0};
  int first_terrain = CITY_TERRAIN_FLAT;
  static const u8 corner_bit[4] = {
      CITY_FOOTPRINT_CORNER_NW, CITY_FOOTPRINT_CORNER_NE, CITY_FOOTPRINT_CORNER_SW, CITY_FOOTPRINT_CORNER_SE};
  static const u8 height_corner[4] = {
      CITY_HEIGHT_CORNER_NW, CITY_HEIGHT_CORNER_NE, CITY_HEIGHT_CORNER_SW, CITY_HEIGHT_CORNER_SE};

  for (int corner = 0; corner < 4; ++corner) {
    TileIndex cell_index = cells[corner];
    const TileInfo *tp = &city->tile[cell_index];
    int tile = tp->kind;
    if (IsTileKindHighwayBridge(tile)) {
      if (CityHighwayAnchor(city, cell_index) != tile_index) return CITY_SPAN_COMPATIBILITY_INVALID;
    } else if (IsTileKindHighwayFootprint(tile)) {
      CityRect bounds = CityHighwayFootprintBounds(city, cell_index);
      if (bounds.left < x || bounds.top < y || bounds.right > x + 2 || bounds.bottom > y + 2)
        return CITY_SPAN_COMPATIBILITY_INVALID;
    } else {
      if (tile == TK_PARK || tile == TK_RADIOACTIVE_WASTE || IsTileKindBridge(tile) ||
          (IsTileKindPlainTransport(tile) && ReadCityStraightLineAxis(city, cell_index) < 0) ||
          IsTileKindTunnel(tile) || IsTileKindGroundCrossing(tile) ||
          tile == TK_HIGHWAY_ENTRANCE || tile == TK_SUBWAY_TO_RAIL ||
          IsTileKindBuilding(tile) || IsTileKindWaterPowerLine(tp->kind))
        return CITY_SPAN_COMPATIBILITY_INVALID;
    }
    int terrain_code = HasTileWater(tp) ?
        CITY_TERRAIN_FLAT : ReadLegacyTerrainCode(city, cell_index);
    if (!corner) first_terrain = terrain_code;
    u32 group = kTransportTerrainClass[terrain_code];
    if (group < CITY_TRANSPORT_TERRAIN_CLASS_COUNT)
      corner_group[group] |= corner_bit[corner];
  }

  int mixed_groups = corner_group[CITY_TRANSPORT_TERRAIN_CLASS_1] | corner_group[CITY_TRANSPORT_TERRAIN_CLASS_2] |
      corner_group[CITY_TRANSPORT_TERRAIN_CLASS_3] | corner_group[CITY_TRANSPORT_TERRAIN_CLASS_4];
  if (mixed_groups == 0)
    return CITY_SPAN_COMPATIBILITY_ALL_DIRECTIONS;
  int diagonal_groups = corner_group[CITY_TRANSPORT_TERRAIN_CLASS_1] | corner_group[CITY_TRANSPORT_TERRAIN_CLASS_3];

  int northwest = ReadCityLandHeight(&city->tile[tile_index]);
  int northeast = ReadCityLandHeight(&city->tile[cells[1]]);
  int southwest = ReadCityLandHeight(&city->tile[cells[2]]);
  int southeast = ReadCityLandHeight(&city->tile[cells[3]]);
  if (southeast < northwest && (mixed_groups & CITY_FOOTPRINT_CORNER_NW) != 0)
    return CITY_SPAN_COMPATIBILITY_INVALID;
  if (southeast > northwest && (mixed_groups & CITY_FOOTPRINT_CORNER_SE) != 0)
    return CITY_SPAN_COMPATIBILITY_INVALID;
  if (northeast < southwest && (mixed_groups & CITY_FOOTPRINT_CORNER_SW) != 0)
    return CITY_SPAN_COMPATIBILITY_INVALID;
  if (northeast > southwest && (mixed_groups & CITY_FOOTPRINT_CORNER_NE) != 0)
    return CITY_SPAN_COMPATIBILITY_INVALID;

  int minimum = northwest;
  minimum = IntMin(minimum, southwest);
  minimum = IntMin(minimum, southeast);
  minimum = IntMin(minimum, northeast);
  int lower_corners = 0;
  if (minimum < northwest)
    lower_corners |= height_corner[0];
  if (minimum < northeast)
    lower_corners |= height_corner[1];
  if (minimum < southwest)
    lower_corners |= height_corner[2];
  if (minimum < southeast)
    lower_corners |= height_corner[3];
  if ((CITY_HEIGHT_CORNERS_ALL - lower_corners) == corner_group[CITY_TRANSPORT_TERRAIN_CLASS_4])
    return CITY_SPAN_COMPATIBILITY_SPECIAL_TERRAIN;

  // West, south, east, north. Footprint and height masks use different bit orders.
  static const struct { u8 flat, lower, opposite; } kDirections[4] = {
    {CITY_FOOTPRINT_CORNERS_NORTH, CITY_HEIGHT_CORNERS_NW_SE, CITY_FOOTPRINT_CORNERS_SOUTH},
    {CITY_FOOTPRINT_CORNERS_WEST, CITY_HEIGHT_CORNERS_NORTH, CITY_FOOTPRINT_CORNERS_EAST},
    {CITY_FOOTPRINT_CORNERS_SOUTH, CITY_HEIGHT_CORNERS_NE_SW, CITY_FOOTPRINT_CORNERS_NORTH},
    {CITY_FOOTPRINT_CORNERS_EAST, CITY_HEIGHT_CORNERS_SOUTH, CITY_FOOTPRINT_CORNERS_WEST}
  };
  if (!lower_corners && first_terrain == CITY_TERRAIN_SHAPE_13)
    return CITY_SPAN_COMPATIBILITY_SPECIAL_TERRAIN;
  int result = 0;
  for (int direction = 0; direction < 4; ++direction) {
    if (!lower_corners && diagonal_groups && !(diagonal_groups & ~kDirections[direction].flat))
      result |= 1 << direction;
    if (lower_corners && !(lower_corners & ~kDirections[direction].lower) &&
        (diagonal_groups & kDirections[direction].opposite) == kDirections[direction].opposite)
      result |= 1 << direction;
  }

  if (mixed_groups == CITY_FOOTPRINT_CORNERS_WITHOUT_NE) {
    if ((corner_group[CITY_TRANSPORT_TERRAIN_CLASS_3] & CITY_FOOTPRINT_CORNER_NW) != 0)
      result |= CONN_EAST;
    if ((corner_group[CITY_TRANSPORT_TERRAIN_CLASS_3] & CITY_FOOTPRINT_CORNER_SE) != 0)
      result |= CONN_SOUTH;
  } else if (mixed_groups == CITY_FOOTPRINT_CORNERS_WITHOUT_SE) {
    if ((corner_group[CITY_TRANSPORT_TERRAIN_CLASS_3] & CITY_FOOTPRINT_CORNER_NE) != 0)
      result |= CONN_SOUTH;
    if ((corner_group[CITY_TRANSPORT_TERRAIN_CLASS_3] & CITY_FOOTPRINT_CORNER_SW) != 0)
      result |= CONN_WEST;
  } else if (mixed_groups == CITY_FOOTPRINT_CORNERS_WITHOUT_SW) {
    if ((corner_group[CITY_TRANSPORT_TERRAIN_CLASS_3] & CITY_FOOTPRINT_CORNER_SE) != 0)
      result |= CONN_WEST;
    if ((corner_group[CITY_TRANSPORT_TERRAIN_CLASS_3] & CITY_FOOTPRINT_CORNER_NW) != 0)
      result |= CONN_NORTH;
  } else if (mixed_groups == CITY_FOOTPRINT_CORNERS_WITHOUT_NW) {
    if ((corner_group[CITY_TRANSPORT_TERRAIN_CLASS_3] & CITY_FOOTPRINT_CORNER_NE) != 0)
      result |= CONN_EAST;
    if ((corner_group[CITY_TRANSPORT_TERRAIN_CLASS_3] & CITY_FOOTPRINT_CORNER_SW) != 0)
      return result | CONN_NORTH;
  }
  return result;
}

// Choose the direction that keeps a transport span compatible. Requires an admitted footprint and candidate direction;
// returns the compatible direction or the routine's rejection value.
int ResolveTransportSpanDirection(const City *city, TileIndex tile) {
  if (tile == INVALID_TILE)
    return CITY_TRANSPORT_FOOTPRINT_NONE;
  int span = ClassifyTransportSpanTile(city, tile);
  if (span == CITY_TRANSPORT_FOOTPRINT_NS || span == CITY_TRANSPORT_FOOTPRINT_EW ||
      (span >= CITY_TRANSPORT_FOOTPRINT_GRADED_4 && span <= CITY_TRANSPORT_FOOTPRINT_GRADED_7) ||
      span >= CITY_TRANSPORT_FOOTPRINT_BRIDGE_13)
    return CITY_TRANSPORT_FOOTPRINT_NONE;

  int neighbors = 0;
  int center_height = GetMaximumTerrainHeightIn2x2(city, tile);
  static const struct { u8 grade, higher, lower; } kNeighbors[4] = {
    {CITY_TRANSPORT_FOOTPRINT_GRADED_5, 0x10, 0x40},
    {CITY_TRANSPORT_FOOTPRINT_GRADED_6, 0x20, 0x80},
    {CITY_TRANSPORT_FOOTPRINT_GRADED_7, 0x40, 0x10},
    {CITY_TRANSPORT_FOOTPRINT_GRADED_4, 0x80, 0x20}
  };
  for (int direction = 0; direction < 4; ++direction) {
    TileIndex neighbor = GetRelativeTileOrInvalid(city, tile,
        2 * kLineXStep[direction], 2 * kLineYStep[direction]);
    if (neighbor == INVALID_TILE) continue;
    int adjacent = ClassifyTransportSpanTile(city, neighbor);
    int axis = direction & 1;
    int straight = axis ? CITY_TRANSPORT_FOOTPRINT_NS : CITY_TRANSPORT_FOOTPRINT_EW;
    int forced = axis ? CITY_TRANSPORT_FOOTPRINT_FORCE_NS : CITY_TRANSPORT_FOOTPRINT_FORCE_EW;
    int perpendicular = axis ? CITY_TRANSPORT_FOOTPRINT_FORCE_EW : CITY_TRANSPORT_FOOTPRINT_FORCE_NS;
    const u8 *classes = axis ? kTransportSpanNorthClass : kTransportSpanSouthClass;
    if (adjacent == straight || adjacent == forced ||
        (adjacent >= CITY_TRANSPORT_FOOTPRINT_GRADED_4 && classes[adjacent] != 0) ||
        (adjacent == perpendicular &&
            GetTransportSpanCompatibilityMask(city, neighbor) != CITY_SPAN_COMPATIBILITY_SPECIAL_TERRAIN)) {
      neighbors |= 1 << direction;
      int adjacent_height = GetMaximumTerrainHeightIn2x2(city, neighbor);
      if (adjacent_height > center_height && adjacent != kNeighbors[direction].grade)
        neighbors |= kNeighbors[direction].higher;
      if (adjacent_height < center_height ||
          (adjacent_height == center_height && adjacent == kNeighbors[direction].grade))
        neighbors |= kNeighbors[direction].lower;
    }
  }

  if (ReadOverlay(city, tile) == CITY_OVERLAY_BORDER_CONNECTION) {
    CityPoint point = MapTilePt(city, tile);
    int x = point.x, y = point.y;
    if (y < 2)
      neighbors |= 8;
    if (y >= city->map_height - 2)
      neighbors |= 2;
    if (x < 2)
      neighbors |= 1;
    if (x >= city->map_width - 2)
      neighbors |= 4;
  }

  int compatibility = GetTransportSpanCompatibilityMask(city, tile);
  if (span == CITY_TRANSPORT_FOOTPRINT_FORCE_NS || span == CITY_TRANSPORT_FOOTPRINT_FORCE_EW) {
    if (neighbors == 1 || neighbors == 4)
      return CITY_TRANSPORT_FOOTPRINT_FORCE_NS;
    if (neighbors == 2 || neighbors == 8)
      return CITY_TRANSPORT_FOOTPRINT_FORCE_EW;
    if (compatibility == CITY_SPAN_COMPATIBILITY_SPECIAL_TERRAIN)
      return CITY_TRANSPORT_FOOTPRINT_NONE;
  }
  if (neighbors == 0) {
    if (compatibility == CITY_SPAN_COMPATIBILITY_ALL_DIRECTIONS)
      return ((city->construction.direction & 1) + 2);
    if ((compatibility & 2) != 0)
      return 5;
    if ((compatibility & 8) != 0)
      return 7;
    if ((compatibility & 1) != 0)
      return 4;
    if ((compatibility & 4) != 0)
      return 6;
  }
  if ((neighbors & 0x10) != 0 && (compatibility & 2) != 0)
    return 5;
  if ((neighbors & 0x40) != 0 && (compatibility & 8) != 0)
    return 7;
  if ((neighbors & 0x80) != 0 && (compatibility & 1) != 0)
    return 4;
  if ((neighbors & 0x20) != 0 && (compatibility & 4) != 0)
    return 6;
  if (compatibility == CITY_SPAN_COMPATIBILITY_ALL_DIRECTIONS)
    return kTransportSpanDirectionByMask[neighbors & 15];
  if ((neighbors & 1) != 0 && (compatibility & 2) != 0)
    return 5;
  if ((neighbors & 4) != 0 && (compatibility & 8) != 0)
    return 7;
  if ((neighbors & 8) != 0 && (compatibility & 1) != 0)
    return 4;
  if ((neighbors & 2) != 0 && (compatibility & 4) != 0)
    return 6;
  return kTransportSpanDirectionByComplement[neighbors & 15];
}

typedef struct CityOnrampConnections {
  TileIndex neighbors[4];
  u8 highways, roads;
} CityOnrampConnections;

// Inspect each neighboring cell once. Masks and neighbor slots use west/south/east/north connection order.
static CityOnrampConnections FindCityOnrampConnections(const City *city, TileIndex tile) {
  static const u8 kCompatibleRoads[16] = {0, 10, 5, 12, 10, 0, 9, 0, 5, 6, 0, 0, 3, 0, 0, 0};
  CityOnrampConnections result = {0};
  for (int direction = 0; direction < 4; ++direction) {
    TileIndex neighbor = GetRelativeTileOrInvalid(city, tile, kLineXStep[direction], kLineYStep[direction]);
    result.neighbors[direction] = neighbor;
    if (neighbor == INVALID_TILE) continue;
    int kind = city->tile[neighbor].kind;
    if (IsTileKindStraightHighway(kind)) result.highways |= 1 << direction;
    if (kind == TK_ROAD) result.roads |= 1 << direction;
  }
  result.roads &= kCompatibleRoads[result.highways];
  return result;
}

bool CanPlaceCityOnramp(const City *city, TileIndex tile) {
  if (!IsMapTileValid(city, tile)) return false;
  const TileInfo *tp = &city->tile[tile];
  if (!IsTileKindLandscape(tp->kind) || tp->kind == TK_RADIOACTIVE_WASTE || !IsFlatDryTile(tp)) return false;
  return FindCityOnrampConnections(city, tile).roads != 0;
}

// Join an ordinary road to an adjacent highway. Requires a valid tile and returns success or a negative gameplay
// error. Road selection keeps west/south/east/north priority.
static i64 PlaceCityHighwayOnramp(City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile));
  const TileInfo *tp = &city->tile[tile];
  if (!IsTileKindLandscape(tp->kind) || tp->kind == TK_RADIOACTIVE_WASTE ||
      !IsFlatDryTile(tp))
    return CITY_COMMAND_COST_BLOCKED;
  CityOnrampConnections connections = FindCityOnrampConnections(city, tile);
  int highway_mask = connections.highways, onramp_mask = connections.roads;
  if (!onramp_mask) return CITY_COMMAND_COST_BLOCKED;
  TileIndex tile_west = connections.neighbors[CITY_LINE_WEST];
  TileIndex tile_south = connections.neighbors[CITY_LINE_SOUTH];
  TileIndex tile_east = connections.neighbors[CITY_LINE_EAST];
  TileIndex tile_north = connections.neighbors[CITY_LINE_NORTH];
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);

  TileIndex road_tile;
  bool mirrored = false;
  if ((onramp_mask & CONN_WEST) != 0) {
    mirrored = true;
    road_tile = tile_west;
    ReplaceCityTileVariant(city, tile, TK_HIGHWAY_ENTRANCE,
        (highway_mask & CONN_SOUTH) != 0 ? 2 : 1);
  } else if ((onramp_mask & CONN_SOUTH) != 0) {
    road_tile = tile_south;
    ReplaceCityTileVariant(city, tile, TK_HIGHWAY_ENTRANCE,
        (highway_mask & CONN_WEST) != 0 ? 0 : 3);
  } else if ((onramp_mask & CONN_EAST) != 0) {
    road_tile = tile_east;
    mirrored = true;
    ReplaceCityTileVariant(city, tile, TK_HIGHWAY_ENTRANCE,
        (highway_mask & CONN_SOUTH) != 0 ? 3 : 0);
  } else {
    road_tile = tile_north;
    ReplaceCityTileVariant(city, tile, TK_HIGHWAY_ENTRANCE,
        (highway_mask & CONN_WEST) != 0 ? 1 : 2);
  }
  if (mirrored)
    WriteTileZoneByte(&city->tile[tile],
        ReadTileZoneByte(&city->tile[tile]) | CITY_HIGHWAY_RAMP_MIRROR_MASK);
  if (city->fixes & (1ull << CITY_FIX_RAMP_CONNECTIONS))
    UpdateRoadConnections(city, road_tile);
  else {
    ReplaceCityTileAndStatistics(city, road_tile, TK_ROAD);
    WriteTransportConnections(&city->tile[road_tile], CONN_ALL);
  }
  for (int direction = 0; direction < 4; ++direction)
    if (highway_mask & (1 << direction)) InvalidateTile(city, connections.neighbors[direction]);
  InvalidateTile(city, tile);
  return true;
}

// Linked two-by-two highway spans are removed from the near shore to the far shore. Rebuild each shore at its
// original point in that traversal; native stored bridges are handled separately by RemoveCityNativeBridge.
void RemoveCityLinkedHighwaySpan(City *city, TileIndex tile_index, TileIndex anchor, int kind, bool animate) {
  CityPoint anchor_point = MapTilePt(city, anchor);
  int orientation = ClassifyTransportSpanTile(city, anchor);
  int step_y = (orientation & 1) == 0 ? 2 : 0;
  int step_x = step_y == 0 ? 2 : 0;
  int linked_y = anchor_point.y;
  int linked_x = anchor_point.x;
  TileIndex linked_index = anchor;
  while (ClassifyTransportSpanTile(city, linked_index) >= CITY_TRANSPORT_FOOTPRINT_BRIDGE_13) {
    linked_y -= step_y;
    linked_x -= step_x;
    linked_index = MapTileXY(city, linked_x, linked_y);
  }
  for (int endpoint = 0; endpoint < 2; ++endpoint) {
    if (kind != TK_HIGHWAY) {
      InvalidateTileRange(city, linked_index, 2, 2);
      RecalculateTerrainCell(city, linked_index);
      RecalculateTerrainCell(city, GetRelativeTileOrInvalid(city, linked_index, 0, 1));
      RecalculateTerrainCell(city, GetRelativeTileOrInvalid(city, linked_index, 1, 1));
      RecalculateTerrainCell(city, GetRelativeTileOrInvalid(city, linked_index, 1, 0));
      InvalidateTileRange(city, linked_index, 2, 2);
    }
    if (endpoint != 0)
      break;
    linked_y += step_y;
    linked_x += step_x;
    linked_index = MapTileXY(city, linked_x, linked_y);
    if (animate) PlayCitySound(city, tile_index, CITY_SOUND_EXPLOSION);
    for (;;) {
      int span = ClassifyTransportSpanTile(city, linked_index);
      if (span < CITY_TRANSPORT_FOOTPRINT_BRIDGE_13) break;
      if (span < CITY_TRANSPORT_FOOTPRINT_ROTATION_0)
        InvalidateTileRange(city, linked_index, 2, 2);
      else
        InvalidateTile(city, linked_index);

      static const i8 kCellX[4] = {0, 1, 1, 0};
      static const i8 kCellY[4] = {0, 0, 1, 1};
      for (int cell = 0; cell < 4; ++cell) {
        int cell_y = linked_y + kCellY[cell];
        int cell_x = linked_x + kCellX[cell];
        const TileIndex cell_index = MapTileXY(city, cell_x, cell_y);
        TileInfo *tp = &city->tile[cell_index];
        ReplaceCityTileAndStatistics(city, cell_index, ReadCityWaterGroundKind(tp));
        if (animate) ShowCityDemolitionEffect(city, cell_index, false);
        WriteTileZoneByte(tp, ReadTileZoneByte(tp) & CITY_ZONE_KIND_MASK);
      }
      linked_y += step_y;
      linked_x += step_x;
      linked_index = MapTileXY(city, linked_x, linked_y);
    }
  }
}

// Rebuild rectangular compact carriageways row by row from their northeast corner. Square footprints keep their
// northeast/southeast/southwest/northwest order; each terrain edit observes the previous edits.
void RecalculateCityHighwayFootprintTerrain(City *city, CityTileArea footprint) {
  TileIndex anchor = footprint.start + footprint.width - 1;
  if (footprint.width != footprint.height) {
    for (int dy = 0; dy < footprint.height; ++dy) for (int dx = 0; dx < footprint.width; ++dx)
      RecalculateTerrainCell(city, GetRelativeTileOrInvalid(city, anchor, -dx, dy));
  } else {
    RecalculateTerrainCell(city, anchor);
    RecalculateTerrainCell(city, GetRelativeTileOrInvalid(city, anchor, 0, 1));
    RecalculateTerrainCell(city, GetRelativeTileOrInvalid(city, anchor, -1, 1));
    RecalculateTerrainCell(city, GetRelativeTileOrInvalid(city, anchor, -1, 0));
  }
}

// Opposite bends join along these camera-relative edges of their two-by-two footprints.
static const i8 kHighwayCurveNeighbors[4][2][2] = {
    {{-1, 0}, {0, 1}}, {{0, 1}, {1, 0}}, {{1, 0}, {0, -1}}, {{0, -1}, {-1, 0}}};

int CityHighwayRampView(const City *city, TileIndex tile, int rotation) {
  const TileInfo *tp = &city->tile[tile];
  int variant = ReadTileVariant(tp);
  bool mirrored = ReadCityHighwayRampMirror(tp);
  // Decode the two physical connections, then turn both into camera space.
  int road = variant == 0 || variant == 3 ? CITY_LINE_SOUTH : CITY_LINE_NORTH;
  int highway = variant < 2 ? CITY_LINE_WEST : CITY_LINE_EAST;
  if (mirrored) {
    road = (road + 1) & 3;
    highway = (highway + 3) & 3;
  }
  road = (road - rotation) & 3;
  highway = (highway - rotation) & 3;
  // The artwork pair distinguishes the connection side; reflection selects
  // whether the road joins along the camera's X or Y axis.
  bool road_positive = road == CITY_LINE_SOUTH || road == CITY_LINE_EAST;
  bool highway_positive = highway == CITY_LINE_SOUTH || highway == CITY_LINE_EAST;
  return highway_positive << 1 | (road_positive == highway_positive) | !(road & 1) << 2;
}

// The finished highway mesh has its own deck profile. Its base follows the
// highest ground in the small footprint, without flattening the ground map.
int CityHighwayDrawHeight(const City *city, TileIndex tile, const CityTileGeometry *tile_geometry) {
  int kind = city->tile[tile].kind;
  CityTileGeometry ground = *tile_geometry;
  if (kind == TK_HIGHWAY && ground.water == CITY_WATER_NONE && ground.land_shape)
    return ground.surface_height + (ground.land_shape == CITY_TERRAIN_SHAPE_13);
  CityRect bounds = CityHighwayFootprintBounds(city, tile);
  int maximum = 0;
  for (int y = bounds.top; y < bounds.bottom; ++y) for (int x = bounds.left; x < bounds.right; ++x) {
    TileIndex probe = MapTileXY(city, x, y);
    CityTileGeometry geometry = probe == tile ? ground : ReadCityMapViewGeometry(city, probe);
    maximum = IntMax(maximum, geometry.surface_height + (geometry.surface_shape != CITY_TERRAIN_FLAT));
  }
  return maximum;
}

// Alternating opposite bends encode a diagonal highway in the stock map.
// Match the next shared footprint in camera space, preserving isolated corners.
static int DiagonalHighwaySprite(const City *city, TileIndex tile, int layout, int zoom, int height) {
  int original = MapSpriteZoomIndex(layout, zoom);
  int curve = layout - CITY_MAP_TILE_SPRITE_HIGHWAY_MULTI_101, mask = 0;
  // Opposite stored orientations remain opposite in every camera rotation.
  // Match the neighboring bend directly without resolving its viewed sprite.
  int opposite_orientation = (ReadCityHighwayOrientation(&city->tile[tile]) + 2) & 3;
  for (int end = 0; end < 2; ++end) {
    TileIndex neighbor = CityMapViewNeighbor(city, tile,
        2 * kHighwayCurveNeighbors[curve][end][0], 2 * kHighwayCurveNeighbors[curve][end][1]);
    if (neighbor == INVALID_TILE || city->tile[neighbor].kind != TK_HIGHWAY_BEND ||
        ReadCityHighwayOrientation(&city->tile[neighbor]) != opposite_orientation) continue;
    CityTileGeometry neighbor_geometry = ReadCityMapViewGeometry(city, neighbor);
    if (CityHighwayDrawHeight(city, neighbor, &neighbor_geometry) == height) mask |= 1 << end;
  }
  if (!mask) return original;
  return MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM - CITY_LANDSCAPE_GROUND_FIRST +
      CITY_LANDSCAPE_DIAGONAL_HIGHWAY_FIRST + curve * 3 + mask - 1;
}

int CityHighwaySpriteFlip(const City *city, TileIndex tile, int layout) {
  int rotation = CityMapCameraRotation(city->drawing);
  const TileInfo *tp = &city->tile[tile];
  if (layout >= CITY_MAP_TILE_SPRITE_HIGHWAY_ENTRANCE_FIRST && layout < CITY_MAP_TILE_SPRITE_HIGHWAY_MULTI_FIRST)
    return CityHighwayRampView(city, tile, rotation) >> 2;
  bool bridge = layout >= CITY_MAP_TILE_SPRITE_HIGHWAY_BRIDGE_FIRST &&
      layout <= CITY_MAP_TILE_SPRITE_HIGHWAY_BRIDGE_LAST;
  if (!bridge && !IsCityMapHighwayLayout(layout)) {
    if (!IsTileKindHighwayIncline(tp->kind)) return 0;
    int axis = ReadCityHighwayAxis(tp) ^ (rotation & 1);
    return axis ^ CityMapHighwayLayoutAxis(CityMapViewTileSpriteIndex(city, tile));
  }
  int axis = ReadCityHighwayAxis(tp) ^ (rotation & 1);
  if (bridge) {
    if ((city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)))
      return axis ^ (layout - CITY_MAP_TILE_SPRITE_HIGHWAY_BRIDGE_FIRST);
    return axis;
  }
  return axis ^ CityMapHighwayLayoutAxis(layout);
}

// Choose the full prepared carriageway, including shoulders that meet nearby ramps.
static int HighwayTransportSprite(const City *city, TileIndex tile, int layout, int zoom,
    const CityTileGeometry *geometry) {
  int original = MapSpriteZoomIndex(layout, zoom);
  int rotation = CityMapCameraRotation(city->drawing);
  const TileInfo *tp = &city->tile[tile];
  // A highway over level water follows its surface, not the submerged bed.
  int shape = geometry->surface_shape;
  if (layout <= CITY_MAP_TILE_SPRITE_HIGHWAY_NS && shape >= CITY_TERRAIN_SHAPE_1 && shape <= CITY_TERRAIN_SHAPE_4)
    return MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM - CITY_LANDSCAPE_GROUND_FIRST +
        CITY_LANDSCAPE_HIGHWAY_SLOPE_FIRST + shape - 1;
  int axis = ReadCityHighwayAxis(tp) ^ (rotation & 1), mask = 0;
  for (int side = -1; side <= 1; side += 2) {
    int dx = axis ? side : 0, dy = axis ? 0 : side;
    TileIndex neighbor = CityMapViewNeighbor(city, tile, dx, dy);
    if (neighbor == INVALID_TILE) continue;
    const TileInfo *other = &city->tile[neighbor];
    if (other->kind != TK_HIGHWAY_ENTRANCE || ReadCityLandHeight(tp) != ReadCityLandHeight(other)) continue;
    int ramp = CityHighwayRampView(city, neighbor, rotation);
    int ramp_dx = (ramp & 3) > 1 ? 1 : -1, ramp_dy = 0;
    if (ramp >> 2) {
      ramp_dy = ramp_dx;
      ramp_dx = 0;
    }
    if (ramp_dx == -dx && ramp_dy == -dy) mask |= side < 0 ? 1 : 2;
  }
  if (!mask) return original;
  return MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM +
      CITY_LANDSCAPE_HIGHWAY_MERGE_FIRST - CITY_LANDSCAPE_GROUND_FIRST +
      (layout - CITY_MAP_TILE_SPRITE_HIGHWAY_FIRST) * 3 + mask - 1;
}

void DrawCityHighwayTransportArtwork(const City *city, TileIndex tile, int layout, int zoom, int x, int baseline,
    const CityTileGeometry *geometry) {
  Drawing *drawing = city->drawing;
  int sprite = MapSpriteZoomIndex(layout, zoom);
  if (!IsCityMapGroundPass(drawing)) {
    bool crossing = layout >= CITY_MAP_TILE_SPRITE_HIGHWAY_ROAD_CROSSING_FIRST &&
        layout <= CITY_MAP_TILE_SPRITE_HIGHWAY_RAIL_CROSSING_LAST;
    if (crossing) {
      int axis = (layout - CITY_MAP_TILE_SPRITE_HIGHWAY_ROAD_CROSSING_FIRST) & 1;
      int first = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM -
          CITY_LANDSCAPE_GROUND_FIRST;
      int prepared = HighwayTransportSprite(city, tile, layout, zoom, geometry);
      int merge = prepared - first - CITY_LANDSCAPE_HIGHWAY_MERGE_FIRST -
          (layout - CITY_MAP_TILE_SPRITE_HIGHWAY_FIRST) * 3;
      merge = merge >= 0 && merge < 3 ? merge + 1 : 0;
      DrawCityMapSpriteAtBaseline(drawing,
          first + CITY_LANDSCAPE_HIGHWAY_OVERPASS_FIRST + axis * 4 + merge, x, baseline, 0);
      return;
    }
  }
  int flipped = CityHighwaySpriteFlip(city, tile, layout);
  if (city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE))
    sprite = HighwayTransportSprite(city, tile, layout, zoom, geometry);
  DrawCityMapSpriteAtBaseline(drawing, sprite, x, baseline, flipped);
}

void DrawCityHighwayCrossingGround(const City *city, int layout, int zoom, int screen_x, int ground_y) {
  Drawing *drawing = city->drawing;
  if (layout >= CITY_MAP_TILE_SPRITE_HIGHWAY_ROAD_CROSSING_FIRST &&
      layout <= CITY_MAP_TILE_SPRITE_HIGHWAY_RAIL_CROSSING_LAST) {
    bool rail = layout >= CITY_MAP_TILE_SPRITE_HIGHWAY_RAIL_CROSSING_FIRST;
    int axis = (layout - CITY_MAP_TILE_SPRITE_HIGHWAY_ROAD_CROSSING_FIRST) & 1;
    DrawCityMapSpriteAtBaseline(drawing,
        MapSpriteZoomIndex(
            (rail ? CITY_MAP_TILE_SPRITE_RAIL_FIRST : CITY_MAP_TILE_SPRITE_ROAD_FIRST) + !axis, zoom),
        screen_x, ground_y, 0);
  }
}

// Catalog highway bridges and shared shapes keep their original anchor and four ground-cell order.
void DrawCityHighwaySharedFootprint(const City *city, TileIndex tile, int layout, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry) {
  if (!IsCityMapViewFootprintAnchor(city, tile)) return;
  Drawing *drawing = city->drawing;
  int half_width = 4 << zoom, row_step = 2 << zoom;
  bool native = (city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) != 0;
  bool draw_underpasses = native && IsCityMapGroundPass(drawing) &&
      CityMapLayerVisible(drawing, CITY_MAP_LAYER_NETWORKS);
  if (zoom != CITY_MAP_ZOOM_SMALL || native) {
    int ground = -1;
    int zone = ReadCityZoneKind(&city->tile[tile]);
    if (zoom == CITY_MAP_ZOOM_SMALL && CityMapDryGroundShape(city, tile, geometry) == CITY_TERRAIN_FLAT &&
        zone != ZONE_NONE)
      ground = 290 + zone;
    // Keep the anchor, rear, right and front order used by the shared two-by-two artwork. Each cell owns its
    // underpass; sorted rendering paints it through DrawHighwayGround before submitting the shared deck.
    static const i8 kGroundTiles[4][2] = {{0, 0}, {-1, 0}, {-1, 1}, {0, 1}};
    for (int index = 0; index < 4; ++index) {
      int dx = kGroundTiles[index][0], dy = kGroundTiles[index][1];
      TileIndex other = index ? CityMapViewNeighbor(city, tile, dx, dy) : tile;
      CityTileGeometry other_geometry = index ? ReadCityMapViewGeometry(city, other) : *geometry;
      int x = screen_x + (dy - dx) * half_width, y = ground_y + (dx + dy) * row_step;
      if (native) y -= (other_geometry.surface_height - geometry->surface_height) * CityMapHeightStep(zoom);
      DrawCityMapTerrainTile(city, other, zoom, index ? -1 : ground, x, y, &other_geometry);
      if (draw_underpasses &&
          IsTileKindHighwayBridge(city->tile[other].kind) && IsTileKindBridgeMiddle(city->tile[other].kind)) {
        int under = ReadCityBridgeUnderpassKind(&city->tile[other]);
        if (under && under != TK_POWER_LINE) DrawCityBridgeUnderpass(city, other, under, zoom, x, y);
      }
      DrawCityMapPlacementTile(city, other);
    }
  } else {
    DrawCityMapPlacementTile(city, tile);
  }
  int baseline = ground_y + row_step, flipped = CityHighwaySpriteFlip(city, tile, layout);
  if (native && IsTileKindHighwayBridge(city->tile[tile].kind)) {
    int base = ReadCityBridgeDeckHeight(city, tile) - CITY_HIGHWAY_DECK_RISE;
    CityRect bounds = CityHighwayFootprintBounds(city, tile);
    const TileInfo *head = &city->tile[MapTileXY(city, bounds.left, bounds.top)];
    bool far_end = false;
    if (!IsCityBridgeHead(head)) {
      head = &city->tile[MapTileXY(city, bounds.right - 1, bounds.bottom - 1)];
      far_end = true;
    }
    if (IsCityBridgeHead(head)) {
      // End footprints use the same one-level grade as the bank highway, rather than a flat bridge mesh.
      int axis = ReadCityBridgeAxis(head), side = far_end ? -1 : 1;
      CityPoint rise = CityMapCameraVector(CityMapCameraRotation(drawing), side * !axis, side * axis);
      int slope = rise.x < 0 ? 2 : rise.x > 0 ? 4 : rise.y < 0 ? 1 : 3;
      layout = CITY_MAP_TILE_SPRITE_HIGHWAY_MULTI_FIRST + slope - 1;
      flipped = 0;
      base -= CITY_HIGHWAY_BRIDGE_GRADE;
    }
    baseline += (geometry->surface_height - base) * CityMapHeightStep(zoom);
  }
  DrawCityMapSpriteAtBaseline(drawing, MapSpriteZoomIndex(layout, zoom), screen_x, baseline, flipped);
}

// Shared two-by-two layout is drawn from its left corner. Paint all four
// ground cells at their actual elevations before placing the independent deck.
static bool DrawHighwayTile(const City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *tile_geometry) {
  Drawing *drawing = city->drawing;
  CityTileGeometry geometry = *tile_geometry;
  int kind = city->tile[tile].kind;
  if (!(city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) ||
      !IsTileKindMultiHighway(kind)) return false;
  if (!CityMapTileDetailsVisible(drawing)) {
    DrawCityMapTerrainTile(city, tile, zoom, -1, screen_x, ground_y, &geometry);
    DrawCityMapPlacementTile(city, tile);
    return true;
  }
  // Older shared inclines use the same per-cell grade as compact carriageways,
  // so neither end is displaced by the shared footprint's maximum height.
  if (IsTileKindHighwayIncline(kind)) {
    DrawCityMapTerrainTile(city, tile, zoom, -1, screen_x, ground_y, &geometry);
    DrawCityMapPlacementTile(city, tile);
    int baseline = ground_y - (geometry.surface_shape == CITY_TERRAIN_SHAPE_13) * CityMapHeightStep(zoom);
    int axis = ReadCityHighwayAxis(&city->tile[tile]) ^ (CityMapCameraRotation(city->drawing) & 1);
    DrawCityHighwayTransportArtwork(city, tile, CITY_MAP_TILE_SPRITE_HIGHWAY_FIRST + axis,
        zoom, screen_x, baseline, &geometry);
    return true;
  }
  if (!IsCityMapViewFootprintAnchor(city, tile)) return true;
  CityRect bounds = CityHighwayFootprintBounds(city, tile);
  int tx = MapTileX(city, tile), ty = MapTileY(city, tile), scale = 1 << zoom;
  int height = geometry.surface_height, deck_height = 0;
  for (int y = bounds.top; y < bounds.bottom; ++y) for (int x = bounds.left; x < bounds.right; ++x) {
    TileIndex ground = MapTileXY(city, x, y);
    CityPoint offset = CityMapCameraVector(CityMapCameraRotation(city->drawing), x - tx, y - ty);
    int dx = offset.x, dy = offset.y;
    CityTileGeometry ground_geometry = ground == tile ? geometry : ReadCityMapViewGeometry(city, ground);
    int ground_height = ground_geometry.surface_height;
    deck_height = IntMax(deck_height, ground_height + (ground_geometry.surface_shape != CITY_TERRAIN_FLAT));
    DrawCityMapTerrainTile(city, ground, zoom, -1,
        screen_x + (dy - dx) * 4 * scale,
        ground_y + (dx + dy) * 2 * scale - (ground_height - height) * CityMapHeightStep(zoom), &ground_geometry);
    DrawCityMapPlacementTile(city, ground);
  }
  int baseline = ground_y +
      (height - deck_height) * CityMapHeightStep(zoom) + 2 * scale;
  int layout = CityMapViewTileSpriteIndex(city, tile);
  int sprite = kind == TK_HIGHWAY_JUNCTION ? MapSpriteZoomIndex(layout, zoom) :
      DiagonalHighwaySprite(city, tile, layout, zoom, deck_height);
  DrawCityMapSpriteAtBaseline(drawing, sprite, screen_x, baseline, 0);
  return true;
}

static bool DrawHighwayGround(const City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry) {
  if (IsTileKindBridge(city->tile[tile].kind))
    DrawCityBridgeGround(city, tile, zoom, screen_x, ground_y, geometry);
  else DrawCityMapTerrainTile(city, tile, zoom, -1, screen_x, ground_y, geometry);
  int layout = CityMapViewTileSpriteIndex(city, tile);
  if (IsCityMapHighwayLayout(layout)) DrawCityHighwayCrossingGround(city, layout, zoom, screen_x, ground_y);
  return true;
}

static void TickHighwayTile(City *city, TileIndex tile) {
  if (IsTileKindBridge(city->tile[tile].kind)) TickCityBridgeTile(city, tile);
  else TickCitySurfaceLineGround(city, tile);
}

const TileModule kCityHighwayModule = {
  .clear = ClearCityBridgeTile,
  .tick = TickHighwayTile, .draw = DrawHighwayTile, .draw_ground = DrawHighwayGround,
};
