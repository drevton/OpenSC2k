// TileInfo transport traversal contract (zero-based bits):
// Surface kind and terrain[4..7] provide the family-specific road/rail/highway/bridge route, as documented in
// surface_lines.c, highways.c and bridges.c; subway connectivity comes from underground[0..5] TUK_ IDs.
// Traversal scratch, its queue and traffic density are separate City storage, not spare TileInfo bits. Density
// is one byte per 2x2 tile region in City.half, so no kind/orientation bit is borrowed to store route usage.
// Height/slope and all flags/overlay fields retain the shared contract in tile_storage.h.

#include "city/infrastructure/transport.h"

#include "city/events/events.h"
#include "city/infrastructure/bridges.h"
#include "city/infrastructure/highways.h"
#include "city/infrastructure/networks.h"
#include "city/maps/tile_queue.h"
#include "city/presentation.h"
#include "city/infrastructure/types.h"
#include "city/maps/tile_storage.h"

#include "city/maps/kinds.h"
#include "city/maps/map.h"
#include "city/state.h"
#include "city/engine/random.h"
#include "game/state.h"
#include "game/observation.h"

#include <assert.h>

// Transport

enum { kTransportPathCapacity = 128 };
_Static_assert(CITY_MAP_MAX_SIZE - 1 <= UINT16_MAX, "Transport path coordinates must fit PointU16");
_Static_assert((int)kTransportPathCapacity < CITY_TILE_QUEUE_BASE_CAPACITY,
    "The smallest route queue must hold the complete path");

// Original startup initializer order, stored as native x/y byte pairs.
const TransportNeighborOffsets kTransportNeighborOffsets = {{
  {1, 0}, {0, 1}, {-1, 0}, {0, -1},
  {2, 0}, {0, 2}, {-2, 0}, {0, -2},
  {3, 0}, {0, 3}, {-3, 0}, {0, -3},
  {1, 1}, {1, -1}, {-1, 1}, {-1, -1},
  {1, 2}, {1, -2}, {-1, 2}, {-1, -2},
  {2, 1}, {2, -1}, {-2, 1}, {-2, -1}
}};

// Indexed by source zone: bit n admits destination zone n. Ordinary RCI zones exclude unzoned ground and their
// own category; the remaining destination bits stay set, including unassigned zones.
static const u16 kRouteDestinationZones[CITY_ZONE_KIND_MASK + 1] = {
  [ZONE_NONE] = UINT16_MAX,
  [ZONE_RESIDENTIAL_LOW] = UINT16_MAX & ~((1 << ZONE_NONE) |
      (1 << ZONE_RESIDENTIAL_LOW) | (1 << ZONE_RESIDENTIAL_HIGH)),
  [ZONE_RESIDENTIAL_HIGH] = UINT16_MAX & ~((1 << ZONE_NONE) |
      (1 << ZONE_RESIDENTIAL_LOW) | (1 << ZONE_RESIDENTIAL_HIGH)),
  [ZONE_COMMERCIAL_LOW] = UINT16_MAX & ~((1 << ZONE_NONE) |
      (1 << ZONE_COMMERCIAL_LOW) | (1 << ZONE_COMMERCIAL_HIGH)),
  [ZONE_COMMERCIAL_HIGH] = UINT16_MAX & ~((1 << ZONE_NONE) |
      (1 << ZONE_COMMERCIAL_LOW) | (1 << ZONE_COMMERCIAL_HIGH)),
  [ZONE_INDUSTRIAL_LOW] = UINT16_MAX & ~((1 << ZONE_NONE) |
      (1 << ZONE_INDUSTRIAL_LOW) | (1 << ZONE_INDUSTRIAL_HIGH)),
  [ZONE_INDUSTRIAL_HIGH] = UINT16_MAX & ~((1 << ZONE_NONE) |
      (1 << ZONE_INDUSTRIAL_LOW) | (1 << ZONE_INDUSTRIAL_HIGH)),
  [ZONE_MILITARY] = 0,
  [ZONE_AIRPORT] = 1 << ZONE_NONE,
  [ZONE_SEAPORT] = 1 << ZONE_NONE,
  // These encoded source zones are unassigned. Preserve their existing masks without assigning gameplay meanings.
  [ZONE_RESERVED_FIRST] = 0x0003,
  [ZONE_RESERVED_FIRST + 1] = 0x0006,
  [ZONE_RESERVED_FIRST + 2] = 0x000C,
  [ZONE_RESERVED_FIRST + 3] = 0,
  [ZONE_RESERVED_FIRST + 4] = 0x070B,
  [ZONE_RESERVED_FIRST + 5] = 0x0D0E,
};

