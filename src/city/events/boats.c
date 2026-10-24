// Ships and sailboats own their spawn, water admission, travel and departure rules here.
// TileInfo water presence and surface geometry come from native map queries; boats add no tile metadata.
// Moving a ship updates shared event membership and notifies nearby drawbridges through their owning module.

#include "city/maps/terrain.h"
#include "city/events/events.h"

#include <assert.h>
#include <string.h>

#include "game/audio.h"
#include "city/disasters/types.h"
#include "city/engine/random.h"
#include "city/infrastructure/bridges.h"
#include "city/maps/layers.h"
#include "city/presentation.h"
#include "city/state.h"

static const i8 kCityEventAdjacentYOffset[8] = {
  0, 1, 0, -1, 0, 0, 0, 0
};

static const i8 kCityEventAdjacentXOffset[8] = {
  -1, 0, 1, 0, 0, 0, 0, 0
};

static const i8 kCityEventOverlayBlockYOffset[8] = {
  0, 3, 4, 3, 0, -3, -4, -3
};

static const i8 kCityEventOverlayBlockXOffset[8] = {
  -4, -3, 0, 3, 4, 3, 0, -3
};

static const i8 kCityEventTurnClockwiseOrder[9] = {
  0, 1, 2, 3, 4, 5, 6, 7, 0
};

static const i8 kCityEventTurnCounterclockwiseOrder[9] = {
  0, 7, 6, 5, 4, 3, 2, 1, 2
};

static const i8 kBorderWaterTileYOffsets[4] = {
  2, 0, -2, 0
};

static const i8 kBorderWaterTileXOffsets[4] = {
  0, 2, 0, -2
};

// Small boats need a level water surface, including beneath water infrastructure. Their pier/marina rules are
// handled by the route caller. Stock comparison retains its broader water-presence check.
static bool IsCitySailboatWaterTile(const City *city, TileIndex tile) {
  if (city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE))
    return ReadCityTileGeometry(city, tile).water_surface == CITY_WATER_SURFACE_LEVEL;
  return HasTileWater(&city->tile[tile]);
}

// Check the sailboat's water route and marina arrival. Requires a live sailboat slot; returns its route/arrival
// predicate without independently advancing the boat.
static bool CheckCityEventAdjacentTile(City *city, int slot, int direction) {
  CityMapEvent *event = &city->events.event[slot];
  CityPoint position = ReadCityMapEventPosition(city, event);
  int y = position.y + kCityEventAdjacentYOffset[direction];
  int x = position.x + kCityEventAdjacentXOffset[direction];
  const TileIndex tile_index = MapTileXYOrInvalid(city, x, y);
  if (tile_index == INVALID_TILE) return true;

  int tile = ReadTile(city, tile_index);
  if (tile == TK_MARINA) {
    ClearCityMapEventSlot(city, slot);
    return false;
  }
  if (tile == TK_PIER)
    return false;
  return ReadOverlay(city, tile_index) == 0 && IsCitySailboatWaterTile(city, tile_index);
}

// Update one sailboat, including its distress state. Slot must contain a sailboat; may change distress state, position
// and active counts or release its record.
void UpdateCityMapSailboat(City *city, int slot) {
  CityMapEvent *event = &city->events.event[slot];
  ++city->disasters.tile248_count;
  if (!(city->fixes & (1ull << CITY_FIX_EXPANDED_EVENTS)) && city->disasters.tile248_count > 4) {
    ClearCityMapEventSlot(city, slot);
    return;
  }
  if (event->state != CITY_SAILBOAT_SAILING) {
    if (NextCityMapRandomModulo(city, 5) == 0)
      ClearCityMapEventSlot(city, slot);
    return;
  }
  // A terrain edit or imported boat can leave a sailing record on a bank. Remove it before another movement step.
  if ((city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) &&
      (event->tile == INVALID_TILE || !IsCitySailboatWaterTile(city, event->tile))) {
    ClearCityMapEventSlot(city, slot);
    return;
  }
  if (NextCityMapRandomModulo(city, 4) != 0) {
    if (CheckCityEventAdjacentTile(city, slot, event->direction))
      AdvanceCitySailboatPosition(city, slot);
    return;
  }

  TileIndex event_tile = event->tile;
  if (event_tile == INVALID_TILE) {
    ClearCityMapEventSlot(city, slot);
    return;
  }
  if (!HasTileWater(&city->tile[event_tile])) {
    ClearCityMapEventSlot(city, slot);
    return;
  }
  if (NextCityMapRandomModulo(city, 4000) == 0) {
    event->state = CITY_SAILBOAT_INCIDENT;
    PlayCityAmbientSound(city, event_tile, CITY_SOUND_SAILBOAT_OR_ZOO);
  }
  event->direction = (u8)((event->direction + Sc2kCrtRandRange(city, 3) - 1) & 3);
}

