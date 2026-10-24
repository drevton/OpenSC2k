// TileInfo storage for roads, rails, ground crossings and subway/rail entrances (zero-based bits):
// TK_ROAD/TK_RAIL terrain[4..7] stores connections: bit 4 west, 5 south, 6 east, 7 north.
// Ground crossing terrain[4] instead selects the primary axis (0 X/east-west, 1 Y/north-south): road for road/rail
// and road/power, rail for rail/power. The other network is perpendicular; terrain[5..7] has no crossing payload.
// TK_SUBWAY_TO_RAIL terrain[4..5] is facing 0 south, 1 east, 2 north, 3 west; terrain[6..7] is not used.
// Its underground[0..5] is TUK_SUBWAY_ACCESS, independently of surface facing; networks.c owns underground IDs.
// terrain[0..3] is dry ground/coverage (5 means paving). zone[4] selects slope versus zoning in zone[0..3].
// zone[5..7] has no line-topology payload. Heights, utility flags, overlays and other underground bits are shared
// as described in tile_storage.h. Bridge and highway crossings use their own modules' payloads, not these masks.

#include "city/events/events.h"
#include "drawing/maps/map_sprites.h"
#include "city/engine/commands.h"
#include "city/infrastructure/lines.h"
#include "city/infrastructure/networks.h"
#include "city/saveload/map_format.h"
#include "city/infrastructure/surface_lines.h"
#include "city/maps/terrain.h"

#include <assert.h>

#include "assets/state.h"
#include "city/infrastructure/bridges.h"
#include "city/infrastructure/highways.h"
#include "city/infrastructure/tunnels.h"
#include "city/infrastructure/power_lines.h"
#include "city/maps/layers.h"
#include "city/maps/module.h"
#include "city/maps/ground.h"
#include "city/maps/water.h"
#include "city/presentation.h"
#include "city/state.h"
#include "drawing/maps/map_terrain.h"
#include "drawing/maps/map_camera.h"
#include "drawing/maps/map_view.h"
#include "drawing/maps/tables.h"
#include "drawing/sprites/sprites.h"
#include "game/state.h"

// Find a valid tile's adjacent aligned rail incline and return its transition direction. Returns the routine's
// no-transition value when none matches; neighbors outside the map are skipped.
int FindCityRailTransition(const City *city, TileIndex tile) {
  static const i8 kX[] = {0, 0, -1, 1}, kY[] = {-1, 1, 0, 0};
  static const u8 kSlopes[] = {1, 3, 2, 4};
  for (int side = 0; side < 4; ++side) {
    TileIndex neighbor = GetRelativeTileOrInvalid(city, tile, kX[side], kY[side]);
    if (neighbor != INVALID_TILE && ReadTile(city, neighbor) == TK_RAIL &&
        ReadTerrainCode(&city->tile[neighbor]) == kSlopes[side]) return side;
  }
  return -1;
}

// Read the axis of the requested straight infrastructure family on a valid tile. Returns -1 when the family or stored
// shape cannot represent a straight segment; does not infer a new connection.
int ReadCityStraightLineAxis(const City *city, TileIndex tile, int family) {
  int kind = ReadTile(city, tile);
  if (family && (family == TK_POWER_LINE ? !IsTileKindPlainPowerLine(kind) : kind != family)) return -1;
  if (kind == TK_HIGHWAY) return ReadCityHighwayAxis(&city->tile[tile]);
  if (!IsTileKindPlainPowerLine(kind) && !IsTileKindPlainTransport(kind)) return -1;
  int shape = ReadTerrainCode(&city->tile[tile]);
  if (IsTileKindPlainPowerLine(kind)) {
    if (shape >= CITY_TERRAIN_SHAPE_1 && shape <= CITY_TERRAIN_SHAPE_4) return -1;
    if (shape >= CITY_TERRAIN_SHAPE_9 && shape <= CITY_TERRAIN_SHAPE_12) return 1;
  } else {
    if (kLineTerrainOverrideBySlope[shape]) return -1;
    if (kind == TK_RAIL && FindCityRailTransition(city, tile) >= 0) return -1;
  }
  int mask = ReadTransportConnections(&city->tile[tile]);
  if (!(mask & CONN_NS)) return 0;
  if (!(mask & CONN_EW)) return 1;
  return -1;
}

// Determine whether a valid plain transport tile is a bend or junction rather than a straight or sloped piece. Reads
// stored connections and rail transitions without changing topology.
bool IsCityLineBendOrJunction(const City *city, TileIndex tile) {
  int kind = ReadTile(city, tile);
  if (!IsTileKindPlainTransport(kind) ||
      kLineTerrainOverrideBySlope[ReadTerrainCode(&city->tile[tile])]) return false;
  if (kind == TK_RAIL && FindCityRailTransition(city, tile) >= 0) return false;
  int mask = ReadTransportConnections(&city->tile[tile]);
  return (mask & CONN_EW) && (mask & CONN_NS);
}

void TickCitySurfaceLineGround(City *city, TileIndex tile) {
  RefreshCityTileGroundCover(city, tile, false);
  if (IsTileKindGroundTransport(city->tile[tile].kind)) GrowCityTransportGround(city, tile);
}

const TileModule kCitySurfaceLineModule = {.tick = TickCitySurfaceLineGround};

// Preserve the reconnectors' north, south, west, east scan order.
static const struct {
  i8 x, y;
  u8 connection, slope, direction;
} kSurfaceLineNeighbors[4] = {
    {0, -1, CONN_NORTH, 1, CITY_LINE_NORTH},
    {0, 1, CONN_SOUTH, 3, CITY_LINE_SOUTH},
    {-1, 0, CONN_WEST, 2, CITY_LINE_WEST},
    {1, 0, CONN_EAST, 4, CITY_LINE_EAST},
};

typedef struct CitySubwayRailConnection {
  TileIndex neighbor;
  u8 direction;
} CitySubwayRailConnection;

