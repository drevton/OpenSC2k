#include "city/maps/tile_storage.h"
#include "city/presentation.h"
#include <assert.h>
#include <string.h>

#include "city/engine/simulation.h"
#include "city/events/events.h"
#include "city/maps/layers.h"
#include "city/maps/tables.h"
#include "city/state.h"
#include "game/state.h"
#include "audio/state.h"
#include "assets/state.h"

// Accept a candidate event target according to its map and underground tiles. The original ABI is (y, x); native
// queries use a full-resolution TileIndex. Invalid or event-occupied tiles return false; the query does not reserve
// the destination or consume RNG.
bool IsCityEventTargetTile(City *city, TileIndex tile_index, int event_type) {
  if (!IsMapTileValid(city, tile_index) || IsCityEventOrMarkerOverlay(ReadOverlay(city, tile_index)))
    return false;

  int tile = ReadTile(city, tile_index);
  if (event_type == CITY_MAP_EVENT_TRAIN_ENGINE) {
    return (ReadCityTileClasses(tile) & (TCLASS_RAIL | TCLASS_RAIL_BRIDGE)) != 0;
  }

  int underground = ReadUnderground(city, tile_index);
  if (!IsSubwayTile(underground))
    return IsTileKindTrainTransition(tile);
  return true;
}

// Copy the movement state of a linked event and advance its type. Both slots must be valid linked-event records;
// copied movement state is reindexed without transferring pool ownership.
void CopyCityMapLinkedEventRecord(City *city, int source_slot, int destination_slot) {
  CityMapEvent *source = &city->events.event[source_slot];
  CityMapEvent *destination = &city->events.event[destination_slot];
  MoveCityMapEvent(city, destination_slot, source->tile);
  destination->train.next_tile = source->train.next_tile;
  destination->coordinate_flags = (destination->coordinate_flags & ~CITY_EVENT_TARGET_TILE) |
      (source->coordinate_flags & CITY_EVENT_TARGET_TILE);
  destination->previous_overlay = source->previous_overlay;
  destination->direction = source->direction;
  destination->train.animation_frame = source->train.animation_frame;
  destination->type = source->type;
  if (destination->type == CITY_MAP_EVENT_TRAIN_ENGINE)
    destination->type = CITY_MAP_EVENT_TRAIN_CAR;
  if (destination->type == CITY_MAP_EVENT_SUBWAY_ENGINE)
    destination->type = CITY_MAP_EVENT_SUBWAY_CAR;
}

// Exchange event positions and reverse the first event's heading. Both slots must be valid; swaps their retained
// motion/target state while keeping hash membership consistent.
void SwapCityMapEventPositions(City *city, int first_slot, int second_slot) {
  CityMapEvent *first = &city->events.event[first_slot];
  CityMapEvent *second = &city->events.event[second_slot];
  TileIndex second_tile = second->tile;
  CityPoint second_position = ReadCityMapEventPosition(city, second);
  int y = second_position.y;
  int x = second_position.x;
  int previous_overlay = second->previous_overlay;
  int direction = second->direction;

  UnlinkCityMapEvent(city, first_slot);
  MoveCityMapEvent(city, second_slot, first->tile);
  second->train.next_tile = first->train.next_tile;
  second->coordinate_flags = (second->coordinate_flags & ~CITY_EVENT_TARGET_TILE) |
      (first->coordinate_flags & CITY_EVENT_TARGET_TILE);
  second->previous_overlay = first->previous_overlay;
  MoveCityMapEvent(city, first_slot, second_tile);
  SetCityMapEventTarget(city, first, x, y);
  first->previous_overlay = previous_overlay;
  first->direction = (direction + 2) & 3;
  first->train.animation_frame = (first->direction + 4) & 7;
}

