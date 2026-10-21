#include "audio/types.h"
#include "city/disasters/types.h"
#include "city/engine/city_types.h"
#include "city/maps/tile_storage.h"
#include "city/presentation.h"
#include <assert.h>
#include <string.h>

#include "audio/audio.h"
#include "city/disasters/disasters.h"
#include "city/engine/simulation.h"
#include "city/events/events.h"
#include "city/infrastructure/bridges.h"
#include "city/maps/layers.h"
#include "city/state.h"
#include "city/tools/tools.h"

// Actors

// Animate an explosion, then apply its nearby disaster effects. Slot must identify a live explosion; animation
// completion may damage tiles and queue subsequent disaster work.
void UpdateCityMapExplosion(City *city, int slot) {
  CityMapEvent *event = &city->events.event[slot];
  CityPoint position = ReadCityMapEventPosition(city, event);
  int stage = event->direction;
  if (stage == CITY_EXPLOSION_START)
    PlayCitySound(city, event->tile, CITY_SOUND_EXPLOSION);
  if (stage <= CITY_EXPLOSION_SECOND_FRAME) {
    ++event->direction;
    return;
  }

  int y = position.y;
  int x = position.x;
  bool trigger_disaster = event->explosion.trigger_disaster != 0;
  int disaster_type = event->state;
  TileIndex tile = event->tile;
  ClearCityMapEventSlot(city, slot);
  WriteOverlay(city, tile, CITY_OVERLAY_NONE);
  bool disaster_applied = false;
  if (trigger_disaster && (city->indicators.event_flag == 0 || city->mission.active != 0)) {
    for (int attempt = 0; attempt < 4; ++attempt) {
      int target_y = y + NextCityMapRandomModulo(city, 5) - 2;
      int target_x = x + NextCityMapRandomModulo(city, 5) - 2;
      TileIndex target = MapTileXYOrInvalid(city, target_x, target_y);
      if (ApplyDisasterCellEffect(city, target, 1))
        disaster_applied = true;
    }
  }
  if (disaster_applied && city->settings.timer_mode != CITY_TIMER_EVENT) {
    SetDisasterLocation((&city->disasters.location), x, y);
    city->disasters.pending_type = disaster_type != CITY_DISASTER_NONE ? disaster_type : CITY_DISASTER_FIRE;
  }
}