// Surface rails take precedence over all underground connections. Each pass keeps south/east/north/west order;
// a subway-only match faces the opposite direction. Neither pass changes topology or consumes RNG.
static CitySubwayRailConnection FindCitySubwayRailConnection(const City *city, TileIndex tile) {
  static const i8 kNeighborX[4] = {0, 1, 0, -1};
  static const i8 kNeighborY[4] = {1, 0, -1, 0};
  TileIndex neighbors[4];
  for (int direction = 0; direction < 4; ++direction) {
    TileIndex neighbor = GetRelativeTileOrInvalid(city, tile, kNeighborX[direction], kNeighborY[direction]);
    neighbors[direction] = neighbor;
    if (neighbor == INVALID_TILE) continue;
    int kind = city->tile[neighbor].kind;
    if (kind == TK_RAIL || kind == TK_ROAD_RAIL_CROSSING || kind == TK_RAIL_POWER_CROSSING ||
        kind == TK_SUBWAY_TO_RAIL)
      return (CitySubwayRailConnection){neighbor, direction};
  }
  for (int direction = 0; direction < 4; ++direction) {
    TileIndex neighbor = neighbors[direction];
    if (neighbor != INVALID_TILE && IsSubwayTile(city->tile[neighbor].underground))
      return (CitySubwayRailConnection){neighbor, (direction + 2) & 3};
  }
  return (CitySubwayRailConnection){.neighbor = INVALID_TILE};
}

bool CanPlaceCitySubwayRailConnection(const City *city, TileIndex tile) {
  if (!IsMapTileValid(city, tile)) return false;
  const TileInfo *tp = &city->tile[tile];
  if (!IsFlatDryUnbuiltTile(tp) || tp->kind == TK_RADIOACTIVE_WASTE) return false;
  return FindCitySubwayRailConnection(city, tile).neighbor != INVALID_TILE;
}

i64 PlaceCitySubwayRailConnection(City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile));
  const TileInfo *tp = &city->tile[tile];
  if (!IsTileKindUnbuilt(tp->kind) || tp->kind == TK_RADIOACTIVE_WASTE ||
      ReadLegacyTerrainCode(city, tile) != CITY_TERRAIN_FLAT)
    return CITY_COMMAND_COST_BLOCKED;
  CitySubwayRailConnection connection = FindCitySubwayRailConnection(city, tile);
  if (connection.neighbor == INVALID_TILE) return CITY_COMMAND_COST_BLOCKED;
  PlaceCitySubwayCell(city, tile);
  SetUndergroundTile(city, tile, TUK_SUBWAY_ACCESS);
  city->tile[tile].flags &= ~TF_PIPED;
  ReplaceCityTileVariant(city, tile, TK_SUBWAY_TO_RAIL, connection.direction);
  InvalidateTile(city, tile);
  UpdateRailConnections(city, connection.neighbor);
  return true;
}

// Join eligible surface rail to a subway connection at the resolved tile. Rejects unused parameters and unsuitable
// terrain; TEST is read-only and a successful connection keeps its original zero-cost contract.
i64 CmdPlaceSubwayRailConnection(City *city, u32 flags, TileIndex tile,
    int reserved) {
  int invalid = CITY_COMMAND_COST_INVALID;
  if (!IsMapTileValid(city, tile) || reserved != 0)
    return invalid;
  if (flags & CITY_COMMAND_TEST)
    return CanPlaceCitySubwayRailConnection(city, tile) ? 0 : CITY_COMMAND_COST_BLOCKED;
  return PlaceCitySubwayRailConnection(city, tile);
}

// Shared bridge, terrain and border handling; each family retains its own crossing rules. The first adjoining rail
// incline fixes the native line axis.
static int FindCitySurfaceConnections(const City *city, TileIndex tile_index, bool rail) {
  assert(IsMapTileValid(city, tile_index));
  int mask = CONN_NONE;
  int surface_height = -1;
  int route_class = rail ? TCLASS_RAIL : TCLASS_ROAD;
  for (int direction = 0; direction < 4; ++direction) {
    TileIndex neighbor_index = GetRelativeTileOrInvalid(city, tile_index,
        kSurfaceLineNeighbors[direction].x, kSurfaceLineNeighbors[direction].y);
    if (neighbor_index == INVALID_TILE) continue;
    const TileInfo *tp = &city->tile[neighbor_index];
    int neighbor = tp->kind;
    bool bridge = IsTileKindBridge(tp->kind);
    if (bridge) {
      int travel = kSurfaceLineNeighbors[direction].direction;
      neighbor = CityBridgeTransportKind(tp, travel);
      if (surface_height < 0) surface_height = ReadCitySurfaceHeight(&city->tile[tile_index]);
      if (CityBridgeTransportEdgeHeight(city, neighbor_index, (travel + 2) & 3) != surface_height) continue;
    }
    if (rail && neighbor == TK_RAIL && !bridge &&
        ReadTerrainCode(tp) == kSurfaceLineNeighbors[direction].slope)
      return kSurfaceLineNeighbors[direction].y ? CONN_NS : CONN_EW;
    if (!rail && neighbor == TK_HIGHWAY_ENTRANCE &&
        (city->fixes & (1ull << CITY_FIX_RAMP_CONNECTIONS))) {
      int variant = ReadTileVariant(tp);
      bool south = variant == 0 || variant == 3;
      int road_direction = ReadCityHighwayRampMirror(tp) ?
          (south ? CONN_EAST : CONN_WEST) :
          (south ? CONN_SOUTH : CONN_NORTH);
      static const u8 kOpposite[] = {CONN_SOUTH, CONN_NORTH,
          CONN_EAST, CONN_WEST};
      if (road_direction != kOpposite[direction]) continue;
    }
    if (!(ReadCityTileClasses(neighbor) & route_class))
      continue;
    int neighbor_terrain = bridge ? CITY_TERRAIN_FLAT : ReadLegacyTerrainCode(city, neighbor_index);
    bool allowed = kSurfaceLineNeighbors[direction].y ? kLineVerticalConnectionAllowed[neighbor_terrain] != 0 :
        kLineHorizontalConnectionAllowed[neighbor_terrain] != 0;
    if (allowed)
      mask |= kSurfaceLineNeighbors[direction].connection;
  }
  if (ReadOverlay(city, tile_index) == CITY_OVERLAY_BORDER_CONNECTION) {
    CityPoint position = MapTilePt(city, tile_index);
    int x = position.x, y = position.y;
    if (y == 0)
      mask |= CONN_NORTH;
    if (y == city->map_height - 1)
      mask |= CONN_SOUTH;
    if (x == 0)
      mask |= CONN_WEST;
    if (x == city->map_width - 1)
      mask |= CONN_EAST;
  }
  return mask;
}

