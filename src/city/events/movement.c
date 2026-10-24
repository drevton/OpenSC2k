#include "city/engine/clock.h"
#include "city/economy/history.h"
// Shared City event pool, per-tile hash and fixed-point movement.
// Family handlers own behavior; this module owns record membership and updates events in stable slot order.

#include "city/maps/labels.h"
#include "city/events/events.h"

#include <assert.h>
#include <string.h>

#include "game/audio.h"
#include "city/engine/city_types.h"
#include "city/engine/random.h"
#include "city/infrastructure/bridges.h"
#include "city/presentation.h"
#include "city/state.h"

typedef enum CityEventMoveMode {
  CITY_EVENT_MOVE_SKIP_MARKERS, CITY_EVENT_MOVE_SHIP, CITY_EVENT_MOVE_SAILBOAT
} CityEventMoveMode;

const i8 kCityMapEventSubtileYStep[8] = {
  0, 1, 1, 1, 0, -1, -1, -1
};

const i8 kCityMapEventSubtileXStep[8] = {
  -1, -1, 0, 1, 1, 1, 0, -1
};

static const i8 kCityShipStepY[8] = {
  0, 3, 4, 3, 0, -3, -4, -3
};

static const i8 kCityShipStepX[8] = {
  -4, -3, 0, 3, 4, 3, 0, -3
};

static const i8 kCitySailboatStepY[8] = {
  0, 16, 0, -16, -16, 0, 16, 0
};

static const i8 kCitySailboatStepX[8] = {
  -16, 0, 16, 0, 0, 1, 0, -1
};

static const u8 kCityMapEventSubtileSpeed[CITY_MAP_EVENT_KIND_COUNT] = {
  [CITY_MAP_EVENT_AIRPLANE] = 16,
  [CITY_MAP_EVENT_HELICOPTER] = 8,
  [CITY_MAP_EVENT_SHIP] = 4,
  [CITY_MAP_EVENT_MONSTER] = 8,
  [CITY_MAP_EVENT_TORNADO] = 8,
  [CITY_MAP_EVENT_MAXIS_MAN] = 16,
};

static const u8 kCityMapEventInitialAge[24] = {
  0, 6, 4, 6, 0, 8, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 9, 10, 11, 12, 13, 3, 1, 0
};

// Tile membership is derived from event->tile. Hash order is stable slot order, independent of allocation/movement
// history and archive reloads. Accepts the event's native tile value and returns a bounded hash bucket; equality is
// checked separately within each chain.
static int CityEventBucket(TileIndex tile) {
  u32 hash = tile;
  hash ^= hash >> 16;
  hash *= 0x7FEB352D;
  hash ^= hash >> 15;
  hash *= 0x846CA68B;
  hash ^= hash >> 16;
  return hash & (CITY_EVENT_HASH_BUCKETS - 1);
}

// Return the supported event-pool capacity for the requested map dimensions. Dimensions must describe a supported map
// profile; this is a sizing policy and does not allocate or count live events.
int CityEventSlotCapacity(int width, int height) {
  // Area thresholds also handle long rectangular maps without charging for a square.
  int area = width * height;
  if (area <= CITY_MAP_SIZE * CITY_MAP_SIZE) return CITY_EVENT_MIN_SLOTS;
  if (area <= 256 * 256) return 256;
  if (area <= 1024 * 1024) return 512;
  if (area <= 2048 * 2048) return 1024;
  return CITY_EVENT_MAX_SLOTS;
}

// Return the usable event-slot limit under the City's expanded-event policy and map profile. Requires initialized
// geometry/capacity; retained records beyond a smaller profile remain owned even when new allocation is limited.
int CityEventAllocationLimit(const City *city) {
  return city->fixes & (1ull << CITY_FIX_EXPANDED_EVENTS) ?
      IntMin(city->events.capacity, CityEventSlotCapacity(city->map_width, city->map_height)) :
      CITY_STOCK_EVENT_SLOT_COUNT;
}

