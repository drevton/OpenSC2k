#ifndef SC2K_CITY_MAPS_TILE_QUEUE_H
#define SC2K_CITY_MAPS_TILE_QUEUE_H

#include "city/maps/map_types.h"
#include "city/types.h"

enum { CITY_TILE_QUEUE_BASE_CAPACITY = 512, CITY_TILE_QUEUE_BASE_MASK = CITY_TILE_QUEUE_BASE_CAPACITY - 1 };

// Shared coordinate queue for power, water, traffic and terrain traversal.
// A ring holds capacity - 1 points; inserting into a full ring discards the oldest entry.
// Supported map dimensions are powers of two, so the scaled capacity is too.
static inline int CityTileQueueCapacity(int width, int height) {
  return CITY_TILE_QUEUE_BASE_CAPACITY * IntMax(width, height) / CITY_MAP_MIN_SIZE;
}

typedef struct CityTileQueueCursor {
  u16 read, write, mask;
} CityTileQueueCursor;
_Static_assert(sizeof(CityTileQueueCursor) == 6 && offsetof(CityTileQueueCursor, mask) == 4,
    "tile queue cursor layout");

// The queue is exclusive workspace shared by utility scans, terrain edits and commuter routes.
// Flood insertion/removal maintains duplicate marks when the fix is enabled and route_queued is non-NULL.
// Without marker backing, every operation skips duplicate bookkeeping. Commuter append bypasses those marks because
// revisiting a tile in a different route state is valid; its backtracking still uses this ring.
// Do not interleave users or retain point references across a reset.
void ClearCityTileQueue(City *city);
void PushCityTileCoordinate(City *city, int x, int y);
void ResetCityTileQueueAndPush(City *city, int x, int y);
// Both removals require a nonempty ring. Dequeue takes the oldest point; Pop takes the newest for backtracking.
PointU16 DequeueCityTileCoordinate(City *city);
PointU16 PopCityTileCoordinate(City *city);
int CityTileQueueLength(const City *city);

#endif