// Read the actual road connections at a valid tile, including eligible crossings. Returns a direction bitmask without
// changing stored topology or discovering a different placement route.
int CityRoadConnectionMask(const City *city, TileIndex tile) {
  const TileInfo *tp = &city->tile[tile];
  return tp->kind == TK_ROAD ? ReadTransportConnections(tp) : FindCitySurfaceConnections(city, tile, false);
}

// Select a surface road tile from terrain and adjacent connections.
void UpdateRoadConnections(City *city, TileIndex tile_index) {
  assert(IsMapTileValid(city, tile_index));
  int tile = ReadTile(city, tile_index);
  if (tile != TK_ROAD)
    return;
  int mask = FindCitySurfaceConnections(city, tile_index, false);
  int shape = ReadTerrainCode(&city->tile[tile_index]);
  if (shape >= CITY_TERRAIN_SHAPE_1 && shape <= CITY_TERRAIN_SHAPE_4)
    mask &= shape & 1 ? CONN_NS : CONN_EW;
  ReplaceCityTileAndStatistics(city, tile_index, TK_ROAD);
  WriteTransportConnections(&city->tile[tile_index], mask);
  InvalidateTile(city, tile_index);
  // Prepared diagonal ends depend on adjoining road connections, even when
  // reconnecting a straight approach leaves its stored tile ID unchanged.
  TileNeighbors neighbors = GetTileNeighbors(city, tile_index);
  for (int index = 0; index < neighbors.count; ++index) {
    TileIndex neighbor = neighbors.tiles[index];
    int road = ReadTile(city, neighbor);
    if (road == TK_ROAD) InvalidateTile(city, neighbor);
  }
}

// Add an admitted surface line cell, preserving eligible crossings and bridge underpasses. Requires a valid tile and
// explicit family; updates statistics, topology and local drawing damage without charging line cost itself.
static void PlaceCitySurfaceLineCell(City *city, TileIndex tile, bool rail) {
  TileInfo *tp = &city->tile[tile];
  int line_kind = rail ? TK_RAIL : TK_ROAD;
  int kind = tp->kind;
  if (IsTileKindBridge(kind)) {
    PlaceCityBridgeUnderpass(city, tile, line_kind);
    return;
  }
  if (ReadCityTileClasses(kind) & (rail ? TCLASS_RAIL : TCLASS_ROAD)) return;
  int other = rail ? TK_ROAD : TK_RAIL;
  int axis = ReadCityStraightLineAxis(city, tile, 0);
  int replacement, crossing_axis = -1;
  if (IsTileKindLandscape(kind)) replacement = line_kind;
  else if (IsTileKindPlainPowerLine(kind) && axis >= 0) {
    replacement = rail ? TK_RAIL_POWER_CROSSING : TK_ROAD_POWER_CROSSING;
    crossing_axis = 1 - axis;
  } else if (kind == other && axis >= 0) {
    replacement = TK_ROAD_RAIL_CROSSING;
    crossing_axis = rail ? axis : 1 - axis;
  } else if (kind == TK_HIGHWAY) replacement = rail ? TK_HIGHWAY_RAIL : TK_HIGHWAY_ROAD;
  else return;
  replacement = CityTileKindForWater(replacement, HasTileWater(tp));
  ReplaceCityTileAndStatistics(city, tile, replacement);
  if (crossing_axis >= 0) WriteCityCrossingAxis(tp, crossing_axis);
  if (rail || ReadCityZoneKind(tp) != ZONE_MILITARY)
    WriteTileZoneByte(tp, ReadTileZoneByte(tp) & CITY_ZONE_METADATA_MASK);
  // Reconnect the edited cell first, then north, south, west and east. Rail's first matching incline fixes its axis.
  if (rail) UpdateRailConnections(city, tile);
  else UpdateRoadConnections(city, tile);
  for (int direction = 0; direction < 4; ++direction) {
    TileIndex neighbor = GetRelativeTileOrInvalid(city, tile,
        kSurfaceLineNeighbors[direction].x, kSurfaceLineNeighbors[direction].y);
    if (neighbor == INVALID_TILE) continue;
    if (rail) UpdateRailConnections(city, neighbor);
    else UpdateRoadConnections(city, neighbor);
  }
  RefreshCityPowerConnections(city, tile);
}

// Add a surface road cell and reconnect its four neighbors. Delegates to PlaceCitySurfaceLineCell; arguments must
// satisfy that function's contract.
void PlaceCityRoadCell(City *city, TileIndex origin_tile) {
  PlaceCitySurfaceLineCell(city, origin_tile, false);
}

// Select a surface rail tile from terrain and adjacent connections.
void UpdateRailConnections(City *city, TileIndex tile_index) {
  assert(IsMapTileValid(city, tile_index));
  int tile = ReadTile(city, tile_index);
  if (tile != TK_RAIL)
    return;
  // A curve and its straight approach borrow a shared prepared path. Updating
  // either connection changes the neighbour's artwork even if its ID stays.
  TileNeighbors neighbors = GetTileNeighbors(city, tile_index);
  for (int index = 0; index < neighbors.count; ++index) {
    TileIndex neighbor = neighbors.tiles[index];
    int rail = ReadTile(city, neighbor);
    if (rail == TK_RAIL) InvalidateTile(city, neighbor);
  }
  int terrain_code = ReadLegacyTerrainCode(city, tile_index);
  int terrain = terrain_code < CITY_TERRAIN_BANK_48 ? terrain_code : 0;
  int mask;
  if (kLineTerrainOverrideBySlope[terrain & CITY_TERRAIN_SHAPE_MASK] != 0) {
    // Stock slopes 1..4 alternate axes. Retain the full terrain-bank table's
    // narrowed 0/1 offsets elsewhere, including its unusual water-bank entries.
    bool north_south = terrain <= CITY_TERRAIN_SHAPE_4 ? terrain & 1 : kLineTileOffsetByTerrain[terrain] & 1;
    mask = north_south ? CONN_NS : CONN_EW;
  } else {
    mask = FindCitySurfaceConnections(city, tile_index, true);
  }
  ReplaceCityTileAndStatistics(city, tile_index, TK_RAIL);
  WriteTransportConnections(&city->tile[tile_index], mask);
  InvalidateTile(city, tile_index);
}