// Vehicle caps roughly double OpenSC2K's map profiles. Train caps also leave enough entries for all dispatch units
// and, on the largest maps, effects. Returns admission only; allocation limits count live records, including those
// retained beyond a smaller map's current budget.
bool CanSpawnCityVehicle(const City *city, int type) {
  int count, stock_limit;
  switch (type) {
  case CITY_MAP_EVENT_AIRPLANE: count = city->disasters.type1_count; stock_limit = 2; break;
  case CITY_MAP_EVENT_HELICOPTER: count = city->disasters.type2_count; stock_limit = 1; break;
  case CITY_MAP_EVENT_SHIP: count = city->disasters.border_water_count; stock_limit = 1; break;
  case CITY_MAP_EVENT_SAILBOAT: count = city->disasters.tile248_count; stock_limit = 4; break;
  case CITY_MAP_EVENT_TRAIN_ENGINE:
  case CITY_MAP_EVENT_SUBWAY_ENGINE: count = city->disasters.tile237_count; stock_limit = 5; break;
  default: return false;
  }
  if (!(city->fixes & (1ull << CITY_FIX_EXPANDED_EVENTS))) return count < stock_limit;

  static const struct {
    int area;
    u16 airplanes, helicopters, ships, sailboats, trains;
  } kProfiles[] = {
      {128 * 128, 4, 2, 2, 8, 16},
      {256 * 256, 8, 4, 4, 16, 32},
      {512 * 512, 16, 8, 8, 32, 64},
      {1024 * 1024, 32, 16, 16, 64, 128},
      {2048 * 2048, 64, 32, 32, 64, 256},
      {4096 * 4096, 128, 64, 64, 128, 512},
  };
  int area = city->map_cell_count;
  int profile = 0;
  while (area > kProfiles[profile].area) ++profile;
  int allocation = CityEventAllocationLimit(city);
  int reserved = IntMax(2 * CITY_CIVIC_DISPATCH_UNITS + CITY_MILITARY_DISPATCH_UNITS, allocation / 16);
  int budget = allocation - CITY_EVENT_SLOT_FIRST - reserved;
  bool train = type == CITY_MAP_EVENT_TRAIN_ENGINE || type == CITY_MAP_EVENT_SUBWAY_ENGINE;
  int limit;
  switch (type) {
  case CITY_MAP_EVENT_AIRPLANE: limit = kProfiles[profile].airplanes; break;
  case CITY_MAP_EVENT_HELICOPTER: limit = kProfiles[profile].helicopters; break;
  case CITY_MAP_EVENT_SHIP: limit = kProfiles[profile].ships; break;
  case CITY_MAP_EVENT_SAILBOAT: limit = kProfiles[profile].sailboats; break;
  default:
    limit = IntMin(kProfiles[profile].trains, (budget - kProfiles[profile].airplanes -
        kProfiles[profile].helicopters - kProfiles[profile].ships - kProfiles[profile].sailboats) / 3);
    break;
  }
  // Recount at admission: saved counters can be stale, and each batch member
  // must see earlier spawns. Retained records beyond a smaller map's budget count too.
  int active = 0;
  count = 0;
  for (int slot = CITY_EVENT_SLOT_FIRST; slot < city->events.capacity; ++slot) {
    int kind = city->events.event[slot].type;
    active += kind != CITY_MAP_EVENT_UNUSED;
    count += train ? kind == CITY_MAP_EVENT_TRAIN_ENGINE || kind == CITY_MAP_EVENT_SUBWAY_ENGINE : kind == type;
  }
  return count < limit && active + (train ? 3 : 1) <= budget;
}

// Return the first unused event slot, excluding reserved slot zero. Returns an admitted unused index or the no-slot
// result; it does not reserve or initialize the selected record.
int FindFreeCityEventSlot(City *city) {
  for (int slot = CITY_EVENT_SLOT_FIRST; slot < CityEventAllocationLimit(city); ++slot)
    if (city->events.event[slot].type == CITY_MAP_EVENT_UNUSED)
      return slot;
  return 0;
}

// Remove a valid event slot from its per-tile hash chain if linked. Requires consistent hash membership and asserts if
// a claimed link is missing; the event record itself is not freed or cleared.
void UnlinkCityMapEvent(City *city, int slot) {
  CityMapEvent *event = &city->events.event[slot];
  if (!(event->coordinate_flags & CITY_EVENT_HASH_LINKED)) return;
  u16 *link = &city->events.buckets[CityEventBucket(event->tile)];
  while (*link && *link != slot + 1) link = &city->events.event[*link - 1].hash_next;
  assert(*link == slot + 1);
  *link = event->hash_next;
  event->hash_next = 0;
  event->coordinate_flags &= ~CITY_EVENT_HASH_LINKED;
}

