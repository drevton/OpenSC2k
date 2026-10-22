// Disaster actors own their spawn, action and expiry rules. Movement and slot membership use the shared event pool.
// Tile effects go through the owning terrain/building operations; this module adds no private TileInfo bits.

#include "city/maps/labels.h"
#include "city/events/events.h"

#include <assert.h>
#include <string.h>

#include "game/audio.h"
#include "city/disasters/disasters.h"
#include "city/disasters/types.h"
#include "city/engine/random.h"
#include "city/maps/buildings.h"
#include "city/maps/layers.h"
#include "city/maps/water.h"
#include "city/presentation.h"
#include "city/state.h"
#include "city/tools/tools.h"

// Animate an explosion, then apply its nearby disaster effects. Slot must identify a live explosion; animation
// completion may damage tiles and queue subsequent disaster work.
void UpdateCityMapExplosion(City *city, int slot) {
  CityMapEvent *event = &city->events.event[slot];
  CityPoint position = ReadCityMapEventPosition(city, event);
  int stage = event->direction;
  if (stage == CITY_EXPLOSION_START)
    PlayCityAmbientSound(city, event->tile, CITY_SOUND_EXPLOSION);
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
  event->direction = CityEventHeadingTowardPoint(x, y, city->disasters.location.x, city->disasters.location.y);
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

    int direction = CityEventHeadingTowardPoint(position.x, position.y, target_x, target_y);
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
        PlayCityAmbientSound(city, event->tile, CITY_SOUND_EXPLOSION);
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

// Advance the monster and apply its tile effect. Slot must contain a live monster; movement can apply disaster damage
// and update other linked event state.
void UpdateCityMapMonster(City *city, int slot) {
  CityMapEvent *event = &city->events.event[slot];
  CityPoint position = ReadCityMapEventPosition(city, event);
  // Invalid stock states read indeterminate stack data; directions index eight-entry tables.
  if (event->state > CITY_MONSTER_LEAVE || event->direction > CITY_HEADING_MASK)
    return;
  int y = position.y;
  int x = position.x;
  int direction = event->direction;
  ++city->disasters.monster_count;

  if (city->disasters.type1_count != 0) {
    for (int aircraft_slot = CITY_EVENT_SLOT_FIRST; aircraft_slot < city->events.capacity; ++aircraft_slot) {
      CityMapEvent *aircraft = &city->events.event[aircraft_slot];
      if (aircraft->type == CITY_MAP_EVENT_AIRPLANE)
        aircraft->state = CITY_AIRPLANE_CRASH;
    }
  }
  if (city->disasters.type2_count != 0) {
    for (int aircraft_slot = CITY_EVENT_SLOT_FIRST; aircraft_slot < city->events.capacity; ++aircraft_slot) {
      CityMapEvent *aircraft = &city->events.event[aircraft_slot];
      if (aircraft->type == CITY_MAP_EVENT_HELICOPTER)
        aircraft->state = CITY_HELICOPTER_CRASH;
    }
  }

  switch (event->state) {
  case CITY_MONSTER_SEEK_CENTER:
    if (event->age <= 8)
      event->state = CITY_MONSTER_APPLY_TILE_EFFECT;
    else
      --event->age;
    event->monster.upper_pose = 0;
    event->monster.lower_pose = 0;
    direction = CityEventHeadingTowardPoint(x, y, city->geography.center_x, city->geography.center_y);
    break;
  case CITY_MONSTER_APPLY_TILE_EFFECT: {
    if (Sc2kCrtRandRange(city, 25) == 0)
      event->state = CITY_MONSTER_RANDOM_MOVEMENT;
    event->monster.upper_pose = Sc2kCrtRand(city) & 0x7F;
    event->monster.lower_pose = Sc2kCrtRand(city) & 0x7F;
    direction = RandomlyPerturbCityEventHeading(city, event->direction, 5);
    if (y < city->map_height - 1 && x < city->map_width - 1) {
      int target_y = y + 1;
      int target_x = x + 1;
      TileIndex target = MapTileXY(city, target_x, target_y);
      int target_kind = ReadTile(city, target);
      if (event->monster.action != CITY_MONSTER_DESTROY_TILE) {
        if ((!IsMapTileValid(city, target) ||
                !HasTileWater(&city->tile[target])) &&
            !IsTileKindLandscape(target_kind) &&
            target_kind != TK_WIND_POWER) {
          BulldozeCityTile(city, target, true);
          switch (event->monster.action) {
          case CITY_MONSTER_PLANT_TREES:
            ReplaceCityTileAndStatistics(city, target, (u8)((Sc2kCrtRand(city) & 3) + 9));
            break;
          case CITY_MONSTER_REGRADE_TERRAIN:
            CreateCityRiverTile(city, target);
            break;
          case CITY_MONSTER_PLACE_WIND_POWER:
            PlaceSpecialZoneFootprint(city, target, TK_WIND_POWER, 1);
            break;
          default:
            break;
          }
          event->monster.upper_pose |= 0x80;
        }
      } else if (!IsTileKindBareGround(target_kind) &&
          ApplyDisasterCellEffect(city, target, 0)) {
        event->monster.upper_pose |= 0x80;
      }
    }
    goto advance_behavior_event;
  }
  case CITY_MONSTER_RANDOM_MOVEMENT:
    if (event->age >= 15) {
      event->state = Sc2kCrtRandRange(city, 3) != 0 ? CITY_MONSTER_SEEK_CENTER : CITY_MONSTER_LEAVE;
    } else {
      ++event->age;
    }
    event->monster.upper_pose = 9 * (Sc2kCrtRand(city) & 7);
    event->monster.lower_pose = event->monster.upper_pose;
    direction = RandomlyPerturbCityEventHeading(city, event->direction, 5);
    break;
  case CITY_MONSTER_LEAVE:
    event->monster.upper_pose = 0;
    event->monster.lower_pose = 0;
    if ((Sc2kCrtRand(city) & 1) != 0) {
      event->monster.upper_pose = 36;
      event->monster.lower_pose = 36;
    }
    if (NextCityMapRandomModulo(city, 100) == 0)
      ClearCityMapEventSlot(city, slot);
    else
      goto advance_behavior_event;
    return;
  default:
    break;
  }

advance_behavior_event:
  if (event->state != CITY_MONSTER_LEAVE) {
    int next_y = y + kCityMapEventSubtileYStep[direction];
    int next_x = x + kCityMapEventSubtileXStep[direction];
    TileIndex next = MapTileXYOrInvalid(city, next_x, next_y);
    if (next != INVALID_TILE) {
      int overlay = ReadOverlay(city, next);
      if (IsCityEventOverlay(overlay) &&
          city->events.event[CityEventOverlaySlot(overlay)].type == CITY_MAP_EVENT_MILITARY) {
        event->state = CITY_MONSTER_LEAVE;
        return;
      }
    }
  }
  event->direction = (u8)direction;
  AdvanceCityMapEventPosition(city, CITY_MAP_EVENT_MONSTER, slot, direction);
  if ((event->monster.upper_pose & 0x80) != 0)
    PlayCitySound(city, event->tile, CITY_SOUND_POWER_LINES);
}