// Add a surface rail cell and reconnect its four neighbors. Delegates to PlaceCitySurfaceLineCell; arguments must
// satisfy that function's contract.
void PlaceCityRailCell(City *city, TileIndex origin_tile) {
  PlaceCitySurfaceLineCell(city, origin_tile, true);
}

// Source artwork to cardinal connections, independent of native tile-kind encoding.
static int CitySurfaceLineConnections(int layout) {
  static const u8 kMasks[15] = {5, 10, 10, 5, 10, 5, 3, 6, 12, 9, 11, 7, 14, 13, 15};
  if (IsCityMapLineLayout(layout, CITY_MAP_TILE_SPRITE_ROAD_FIRST))
    return kMasks[layout - CITY_MAP_TILE_SPRITE_ROAD_FIRST];
  if (IsCityMapLineLayout(layout, CITY_MAP_TILE_SPRITE_RAIL_FIRST))
    return kMasks[layout - CITY_MAP_TILE_SPRITE_RAIL_FIRST];
  return layout & 1 ? CONN_NS : CONN_EW;
}

// Only connected neighbouring road or rail tiles participate in the prepared diagonal
// layout. Extended end pairs share one curve across the bend and straight.
static const i8 kCurveNeighbors[4][2][2] = {
    {{-1, 0}, {0, 1}}, {{0, 1}, {1, 0}}, {{1, 0}, {0, -1}}, {{0, -1}, {-1, 0}}};

static TileIndex SurfaceLineNeighbor(const City *city, TileIndex tile, int rotation, int dx, int dy) {
  CityPoint offset = CityMapCameraVector(-rotation, dx, dy);
  return GetRelativeTileOrInvalid(city, tile, offset.x, offset.y);
}

// Raised-flat terrain and level foundations share the same transport plane as flat
// terrain one level above. Compare the rendered surface, including its lift.
static int LevelSurfaceLineElevation(const City *city, TileIndex cell) {
  CityMapSurface surface = ResolveCitySurfaceLineGround(city, cell);
  if ((!IsCityMapLineLayout(surface.layout, CITY_MAP_TILE_SPRITE_ROAD_FIRST) &&
      !IsCityMapLineLayout(surface.layout, CITY_MAP_TILE_SPRITE_RAIL_FIRST)) || surface.shape ||
      HasTileWater(&city->tile[cell])) return -1;
  return ReadCityLandHeight(&city->tile[cell]) + surface.raised;
}

static int DiagonalSurfaceLineMask(const City *city, TileIndex cell, int base, int curve, int rotation) {
  int elevation = LevelSurfaceLineElevation(city, cell);
  if (elevation < 0) return 0;
  int opposite = base + 6 + ((curve + 2) & 3), mask = 0;
  for (int end = 0; end < 2; ++end) {
    TileIndex neighbor =
        SurfaceLineNeighbor(city, cell, rotation, kCurveNeighbors[curve][end][0], kCurveNeighbors[curve][end][1]);
    if (neighbor == INVALID_TILE) continue;
    if (CityMapCameraSprite(city->game->resources, CityMapTileSpriteIndex(city, neighbor), rotation) == opposite &&
        LevelSurfaceLineElevation(city, neighbor) == elevation)
      mask |= 1 << end;
  }
  return mask;
}

static TileIndex DiagonalSurfaceLineApproach(const City *city, TileIndex cell, int base, int curve, int mask,
    int rotation) {
  int end = mask == 1, dx = kCurveNeighbors[curve][end][0], dy = kCurveNeighbors[curve][end][1];
  TileIndex neighbor = SurfaceLineNeighbor(city, cell, rotation, dx, dy);
  if (neighbor == INVALID_TILE) return INVALID_TILE;
  int straight = base + !dx;
  CityMapSurface surface = ResolveCitySurfaceLineGround(city, neighbor);
  if (CityMapCameraSprite(city->game->resources, surface.layout, rotation) != straight ||
      LevelSurfaceLineElevation(city, cell) != LevelSurfaceLineElevation(city, neighbor))
    return INVALID_TILE;
  return neighbor;
}

int CitySurfaceLineSprite(const City *city, TileIndex tile, int layout, int zoom, int rotation) {
  int original = layout + CITY_MAP_SPRITES_PER_ZOOM * zoom;
  bool rail = layout >= CITY_MAP_TILE_SPRITE_RAIL_FIRST;
  int base = rail ? CITY_MAP_TILE_SPRITE_RAIL_FIRST : CITY_MAP_TILE_SPRITE_ROAD_FIRST;
  if (layout < base || layout > base + 9 ||
      LevelSurfaceLineElevation(city, tile) < 0) return original;
  int first = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM - CITY_LANDSCAPE_GROUND_FIRST;
  int diagonal = rail ? CITY_LANDSCAPE_DIAGONAL_RAIL_FIRST : CITY_LANDSCAPE_DIAGONAL_ROAD_FIRST;
  int transition = rail ? CITY_LANDSCAPE_DIAGONAL_RAIL_TRANSITION_FIRST :
      CITY_LANDSCAPE_DIAGONAL_ROAD_TRANSITION_FIRST;
  int shared = rail ? CITY_LANDSCAPE_DIAGONAL_RAIL_SHARED_FIRST : CITY_LANDSCAPE_DIAGONAL_ROAD_SHARED_FIRST;
  if (layout <= base + 1) {
    int curves[2] = {-1, -1}, masks[2] = {0, 0};
    for (int side = -1; side <= 1; side += 2) {
      int dx = layout == base ? side : 0;
      int dy = layout == base + 1 ? side : 0;
      TileIndex neighbor = SurfaceLineNeighbor(city, tile, rotation, dx, dy);
      if (neighbor == INVALID_TILE) continue;
      int curve = CityMapCameraSprite(city->game->resources,
          CityMapTileSpriteIndex(city, neighbor), rotation) - base - 6;
      if (curve < 0 || curve >= 4) continue;
      int mask = DiagonalSurfaceLineMask(city, neighbor, base, curve, rotation);
      if (mask != 1 && mask != 2) continue;
      if (DiagonalSurfaceLineApproach(city, neighbor, base, curve, mask, rotation) == tile) {
        curves[side > 0] = curve;
        masks[side > 0] = mask;
      }
    }
    if (curves[0] >= 0 && curves[1] >= 0) {
      // Each bend owns one half and meets the other tangent to the straight.
      int axis = layout == base + 1;
      int first_turn = axis ? curves[0] : curves[0] - 1;
      int second_turn = axis ? curves[1] - 2 : curves[1] == 3;
      return first + shared + axis * 4 + first_turn * 2 + second_turn;
    }
    for (int side = 0; side < 2; ++side)
      if (curves[side] >= 0)
        return first + transition + curves[side] * 4 +
            (masks[side] - 1) * 2 + 1;
    return original;
  }
  if (layout < base + 6) return original;
  int curve = layout - base - 6;
  int mask = DiagonalSurfaceLineMask(city, tile, base, curve, rotation);
  if (!mask) return original;
  if (mask != 3 && DiagonalSurfaceLineApproach(city, tile, base, curve, mask, rotation) != INVALID_TILE)
    return first + transition + curve * 4 + (mask - 1) * 2;
  return first + diagonal + curve * 3 + mask - 1;
}