// Insert a valid active event slot into its tile bucket in stable slot order. Already-linked, unused and off-map
// events are ignored; requires initialized buckets and no duplicate chain entry.
void LinkCityMapEvent(City *city, int slot) {
  CityMapEvent *event = &city->events.event[slot];
  if (event->coordinate_flags & CITY_EVENT_HASH_LINKED) return;
  if (event->type == CITY_MAP_EVENT_UNUSED || !IsMapTileValid(city, event->tile)) return;
  u16 *link = &city->events.buckets[CityEventBucket(event->tile)];
  while (*link && *link < slot + 1) link = &city->events.event[*link - 1].hash_next;
  event->hash_next = *link;
  *link = slot + 1;
  event->coordinate_flags |= CITY_EVENT_HASH_LINKED;
}

// Move a valid event slot to its new tile and maintain its hash membership. Destination validity controls whether it
// can be linked; this changes the event position, not the underlying terrain tile.
void MoveCityMapEvent(City *city, int slot, TileIndex tile) {
  TileIndex previous = city->events.event[slot].tile;
  if (city->events.event[slot].tile != tile) UnlinkCityMapEvent(city, slot);
  city->events.event[slot].tile = tile;
  LinkCityMapEvent(city, slot);
  if (city->events.event[slot].type == CITY_MAP_EVENT_SHIP)
    InvalidateCityDrawbridgesForShip(city, slot, previous, tile);
}

// Find the first event on the requested tile in stable slot order. Requires initialized event buckets; returns the
// slot index or -1 and does not remove the event.
int FirstCityMapEventOnTile(const City *city, TileIndex tile) {
  for (int link = city->events.buckets[CityEventBucket(tile)]; link; link = city->events.event[link - 1].hash_next)
    if (city->events.event[link - 1].tile == tile) return link - 1;
  return -1;
}

// Find the next event sharing the valid starting slot's tile. Requires an intact hash chain; returns a slot index or
// -1 without changing iteration or event state.
int NextCityMapEventOnTile(const City *city, int slot) {
  TileIndex tile = city->events.event[slot].tile;
  for (int link = city->events.event[slot].hash_next; link; link = city->events.event[link - 1].hash_next)
    if (city->events.event[link - 1].tile == tile) return link - 1;
  return -1;
}

void ClearCityMapEvents(City *city) {
  memset(city->events.event, 0, city->events.capacity * sizeof(*city->events.event));
  memset(city->events.buckets, 0, CITY_EVENT_HASH_BUCKETS * sizeof(*city->events.buckets));
}

// Reconstruct all per-tile event buckets from native event positions. Requires allocated events/buckets; clears old
// link flags and produces stable ascending-slot chains independently of insertion history.
void RebuildCityEventHash(City *city) {
  memset(city->events.buckets, 0, CITY_EVENT_HASH_BUCKETS * sizeof(u16));
  // Descending slots pushed at the front produce ascending lists in linear time.
  for (int slot = city->events.capacity - 1; slot >= 0; --slot) {
    CityMapEvent *event = &city->events.event[slot];
    event->hash_next = 0;
    event->coordinate_flags &= ~CITY_EVENT_HASH_LINKED;
    if (event->type == CITY_MAP_EVENT_UNUSED || !IsMapTileValid(city, event->tile)) continue;
    u16 *head = &city->events.buckets[CityEventBucket(event->tile)];
    event->hash_next = *head;
    *head = slot + 1;
    event->coordinate_flags |= CITY_EVENT_HASH_LINKED;
  }
}

// Read the effective overlay at a valid tile, preferring its first active event over the stored base overlay. Requires
// initialized event buckets; returns a value and does not modify tile or event storage.
int ReadOverlay(const City *city, TileIndex tile) {
  int slot = FirstCityMapEventOnTile(city, tile);
  return slot >= 0 ? CityEventOverlay(slot) : ReadCityBaseOverlay(city, tile);
}

