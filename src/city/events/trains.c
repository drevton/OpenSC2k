// Surface trains and subways own their spawn, linked coaches, route choice and reversal rules here.
// TileInfo supplies kind, transport connections, slope and underground track; trains add no private tile bits.
// Shared event movement maintains hash membership while this module copies each coach's route state.

#include "city/maps/labels.h"
#include "city/events/events.h"

#include <assert.h>
#include <string.h>

#include "game/audio.h"
#include "city/engine/random.h"
#include "city/infrastructure/lines.h"
#include "city/infrastructure/surface_lines.h"
#include "city/maps/layers.h"
#include "city/presentation.h"
#include "city/state.h"

static const u8 kCityEventTile237DirectionOrderA[4] = {
  0, 1, 3, 2
};

static const u8 kCityEventTile237DirectionOrderB[4] = {
  0, 3, 1, 2
};

static const i8 kCityEventTile237DirectionYOffset[4] = {
  0, 1, 0, -1
};

static const i8 kCityEventTile237DirectionXOffset[4] = {
  -1, 0, 1, 0
};

static const i8 kCityTrainSpawnYStep[4] = {
  0, 1, 0, -1
};

static const i8 kCityTrainSpawnXStep[4] = {
  -1, 0, 1, 0
};

// Train updates consume all 16 low-nibble direction values; only entries 0..3 are cardinal directions.
// Keep the other values for simulation compatibility. Drawing uses ordinary cardinal vectors.
static const i8 kCityTrainUpdateYStep[16] = {
  0, 1, 0, -1, 0, 0, 0, 0, -1, 0, 1, 0, 0, 0, 0, 0
};

static const i16 kCityTrainUpdateXStep[16] = {
  -1, 0, 1, 0, 0, 0, 0, 0, 256, 1794, 513, 1027, 770, 1284, 1031, 1541
};

static const u8 kCityTrainUpdateAnimation[64] = {
  0, 1, 2, 7, 1, 2, 3, 4, 2, 3, 4, 5, 7, 4, 5, 6, 0, 1, 3, 2, 0, 3, 1, 2, 0, 0, 1, 0, 0, 0, 255, 255, 255,
  255, 0, 0, 1, 0, 0, 0, 0, 1, 3, 2, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 255, 255, 255, 255, 0, 0, 1, 0, 0, 0
};