CitySurfaceLinePath ResolveCitySurfaceLinePath(const City *city, TileIndex tile, int entry, int exit, bool rail) {
  // Both vehicle families cache this descriptor on tile entry.
  int sprite = ReadTile(city, tile) == (rail ? TK_RAIL : TK_ROAD)
      ? CitySurfaceLineSprite(
            city, tile, ResolveCitySurfaceLineGround(city, tile).layout, CITY_MAP_ZOOM_LARGE, 0)
      : 0;
  int first =
      MAP_LANDSCAPE_SPRITE_FIRST + CITY_MAP_ZOOM_LARGE * MAP_LANDSCAPE_SPRITES_PER_ZOOM - CITY_LANDSCAPE_GROUND_FIRST;
  int diagonal = sprite - first - (rail ? CITY_LANDSCAPE_DIAGONAL_RAIL_FIRST : CITY_LANDSCAPE_DIAGONAL_ROAD_FIRST);
  int transition = sprite - first -
      (rail ? CITY_LANDSCAPE_DIAGONAL_RAIL_TRANSITION_FIRST : CITY_LANDSCAPE_DIAGONAL_ROAD_TRANSITION_FIRST);
  int shared = sprite - first -
      (rail ? CITY_LANDSCAPE_DIAGONAL_RAIL_SHARED_FIRST : CITY_LANDSCAPE_DIAGONAL_ROAD_SHARED_FIRST);
  int kind, rotation;
  if (entry == exit) {
    kind = CITY_LINE_PATH_TURNAROUND;
    rotation = exit;
  } else if (diagonal >= 0 && diagonal < 12) {
    rotation = diagonal / 3;
    int mask = diagonal % 3;
    bool reverse = ((entry - rotation) & 3) != CITY_LINE_WEST;
    kind = CITY_LINE_PATH_DIAGONAL + mask * 2 + reverse;
  } else if (transition >= 0 && transition < 16) {
    rotation = transition / 4;
    int mask = transition / 2 & 1;
    bool approach = transition & 1;
    bool reverse = ((entry - rotation) & 3) != (approach && !mask ? CITY_LINE_NORTH : CITY_LINE_WEST);
    kind = (approach ? CITY_LINE_PATH_APPROACH : CITY_LINE_PATH_TRANSITION) + mask * 2 + reverse;
  } else if (shared >= 0 && shared < 8) {
    rotation = 0;
    bool reverse = entry != (shared / 4 ? CITY_LINE_NORTH : CITY_LINE_WEST);
    kind = CITY_LINE_PATH_SHARED + shared * 2 + reverse;
  } else if (exit == ((entry + 2) & 3)) {
    kind = CITY_LINE_PATH_STRAIGHT;
    rotation = (exit - CITY_LINE_EAST) & 3;
  } else {
    rotation = exit == ((entry + 1) & 3) ? entry : exit;
    kind = CITY_LINE_PATH_CURVE + (entry != rotation);
  }
  return (CitySurfaceLinePath){kind, rotation};
}

// OpenTTD's W/S/E/N slope bits, converted from SC2's shape IDs.
static const u8 kFoundationSlopes[] = {0, 9, 12, 6, 3, 13, 14, 7, 11, 8, 4, 2, 1, 15};

