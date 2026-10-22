// Shared City coordinate ring for utility floods, terrain traversal and commuter backtracking.
// Storage belongs to City.map_storage. This module adds no TileInfo payload or extra allocation.
#include "city/maps/tile_queue.h"

#include "city/maps/layers.h"
#include "city/state.h"

// Append an in-map coordinate to the reusable flood queue, suppressing duplicates when enabled. Out-of-map coordinates
// are ignored; a full ring drops its oldest entry and updates duplicate tracking consistently.
void PushCityTileCoordinate(City *city, int x, int y) {
  if ((u32)x >= city->map_width || (u32)y >= city->map_height) return;
  CityTileQueueCursor *queue = &city->route_queue_cursor;
  u8 *queued = city->fixes & (1u << CITY_FIX_ROUTE_QUEUE_DEDUPLICATION) ? city->route_queued : NULL;
  if (queued) {
    TileIndex tile = MapTileXY(city, x, y);
    if (queued[tile / 8] & (1 << (tile & 7))) return;
    queued[tile / 8] |= 1 << (tile & 7);
  }
  city->route_points[queue->write] = (PointU16){x, y};
  int next = (queue->write + 1) & queue->mask;
  queue->write = next;
  if (queue->read == next) {
    if (queued) {
      PointU16 point = city->route_points[next];
      TileIndex dropped = MapTileXY(city, point.x, point.y);
      queued[dropped / 8] &= ~(1 << (dropped & 7));
    }
    queue->read = (next + 1) & queue->mask;
  }
}

// Empty the reusable route ring and clear duplicate marks for its queued entries. Requires an initialized queue and
// matching map workspace; this does not clear unrelated scratch state.
void ClearCityTileQueue(City *city) {
  CityTileQueueCursor *queue = &city->route_queue_cursor;
  if (city->fixes & (1u << CITY_FIX_ROUTE_QUEUE_DEDUPLICATION)) {
    u8 *queued = city->route_queued;
    for (int index = queue->read; index != queue->write; index = (index + 1) & queue->mask) {
      PointU16 point = city->route_points[index];
      TileIndex tile = MapTileXY(city, point.x, point.y);
      queued[tile / 8] &= ~(1 << (tile & 7));
    }
  }
  queue->read = queue->write = 0;
}

// Discard the previous traversal and enqueue the supplied starting coordinate. The queue and optional duplicate map
// must be initialized; an out-of-map starting point leaves the queue empty.
void ResetCityTileQueueAndPush(City *city, int x, int y) {
  ClearCityTileQueue(city);
  PushCityTileCoordinate(city, x, y);
}

// Remove and return the oldest queued flood coordinate. Requires a nonempty initialized ring; releases that
// coordinate's duplicate marker when deduplication is enabled.
PointU16 DequeueCityTileCoordinate(City *city) {
  CityTileQueueCursor *queue = &city->route_queue_cursor;
  PointU16 point = city->route_points[queue->read];
  queue->read = (queue->read + 1) & queue->mask;
  if (city->fixes & (1u << CITY_FIX_ROUTE_QUEUE_DEDUPLICATION)) {
    TileIndex tile = MapTileXY(city, point.x, point.y);
    city->route_queued[tile / 8] &= ~(1 << (tile & 7));
  }
  return point;
}

// Remove and return the newest queued coordinate for route backtracking. Requires a nonempty initialized ring; clears
// its duplicate marker when that mode is enabled.
PointU16 PopCityTileCoordinate(City *city) {
  CityTileQueueCursor *queue = &city->route_queue_cursor;
  queue->write = (queue->write - 1) & queue->mask;
  PointU16 point = city->route_points[queue->write];
  if (city->fixes & (1u << CITY_FIX_ROUTE_QUEUE_DEDUPLICATION)) {
    TileIndex tile = MapTileXY(city, point.x, point.y);
    city->route_queued[tile / 8] &= ~(1 << (tile & 7));
  }
  return point;
}

// Return the number of entries currently retained by the route ring. Requires a power-of-two queue mask and
// initialized cursors; this query does not consume entries.
int CityTileQueueLength(const City *city) {
  const CityTileQueueCursor *queue = &city->route_queue_cursor;
  return (queue->write - queue->read) & queue->mask;
}