// Choose an eligible adjacent tile in the event's randomized direction order.
i32 FindTile237EventDirection(City *city, TileIndex tile_index, int direction, int event_type) {
  assert(IsMapTileValid(city, tile_index));
  int order = NextEventOrientationRandom(city, 2);
  for (int attempt = 0; attempt < 4; ++attempt) {
    int turn =
        order != 0 ? kCityEventTile237DirectionOrderA[attempt] : kCityEventTile237DirectionOrderB[attempt];
    int candidate_direction = (turn + direction) & 3;
    TileIndex target = GetRelativeTileOrInvalid(city, tile_index,
        kCityEventTile237DirectionXOffset[candidate_direction], kCityEventTile237DirectionYOffset[candidate_direction]);
    if (target == INVALID_TILE) continue;
    if (IsCityEventTargetTile(city, target, event_type) && !IsCityEventOrMarkerOverlay(ReadOverlay(city, target)))
      return candidate_direction;
  }
  return -1;
}

// Create a type-2 event from an active zone-8 tile.
void TrySpawnCityEventType2(City *city, TileIndex tile_index) {
  assert(IsMapTileValid(city, tile_index));
  CityAudioState *audio = city->game->audio;
  int overlay = ReadOverlay(city, tile_index);
  if (IsCityEventOrMarkerOverlay(overlay) || city->disasters.monster_count != 0 ||
      !CanSpawnCityVehicle(city, CITY_MAP_EVENT_HELICOPTER))
    return;
  CityMapEvents *events = &city->events;
  int slot = FindFreeCityEventSlot(city);
  if (slot == 0)
    return;
  CityMapEvent *event = &events->event[slot];
  memset(event, 0, sizeof(*event));
  event->type = CITY_MAP_EVENT_HELICOPTER;
  event->direction = CITY_HEADING_SOUTH;
  event->next_slot = CITY_HELICOPTER_TAKEOFF;
  event->tile = tile_index;
  event->height = 0;
  SetCityMapEventTarget(city, event, 128, 128);
  event->helicopter.destination_y = Sc2kCrtRandRange(city, city->map_height);
  event->helicopter.destination_x = Sc2kCrtRandRange(city, city->map_width);
  event->previous_overlay = overlay;
  ++city->disasters.type2_count;
  audio->helicopter_sound_deadline = SimulationFrameTime(city);
  LinkCityMapEvent(city, slot);
  InvalidateTile(city, tile_index);
  InvalidateCityMapEvent(city, slot);
}

// Decode the approach-waypoint offset for an airplane record's stored direction. Event and x/y outputs must be valid;
// writes only the two integer outputs without changing motion or drawing random values.
void ReadCityAirplaneApproachOffset(const CityMapEvent *event, int *x, int *y) {
  static const i8 kOffsetX[4] = {16, 0, -16, 0};
  static const i8 kOffsetY[4] = {0, -16, 0, 16};
  int axis = (event->next_slot >> 5) & 3;
  *x = kOffsetX[axis];
  *y = kOffsetY[axis];
}