// Recount the industry rail/highway border bonus from current surface tiles. Requires normalized native storage;
// replaces the cached count without changing tiles or the route queue.
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

// Decay one complete traffic section by one quarter and sum its remaining density. Section must be in range; caller
// owns layer invalidation and accumulation of the returned total across sections.
u32 DecayTrafficDensitySection(City *city, int section) {
  const int width = city->map_width / 2;
  const int section_columns = city->map_width / CITY_MAP_SECTION_SIZE;
  TileIndex first = section / section_columns * (CITY_MAP_SECTION_SIZE / 2) * width +
      section % section_columns * (CITY_MAP_SECTION_SIZE / 2);
  CityHalfMapStatistics *restrict statistics = city->half + first;
  u32 total = 0;
  for (size_t y = 0; y < CITY_MAP_SECTION_SIZE / 2; ++y) {
    CityHalfMapStatistics *row = statistics + y * width;
    for (size_t x = 0; x < CITY_MAP_SECTION_SIZE / 2; ++x) {
      int value = row[x].traffic_density;
      value -= value >> 2;
      row[x].traffic_density = value;
      total += value;
    }
  }
  return total;
}

// Decay the half-resolution traffic layer and replace the city-wide total with its decayed sum.
// Complete section-aligned map storage is required; presentation is invalidated before the first section changes.
void DecayTrafficDensity(City *city) {
  InvalidateMapLayer(city, CITY_MAP_CHANGE_TRAFFIC_DENSITY);
  u32 total = 0;
  int section_count = city->map_cell_count / CITY_MAP_SECTION_CELL_COUNT;
  for (int section = 0; section < section_count; ++section)
    total += DecayTrafficDensitySection(city, section);
  city->indicators.city_traffic = total;
}

// Append a valid transport path point without flood-queue duplicate suppression. The path owns queue capacity and
// traversal state; revisiting a tile in a different transport state is permitted.
static void PushTransportRoutePoint(City *city, int x, int y) {
  CityTileQueueCursor *queue = &city->route_queue_cursor;
  city->route_points[queue->write] = (PointU16){x, y};
  queue->write = (queue->write + 1) & queue->mask;
}

// Probe the fixed 24-neighbor transport-access pattern without changing City. This is the growth admission
// predicate; the route search separately resolves its starting state and native bridge geometry.
bool TileHasTransportAccess(const City *city, TileIndex tile_index) {
  assert(IsMapTileValid(city, tile_index));
  CityPoint point = MapTilePt(city, tile_index);
  for (int i = 0; i < CITY_TRANSPORT_NEIGHBOR_COUNT; ++i) {
    int nx = point.x + kTransportNeighborOffsets.offset[i].x;
    int ny = point.y + kTransportNeighborOffsets.offset[i].y;
    if (!IsMapCoordinateValid(city, nx, ny)) continue;
    TileIndex neighbor = MapTileXY(city, nx, ny);
    int kind = city->tile[neighbor].kind;
    if (IsTileKindRoadRoute(kind) ||
        kind == TK_BUS_STATION || kind == TK_RAIL_STATION || kind == TK_SUBWAY_STATION)
      return true;
  }
  return false;
}

