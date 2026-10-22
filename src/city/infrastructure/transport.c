// TileInfo transport traversal contract (zero-based bits):
// Surface kind and terrain[4..7] provide the family-specific road/rail/highway/bridge route, as documented in
// surface_lines.c, highways.c and bridges.c; subway connectivity comes from underground[0..5] TUK_ IDs.
// Traversal scratch, its queue and traffic density are separate City storage, not spare TileInfo bits. Density
// is one byte per 2x2 tile region in City.half, so no kind/orientation bit is borrowed to store route usage.
// Height/slope and all flags/overlay fields retain the shared contract in tile_storage.h.

#include "city/infrastructure/bridges.h"
#include "city/infrastructure/highways.h"
#include "city/infrastructure/transport.h"
#include "city/maps/tile_queue.h"
#include "city/presentation.h"
#include "city/infrastructure/types.h"
#include "city/maps/tile_storage.h"

#include "city/state.h"
#include "city/engine/random.h"
#include "city/growth/growth.h"
#include "city/maps/layers.h"
#include "game/state.h"
#include "game/observation.h"

// Transport

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
      WriteByte(&row[x].traffic_density, value);
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
  for (int section = 0; section < city->map_cell_count / CITY_MAP_SECTION_CELL_COUNT; ++section)
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

// Lane changes stay inside a footprint. Separate straight spans join only at their ends, with the same explicit axis
// and carriageway alignment. From/to must be adjacent valid tiles and direction their cardinal edge; false rejects
// misaligned separate carriageways.
static bool CityHighwaysCanJoin(const City *city, TileIndex from, TileIndex to, int direction) {
  if (!IsTileKindHighwayAxis(ReadTile(city, from)) ||
      !IsTileKindHighwayAxis(ReadTile(city, to))) return true;
  TileIndex first = CityHighwayAnchor(city, from), second = CityHighwayAnchor(city, to);
  if (first == INVALID_TILE || second == INVALID_TILE) return false;
  if (first == second) return true;
  int axis = ReadCityHighwayAxis(&city->tile[from]);
  if (axis != ReadCityHighwayAxis(&city->tile[to]) || axis != (direction & 1)) return false;
  return axis == CITY_HIGHWAY_AXIS_Y ? MapTileX(city, first) == MapTileX(city, second) :
      MapTileY(city, first) == MapTileY(city, second);
}