// Retained stack links describe stock associations and native event IDs. Large sparse association IDs need no link:
// their presence remains in the tile while events come and go.
u16 ReadCityEventUnderlay(const City *city, TileIndex tile) {
  int overlay = ReadOverlay(city, tile);
  return overlay < CITY_SIGN_OVERLAY_EXTENDED ? overlay : 0;
}

// Decode an event's native tile index to map coordinates. Event tile must be valid for the supplied City's geometry;
// returns a value without changing hash membership.
CityPoint ReadCityMapEventPosition(const City *city, const CityMapEvent *event) {
  return MapTilePt(city, event->tile);
}

// Return a train's next tile coordinates or another event's stored sub-tile offset pair.
// Train targets must be valid. Aircraft destination waypoints are separate fields, not these offsets.
CityPoint ReadCityMapEventTarget(const City *city, const CityMapEvent *event) {
  if (event->coordinate_flags & CITY_EVENT_TARGET_TILE)
    return MapTilePt(city, event->train.next_tile);
  if (event->type == CITY_MAP_EVENT_SHIP)
    return (CityPoint){event->ship.offset_x, event->ship.offset_y};
  return (CityPoint){event->motion.offset_x, event->motion.offset_y};
}

// Store a next tile for trains or byte offsets for ships and other moving events. Train coordinates must be valid;
// other offsets must already be normalized for their movement period. This does not relocate or rehash the event.
void SetCityMapEventTarget(City *city, CityMapEvent *event, int x, int y) {
  if (IsCityMapEventTrain(event->type)) {
    event->train.next_tile = MapTileXY(city, x, y);
    event->coordinate_flags |= CITY_EVENT_TARGET_TILE;
  } else if (event->type == CITY_MAP_EVENT_SHIP) {
    event->ship.offset_x = x;
    event->ship.offset_y = y;
    event->coordinate_flags &= ~CITY_EVENT_TARGET_TILE;
  } else {
    event->motion = (CityMapEventMotion){.offset_x = x, .offset_y = y};
    event->coordinate_flags &= ~CITY_EVENT_TARGET_TILE;
  }
}

// Resize changes the row stride. Keep world coordinates, clamping positions if a smaller replacement map no longer
// contains their old tile. Call while City still has its old row stride; destination dimensions must be supported and
// hash links are rebuilt after rebinding storage.
void ReindexCityMapEvents(City *city, int width, int height) {
  for (int slot = 0; slot < city->events.capacity; ++slot) {
    CityMapEvent *event = &city->events.event[slot];
    UnlinkCityMapEvent(city, slot);
    CityPoint position = ReadCityMapEventPosition(city, event);
    event->tile = IntMin(position.y, height - 1) * width + IntMin(position.x, width - 1);
    if (event->coordinate_flags & CITY_EVENT_TARGET_TILE) {
      CityPoint target = ReadCityMapEventTarget(city, event);
      event->train.next_tile = IntMin(target.y, height - 1) * width + IntMin(target.x, width - 1);
    }
    if (event->type == CITY_MAP_EVENT_SHIP && event->ship.home_tile != INVALID_TILE) {
      CityPoint home_point = MapTilePt(city, event->ship.home_tile);
      event->ship.home_tile = IntMin(home_point.y, height - 1) * width + IntMin(home_point.x, width - 1);
    }
  }
}

// Allocate one event record and install its overlay marker. Type and destination must be admitted; City owns the
// record and index and allocation failure leaves no new active event.
void CreateCityMapEventAtTile(City *city, TileIndex tile_index, int type, int direction) {
  assert(tile_index < (u32)city->map_cell_count);
  int slot;
  int limit = CityEventAllocationLimit(city);
  for (slot = CITY_EVENT_SLOT_FIRST; slot < limit; ++slot) {
    if (city->events.event[slot].type == CITY_MAP_EVENT_UNUSED)
      break;
    InvalidateCityMapEvent(city, slot);
  }
  if (slot == limit) {
    if (city->settings.timer_mode != CITY_TIMER_EVENT)
      return;
    slot = limit - 1;
    ClearCityMapEventSlot(city, slot);
  }
  if (ReadOverlay(city, tile_index) != 0)
    return;

  CityMapEvent *event = &city->events.event[slot];
  memset(event, 0, sizeof(*event));
  event->type = (u8)type;
  event->direction = (u8)direction;
  event->tile = tile_index;
  SetCityMapEventTarget(city, event, 0, 0);
  event->age = kCityMapEventInitialAge[type];
  event->previous_overlay = ReadCityEventUnderlay(city, tile_index);
  LinkCityMapEvent(city, slot);
  InvalidateCityMapEvent(city, slot);
}