// Boats need level water. Native road/rail bridge middles and raised power lines leave water navigable;
// highway bridges retain their alternating passage pieces. Bridge heads and other occupants block passage.
static bool IsCityBoatPassageTile(const City *city, TileIndex tile) {
  if (!IsMapTileValid(city, tile)) return false;
  const TileInfo *tp = &city->tile[tile];
  CityTileGeometry geometry = ReadCityTileGeometry(city, tile);
  if (geometry.water_surface != CITY_WATER_SURFACE_LEVEL) return false;

  if (IsTileKindExposedWater(tp->kind) || IsTileKindPlainPowerLine(tp->kind)) return true;
  return CityBridgeAllowsBoat(city, tile);
}

// Check the next directional water-route position and absence of an event overlay. Requires a valid slot/heading;
// off-map positions return true so the caller can finish its departure path.
static bool CanCityEventAdvanceInDirection(City *city, int slot, int direction) {
  CityMapEvent *event = &city->events.event[slot];
  CityPoint position = ReadCityMapEventPosition(city, event);
  int y = position.y + kCityEventOverlayBlockYOffset[direction];
  int x = position.x + kCityEventOverlayBlockXOffset[direction];
  const TileIndex tile_index = MapTileXYOrInvalid(city, x, y);
  if (tile_index == INVALID_TILE) return true;
  return IsCityBoatPassageTile(city, tile_index) && !IsCityEventOrMarkerOverlay(ReadOverlay(city, tile_index));
}

// Advance one ship through travel, pier, and departure stages. Slot must identify a live ship with consistent route
// data; departure can clear the slot and decrement its active count.
static void StepCityMapShip(City *city, int slot) {
  CityMapEvent *event = &city->events.event[slot];
  CityPoint position = ReadCityMapEventPosition(city, event);
  ++city->disasters.border_water_count;
  if ((Sc2kCrtRand(city) & 0xFF) == 0)
    PlayCityAmbientSound(city, event->tile, CITY_SOUND_SHIP);

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
          SteerCityEventTowardPoint(event->direction, x, y, event->ship.destination_x, event->ship.destination_y);
      if (CanCityEventAdvanceInDirection(city, slot, direction))
        event->direction = (u8)direction;
    }
    if (CanCityEventAdvanceInDirection(city, slot, event->direction))
      AdvanceCityShipPosition(city, slot);
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
    int target = CityEventHeadingTowardPoint(position.x, position.y, event->ship.destination_x, event->ship.destination_y);
    event->direction = (u8)TurnCityEventHeadingToward(event->direction, target);
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
      PlayCityAmbientSound(city, event->tile, CITY_SOUND_SHIP);
      TileIndex home = event->ship.home_tile != INVALID_TILE ? event->ship.home_tile : event->tile;
      CityPoint point = MapTilePt(city, home);
      event->ship.destination_y = point.y;
      event->ship.destination_x = point.x;
    }
    break;
  case CITY_SHIP_DEPART:
    if (CanCityEventAdvanceInDirection(city, slot, event->direction)) {
      AdvanceCityShipPosition(city, slot);
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
  event->direction = CityEventHeadingTowardPoint(destination.x, destination.y, origin.x, origin.y);
  event->tile = tile_index;
  SetCityMapEventTarget(city, event, 128, 128);
  event->ship.home_tile = tile_index;
  event->height = 1;
  event->previous_overlay = overlay;
  event->next_slot = CITY_SHIP_TRAVEL;
  city->disasters.border_water_y = destination.y;
  city->disasters.border_water_x = destination.x;
  PlayCityAmbientSound(city, source, CITY_SOUND_SHIP);
  ++city->disasters.border_water_count;
  LinkCityMapEvent(city, slot);
  InvalidateCityDrawbridgesForShip(city, slot, INVALID_TILE, event->tile);
}

// Create a sailboat on an unoccupied exposed water tile. Native landscape behavior excludes banks and rapids.
static void TryCreateCitySailboat(City *city, TileIndex tile_index) {
  assert(IsMapTileValid(city, tile_index));
  if ((city->fixes & (1ull << CITY_FIX_EXPANDED_EVENTS)) && !CanSpawnCityVehicle(city, CITY_MAP_EVENT_SAILBOAT))
    return;
  int slot = FindFreeCityEventSlot(city);
  if (slot == CITY_OVERLAY_NONE || !IsTileKindExposedWater(ReadTile(city, tile_index)) ||
      ReadOverlay(city, tile_index) != CITY_OVERLAY_NONE) return;
  if (!IsCitySailboatWaterTile(city, tile_index)) return;

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

// Try unoccupied neighboring water tiles around a marina; each spawn applies the same surface admission as movement.
void TrySpawnCityMarinaSailboats(City *city, TileIndex tile_index) {
  assert(IsMapTileValid(city, tile_index));
  static const i8 offset_y[4] = {0, 1, 0, -1};
  static const i8 offset_x[4] = {-1, 0, 1, 0};
  if (!CanSpawnCityVehicle(city, CITY_MAP_EVENT_SAILBOAT))
    return;

  for (int i = 0; i < 4; ++i) {
    TileIndex candidate = GetRelativeTileOrInvalid(city, tile_index, offset_x[i], offset_y[i]);
    if (candidate == INVALID_TILE) continue;
    if (!HasTileWater(&city->tile[candidate]) || ReadOverlay(city, candidate) != 0) continue;
    TryCreateCitySailboat(city, candidate);
  }
}