// Admit a candidate rail/rail-bridge tile for a surface engine, or subway/connector tile for a subway engine.
// Invalid or event-occupied tiles are rejected. This query neither reserves the destination nor consumes RNG.
static bool CanCityTrainEnterTile(City *city, TileIndex tile_index, int event_type) {
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
static void CopyCityTrainCoachState(City *city, int source_slot, int destination_slot) {
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
void SwapCityTrainEnds(City *city, int first_slot, int second_slot) {
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
static i32 FindCityTrainExitDirection(City *city, TileIndex tile_index, int direction, int event_type) {
  assert(IsMapTileValid(city, tile_index));
  int order = NextEventOrientationRandom(city, 2);
  for (int attempt = 0; attempt < 4; ++attempt) {
    int turn =
        order != 0 ? kCityEventTile237DirectionOrderA[attempt] : kCityEventTile237DirectionOrderB[attempt];
    int candidate_direction = (turn + direction) & 3;
    TileIndex target = GetRelativeTileOrInvalid(city, tile_index,
        kCityEventTile237DirectionXOffset[candidate_direction], kCityEventTile237DirectionYOffset[candidate_direction]);
    if (target == INVALID_TILE) continue;
    if (CanCityTrainEnterTile(city, target, event_type) && !IsCityEventOrMarkerOverlay(ReadOverlay(city, target)))
      return candidate_direction;
  }
  return -1;
}

// Create an engine and two linked coaches on an eligible plain rail tile.
// Spawn direction consumes City RNG; failed pool admission leaves no partially allocated train.
bool TryCreateCityTrain(City *city, TileIndex tile_index) {
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
  int event_direction = FindCityTrainExitDirection(city, tile_index, direction, CITY_MAP_EVENT_TRAIN_ENGINE);
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

// Check the four fixed offsets around the rail station footprint until one train is spawned.
void TrySpawnCityStationTrains(City *city, TileIndex tile_index) {
  assert(IsMapTileValid(city, tile_index));
  static const i8 offset_y[4] = {0, 2, 0, -1};
  static const i8 offset_x[4] = {-1, 0, 2, 0};
  for (int i = 0; i < 4; ++i) {
    TileIndex candidate = GetRelativeTileOrInvalid(city, tile_index, offset_x[i], offset_y[i]);
    if (candidate != INVALID_TILE && TryCreateCityTrain(city, candidate))
      break;
  }
}

// Detect removed track beneath a train. Surface rails, rail bridges, subway track and connectors keep it alive;
// this query does not test event occupancy or change the tile.
static bool IsCityTrainTrackRemoved(City *city, TileIndex tile_index) {
  if (!IsMapTileValid(city, tile_index))
    return false;
  int tile = ReadTile(city, tile_index);
  if (ReadCityTileClasses(tile) & (TCLASS_RAIL | TCLASS_RAIL_BRIDGE))
    return false;
  int underground = ReadUnderground(city, tile_index);
  if (IsSubwayTile(underground))
    return false;
  return !IsTileKindTrainTransition(tile);
}

// Move a train or subway engine with its two linked cars. Slot must identify an engine; missing, reused or aliased
// follower slots stop movement before unrelated event records can be read as train payloads or changed.
void UpdateCityMapTrain(City *city, int slot) {
  CityMapEvent *engine = &city->events.event[slot];
  CityPoint position = ReadCityMapEventPosition(city, engine);
  int first_car_slot = engine->next_slot;
  if (first_car_slot < CITY_EVENT_SLOT_FIRST || first_car_slot >= city->events.capacity || first_car_slot == slot)
    return;
  CityMapEvent *first_car = &city->events.event[first_car_slot];
  if (first_car->type != CITY_MAP_EVENT_TRAIN_CAR && first_car->type != CITY_MAP_EVENT_SUBWAY_CAR) return;
  int last_car_slot = first_car->next_slot;
  if (last_car_slot < CITY_EVENT_SLOT_FIRST || last_car_slot >= city->events.capacity ||
      last_car_slot == slot || last_car_slot == first_car_slot)
    return;
  CityMapEvent *last_car = &city->events.event[last_car_slot];
  if (last_car->type != CITY_MAP_EVENT_TRAIN_CAR && last_car->type != CITY_MAP_EVENT_SUBWAY_CAR) return;
  int original_type = engine->type;
  int y = position.y;
  int x = position.x;
  CityPoint target_position = ReadCityMapEventTarget(city, engine);
  int target_y = target_position.y;
  int target_x = target_position.x;
  TileIndex current_tile = engine->tile;
  TileIndex next_tile = engine->train.next_tile;

  if (original_type == CITY_MAP_EVENT_TRAIN_ENGINE && NextCityMapRandomModulo(city, 2) != 0) {
    int orientation = NextEventOrientationRandom(city, 2) + ((engine->direction & 1) != 0 ? 2 : 0);
    static const i8 station_probe_step[4][2] = {
        [CITY_RAIL_PROBE_NORTH] = {0, -1},
        [CITY_RAIL_PROBE_SOUTH] = {0, 1},
        [CITY_RAIL_PROBE_WEST] = {-1, 0},
        [CITY_RAIL_PROBE_EAST] = {1, 0},
    };
    int probe_y = y + station_probe_step[orientation][1];
    int probe_x = x + station_probe_step[orientation][0];
    TileIndex probe = MapTileXYOrInvalid(city, probe_x, probe_y);
    if (probe != INVALID_TILE && ReadTile(city, probe) == TK_RAIL_STATION)
      return;
  }

  if (IsCityTrainTrackRemoved(city, current_tile)) {
    int train_slots[] = {slot, first_car_slot, last_car_slot};
    CityMapEvent *train[] = {engine, first_car, last_car};
    for (int car = 0; car < 3; ++car) {
      TileIndex car_tile = train[car]->tile;
      if (ReadOverlay(city, car_tile) == CityEventOverlay(train_slots[car]))
        WriteOverlay(city, car_tile, CITY_OVERLAY_NONE);
    }
    for (int car = 0; car < 3; ++car)
      ClearCityMapEventSlot(city, train_slots[car]);
    --city->disasters.tile237_count;
    CreateCityMapEffectEvent(city, current_tile, 0, 0, 0);
    return;
  }

  int target_overlay = ReadOverlay(city, next_tile);
  // Reversing sets the next position to the engine's new current tile. Its own marker does not block it.
  if (IsCityEventOrMarkerOverlay(target_overlay) && target_overlay != CityEventOverlay(last_car_slot) &&
      !(next_tile == current_tile && target_overlay == CityEventOverlay(slot)))
    return;
  if (y != target_y || x != target_x) {
    CopyCityTrainCoachState(city, first_car_slot, last_car_slot);
    CopyCityTrainCoachState(city, slot, first_car_slot);
    engine->previous_overlay = ReadCityStockBaseOverlay(city, next_tile);
  }

  MoveCityMapEvent(city, slot, next_tile);
  y = target_y;
  x = target_x;
  int direction = engine->direction & 0x0F;
  int tile = ReadTile(city, next_tile);
  if (IsTileKindTrainTransition(tile))
    engine->type =
        original_type == CITY_MAP_EVENT_TRAIN_ENGINE ? CITY_MAP_EVENT_SUBWAY_ENGINE : CITY_MAP_EVENT_TRAIN_ENGINE;
  int event_type = engine->type;

  if (NextCityMapRandomModulo(city, 4) == 0) {
    int turned_direction = (direction + (NextCityMapRandomModulo(city, 2) != 0 ? 1 : -1)) & 3;
    int turned_y = y + kCityTrainUpdateYStep[turned_direction];
    int turned_x = x + kCityTrainUpdateXStep[turned_direction];
    TileIndex turned = MapTileXYOrInvalid(city, turned_x, turned_y);
    if (turned != INVALID_TILE && CanCityTrainEnterTile(city, turned, event_type))
      direction = turned_direction;
    if ((Sc2kCrtRand(city) & 0xFF) == 0)
      PlayCityAmbientSound(city, next_tile, CITY_SOUND_TRAIN);
  }

  target_y = y + kCityTrainUpdateYStep[direction];
  target_x = x + kCityTrainUpdateXStep[direction];
  TileIndex target_tile = MapTileXYOrInvalid(city, target_x, target_y);
  if (target_tile != INVALID_TILE && CanCityTrainEnterTile(city, target_tile, event_type)) {
    engine->direction &= 0x0F;
    engine->train.animation_frame = 2 * direction;
    SetCityMapEventTarget(city, engine, target_x, target_y);
    return;
  }

  int next_direction = FindCityTrainExitDirection(city, next_tile, direction, event_type);
  if (next_direction >= 0) {
    engine->direction = (u8)next_direction;
    engine->train.animation_frame = kCityTrainUpdateAnimation[4 * direction + next_direction];
    SetCityMapEventTarget(city, engine, x + kCityTrainUpdateXStep[next_direction],
        y + kCityTrainUpdateYStep[next_direction]);
  } else {
    SwapCityTrainEnds(city, slot, last_car_slot);
  }
}