// Dispatch enabled event slots and reset the event-frame accumulators. Requires consistent active event records and
// links; each handler may move or remove its slot and may consume deterministic RNG.
void UpdateCityMapEvents(City *city) {
  static const u8 update_event_type[CITY_MAP_EVENT_KIND_COUNT] = {
      [CITY_MAP_EVENT_AIRPLANE] = true,
      [CITY_MAP_EVENT_HELICOPTER] = true,
      [CITY_MAP_EVENT_SHIP] = true,
      [CITY_MAP_EVENT_BULLDOZER] = true,
      [CITY_MAP_EVENT_MONSTER] = true,
      [CITY_MAP_EVENT_EXPLOSION] = true,
      [CITY_MAP_EVENT_SAILBOAT] = true,
      [CITY_MAP_EVENT_TRAIN_ENGINE] = true,
      [CITY_MAP_EVENT_SUBWAY_ENGINE] = true,
      [CITY_MAP_EVENT_TORNADO] = true,
      [CITY_MAP_EVENT_MAXIS_MAN] = true,
  };

  ++city->event_runtime.animation_phase;
  if (city->event_runtime.animation_phase > 1023)
    city->event_runtime.animation_phase = 0;
  city->disasters.type1_count = 0;
  city->disasters.type2_count = 0;
  city->disasters.border_water_count = 0;
  city->disasters.monster_count = 0;
  city->disasters.tile237_count = 0;
  city->disasters.type16_count = 0;
  city->disasters.tile248_count = 0;
  city->disasters.tornado_count = 0;

  if ((int)ReadHistorySample(city, CITY_HISTORY_TRAFFIC, 0) > 35 && (Sc2kCrtRand(city) & 0xFF) == 0)
    PlayCityAmbientSound(city, INVALID_TILE, CITY_SOUND_BUS_DEPOT);
  if (city->statistics.value[TK_POLICE_STATION] != 0 &&
      (int)ReadHistorySample(city, CITY_HISTORY_CRIME, 0) > 40 && (Sc2kCrtRand(city) & 0xFF) == 0)
    PlayCityAmbientSound(city, INVALID_TILE, CITY_SOUND_POLICE_OR_HOSPITAL);

  for (int slot = CITY_EVENT_SLOT_FIRST; slot < city->events.capacity; ++slot) {
    CityMapEvent *event = &city->events.event[slot];
    int type = event->type;
    if (type == CITY_MAP_EVENT_UNUSED || type >= CITY_MAP_EVENT_KIND_COUNT || !update_event_type[type])
      continue;
    if ((type != CITY_MAP_EVENT_EXPLOSION && type != CITY_MAP_EVENT_TRAIN_ENGINE &&
            type != CITY_MAP_EVENT_SUBWAY_ENGINE) &&
        event->direction >= CITY_HEADING_COUNT)
      continue;

    InvalidateCityMapEvent(city, slot);
    switch (type) {
    case CITY_MAP_EVENT_AIRPLANE:
      UpdateCityMapAirplane(city, slot);
      break;
    case CITY_MAP_EVENT_HELICOPTER:
      UpdateCityMapHelicopter(city, slot);
      break;
    case CITY_MAP_EVENT_SHIP:
      UpdateCityMapShip(city, slot);
      break;
    case CITY_MAP_EVENT_MONSTER:
      UpdateCityMapMonster(city, slot);
      break;
    case CITY_MAP_EVENT_EXPLOSION:
      UpdateCityMapExplosion(city, slot);
      break;
    case CITY_MAP_EVENT_SAILBOAT:
      UpdateCityMapSailboat(city, slot);
      break;
    case CITY_MAP_EVENT_TRAIN_ENGINE:
    case CITY_MAP_EVENT_SUBWAY_ENGINE:
      ++city->disasters.tile237_count;
      UpdateCityMapTrain(city, slot);
      break;
    case CITY_MAP_EVENT_TORNADO:
      UpdateCityMapTornado(city, slot);
      break;
    case CITY_MAP_EVENT_MAXIS_MAN:
      UpdateCityMapMaxisMan(city, slot);
      break;
    default:
      break;
    }
    InvalidateCityMapEvent(city, slot);
  }
}