// Find transport access, traverse its graph, and apply route traffic. Requires valid source-zone/footprint inputs and
// exclusive route workspace; returns route success and may consume RNG and add traffic.
i32 FindTransportRouteAndApplyTraffic(City *city, int x, int y, int source_zone, int size, int maximum_cost) {
  const int width = city->map_width, height = city->map_height;
  const TileInfo *tiles = city->tile;
  bool native_routes = (city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE)) != 0;
  const u16 compatibility = kZoneCompatibilityMask[source_zone];
  static const i8 dr[4] = {0, 1, 0, -1};
  static const i8 dc[4] = {-1, 0, 1, 0};
  // Each pushed depth initializes its entries before use; only the root cost
  // needs a default. Avoid clearing the entire stack for every zoned tile.
  int path_row[128], path_column[128];
  u8 remaining[128], cost[128], state[128];
  int road_y = 0;
  int road_x = 0;
  int initial_state = CITY_ROUTE_ROAD;
  int found_start = 0;
  for (int neighbor = 0; neighbor < 24; ++neighbor) {
    int neighbor_y = y + kTransportNeighborOffsets.offset[neighbor].y;
    int neighbor_x = x + kTransportNeighborOffsets.offset[neighbor].x;
    TileIndex neighbor_tile = MapTileXYOrInvalid(city, neighbor_x, neighbor_y);
    if (neighbor_tile == INVALID_TILE) continue;
    const TileInfo *neighbor_cell = &tiles[neighbor_tile];
    int tile = neighbor_cell->kind;
    if (native_routes && IsTileKindBridge(neighbor_cell->kind)) {
      int axis = ReadCityBridgeAxis(&city->tile[neighbor_tile]);
      tile = CityBridgeTransportKind(neighbor_cell, IsCityBridgeHead(neighbor_cell) ?
          axis : !axis);
    }
    if (ReadCityTileClasses(tile) & (TCLASS_ROAD | TCLASS_ROUTE_STATION)) {
      road_y = neighbor_y;
      road_x = neighbor_x;
      initial_state = tile == TK_BUS_STATION ? CITY_ROUTE_BUS_STATION
          : tile == TK_RAIL_STATION          ? CITY_ROUTE_RAIL_STATION
          : tile == TK_SUBWAY_STATION        ? CITY_ROUTE_SUBWAY_STATION
                                                    : CITY_ROUTE_ROAD;
      found_start = 1;
      break;
    }
  }
  if (!found_start)
    return 0;
  int root_cost = 0;
  if (!(city->fixes & (1 << CITY_FIX_ROUTE_ROOT_COST)) && city->game->observation &&
      city->game->observation->transport_root_cost)
    root_cost = city->game->observation->transport_root_cost(city->game);

  if (size == 1)
    maximum_cost -= maximum_cost >> 2;
  int depth = 1;
  path_row[0] = road_y;
  path_column[0] = road_x;
  cost[0] = 0;
  state[0] = initial_state;
  remaining[0] = 15;
  bool route_found = false;
  ClearCityTileQueue(city);
  PushTransportRoutePoint(city, road_x, road_y);
  int step = 2 * (NextCrtRandom(&city->random.crt) & 1) + 1;
  while (depth && cost[depth - 1] < maximum_cost) {
    int direction = NextCrtRandom(&city->random.crt) & 3;
    int moved = 0;
    for (int j = 0; j < 4; ++j) {
      direction = (direction + step) & 3;
      int bit = (u8)(1u << direction);
      if (!(remaining[depth - 1] & bit))
        continue;
      remaining[depth - 1] &= (u8)~bit;
      int r = path_row[depth - 1] + dr[direction];
      int c = path_column[depth - 1] + dc[direction];
      if (r < 0 || r >= height || c < 0 || c >= width) {
        if (ReadOverlay(city, path_row[depth - 1] * width + path_column[depth - 1]) == CITY_OVERLAY_BORDER_CONNECTION) {
          route_found = true;
          goto route_complete;
        }
        continue;
      }
      TileIndex next_tile = r * width + c;
      TileIndex current_tile = path_row[depth - 1] * width + path_column[depth - 1];
      if (native_routes &&
          (state[depth - 1] == CITY_ROUTE_HIGHWAY || state[depth - 1] == CITY_ROUTE_BUS_HIGHWAY) &&
          !CityHighwaysCanJoin(city, current_tile, next_tile, direction)) continue;
      bool current_bridge = IsTileKindBridge(city->tile[current_tile].kind);
      bool next_bridge = IsTileKindBridge(city->tile[next_tile].kind);
      if (native_routes && state[depth - 1] != CITY_ROUTE_SUBWAY && (current_bridge || next_bridge)) {
        if (current_bridge) {
          int axis = ReadCityBridgeAxis(&city->tile[current_tile]);
          int entry_axis = depth > 1 ? path_row[depth - 2] != path_row[depth - 1] :
              IsCityBridgeHead(&city->tile[current_tile]) ? axis : !axis;
          if ((direction & 1) != entry_axis) continue;
        }
        if (CityBridgeTransportEdgeHeight(city, current_tile, direction) !=
            CityBridgeTransportEdgeHeight(city, next_tile, (direction + 2) & 3)) continue;
      }
      int next_state = state[depth - 1];
      int move_cost = 3;
      int tile = native_routes && next_bridge && state[depth - 1] != CITY_ROUTE_SUBWAY ?
          CityBridgeTransportKind(&city->tile[next_tile], direction) : ReadTile(city, next_tile);
      u32 classes = ReadCityTileClasses(tile);
      // Stock explores bridge heads from every side, including failed detours.
      // Preserve those graph steps and RNG draws when native geometry is off.
      bool connector = IsTileKindBridge(tile) ||
          (!native_routes && IsCityWaterPowerLine(&tiles[next_tile]));
      if (!native_routes && IsTileKindHighwayBridge(tile)) classes |= TCLASS_HIGHWAY;
      if ((next_state == CITY_ROUTE_ROAD || next_state == CITY_ROUTE_BUS_ROAD || next_state == CITY_ROUTE_BUS_STATION ||
              next_state == CITY_ROUTE_ALIGHTING_STATION) &&
          (compatibility & (1u << ReadCityZoneKind(&tiles[next_tile])))) {
        route_found = true;
        goto route_complete;
      }
      if ((next_state == CITY_ROUTE_ROAD || next_state == CITY_ROUTE_BUS_ROAD || next_state == CITY_ROUTE_RAIL) &&
          IsTileKindArcology(tile)) {
        route_found = true;
        goto route_complete;
      }
      if (next_state == CITY_ROUTE_ROAD || next_state == CITY_ROUTE_BUS_ROAD) {
        bool bus = next_state == CITY_ROUTE_BUS_ROAD;
        if (bus) move_cost = 2;
        if (classes & TCLASS_ROUTE_STATION) {
          next_state = tile == TK_BUS_STATION ? (bus ? CITY_ROUTE_ALIGHTING_STATION : CITY_ROUTE_BUS_STATION)
              : tile == TK_RAIL_STATION ? CITY_ROUTE_RAIL_STATION : CITY_ROUTE_SUBWAY_STATION;
          move_cost = 4;
        } else if (connector)
          next_state = bus ? CITY_ROUTE_BUS_CONNECTOR : CITY_ROUTE_CONNECTOR;
        else if (!(classes & TCLASS_ROAD))
          continue;
        else if (IsTileKindTunnel(tile))
          next_state = bus ? CITY_ROUTE_BUS_TUNNEL : CITY_ROUTE_TUNNEL;
        else if (tile == TK_HIGHWAY_ENTRANCE) {
          next_state = bus ? CITY_ROUTE_BUS_HIGHWAY : CITY_ROUTE_HIGHWAY;
          if (!bus) move_cost = 2;
        }
      } else if (next_state == CITY_ROUTE_HIGHWAY || next_state == CITY_ROUTE_BUS_HIGHWAY) {
        bool bus = next_state == CITY_ROUTE_BUS_HIGHWAY;
        if (tile == TK_HIGHWAY_ENTRANCE)
          next_state = bus ? CITY_ROUTE_BUS_ROAD : CITY_ROUTE_ROAD;
        else if (!(classes & TCLASS_HIGHWAY))
          continue;
        move_cost = 1;
      } else if (next_state == CITY_ROUTE_TUNNEL || next_state == CITY_ROUTE_BUS_TUNNEL) {
        bool bus = next_state == CITY_ROUTE_BUS_TUNNEL;
        if (((tiles[next_tile].altitude >> 8) & 0x7C) == 0) {
          if (!(classes & TCLASS_ROAD))
            continue;
          next_state = bus ? CITY_ROUTE_BUS_ROAD : CITY_ROUTE_ROAD;
        }
        if (bus) move_cost = 2;
      } else if (next_state == CITY_ROUTE_CONNECTOR || next_state == CITY_ROUTE_BUS_CONNECTOR) {
        bool bus = next_state == CITY_ROUTE_BUS_CONNECTOR;
        if (classes & TCLASS_ROAD)
          next_state = bus ? CITY_ROUTE_BUS_ROAD : CITY_ROUTE_ROAD;
        else if (!connector)
          continue;
        if (bus) move_cost = 2;
      } else if (next_state == CITY_ROUTE_BUS_STATION) {
        if (tile == TK_BUS_STATION)
          move_cost = 4;
        else if (classes & TCLASS_ROAD) {
          next_state = CITY_ROUTE_BUS_ROAD;
          move_cost = 2;
        } else
          continue;
      } else if (next_state == CITY_ROUTE_ALIGHTING_STATION) {
        if (tile == TK_BUS_STATION || tile == TK_RAIL_STATION)
          move_cost = 4;
        else if (classes & TCLASS_ROAD)
          next_state = CITY_ROUTE_ROAD;
        else
          continue;
      } else if (next_state == CITY_ROUTE_RAIL_STATION) {
        if (tile == TK_RAIL_STATION)
          move_cost = 4;
        else if (classes & TCLASS_RAIL) {
          next_state = CITY_ROUTE_RAIL;
          move_cost = 1;
        } else
          continue;
      } else if (next_state == CITY_ROUTE_SUBWAY_STATION) {
        int water = ReadUnderground(city, next_tile);
        if (!IsSubwayTile(water))
          continue;
        next_state = CITY_ROUTE_SUBWAY;
        move_cost = 1;
      } else if (next_state == CITY_ROUTE_RAIL) {
        if (tile == TK_RAIL_STATION) {
          next_state = CITY_ROUTE_ALIGHTING_STATION;
          move_cost = 4;
        } else if (classes & TCLASS_RAIL)
          move_cost = 1;
        else if (IsTileKindArcology(tile)) {
          route_found = true;
          goto route_complete;
        } else
          continue;
      } else if (next_state == CITY_ROUTE_SUBWAY) {
        if (tile == TK_SUBWAY_STATION) {
          next_state = CITY_ROUTE_ALIGHTING_STATION;
          move_cost = 4;
        } else {
          int water = ReadUnderground(city, next_tile);
          if (!IsSubwayTile(water))
            continue;
          move_cost = 1;
        }
      } else
        continue;
      path_row[depth] = r;
      path_column[depth] = c;
      cost[depth] = (u8)(cost[depth - 1] + move_cost);
      state[depth] = next_state;
      remaining[depth] =
          (u8)(next_state == CITY_ROUTE_CONNECTOR || next_state == CITY_ROUTE_BUS_CONNECTOR ? 1u << direction
                  : next_state == CITY_ROUTE_SUBWAY_STATION                                 ? 15u
                                                            : 15 & ~(1u << ((direction + 2) & 3)));
      PushTransportRoutePoint(city, c, r);
      ++depth;
      moved = 1;
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
  int used_bus = 0, used_rail = 0, used_subway = 0;
  while (city->route_queue_cursor.write != city->route_queue_cursor.read) {
    PointU16 coordinate = PopCityTileCoordinate(city);
    int route_state = state[--depth];
    if (route_state == CITY_ROUTE_BUS_STATION)
      used_bus = 1;
    if (route_state == CITY_ROUTE_RAIL_STATION)
      used_rail = 1;
    if (route_state == CITY_ROUTE_SUBWAY_STATION)
      used_subway = 1;
    if (route_state == CITY_ROUTE_ROAD || route_state == CITY_ROUTE_HIGHWAY || route_state == CITY_ROUTE_CONNECTOR) {
      int y = (i32)(coordinate.y / 2u);
      int x = (i32)(coordinate.x / 2u);
      TileIndex tile = MapTileXY(city, x * 2, y * 2);
      u32 value = ReadTrafficDensity(city, tile) + size;
      WriteTrafficDensity(city, tile, (u8)(value < 255 ? value : 255u));
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