// Advance one ship through travel, pier, and departure stages. Slot must identify a live ship with consistent route
// data; departure can clear the slot and decrement its active count.
static void StepCityMapShip(City *city, int slot) {
  CityMapEvent *event = &city->events.event[slot];
  CityPoint position = ReadCityMapEventPosition(city, event);
  ++city->disasters.border_water_count;
  if ((Sc2kCrtRand(city) & 0xFF) == 0)
    PlayCitySound(city, event->tile, CITY_SOUND_SHIP);

  int y = position.y;
  int x = position.x;
  TileIndex tile = event->tile;
  if (tile == INVALID_TILE || !HasTileWater(&city->tile[tile])) {
    CityPoint offset = ReadCityMapEventTarget(city, event);
    memset(event->object_data, 0, sizeof(event->object_data));
    event->type = CITY_MAP_EVENT_EXPLOSION;
    SetCityMapEventTarget(city, event, offset.x, offset.y);
    event->next_slot = CITY_DISASTER_NONE;
    event->explosion.trigger_disaster = 0;
    event->direction = CITY_EXPLOSION_START;
    return;
  }

  switch (event->next_slot) {
  case CITY_SHIP_TRAVEL:
    if (NextCityMapRandomModulo(city, 10) == 0) {
      int direction =
          SteerCityEventTowardTile(event->direction, x, y, event->ship.destination_x, event->ship.destination_y);
      if (CanCityEventAdvanceInDirection(city, slot, direction))
        event->direction = (u8)direction;
    }
    if (CanCityEventAdvanceInDirection(city, slot, event->direction))
      AdvanceCityMapEventPosition12(city, slot);
    else
      event->next_slot = CITY_SHIP_TURN_TO_TARGET;

    for (int index = 0; index < 4; ++index) {
      int target_y = y + kBorderWaterTileYOffsets[index];
      int target_x = x + kBorderWaterTileXOffsets[index];
      TileIndex target = MapTileXYOrInvalid(city, target_x, target_y);
      if (target != INVALID_TILE && ReadTile(city, target) == TK_PIER)
        event->next_slot = CITY_SHIP_AT_PIER;
    }
    break;
  case CITY_SHIP_TURN_TO_TARGET: {
    int target = CityEventDirection(position.x, position.y, event->ship.destination_x, event->ship.destination_y);
    event->direction = (u8)TurnCityEventDirectionToward(event->direction, target);
    if (event->direction == target) {
      if (!CanCityEventAdvanceInDirection(city, slot, target))
        event->next_slot = CITY_SHIP_FIND_PASSAGE;
      else
        event->next_slot = CITY_SHIP_TRAVEL;
    }
    break;
  }
  case CITY_SHIP_FIND_PASSAGE: {
    int previous_direction = event->direction;
    const i8 *turn_offsets = NextCityMapRandomModulo(city, 2) != 0 ? kCityEventTurnClockwiseOrder
                                                                   : kCityEventTurnCounterclockwiseOrder;
    int candidate = previous_direction;
    int index;
    for (index = 0; index < 9; ++index) {
      candidate = (previous_direction + turn_offsets[index]) & 7;
      if (CanCityEventAdvanceInDirection(city, slot, candidate))
        break;
    }
    // The original clears only a turn found at index8, not an exhausted search.
    if (index == 8) {
      ClearCityMapEventSlot(city, slot);
      return;
    }
    event->direction = (u8)candidate;
    event->next_slot = CITY_SHIP_TRAVEL;
    break;
  }
  case CITY_SHIP_AT_PIER:
    if (NextCityMapRandomModulo(city, 30) == 0) {
      event->next_slot = CITY_SHIP_DEPART;
      PlayCitySound(city, event->tile, CITY_SOUND_SHIP);
      TileIndex home = event->ship.home_tile != INVALID_TILE ? event->ship.home_tile : event->tile;
      CityPoint point = MapTilePt(city, home);
      event->ship.destination_y = point.y;
      event->ship.destination_x = point.x;
    }
    break;
  case CITY_SHIP_DEPART:
    if (CanCityEventAdvanceInDirection(city, slot, event->direction)) {
      AdvanceCityMapEventPosition12(city, slot);
    } else {
      int previous_direction = event->direction;
      int index = NextCityMapRandomModulo(city, 2);
      int candidate = previous_direction;
      for (; index < 9; ++index) {
        candidate = (previous_direction + kCityEventTurnClockwiseOrder[index]) & 7;
        if (CanCityEventAdvanceInDirection(city, slot, candidate))
          break;
      }
      event->direction = (u8)candidate;
    }
    break;
  default:
    break;
  }
}

void UpdateCityMapShip(City *city, int slot) {
  TileIndex previous = city->events.event[slot].tile;
  StepCityMapShip(city, slot);
  const CityMapEvent *event = &city->events.event[slot];
  // ClearCityMapEventSlot already notifies bridge damage when the ship leaves its pool slot.
  if (event->type != CITY_MAP_EVENT_UNUSED)
    InvalidateCityDrawbridgesForShip(city, slot, previous,
        event->type == CITY_MAP_EVENT_SHIP ? event->tile : INVALID_TILE);
}