// Create a type-1 event from an active zone-8 tile.
void TrySpawnCityEventType1(City *city, TileIndex tile_index, int axis) {
  assert(IsMapTileValid(city, tile_index));
  CityPoint point = MapTilePt(city, tile_index);
  int overlay = ReadOverlay(city, tile_index);
  if (IsCityEventOrMarkerOverlay(overlay) || city->disasters.monster_count != 0 ||
      !CanSpawnCityVehicle(city, CITY_MAP_EVENT_AIRPLANE))
    return;
  CityMapEvents *events = &city->events;
  int slot = FindFreeCityEventSlot(city);
  if (slot == 0)
    return;
  CityMapEvent *event = &events->event[slot];
  memset(event, 0, sizeof(*event));
  event->type = CITY_MAP_EVENT_AIRPLANE;
  SetCityMapEventTarget(city, event, 128, 128);
  if (Sc2kCrtRandRange(city, 10) >= 5) {
    event->tile = tile_index;
    event->airplane.destination_y = 20;
    event->airplane.destination_x = 20;
    event->direction = axis;
    event->height = 0;
    event->next_slot = CITY_AIRPLANE_TAKEOFF;
    event->previous_overlay = overlay;
    LinkCityMapEvent(city, slot);
    InvalidateTile(city, tile_index);
  } else {
    // Preserve the stock 10..109 range at 128, extending it along each map axis.
    enum { ENTRY_LOW = 10, ENTRY_HIGH = 18 };
    int width_span = city->map_width - ENTRY_LOW - ENTRY_HIGH;
    int height_span = city->map_height - ENTRY_LOW - ENTRY_HIGH;
    switch (Sc2kCrtRand(city) & 3) {
    case CITY_AIRPLANE_SPAWN_NORTH:
      event->tile = MapTileXY(city, Sc2kCrtRandRange(city, width_span) + ENTRY_LOW, 0);
      event->direction = CITY_HEADING_SOUTH_EAST;
      break;
    case CITY_AIRPLANE_SPAWN_WEST:
      event->tile = MapTileXY(city, 0, Sc2kCrtRandRange(city, height_span) + ENTRY_LOW);
      event->direction = CITY_HEADING_NORTH_EAST;
      break;
    case CITY_AIRPLANE_SPAWN_SOUTH:
      event->tile = MapTileXY(city, Sc2kCrtRandRange(city, width_span) + ENTRY_LOW, city->map_height - 1);
      event->direction = CITY_HEADING_NORTH_WEST;
      break;
    default:
      event->tile = MapTileXY(city, city->map_width - 1, Sc2kCrtRandRange(city, height_span) + ENTRY_LOW);
      event->direction = CITY_HEADING_SOUTH_WEST;
      break;
    }
    event->height = 16;
    event->next_slot = 16 * axis + 3;
    event->airplane.destination_y = point.y;
    event->airplane.destination_x = point.x;
    TileIndex event_tile = event->tile;
    event->previous_overlay = ReadCityEventUnderlay(city, event_tile);
    LinkCityMapEvent(city, slot);
    InvalidateTile(city, event_tile);
  }
  ++city->disasters.type1_count;
  InvalidateCityMapEvent(city, slot);
}

// Create the three linked records for one eligible tile-237 event.
bool TryCreateTile237Event(City *city, TileIndex tile_index) {
  assert(IsMapTileValid(city, tile_index));
  CityPoint point = MapTilePt(city, tile_index);
  if (!CanSpawnCityVehicle(city, CITY_MAP_EVENT_TRAIN_ENGINE) ||
      point.y < 2 || point.y > city->map_height - 4 || point.x < 2 || point.x > city->map_width - 4)
    return false;
  int tile = ReadTile(city, tile_index);
  int mask = ReadTransportConnections(&city->tile[tile_index]);
  bool slope = kLineTerrainOverrideBySlope[ReadTerrainCode(&city->tile[tile_index])] != 0;
  if (tile != TK_RAIL || (!slope && (FindCityRailTransition(city, tile_index) >= 0 ||
          mask == 7 || mask == 11 || mask >= 13)) || ReadOverlay(city, tile_index) != 0)
    return false;

  int direction = NextCityMapRandomModulo(city, 4);
  int event_direction = FindTile237EventDirection(city, tile_index, direction, CITY_MAP_EVENT_TRAIN_ENGINE);
  if (event_direction < 0)
    return false;

  CityMapEvents *events = &city->events;
  int free_slots = 0;
  for (int slot = CITY_EVENT_SLOT_FIRST; slot < CityEventAllocationLimit(city); ++slot)
    free_slots += events->event[slot].type == CITY_MAP_EVENT_UNUSED;
  if (free_slots < 3) return false;
  int first_slot = FindFreeCityEventSlot(city);
  CityMapEvent *first = &events->event[first_slot];
  memset(first, 0, sizeof(*first));
  first->type = CITY_MAP_EVENT_TRAIN_ENGINE;
  int second_slot = FindFreeCityEventSlot(city);
  CityMapEvent *second = &events->event[second_slot];
  memset(second, 0, sizeof(*second));
  second->type = CITY_MAP_EVENT_TRAIN_CAR;
  int third_slot = FindFreeCityEventSlot(city);
  CityMapEvent *third = &events->event[third_slot];
  memset(third, 0, sizeof(*third));
  third->type = CITY_MAP_EVENT_TRAIN_CAR;

  first->direction = event_direction;
  second->direction = event_direction;
  third->direction = event_direction;
  first->tile = tile_index;
  second->tile = tile_index;
  third->tile = tile_index;
  SetCityMapEventTarget(city, first, point.x + kCityTrainSpawnXStep[event_direction],
      point.y + kCityTrainSpawnYStep[event_direction]);
  SetCityMapEventTarget(city, second, point.x, point.y);
  SetCityMapEventTarget(city, third, point.x, point.y);
  first->height = 0;
  second->height = 0;
  third->height = 0;
  first->previous_overlay = ReadCityEventUnderlay(city, tile_index);
  second->previous_overlay = 0;
  third->previous_overlay = 0;
  first->next_slot = second_slot;
  second->next_slot = third_slot;
  LinkCityMapEvent(city, first_slot);
  LinkCityMapEvent(city, second_slot);
  LinkCityMapEvent(city, third_slot);
  InvalidateTile(city, tile_index);
  ++city->disasters.tile237_count;
  return true;
}