CityMapSurface ResolveCitySurfaceLineGround(const City *city, TileIndex tile) {
  const TileInfo *tp = &city->tile[tile];
  int layout = CityMapTileSpriteIndex(city, tile), shape = ReadTerrainCode(tp);
  CityMapSurface surface = {.shape = shape, .layout = layout, .foundation = -1};
  // Older ramp placement stored a crossroads regardless of nearby streets.
  if (IsCityMapLineLayout(layout, CITY_MAP_TILE_SPRITE_ROAD_FIRST) &&
      CitySurfaceLineConnections(layout) == CONN_ALL &&
          shape == CITY_TERRAIN_FLAT) {
    static const i8 kNeighborOffsets[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (int direction = 0; direction < 4; ++direction) {
      TileIndex cell = GetRelativeTileOrInvalid(city, tile,
          kNeighborOffsets[direction][0], kNeighborOffsets[direction][1]);
      if (cell == INVALID_TILE) continue;
      if (ReadTile(city, cell) == TK_HIGHWAY_ENTRANCE) {
        layout = CITY_MAP_TILE_SPRITE_ROAD_FIRST + kLineTileOffsetByConnectionMask[CityRoadConnectionMask(city, tile)];
        surface.layout = layout;
        break;
      }
    }
  }
  if (HasTileWater(tp) || shape > 13 ||
      !IsCityMapSurfaceLayout(layout)) return surface;
  // A bridge ramp is a structure above its dry bank. The adjoining deck
  // supplies its axis and height; no additional per-tile metadata is needed.
  bool rail = layout >= CITY_MAP_TILE_SPRITE_RAIL_FIRST;
  int base = rail ? CITY_MAP_TILE_SPRITE_RAIL_FIRST : CITY_MAP_TILE_SPRITE_ROAD_FIRST;
  if (shape <= CITY_TERRAIN_SHAPE_4 && layout < base + 6) {
    for (int direction = 0; direction < 4; ++direction) {
      TileIndex deck = GetRelativeTileOrInvalid(city, tile, kLineXStep[direction], kLineYStep[direction]);
      if (deck == INVALID_TILE) continue;
      int bridge = ReadTile(city, deck);
      const TileInfo *neighbor = &city->tile[deck];
      if (!HasTileWater(neighbor) ||
          (direction & 1) != ReadCityBridgeAxis(neighbor) ||
          (rail ? !IsTileKindRailBridge(bridge) :
              !IsTileKindBridge(bridge) || IsTileKindRailBridge(bridge) || IsTileKindHighwayBridge(bridge)) ||
          ReadCityLandHeight(tp) != ReadCityWaterHeight(neighbor)) continue;
      if (shape) {
        if (shape != ((direction - 1) & 3) + 1) continue;
        surface.shape = CITY_TERRAIN_FLAT;
        surface.raised = 1;
        surface.foundation = CITY_LANDSCAPE_LEVEL_FOUNDATION_FIRST + kFoundationSlopes[shape] - 1;
        surface.layout = base + (direction & 1);
      } else {
        surface.shape = ((direction + 1) & 3) + 1;
        surface.layout = CityMapLineSlopeLayout(base, surface.shape);
      }
      surface.bridge_approach = true;
      return surface;
    }
  }
  if (layout >= CITY_MAP_TILE_SPRITE_RAIL_FIRST) {
    // All surface rail uses the same terrain lookup. Stored climbing and
    // transition IDs only supply their flat layout; never infer the slope
    // from that ID or from neighboring track connections.
    static const u8 kRailSurfaceShapes[] = {0, 1, 2, 3, 4, 0, 0, 0, 0, 1, 3, 3, 1, 0};
    surface.shape = kRailSurfaceShapes[shape];
    if (shape >= 5 && shape <= 8) {
      surface.foundation = CITY_LANDSCAPE_LEVEL_FOUNDATION_FIRST + kFoundationSlopes[shape] - 1;
      surface.raised = 1;
    } else if (shape >= 9 && shape <= 12) {
      // SC2's single-corner rail forms use the north-south layout. Reuse
      // its prepared incline and the existing side walls.
      int corner = __builtin_ctz(kFoundationSlopes[shape]);
      surface.foundation = CITY_LANDSCAPE_INCLINED_FOUNDATION_FIRST + corner * 2 + 1;
    } else if (shape == CITY_TERRAIN_SHAPE_13) {
      surface.raised = 1;
    }
    if (surface.shape) {
      surface.layout = CityMapLineSlopeLayout(CITY_MAP_TILE_SPRITE_RAIL_FIRST, surface.shape);
    } else if (IsCityMapRailSlopeLayout(layout)) {
      surface.layout = CityMapRailFlatLayout(layout);
    } else if (IsCityMapRailTransitionLayout(layout)) {
      surface.layout = CityMapRailFlatLayout(layout);
    }
    return surface;
  }
  base = CITY_MAP_TILE_SPRITE_ROAD_FIRST;
  int offset = layout - base, slope = kFoundationSlopes[shape];
  static const u8 kRoadConnections[] = {10, 5, 5, 10, 5, 10, 12, 6, 3, 9, 13, 14, 7, 11, 15};
  static const u8 kInvalidLevelRoads[] = {0, 12, 9, 8, 3, 0, 1, 0, 6, 4, 0, 0, 2, 0, 0};
  static const u8 kInvalidSlopeRoads[] = {0, 0, 0, 5, 0, 15, 10, 15, 0, 10, 15, 15, 5, 15, 15};
  int connections = kRoadConnections[offset];
  if (slope == 15) {
    surface.shape = 0;
    surface.raised = 1;
  } else if (slope) {
    // SC2 road curves and junctions occupy a complete tile, so use a full
    // level foundation for them.
    bool straight = connections == 10 || connections == 5;
    if (!(kInvalidLevelRoads[slope] & connections) || !straight) {
      surface.foundation = CITY_LANDSCAPE_LEVEL_FOUNDATION_FIRST + slope - 1;
      surface.shape = 0;
      surface.raised = 1;
    } else if (!(slope & (slope - 1)) || (kInvalidSlopeRoads[slope] & connections)) {
      // Only single-corner slopes admit inclined full-tile foundations.
      if (!(slope & (slope - 1))) {
        int corner = __builtin_ctz(slope), axis = connections == 5;
        surface.foundation = CITY_LANDSCAPE_INCLINED_FOUNDATION_FIRST + corner * 2 + axis;
        surface.shape = axis ? (corner == 1 || corner == 2 ? 3 : 1) : (corner < 2 ? 4 : 2);
      } else {
        surface.foundation = CITY_LANDSCAPE_LEVEL_FOUNDATION_FIRST + slope - 1;
        surface.shape = 0;
        surface.raised = 1;
      }
    }
  }
  if (offset < 6)
    surface.layout = base + (surface.shape >= 1 && surface.shape <= 4 ?
        surface.shape + 1 : connections == 5);
  return surface;
}

// Preserve the world surface's chosen rail/road axis, including SC2's
// asymmetric single-corner rail rule. Rotate its local layout and foundation.
CityMapSurface ResolveCitySurfaceLineViewGround(const City *city, TileIndex tile, int rotation) {
  CityMapSurface surface = ResolveCitySurfaceLineGround(city, tile);
  if (!rotation) return surface;
  surface.shape = CityMapCameraShape(surface.shape, rotation);
  surface.layout = CityMapCameraSprite(city->game->resources, surface.layout, rotation);
  if (surface.foundation >= CITY_LANDSCAPE_INCLINED_FOUNDATION_FIRST) {
    int form = surface.foundation - CITY_LANDSCAPE_INCLINED_FOUNDATION_FIRST;
    surface.foundation = CITY_LANDSCAPE_INCLINED_FOUNDATION_FIRST +
        (((form / 2 + rotation) & 3) * 2) + ((form & 1) ^ (rotation & 1));
  } else if (surface.foundation >= 0) {
    int slope = surface.foundation - CITY_LANDSCAPE_LEVEL_FOUNDATION_FIRST + 1;
    surface.foundation = CITY_LANDSCAPE_LEVEL_FOUNDATION_FIRST + CityMapCameraMask(slope, (4 - rotation) & 3) - 1;
  }
  return surface;
}

// OpenTTD omits a rear wall when the adjoining surface already covers it.
// Inspect only the two adjacent cells, including their own foundations.
static int CitySurfaceLineFoundationBlock(const City *city, TileIndex tile, CityMapSurface surface) {
  static const u8 kNearCorners[] = {1, 4}, kOppositeCorners[] = {2, 2};
  int block = 0;
  int rotation = CityMapCameraRotation(city->drawing);
  int slope = kFoundationSlopes[surface.shape];
  int height = ReadCityLandHeight(&city->tile[tile]) + surface.raised;
  for (int edge = 0; edge < 2; ++edge) {
    TileIndex neighbor = CityMapViewNeighbor(city, tile, -edge, edge - 1);
    if (neighbor == INVALID_TILE) continue;
    CityMapSurface other = ResolveCitySurfaceLineViewGround(city, neighbor, rotation);
    int adjacent = other.shape < 14 ? kFoundationSlopes[other.shape] : 0;
    int other_height = ReadCityLandHeight(&city->tile[neighbor]) + other.raised;
    CityTileGeometry geometry = ReadCityMapViewGeometry(city, neighbor);
    if (geometry.water != CITY_WATER_NONE) {
      other_height = geometry.surface_height;
      adjacent = kFoundationSlopes[geometry.surface_shape];
    }
    int far_corner = edge ? 1 : 4;
    if (height + !!(slope & kNearCorners[edge]) <= other_height + !!(adjacent & kOppositeCorners[edge]) &&
        height + !!(slope & 8) <= other_height + !!(adjacent & far_corner)) block |= 1 << edge;
  }
  return block;
}

bool DrawCitySurfaceLineSprite(Drawing *drawing, int id, int x, int baseline, int flipped) {
  if (id >= MAP_LANDSCAPE_SPRITE_FIRST) {
    int zoom = (id - MAP_LANDSCAPE_SPRITE_FIRST) / MAP_LANDSCAPE_SPRITES_PER_ZOOM;
    int first = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM - CITY_LANDSCAPE_GROUND_FIRST;
    int layout = id - first;
    bool rail = layout >= CITY_LANDSCAPE_DIAGONAL_RAIL_FIRST &&
        layout < CITY_LANDSCAPE_DIAGONAL_RAIL_SHARED_FIRST + 8;
    int transition = rail ? CITY_LANDSCAPE_DIAGONAL_RAIL_TRANSITION_FIRST :
        CITY_LANDSCAPE_DIAGONAL_ROAD_TRANSITION_FIRST;
    int shared = rail ? CITY_LANDSCAPE_DIAGONAL_RAIL_SHARED_FIRST : CITY_LANDSCAPE_DIAGONAL_ROAD_SHARED_FIRST;
    int piece = layout - transition, pair = layout - shared;
    // Shared IDs describe combinations only; no precombined image is loaded.
    if (pair >= 0 && pair < 8) {
      static const u8 kPieces[2][2][2] = {{{5, 11}, {3, 13}}, {{1, 7}, {9, 15}}};
      int axis = pair / 4;
      bool a = DrawCityMapSpriteAtBaseline(drawing, first + transition + kPieces[axis][0][(pair >> 1) & 1],
          x, baseline, flipped);
      bool b = DrawCityMapSpriteAtBaseline(drawing, first + transition + kPieces[axis][1][pair & 1],
          x, baseline, flipped);
      return a || b;
    }
    if (piece >= 0 && piece < 16 && (piece & 1)) {
      // Both halves are ordinary run sprites; their boundaries are baked.
      static const u8 kHalves[8] = {2, 1, 0, 2, 3, 0, 1, 3};
      int half = kHalves[piece / 2] ^ 1;
      int straight = first + (rail ? CITY_LANDSCAPE_RAIL_STRAIGHT_HALF_FIRST :
          CITY_LANDSCAPE_ROAD_STRAIGHT_HALF_FIRST) + half;
      bool a = DrawCityMapSpriteAtBaseline(drawing, id, x, baseline, flipped);
      bool b = DrawCityMapSpriteAtBaseline(drawing, straight, x, baseline, flipped);
      return a || b;
    }
  }
  return DrawCityMapSpriteAtBaseline(drawing, id, x, baseline, flipped);
}

// Return the object baseline; sprite preparation never happens in this path.
int DrawCityMapInfrastructureGround(City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
    CityMapSurface surface,
    const CityTileGeometry *tile_geometry) {
  Drawing *drawing = city->drawing;
  CityTileGeometry geometry = *tile_geometry;
  if (!IsCityMapGroundPass(drawing))
    return ground_y - (surface.raised + (surface.shape == CITY_TERRAIN_SHAPE_13)) * CityMapHeightStep(zoom);
  DrawCityMapTerrainSkirt(city, tile, zoom, screen_x, ground_y, geometry.land_height);
  int original_shape = geometry.land_shape;
  int first = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM - CITY_LANDSCAPE_GROUND_FIRST;
  if (original_shape < 14) DrawCityMapTerrainFace(city, tile, zoom,
      first + CITY_LANDSCAPE_TERRAIN_FACE_FIRST + original_shape, screen_x, ground_y,
      CityMapSlopeFaceSides(city, tile, original_shape, geometry.land_height));
  if (surface.bridge_approach) DrawCityMapSpriteAtBaseline(drawing,
      CityGroundSpriteForShape(city, tile, zoom, original_shape), screen_x, ground_y, 0);
  if (surface.bridge_approach && surface.shape) DrawCityMapTerrainFace(city, tile, zoom,
      first + CITY_LANDSCAPE_TERRAIN_FACE_FIRST + surface.shape, screen_x, ground_y,
      CityMapSlopeFaceSides(city, tile, surface.shape, geometry.land_height));
  if (surface.foundation >= 0) {
    int block = CitySurfaceLineFoundationBlock(city, tile, surface);
    int count = surface.foundation < CITY_LANDSCAPE_INCLINED_FOUNDATION_FIRST ? 14 : 8;
    DrawCityMapSpriteAtBaseline(drawing, first + surface.foundation + block * count, screen_x, ground_y, 0);
  }
  ground_y -= surface.raised * CityMapHeightStep(zoom);
  if (!surface.bridge_approach) DrawCityMapSpriteAtBaseline(drawing,
      CityGroundSpriteForShape(city, tile, zoom, surface.shape), screen_x, ground_y, 0);
  return ground_y - (surface.shape == CITY_TERRAIN_SHAPE_13 ? CityMapHeightStep(zoom) : 0);
}

void DrawCityMapTransportArtwork(City *city, TileIndex tile, int layout, int zoom, int x, int baseline,
    const CityTileGeometry *tile_geometry) {
  if (IsCityMapHighwayLayout(layout)) {
    DrawCityHighwayTransportArtwork(city, tile, layout, zoom, x, baseline, tile_geometry);
    return;
  }
  if (IsTileKindBridge(city->tile[tile].kind) && layout >= CITY_MAP_TILE_SPRITE_SUSPENSION_BRIDGE_FIRST &&
      layout <= CITY_MAP_TILE_SPRITE_RAIL_BRIDGE_LAST) {
    DrawCityBridgeDeckArtwork(city, tile, layout, zoom, x, baseline, ReadCityBridgeStyle(city, tile));
    return;
  }
  Drawing *drawing = city->drawing;
  int sprite = layout + CITY_MAP_SPRITES_PER_ZOOM * zoom;
  if (!IsCityMapGroundPass(drawing)) {
    bool surface = IsCityMapSurfaceLayout(layout) ||
        (layout >= CITY_MAP_TILE_SPRITE_ROAD_RAIL_CROSSING_FIRST &&
            layout <= CITY_MAP_TILE_SPRITE_ROAD_RAIL_CROSSING_LAST);
    if (surface) return;
  }
  int flipped = IsCityMapMultiHighwayLayout(layout) ?
      CityHighwaySpriteFlip(city, tile, layout) : CityMapViewTileFlip(city, tile);
  if ((city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) && layout < CITY_MAP_TILE_SPRITE_HIGHWAY_FIRST)
    sprite = CitySurfaceLineSprite(city, tile, layout, zoom, CityMapCameraRotation(drawing));
  DrawCitySurfaceLineSprite(drawing, sprite, x, baseline, flipped);
}

// Shared transport fallback after a family's native draw callback declines the tile. The renderer has already
// applied visibility, detail and clip checks. Preserve ground, selection, object and power-warning order.
void DrawCityTransportTileFallback(City *city, TileIndex tile, int layout, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *tile_geometry) {
  Drawing *drawing = city->drawing;
  const TileInfo *tp = &city->tile[tile];
  CityTileGeometry geometry = *tile_geometry;
  bool native = city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE);
  int kind = tp->kind, terrain_code = CityMapDryGroundShape(city, tile, tile_geometry);
  int sprite_base = CITY_MAP_SPRITES_PER_ZOOM * zoom, half_width = 4 << zoom;
  int tile_sprite = MapSpriteZoomIndex(layout, zoom), terrain_sprite = -1;
  if (IsTileKindTunnel(kind)) {
    DrawCityRoadTunnelEntrance(city, tile, tile_sprite, zoom, screen_x, ground_y, &geometry);
    return;
  }

  if (zoom == CITY_MAP_ZOOM_SMALL && terrain_code == CITY_TERRAIN_FLAT) {
    int zone = ReadCityZoneKind(tp);
    if (zone != ZONE_NONE) terrain_sprite = 290 + zone;
  }

  if (IsCityMapMultiHighwayLayout(layout)) {
    DrawCityHighwaySharedFootprint(city, tile, layout, zoom, screen_x, ground_y, &geometry);
    return;
  }

  int tile_ground_y = ground_y;
  if (native && geometry.water == CITY_WATER_NONE &&
      IsCityMapSurfaceLayout(layout)) {
    CityMapSurface surface = ResolveCitySurfaceLineViewGround(city, tile, CityMapCameraRotation(drawing));
    tile_ground_y = DrawCityMapInfrastructureGround(city, tile, zoom, screen_x, ground_y, surface, &geometry);
    layout = surface.layout;
  } else {
    if (zoom != CITY_MAP_ZOOM_SMALL || native)
      DrawCityMapTerrainTile(city, tile, zoom, terrain_sprite, screen_x, ground_y, &geometry);
    if (terrain_code == CITY_TERRAIN_SHAPE_13) {
      if (zoom == CITY_MAP_ZOOM_SMALL)
        DrawCityMapSpriteAtBaseline(drawing, CITY_SPRITE_CATALOG_SMALL_LAND_EDGE, screen_x, ground_y, 0);
      tile_ground_y -= CityMapHeightStep(zoom);
    }
  }
  if (native && IsCityMapHighwayLayout(layout))
    tile_ground_y = ground_y + (geometry.surface_height - CityHighwayDrawHeight(city, tile, &geometry)) *
        CityMapHeightStep(zoom);
  bool elevated = IsTileKindPowerLine(tp->kind) ||
      (layout >= CITY_MAP_TILE_SPRITE_HIGHWAY_FIRST && layout <= CITY_MAP_TILE_SPRITE_HIGHWAY_BRIDGE_LAST);
  if (elevated) DrawCityMapPlacementTile(city, tile);
  DrawCityMapTransportArtwork(city, tile, layout, zoom, screen_x, tile_ground_y, &geometry);
  if (!elevated) DrawCityMapPlacementTile(city, tile);
  if ((tp->flags & TF_POWER_SERVICE) == TF_POWERABLE) {
    CityPoint extent = CityMapSpriteExtent(drawing, layout + sprite_base);
    DrawCityMapSprite(drawing, sprite_base + CITY_SPRITE_CATALOG_SMALL_POWER_WARNING,
        screen_x + (extent.x >> 1) - half_width, ground_y - half_width, 0);
  }
}