// Advance the city-event timer; Game schedules UI previews separately. Uses logical frame samples and updates retained
// averages; it does not sample the wall clock or run UI tool previews.
void UpdateCityMapEventTimer(City *city) {
  if (city->settings.speed <= CITY_SPEED_PAUSED) return;
  u32 time = SimulationFrameTime(city);
  CityEventRuntime *runtime = &city->event_runtime;
  // Short deadlines use a signed modular difference so u32 clock wrap preserves their order.
  if ((i32)(time - runtime->timer_deadline) <= 0) return;
  runtime->tick_sample *= 4;
  int sample = runtime->tick_sample;
  runtime->tick_sample = 0;
  int average = (7 * runtime->tick_average + sample) / 8;
  runtime->tick_average = average;
  int interval = city->settings.timer_mode == CITY_TIMER_EVENT || average > 5 ? 30 : 45;
  runtime->timer_deadline = time + interval;
  UpdateCityMapEvents(city);
}

// Free one event slot and clear its overlay marker. Slot must be allocated and valid; unlinks its hash membership and
// clears the record while City retains pool storage.
void ClearCityMapEventSlot(City *city, int slot) {
  CityMapEvent *event = &city->events.event[slot];
  TileIndex ship = event->type == CITY_MAP_EVENT_SHIP ? event->tile : INVALID_TILE;
  UnlinkCityMapEvent(city, slot);
  memset(event, 0, sizeof(*event));
  if (ship != INVALID_TILE) InvalidateCityDrawbridgesForShip(city, slot, ship, INVALID_TILE);
}

// Move a valid event record by the selected fixed-point distance when the next position remains supported. Returns
// whether movement can continue; updates its position/hash through the existing event movement path.
static bool AdvanceCityEventByStep(City *city, int slot, int x_step, int y_step,
    int fraction_size, CityEventMoveMode mode) {
  CityMapEvent *event = &city->events.event[slot];
  CityPoint position = ReadCityMapEventPosition(city, event);
  CityPoint offset = ReadCityMapEventTarget(city, event);
  int next_y = offset.y + CITY_EVENT_OFFSET_SCALE * y_step;
  int next_x = offset.x + CITY_EVENT_OFFSET_SCALE * x_step;
  int cell_y_step = 0, cell_x_step = 0;
  if (next_y >= fraction_size) { next_y -= fraction_size; cell_y_step = 1; }
  if (next_y < 0) { next_y += fraction_size; cell_y_step = -1; }
  if (next_x >= fraction_size) { next_x -= fraction_size; cell_x_step = 1; }
  if (next_x < 0) { next_x += fraction_size; cell_x_step = -1; }
  SetCityMapEventTarget(city, event, next_x, next_y);
  if (cell_y_step == 0 && cell_x_step == 0) return true;

  int y = position.y + cell_y_step, x = position.x + cell_x_step;
  UnlinkCityMapEvent(city, slot);
  // Sailboat movement deliberately excludes the final row and column.
  if (x < 0 || y < 0 || x >= city->map_width - (mode == CITY_EVENT_MOVE_SAILBOAT) ||
      y >= city->map_height - (mode == CITY_EVENT_MOVE_SAILBOAT)) {
    if (mode != CITY_EVENT_MOVE_SHIP) ClearCityMapEventSlot(city, slot);
    return false;
  }
  TileIndex tile = event->tile + cell_y_step * city->map_width + cell_x_step;
  if (mode == CITY_EVENT_MOVE_SKIP_MARKERS) {
    while (IsCityEventOrMarkerOverlay(ReadOverlay(city, tile))) {
      event->tile = tile;
      y += cell_y_step;
      x += cell_x_step;
      tile = MapTileXYOrInvalid(city, x, y);
      if (tile == INVALID_TILE) {
        ClearCityMapEventSlot(city, slot);
        return false;
      }
    }
  }
  event->tile = tile;
  if (mode != CITY_EVENT_MOVE_SAILBOAT) event->previous_overlay = ReadCityEventUnderlay(city, tile);
  LinkCityMapEvent(city, slot);
  return true;
}