// Reject cells that block an event's surface destination. Invalid indices and occupied overlays are rejected; the
// predicate neither reserves a slot nor changes terrain.
bool IsCityEventTileUnoccupiedAndBuildable(City *city, TileIndex tile_index) {
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

// Spawn a ship from the last flat open-water cell on a randomly selected border; rivers and banks are excluded.
// Source must be valid. Admission consumes selection draws even when no ship is spawned.
void SpawnBorderWaterEvent(City *city, TileIndex source) {
  assert(source < (u32)city->map_cell_count);
  if (!CanSpawnCityVehicle(city, CITY_MAP_EVENT_SHIP))
    return;
  int side = Sc2kCrtRand(city) & 3;
  bool horizontal_border = side < CITY_SHIP_SPAWN_WEST;
  int border_coordinate = side == CITY_SHIP_SPAWN_NORTH || side == CITY_SHIP_SPAWN_WEST ? 2 :
      (horizontal_border ? city->map_height : city->map_width) - 2;
  TileIndex tile_index = INVALID_TILE;
  TileIndex at = horizontal_border ? border_coordinate * city->map_width : border_coordinate;
  int stride = horizontal_border ? 1 : city->map_width;
  int length = horizontal_border ? city->map_width : city->map_height;
  for (int position = 0; position < length; ++position, at += stride) {
    const TileInfo *tp = &city->tile[at];
    if (ReadCityWaterGroundKind(tp) == TK_WATER && !ReadTerrainCode(tp))
      tile_index = at;
  }
  if (tile_index == INVALID_TILE)
    return;
  int overlay = ReadOverlay(city, tile_index);
  if (IsCityEventOrMarkerOverlay(overlay))
    return;
  CityMapEvents *events = &city->events;
  int slot = FindFreeCityEventSlot(city);
  if (slot == 0)
    return;
  CityMapEvent *event = &events->event[slot];
  memset(event, 0, sizeof(*event));
  event->type = CITY_MAP_EVENT_SHIP;
  CityPoint destination = MapTilePt(city, tile_index);
  CityPoint origin = MapTilePt(city, source);
  event->direction = CityEventDirection(destination.x, destination.y, origin.x, origin.y);
  event->tile = tile_index;
  SetCityMapEventTarget(city, event, 128, 128);
  event->ship.home_tile = tile_index;
  event->height = 1;
  event->previous_overlay = overlay;
  event->next_slot = CITY_SHIP_TRAVEL;
  city->disasters.border_water_y = destination.y;
  city->disasters.border_water_x = destination.x;
  PlayCitySound(city, source, CITY_SOUND_SHIP);
  ++city->disasters.border_water_count;
  LinkCityMapEvent(city, slot);
  InvalidateCityDrawbridgesForShip(city, slot, INVALID_TILE, event->tile);
}

// Return the first unused event slot, excluding reserved slot zero. Returns an admitted unused index or the no-slot
// result; it does not reserve or initialize the selected record.
int FindFreeCityEventSlot(City *city) {
  for (int slot = CITY_EVENT_SLOT_FIRST; slot < CityEventAllocationLimit(city); ++slot)
    if (city->events.event[slot].type == CITY_MAP_EVENT_UNUSED)
      return slot;
  return 0;
}

static bool IsCityMaxisManTargetSlot(const City *city, int slot) {
  if (slot < CITY_EVENT_SLOT_FIRST || slot >= city->events.capacity) return false;
  const CityMapEvent *event = &city->events.event[slot];
  return (event->type == CITY_MAP_EVENT_MONSTER || event->type == CITY_MAP_EVENT_TORNADO) &&
      IsMapTileValid(city, event->tile);
}

// Dispatch Maxis Man near the latest disaster. Event targets must still identify the corresponding live disaster;
// marker targets use the disaster location. Searches and event creation may consume RNG and update active counters.
void CreateCityMapMaxisMan(City *city, int disaster_type) {
  if (city->disasters.type16_count >= 1)
    return;
  if (disaster_type == CITY_DISASTER_TORNADO || disaster_type == CITY_DISASTER_MONSTER) {
    int target = city->disasters.last_event_slot;
    int type = disaster_type == CITY_DISASTER_TORNADO ? CITY_MAP_EVENT_TORNADO : CITY_MAP_EVENT_MONSTER;
    if (!IsCityMaxisManTargetSlot(city, target) || city->events.event[target].type != type) return;
  }

  int slot = FindFreeCityEventSlot(city);
  if (slot == 0)
    return;

  int y = city->disasters.location.y;
  int x = city->disasters.location.x;
  switch (Sc2kCrtRand(city) & 3) {
  case CITY_DISASTER_SPAWN_SOUTH:
    y += 16;
    y = IntMin(y, city->map_height - 1);
    break;
  case CITY_DISASTER_SPAWN_EAST:
    x += 16;
    x = IntMin(x, city->map_width - 1);
    break;
  case CITY_DISASTER_SPAWN_NORTH:
    y -= 16;
    y = IntMax(y, 0);
    break;
  case CITY_DISASTER_SPAWN_WEST:
    x -= 16;
    x = IntMax(x, 0);
    break;
  }

  TileIndex tile_index = MapTileXY(city, x, y);
  CityMapEvent *event = &city->events.event[slot];
  memset(event, 0, sizeof(*event));
  event->type = CITY_MAP_EVENT_MAXIS_MAN;
  event->direction = CityEventDirection(x, y, city->disasters.location.x, city->disasters.location.y);
  event->next_slot = 0;
  event->tile = tile_index;
  event->height = (u8)(ReadCityLandHeight(&city->tile[tile_index]) + 2);
  SetCityMapEventTarget(city, event, 128, 128);
  event->maxis_man.destination_y = city->disasters.location.y;
  event->maxis_man.destination_x = city->disasters.location.x;
  event->previous_overlay = ReadCityEventUnderlay(city, tile_index);
  switch (disaster_type) {
  case CITY_DISASTER_FIRE:
  case CITY_DISASTER_CRASH:
  case CITY_DISASTER_EARTHQUAKE:
  case CITY_DISASTER_MELTDOWN:
  case CITY_DISASTER_MICROWAVE:
  case CITY_DISASTER_VOLCANO:
  case CITY_DISASTER_FIRE_STORM:
    event->maxis_man.goal = 0xFF;
    break;
  case CITY_DISASTER_FLOOD:
  case CITY_DISASTER_MAJOR_FLOOD:
    event->maxis_man.goal = 0xFC;
    break;
  case CITY_DISASTER_RIOTS:
  case CITY_DISASTER_MASS_RIOTS:
    event->maxis_man.goal = (u8)((Sc2kCrtRand(city) & 1) - 3);
    break;
  case CITY_DISASTER_POLLUTION:
  case CITY_DISASTER_CHEMICAL_SPILL:
    event->maxis_man.goal = 0xFB;
    break;
  case CITY_DISASTER_TORNADO:
  case CITY_DISASTER_MONSTER:
    event->maxis_man.goal = CityEventOverlay(city->disasters.last_event_slot);
    break;
  }

  CenterCityMapViewAndResetOverlays(city, x, y, 2);
  PlayCitySound(city, tile_index, CITY_SOUND_CIVIC_PLACEMENT);
  ++city->disasters.type16_count;
  LinkCityMapEvent(city, slot);
}

// Create an effect event if the destination overlay is available. Requires an admitted event type and valid target;
// returns its created-slot result or failure without transferring record ownership.
int CreateCityMapEffectEvent(City *city, TileIndex tile_index, int variant, int state, int linked) {
  assert(tile_index < (u32)city->map_cell_count);
  int slot = FindFreeCityEventSlot(city);
  if (slot == CITY_OVERLAY_NONE || IsCityEventOrMarkerOverlay(ReadOverlay(city, tile_index)))
    return slot;

  CityMapEvent *event = &city->events.event[slot];
  memset(event, 0, sizeof(*event));
  event->type = CITY_MAP_EVENT_EXPLOSION;
  event->direction = CITY_EXPLOSION_START;
  event->next_slot = state;
  event->tile = tile_index;
  event->height = (u8)variant;
  SetCityMapEventTarget(city, event, 128, 128);
  event->previous_overlay = ReadCityEventUnderlay(city, tile_index);
  event->explosion.trigger_disaster = linked;
  LinkCityMapEvent(city, slot);
  return slot - 55;
}

// Move Maxis Man toward disaster overlays or a linked target. Slot must contain Maxis Man; movement and target
// completion update event/hash state and active-disaster counters.
void UpdateCityMapMaxisMan(City *city, int slot) {
  CityMapEvent *event = &city->events.event[slot];
  CityPoint position = ReadCityMapEventPosition(city, event);
  int target_marker = event->maxis_man.goal;
  int target_slot = IsCityEventOverlay(target_marker) ? CityEventOverlaySlot(target_marker) : -1;
  // Targets can disappear or their slots can be reused while Maxis Man is taking a random step or departing.
  // Validate before every movement branch, including the height update that follows those branches.
  if (!IsCityMarkerOverlay(target_marker)) {
    if (target_slot < CITY_EVENT_SLOT_FIRST || target_slot >= city->events.capacity) {
      ClearCityMapEventSlot(city, slot);
      return;
    }
    if (!IsCityMaxisManTargetSlot(city, target_slot)) event->next_slot = CITY_MAXIS_MAN_DEPART;
  }
  ++city->disasters.type16_count;
  // Stock probes can leave the map and dereference unmapped guest rows. End invalid events safely.
  if (position.y >= city->map_height || position.x >= city->map_width) {
    ClearCityMapEventSlot(city, slot);
    return;
  }

  int target_y;
  int target_x;
  if (event->next_slot != CITY_MAXIS_MAN_TRACK_TARGET) {
    if (event->next_slot == CITY_MAXIS_MAN_RANDOM_STEP) {
      if ((Sc2kCrtRand(city) & 3) != 0) {
        int direction = Sc2kCrtRand(city) & 7;
        target_y = position.y + kCityMapEventSubtileYStep[direction];
        // The original random-step probe uses y for both coordinates.
        target_x = position.y + kCityMapEventSubtileXStep[direction];
        TileIndex target = MapTileXYOrInvalid(city, target_x, target_y);
        if (target != INVALID_TILE && !IsCityEventOrMarkerOverlay(ReadOverlay(city, target))) {
          AdvanceCityMapEventPosition(city, CITY_MAP_EVENT_MAXIS_MAN, slot, direction);
          if (event->type == CITY_MAP_EVENT_UNUSED) return;
          position = ReadCityMapEventPosition(city, event);
          event->direction = (u8)direction;
        }
      } else {
        event->next_slot = CITY_MAXIS_MAN_TRACK_TARGET;
      }
    } else if (event->next_slot == CITY_MAXIS_MAN_DEPART) {
      AdvanceCityMapEventPosition(city, CITY_MAP_EVENT_MAXIS_MAN, slot, event->direction);
      if (event->type == CITY_MAP_EVENT_UNUSED) return;
      position = ReadCityMapEventPosition(city, event);
      AdvanceCityMapEventPosition(city, CITY_MAP_EVENT_MAXIS_MAN, slot, event->direction);
      if (event->type == CITY_MAP_EVENT_UNUSED) return;
      position = ReadCityMapEventPosition(city, event);
    }
  } else {
    if (IsCityMarkerOverlay(target_marker)) {
      target_y = event->maxis_man.destination_y;
      target_x = event->maxis_man.destination_x;
      TileIndex target = MapTileXYOrInvalid(city, target_x, target_y);
      if (target == INVALID_TILE || !IsCityMarkerOverlay(ReadOverlay(city, target))) {
        event->next_slot = CITY_MAXIS_MAN_DEPART;
        target_y = position.y - 32;
        for (; target_y <= position.y + 32 && event->next_slot == CITY_MAXIS_MAN_DEPART; ++target_y) {
          for (target_x = position.x - 32; target_x <= position.x + 32 && event->next_slot == CITY_MAXIS_MAN_DEPART;
              ++target_x) {
            target = MapTileXYOrInvalid(city, target_x, target_y);
            if (target != INVALID_TILE && IsCityMarkerOverlay(ReadOverlay(city, target))) {
              event->maxis_man.destination_y = target_y;
              event->maxis_man.destination_x = target_x;
              event->next_slot = CITY_MAXIS_MAN_TRACK_TARGET;
            }
          }
        }
        if (event->next_slot == CITY_MAXIS_MAN_DEPART)
          goto update_height;
      }
    } else {
      CityMapEvent *target_event = &city->events.event[target_slot];
      CityPoint target_position = ReadCityMapEventPosition(city, target_event);
      target_y = target_position.y;
      target_x = target_position.x;
    }

    int direction = CityEventDirection(position.x, position.y, target_x, target_y);
    event->direction = (u8)direction;
    int next_y = position.y + kCityMapEventSubtileYStep[direction];
    int next_x = position.x + kCityMapEventSubtileXStep[direction];
    TileIndex next = MapTileXYOrInvalid(city, next_x, next_y);
    if (next == INVALID_TILE) goto update_height;
    int overlay = ReadOverlay(city, next);
    if (overlay <= CITY_OVERLAY_BORDER_CONNECTION || IsCityEventOverlay(overlay) ||
        IsCitySignOverlay(overlay) || IsCityFacilityOverlay(overlay)) {
      if (!IsCityEventOrMarkerOverlay(overlay)) {
        AdvanceCityMapEventPosition(city, CITY_MAP_EVENT_MAXIS_MAN, slot, direction);
        if (event->type == CITY_MAP_EVENT_UNUSED) return;
        position = ReadCityMapEventPosition(city, event);
        next_y = position.y + kCityMapEventSubtileYStep[direction];
        next_x = position.x + kCityMapEventSubtileXStep[direction];
        next = MapTileXYOrInvalid(city, next_x, next_y);
        if (next != INVALID_TILE && !IsCityEventOrMarkerOverlay(ReadOverlay(city, next))) {
          AdvanceCityMapEventPosition(city, CITY_MAP_EVENT_MAXIS_MAN, slot, direction);
          if (event->type == CITY_MAP_EVENT_UNUSED) return;
          position = ReadCityMapEventPosition(city, event);
        }
      } else if (event->maxis_man.goal != overlay || (Sc2kCrtRand(city) & 3) != 0) {
        event->next_slot = CITY_MAXIS_MAN_RANDOM_STEP;
      } else {
        PlayCitySound(city, event->tile, CITY_SOUND_EXPLOSION);
        ClearCityMapEventSlot(city, target_slot);
        CreateCityMapEffectEvent(city, next, event->height, 0, 1);
        event->next_slot = CITY_MAXIS_MAN_DEPART;
      }
    } else if ((Sc2kCrtRand(city) & 1) != 0) {
      WriteOverlay(city, next, CITY_OVERLAY_NONE);
      AdvanceCityMapEventPosition(city, CITY_MAP_EVENT_MAXIS_MAN, slot, direction);
      if (event->type == CITY_MAP_EVENT_UNUSED) return;
      position = ReadCityMapEventPosition(city, event);
    }
  }

update_height:
  if (IsCityMarkerOverlay(event->maxis_man.goal)) {
    int y = position.y;
    int x = position.x;
    if (y >= city->map_height || x >= city->map_width) {
      ClearCityMapEventSlot(city, slot);
      return;
    }
    TileIndex tile_index = MapTileXY(city, x, y);
    int power = ReadAltitudeWord(city, tile_index);
    if (HasTileWater(&city->tile[tile_index]))
      event->height = (u8)(event->next_slot + ((power >> CITY_ALTITUDE_WATER_SHIFT) & CITY_ALTITUDE_LAND_MASK));
    else
      event->height = (u8)(event->next_slot + (power & CITY_ALTITUDE_LAND_MASK));
  } else if (IsCityMaxisManTargetSlot(city, target_slot)) {
    event->height = city->events.event[target_slot].height;
  }
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

  if (IsCityEventTileUnoccupiedAndBuildable(city, current_tile)) {
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
    CopyCityMapLinkedEventRecord(city, first_car_slot, last_car_slot);
    CopyCityMapLinkedEventRecord(city, slot, first_car_slot);
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
    int turned_y = y + CityTrainUpdateYStep(turned_direction);
    int turned_x = x + CityTrainUpdateXStep(turned_direction);
    TileIndex turned = MapTileXYOrInvalid(city, turned_x, turned_y);
    if (turned != INVALID_TILE && IsCityEventTargetTile(city, turned, event_type))
      direction = turned_direction;
    if ((Sc2kCrtRand(city) & 0xFF) == 0)
      PlayCitySound(city, next_tile, CITY_SOUND_TRAIN);
  }

  target_y = y + CityTrainUpdateYStep(direction);
  target_x = x + CityTrainUpdateXStep(direction);
  TileIndex target_tile = MapTileXYOrInvalid(city, target_x, target_y);
  if (target_tile != INVALID_TILE && IsCityEventTargetTile(city, target_tile, event_type)) {
    engine->direction &= 0x0F;
    engine->train.animation_frame = 2 * direction;
    SetCityMapEventTarget(city, engine, target_x, target_y);
    return;
  }

  int next_direction = FindTile237EventDirection(city, next_tile, direction, event_type);
  if (next_direction >= 0) {
    engine->direction = (u8)next_direction;
    engine->train.animation_frame = CityTrainUpdateAnimation(direction, next_direction);
    SetCityMapEventTarget(city, engine, x + CityTrainUpdateXStep(next_direction),
        y + CityTrainUpdateYStep(next_direction));
  } else {
    SwapCityMapEventPositions(city, slot, last_car_slot);
  }
}

// Move a tornado and bulldoze the tile under it. Slot must identify a live tornado; terrain destruction and event
// movement are deterministic simulation effects.
void UpdateCityMapTornado(City *city, int slot) {
  CityMapEvent *event = &city->events.event[slot];
  ++city->disasters.tornado_count;

  int tile = ReadTile(city, event->tile);
  bool survives_bulldoze = false;
  if (!IsTileKindBareGround(tile)) {
    BulldozeCityTile(city, event->tile, true);
    survives_bulldoze = !IsTileKindUnbuilt(tile);
  }

  int direction = event->direction;
  int clockwise_turn = Sc2kCrtRandRange(city, 3);
  int counterclockwise_turn = Sc2kCrtRandRange(city, 3);
  AdvanceCityMapEventPosition(
      city, CITY_MAP_EVENT_TORNADO, slot, (direction + clockwise_turn - counterclockwise_turn) & CITY_HEADING_MASK);
  if ((Sc2kCrtRand(city) & 0xFF) == 0)
    ClearCityMapEventSlot(city, slot);

  if (!survives_bulldoze) {
    int left_turn = Sc2kCrtRand(city) & 1;
    int right_turn = Sc2kCrtRand(city) & 1;
    if (event->type != CITY_MAP_EVENT_UNUSED)
      AdvanceCityMapEventPosition(
          city, CITY_MAP_EVENT_TORNADO, slot, (direction + left_turn - right_turn) & CITY_HEADING_MASK);
    if ((Sc2kCrtRand(city) & 0xFF) == 0)
      ClearCityMapEventSlot(city, slot);
  }
}

// Tables

const i8 kCityMapEventSubtileYStep[8] = {
  0, 1, 1, 1, 0, -1, -1, -1
};

const i8 kCityMapEventSubtileXStep[8] = {
  -1, -1, 0, 1, 1, 1, 0, -1
};

const i8 kCityMapEvent12StepY[8] = {
  0, 3, 4, 3, 0, -3, -4, -3
};

const i8 kCityMapEvent12StepX[8] = {
  -4, -3, 0, 3, 4, 3, 0, -3
};

const i8 kCityMapEvent16StepY[8] = {
  0, 16, 0, -16, -16, 0, 16, 0
};

const i8 kCityMapEvent16StepX[8] = {
  -16, 0, 16, 0, 0, 1, 0, -1
};

const i8 kCityEventAdjacentYOffset[8] = {
  0, 1, 0, -1, 0, 0, 0, 0
};

const i8 kCityEventAdjacentXOffset[8] = {
  -1, 0, 1, 0, 0, 0, 0, 0
};

const u8 kCityMapEventSubtileSpeed[17] = {
  0, 16, 8, 4, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 16
};

const u8 kCityEventDirectionSearchOrder[7] = {
  1, 7, 2, 6, 3, 5, 4
};

const u8 kCityEventTile237DirectionOrderA[4] = {
  0, 1, 3, 2
};

const u8 kCityEventTile237DirectionOrderB[4] = {
  0, 3, 1, 2
};

const i8 kCityEventTile237DirectionYOffset[4] = {
  0, 1, 0, -1
};

const i8 kCityEventTile237DirectionXOffset[4] = {
  -1, 0, 1, 0
};

const i8 kCityEventOverlayBlockYOffset[8] = {
  0, 3, 4, 3, 0, -3, -4, -3
};

const i8 kCityEventOverlayBlockXOffset[8] = {
  -4, -3, 0, 3, 4, 3, 0, -3
};

const i8 kCityEventTurnClockwiseOrder[9] = {
  0, 1, 2, 3, 4, 5, 6, 7, 0
};

const i8 kCityEventTurnCounterclockwiseOrder[9] = {
  0, 7, 6, 5, 4, 3, 2, 1, 2
};

const i8 kBorderWaterTileYOffsets[4] = {
  2, 0, -2, 0
};

const i8 kBorderWaterTileXOffsets[4] = {
  0, 2, 0, -2
};

const i8 kCityEventDestinationYOffset[8] = {
  0, 3, 3, 3, 0, -3, -3, -3
};

const i8 kCityEventDestinationXOffset[8] = {
  -3, -3, 0, 3, 3, 3, 0, -3
};

const u8 kCityMapEventInitialAge[24] = {
  0, 6, 4, 6, 0, 8, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 9, 10, 11, 12, 13, 3, 1, 0
};

const i8 kCityTrainSpawnYStep[4] = {
  0, 1, 0, -1
};

const i8 kCityTrainSpawnXStep[4] = {
  -1, 0, 1, 0
};

const i8 kCityTrainUpdateYStep[16] = {
  0, 1, 0, -1, 0, 0, 0, 0, -1, 0, 1, 0, 0, 0, 0, 0
};

const i16 kCityTrainUpdateXStep[16] = {
  -1, 0, 1, 0, 0, 0, 0, 0, 256, 1794, 513, 1027, 770, 1284, 1031, 1541
};

const u8 kCityTrainUpdateAnimation[64] = {
  0, 1, 2, 7, 1, 2, 3, 4, 2, 3, 4, 5, 7, 4, 5, 6, 0, 1, 3, 2, 0, 3, 1, 2, 0, 0, 1, 0, 0, 0, 255, 255, 255,
  255, 0, 0, 1, 0, 0, 0, 0, 1, 3, 2, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 255, 255, 255, 255, 0, 0, 1, 0, 0, 0
};