// Check the four stock tile-237 spawn offsets until one succeeds.
void TrySpawnTile237Events(City *city, TileIndex tile_index) {
  assert(IsMapTileValid(city, tile_index));
  static const i8 offset_y[4] = {0, 2, 0, -1};
  static const i8 offset_x[4] = {-1, 0, 2, 0};
  for (int i = 0; i < 4; ++i) {
    TileIndex candidate = GetRelativeTileOrInvalid(city, tile_index, offset_x[i], offset_y[i]);
    if (candidate != INVALID_TILE && TryCreateTile237Event(city, candidate))
      break;
  }
}

// Create a type-9 event on one empty powered cell.
void TryCreateTile248Event(City *city, TileIndex tile_index) {
  assert(IsMapTileValid(city, tile_index));
  if ((city->fixes & (1 << CITY_FIX_EXPANDED_EVENTS)) && !CanSpawnCityVehicle(city, CITY_MAP_EVENT_SAILBOAT))
    return;
  int slot = FindFreeCityEventSlot(city);
  if (slot == CITY_OVERLAY_NONE || !IsTileKindExposedWater(ReadTile(city, tile_index)) ||
      ReadOverlay(city, tile_index) != CITY_OVERLAY_NONE) return;

  CityMapEvent *event = &city->events.event[slot];
  memset(event, 0, sizeof(*event));
  event->type = CITY_MAP_EVENT_SAILBOAT;
  event->direction = NextCityMapRandomModulo(city, 3);
  event->tile = tile_index;
  SetCityMapEventTarget(city, event, 64, 64);
  event->age = 0;
  event->previous_overlay = 0;
  event->next_slot = CITY_SAILBOAT_SAILING;
  LinkCityMapEvent(city, slot);
  InvalidateTile(city, tile_index);
  ++city->disasters.tile248_count;
}

// Create type-9 events on powered empty cells around tile 248.
void TrySpawnTile248Events(City *city, TileIndex tile_index) {
  assert(IsMapTileValid(city, tile_index));
  static const i8 offset_y[4] = {0, 1, 0, -1};
  static const i8 offset_x[4] = {-1, 0, 1, 0};
  if (!CanSpawnCityVehicle(city, CITY_MAP_EVENT_SAILBOAT))
    return;

  for (int i = 0; i < 4; ++i) {
    TileIndex candidate = GetRelativeTileOrInvalid(city, tile_index, offset_x[i], offset_y[i]);
    if (candidate == INVALID_TILE) continue;
    if (!HasTileWater(&city->tile[candidate]) || ReadOverlay(city, candidate) != 0) continue;
    TryCreateTile248Event(city, candidate);
  }
}