// Move a live event using the speed_type event kind's speed and an explicit eight-way direction.
// Transfers hash membership, skipping occupied markers; an off-map step releases the event.
void AdvanceCityMapEventPosition(City *city, int speed_type, int slot, int direction) {
  AdvanceCityEventByStep(city, slot, kCityMapEventSubtileSpeed[speed_type] * kCityMapEventSubtileXStep[direction],
      kCityMapEventSubtileSpeed[speed_type] * kCityMapEventSubtileYStep[direction],
      CITY_EVENT_TILE_FRACTION, CITY_EVENT_MOVE_SKIP_MARKERS);
}

// Move a live ship using its eight-way step table and CITY_EVENT_SHIP_FRACTION period.
// At the map edge, return false with the event unlinked but retained for the ship handler to finish.
bool AdvanceCityShipPosition(City *city, int slot) {
  int direction = city->events.event[slot].direction;
  return AdvanceCityEventByStep(city, slot, kCityShipStepX[direction], kCityShipStepY[direction],
      CITY_EVENT_SHIP_FRACTION, CITY_EVENT_MOVE_SHIP);
}

// Move a live sailboat using its cardinal step table and CITY_EVENT_TILE_FRACTION period.
// Preserve its retained underlay; release the event if the step leaves the map excluding its last row/column.
bool AdvanceCitySailboatPosition(City *city, int slot) {
  int direction = city->events.event[slot].direction;
  return AdvanceCityEventByStep(city, slot, kCitySailboatStepX[direction], kCitySailboatStepY[direction],
      CITY_EVENT_TILE_FRACTION, CITY_EVENT_MOVE_SAILBOAT);
}

// Occasionally turn an eight-way heading left or right using the City CRT generator. Direction must be supported and
// chance positive; the first draw gates whether the second turn-selection draw is consumed.
int RandomlyPerturbCityEventHeading(City *city, int direction, int chance) {
  if (Sc2kCrtRandRange(city, chance) == 0)
    return (Sc2kCrtRandRange(city, 3) + direction - 1) & CITY_HEADING_MASK;
  return direction;
}

// Always turn one eight-way step, including an increasing step when already aligned.
// Half-turn ties follow the sign of target - direction. Inputs must be supported headings.
int TurnCityEventHeadingToward(int direction, int target) {
  int difference = target - direction;
  bool advance_heading = difference >= 0 ? difference <= CITY_HEADING_COUNT / 2 : difference < -CITY_HEADING_COUNT / 2;
  return (direction + (advance_heading ? 1 : -1)) & CITY_HEADING_MASK;
}

// Compute the eight-way heading from the first coordinate pair toward the second using half-axis thresholds.
// Coordinate differences must fit integer arithmetic; coincident points select south-east. No RNG is consumed.
int CityEventHeadingTowardPoint(int x, int y, int target_x, int target_y) {
  int dy = target_y - y;
  int dx = target_x - x;
  int absolute_y = dy < 0 ? -dy : dy;
  int absolute_x = dx < 0 ? -dx : dx;
  if ((absolute_x + 1) / 2 > absolute_y)
    return dx < 0 ? CITY_HEADING_WEST : CITY_HEADING_EAST;
  if ((absolute_y + 1) / 2 <= absolute_x) {
    if (dy >= 0)
      return dx < 0 ? CITY_HEADING_SOUTH_WEST : CITY_HEADING_SOUTH_EAST;
    return dx < 0 ? CITY_HEADING_NORTH_WEST : CITY_HEADING_NORTH_EAST;
  }
  return dy >= 0 ? CITY_HEADING_SOUTH : CITY_HEADING_NORTH;
}

// Turn an eight-way heading one step toward the direction from x/y to target_x/target_y. Heading must be supported and
// coordinate differences representable; coordinates may be off-map waypoints and no event record is modified.
int SteerCityEventTowardPoint(int direction, int x, int y, int target_x, int target_y) {
  int target = CityEventHeadingTowardPoint(x, y, target_x, target_y);
  return target == direction ? direction : TurnCityEventHeadingToward(direction, target);
}