// Find transport access, traverse its graph, and apply route traffic. Requires valid source-zone/footprint inputs and
// exclusive route workspace; returns route success and may consume RNG and add traffic.
bool FindTransportRouteAndApplyTraffic(City *city, TileIndex source, int source_zone, int size, int maximum_cost) {
  assert(maximum_cost >= 0 && maximum_cost < kTransportPathCapacity);
  CityPoint point = MapTilePt(city, source);
  int x = point.x, y = point.y;
  const int width = city->map_width, height = city->map_height;
  const TileInfo *tiles = city->tile;
  bool native_routes = (city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) != 0;
  const u16 compatibility = kRouteDestinationZones[source_zone];
  static const i8 kRouteStepY[4] = {0, 1, 0, -1};
  static const i8 kRouteStepX[4] = {-1, 0, 1, 0};
  // Every stored point passed the map bounds check; supported dimensions fit u16.
  // Each pushed depth initializes its entries before use; only the root cost needs a default.
  PointU16 path[kTransportPathCapacity];
  u8 remaining[kTransportPathCapacity], cost[kTransportPathCapacity], state[kTransportPathCapacity];
  int road_y = 0;
  int road_x = 0;
  int initial_state = CITY_ROUTE_ROAD;
  bool found_start = false;
  for (int neighbor = 0; neighbor < CITY_TRANSPORT_NEIGHBOR_COUNT; ++neighbor) {
    int neighbor_y = y + kTransportNeighborOffsets.offset[neighbor].y;
    int neighbor_x = x + kTransportNeighborOffsets.offset[neighbor].x;
    TileIndex neighbor_tile = MapTileXYOrInvalid(city, neighbor_x, neighbor_y);
    if (neighbor_tile == INVALID_TILE) continue;
    const TileInfo *tp = &tiles[neighbor_tile];
    int kind = tp->kind;
    if (native_routes && IsTileKindBridge(kind)) {
      int axis = ReadCityBridgeAxis(tp);
      kind = CityBridgeTransportKind(tp, IsCityBridgeHead(tp) ?
          axis : !axis);
    }
    if (ReadCityTileClasses(kind) & (TCLASS_ROAD | TCLASS_ROUTE_STATION)) {
      road_y = neighbor_y;
      road_x = neighbor_x;
      initial_state = kind == TK_BUS_STATION ? CITY_ROUTE_BUS_STATION
          : kind == TK_RAIL_STATION          ? CITY_ROUTE_RAIL_STATION
          : kind == TK_SUBWAY_STATION        ? CITY_ROUTE_SUBWAY_STATION
                                                    : CITY_ROUTE_ROAD;
      found_start = true;
      break;
    }
  }
  if (!found_start)
    return 0;
  int root_cost = 0;
  if (!(city->fixes & (1ull << CITY_FIX_ROUTE_ROOT_COST)) && city->game->observation &&
      city->game->observation->transport_root_cost)
    root_cost = city->game->observation->transport_root_cost(city->game);

  if (size == 1)
    maximum_cost -= maximum_cost >> 2;
  int depth = 1;
  path[0] = (PointU16){road_x, road_y};
  cost[0] = 0;
  state[0] = initial_state;
  remaining[0] = 15;
  bool route_found = false;
  ClearCityTileQueue(city);
  PushTransportRoutePoint(city, road_x, road_y);
  int step = 2 * (NextCrtRandom(&city->random.crt) & 1) + 1;
  while (depth && cost[depth - 1] < maximum_cost) {
    int direction = NextCrtRandom(&city->random.crt) & 3;
    // The current depth stays fixed until a candidate is pushed or backtracking starts.
    PointU16 current = path[depth - 1];
    TileIndex current_tile = current.y * width + current.x;
    const TileInfo *current_tp = &tiles[current_tile];
    int current_state = state[depth - 1];
    bool moved = false;
    for (int j = 0; j < 4; ++j) {
      direction = (direction + step) & 3;
      int bit = (u8)(1u << direction);
      if (!(remaining[depth - 1] & bit))
        continue;
      remaining[depth - 1] &= (u8)~bit;
      int next_y = current.y + kRouteStepY[direction];
      int next_x = current.x + kRouteStepX[direction];
      if (next_y < 0 || next_y >= height || next_x < 0 || next_x >= width) {
        if (ReadOverlay(city, current_tile) == CITY_OVERLAY_BORDER_CONNECTION) {
          route_found = true;
          goto route_complete;
        }
        continue;
      }
      TileIndex next_tile = next_y * width + next_x;
      const TileInfo *tp = &tiles[next_tile];
      if (native_routes &&
          (current_state == CITY_ROUTE_HIGHWAY || current_state == CITY_ROUTE_BUS_HIGHWAY) &&
          !CityHighwaysCanJoin(city, current_tile, next_tile, direction)) continue;
      bool current_bridge = IsTileKindBridge(current_tp->kind);
      bool next_bridge = IsTileKindBridge(tp->kind);
      if (native_routes && current_state != CITY_ROUTE_SUBWAY && (current_bridge || next_bridge)) {
        if (current_bridge) {
          int axis = ReadCityBridgeAxis(current_tp);
          int entry_axis = depth > 1 ? path[depth - 2].y != current.y :
              IsCityBridgeHead(current_tp) ? axis : !axis;
          if ((direction & 1) != entry_axis) continue;
        }
        if (CityBridgeTransportEdgeHeight(city, current_tile, direction) !=
            CityBridgeTransportEdgeHeight(city, next_tile, (direction + 2) & 3)) continue;
      }
      int next_state = current_state;
      int move_cost = 3;
      int kind = native_routes && next_bridge && current_state != CITY_ROUTE_SUBWAY ?
          CityBridgeTransportKind(tp, direction) : tp->kind;
      u32 classes = ReadCityTileClasses(kind);
      // Stock explores bridge heads from every side, including failed detours.
      // Preserve those graph steps and RNG draws when native geometry is off.
      bool connector = IsTileKindBridge(kind) ||
          (!native_routes && IsTileKindWaterPowerLine(tp->kind));
      if (!native_routes && IsTileKindHighwayBridge(kind)) classes |= TCLASS_HIGHWAY;
      if ((next_state == CITY_ROUTE_ROAD || next_state == CITY_ROUTE_BUS_ROAD || next_state == CITY_ROUTE_BUS_STATION ||
              next_state == CITY_ROUTE_ALIGHTING_STATION) &&
          (compatibility & (1u << ReadCityZoneKind(tp)))) {
        route_found = true;
        goto route_complete;
      }
      if ((next_state == CITY_ROUTE_ROAD || next_state == CITY_ROUTE_BUS_ROAD || next_state == CITY_ROUTE_RAIL) &&
          IsTileKindArcology(kind)) {
        route_found = true;
        goto route_complete;
      }
      switch (next_state) {
      case CITY_ROUTE_ROAD:
      case CITY_ROUTE_BUS_ROAD: {
        // Road and bus travel choose their next station, connector, tunnel or highway state here.
        bool bus = next_state == CITY_ROUTE_BUS_ROAD;
        if (bus) move_cost = 2;
        if (classes & TCLASS_ROUTE_STATION) {
          next_state = kind == TK_BUS_STATION ? (bus ? CITY_ROUTE_ALIGHTING_STATION : CITY_ROUTE_BUS_STATION)
              : kind == TK_RAIL_STATION ? CITY_ROUTE_RAIL_STATION : CITY_ROUTE_SUBWAY_STATION;
          move_cost = 4;
        } else if (connector)
          next_state = bus ? CITY_ROUTE_BUS_CONNECTOR : CITY_ROUTE_CONNECTOR;
        else if (!(classes & TCLASS_ROAD))
          continue;
        else if (IsTileKindTunnel(kind))
          next_state = bus ? CITY_ROUTE_BUS_TUNNEL : CITY_ROUTE_TUNNEL;
        else if (kind == TK_HIGHWAY_ENTRANCE) {
          next_state = bus ? CITY_ROUTE_BUS_HIGHWAY : CITY_ROUTE_HIGHWAY;
          if (!bus) move_cost = 2;
        }
        break;
      }
      case CITY_ROUTE_HIGHWAY:
      case CITY_ROUTE_BUS_HIGHWAY: {
        // Only an entrance returns highway travel to roads; other accepted cells keep the highway cost.
        bool bus = next_state == CITY_ROUTE_BUS_HIGHWAY;
        if (kind == TK_HIGHWAY_ENTRANCE)
          next_state = bus ? CITY_ROUTE_BUS_ROAD : CITY_ROUTE_ROAD;
        else if (!(classes & TCLASS_HIGHWAY))
          continue;
        move_cost = 1;
        break;
      }
      case CITY_ROUTE_TUNNEL:
      case CITY_ROUTE_BUS_TUNNEL: {
        // Stay in the bore until its depth ends on a road. Bus tunnel steps retain their distinct cost.
        bool bus = next_state == CITY_ROUTE_BUS_TUNNEL;
        if (((tp->altitude >> 8) & 0x7C) == 0) {
          if (!(classes & TCLASS_ROAD))
            continue;
          next_state = bus ? CITY_ROUTE_BUS_ROAD : CITY_ROUTE_ROAD;
        }
        if (bus) move_cost = 2;
        break;
      }
      case CITY_ROUTE_CONNECTOR:
      case CITY_ROUTE_BUS_CONNECTOR: {
        // Connector spans join back to roads without admitting unrelated surface kinds.
        bool bus = next_state == CITY_ROUTE_BUS_CONNECTOR;
        if (classes & TCLASS_ROAD)
          next_state = bus ? CITY_ROUTE_BUS_ROAD : CITY_ROUTE_ROAD;
        else if (!connector)
          continue;
        if (bus) move_cost = 2;
        break;
      }
      case CITY_ROUTE_BUS_STATION: {
        // Station footprint steps retain boarding cost; a road exit starts bus mode.
        if (kind == TK_BUS_STATION)
          move_cost = 4;
        else if (classes & TCLASS_ROAD) {
          next_state = CITY_ROUTE_BUS_ROAD;
          move_cost = 2;
        } else
          continue;
        break;
      }
      case CITY_ROUTE_ALIGHTING_STATION: {
        // Bus/rail station cells retain their station cost; a road exit resumes ordinary travel.
        if (kind == TK_BUS_STATION || kind == TK_RAIL_STATION)
          move_cost = 4;
        else if (classes & TCLASS_ROAD)
          next_state = CITY_ROUTE_ROAD;
        else
          continue;
        break;
      }
      case CITY_ROUTE_RAIL_STATION: {
        // Station footprint steps retain boarding cost; a railway exit starts rail mode.
        if (kind == TK_RAIL_STATION)
          move_cost = 4;
        else if (classes & TCLASS_RAIL) {
          next_state = CITY_ROUTE_RAIL;
          move_cost = 1;
        } else
          continue;
        break;
      }
      case CITY_ROUTE_SUBWAY_STATION: {
        // A subway-compatible buried kind starts subway travel beneath the entry station.
        int underground = ReadTileUndergroundKind(tp);
        if (!IsUndergroundKindSubway(underground))
          continue;
        next_state = CITY_ROUTE_SUBWAY;
        move_cost = 1;
        break;
      }
      case CITY_ROUTE_RAIL: {
        // Rail stations start alighting; arcology arrival was already handled before dispatch.
        if (kind == TK_RAIL_STATION) {
          next_state = CITY_ROUTE_ALIGHTING_STATION;
          move_cost = 4;
        } else if (classes & TCLASS_RAIL)
          move_cost = 1;
        else
          continue;
        break;
      }
      case CITY_ROUTE_SUBWAY: {
        // A subway station starts alighting; other steps must retain a subway-compatible buried kind.
        if (kind == TK_SUBWAY_STATION) {
          next_state = CITY_ROUTE_ALIGHTING_STATION;
          move_cost = 4;
        } else {
          int underground = ReadTileUndergroundKind(tp);
          if (!IsUndergroundKindSubway(underground))
            continue;
          move_cost = 1;
        }
        break;
      }
      default:
        continue;
      }
      path[depth] = (PointU16){next_x, next_y};
      cost[depth] = (u8)(cost[depth - 1] + move_cost);
      state[depth] = next_state;
      remaining[depth] =
          (u8)(next_state == CITY_ROUTE_CONNECTOR || next_state == CITY_ROUTE_BUS_CONNECTOR ? 1u << direction
                  : next_state == CITY_ROUTE_SUBWAY_STATION                                 ? 15u
                                                            : 15 & ~(1u << ((direction + 2) & 3)));
      PushTransportRoutePoint(city, next_x, next_y);
      ++depth;
      moved = true;
      break;
    }
    if (moved)
      continue;
    do {
      --depth;
      if (depth) {
        city->route_queue_cursor.write = (city->route_queue_cursor.write - 1) & city->route_queue_cursor.mask;
        if (depth == 1)
          cost[0] = root_cost;
      }
    } while (depth && remaining[depth - 1] == 0);
  }
route_complete:
  if (!route_found || size == 0) {
    return route_found;
  }
  bool used_bus = false, used_rail = false, used_subway = false;
  // Successful paths keep depth queued points: root/push append one, and backtracking removes one per depth.
  // Costs bound the path below queue capacity, so consuming depth also empties the ring without wrapping over it.
  while (depth) {
    PointU16 coordinate = PopCityTileCoordinate(city);
    int route_state = state[--depth];
    if (route_state == CITY_ROUTE_BUS_STATION)
      used_bus = true;
    if (route_state == CITY_ROUTE_RAIL_STATION)
      used_rail = true;
    if (route_state == CITY_ROUTE_SUBWAY_STATION)
      used_subway = true;
    if (route_state == CITY_ROUTE_ROAD || route_state == CITY_ROUTE_HIGHWAY || route_state == CITY_ROUTE_CONNECTOR) {
      int x = coordinate.x >> 1, y = coordinate.y >> 1;
      CityHalfMapStatistics *statistics = &city->half[y * (city->map_width >> 1) + x];
      u32 value = statistics->traffic_density + size;
      statistics->traffic_density = value < 255 ? value : 255;
    }
  }
  if (used_bus)
    city->infrastructure.counter_a += size;
  if (used_rail)
    city->infrastructure.counter_b += size;
  if (used_subway)
    city->infrastructure.counter_c += size;
  return route_found;
}
