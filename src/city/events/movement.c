#include "city/maps/buildings.h"
#include "city/infrastructure/bridges.h"
#include "city/military.h"
#include "assets/resource_types.h"
#include "audio/types.h"
#include "city/disasters/types.h"
#include "city/economy/types.h"
#include "city/engine/city_types.h"
#include "city/maps/tile_storage.h"
#include "city/tools/types.h"
#include "city/presentation.h"
#include <assert.h>
#include <string.h>

#include "audio/audio.h"
#include "city/disasters/disasters.h"
#include "city/engine/simulation.h"
#include "city/events/events.h"
#include "city/maps/layers.h"
#include "city/tools/tools.h"
#include "game/state.h"
#include "audio/state.h"
#include "city/state.h"
#include "assets/state.h"
#include "drawing/maps/tables.h"


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
  return city->fixes & (1 << CITY_FIX_EXPANDED_EVENTS) ?
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
  if (!(city->fixes & (1 << CITY_FIX_EXPANDED_EVENTS))) return count < stock_limit;

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

// Separate stock overlay stacks into native terrain overlays and per-tile event membership. Run after stock records
// are decoded; clears unused event records and rebuilds hash links from active event locations.
void ImportStockCityEventLinks(City *city) {
  // Stock tiles keep only the top overlay. Native tiles retain the non-event
  // association underneath, and every active record is indexed at its own tile.
  for (TileIndex tile = 0; tile < (u32)city->map_cell_count; ++tile) {
    TileInfo *tp = &city->tile[tile];
    int overlay = tp->overlay;
    for (int depth = 0; IsCityEventOverlay(overlay) && depth < CITY_STOCK_EVENT_SLOT_COUNT; ++depth)
      overlay = city->events.event[CityEventOverlaySlot(overlay)].previous_overlay;
    tp->overlay = IsCityEventOverlay(overlay) ? CITY_OVERLAY_NONE : overlay;
  }
  for (int slot = 0; slot < city->events.capacity; ++slot) {
    CityMapEvent *event = &city->events.event[slot];
    if (event->type == CITY_MAP_EVENT_UNUSED) memset(event, 0, sizeof(*event));
    else if (IsMapTileValid(city, event->tile)) event->previous_overlay = city->tile[event->tile].overlay;
  }
  RebuildCityEventHash(city);
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

// Decode a train's target tile or another event's stored offset pair. Tile targets must be valid; non-train offsets
// may represent off-map waypoints and must not be forced through tile admission.
CityPoint ReadCityMapEventTarget(const City *city, const CityMapEvent *event) {
  if (event->coordinate_flags & CITY_EVENT_TARGET_TILE)
    return MapTilePt(city, event->train.next_tile);
  if (event->type == CITY_MAP_EVENT_SHIP)
    return (CityPoint){event->ship.offset_x, event->ship.offset_y};
  return (CityPoint){event->motion.offset_x, event->motion.offset_y};
}

// Store a target as a tile for trains or signed offsets for ships and other moving events. Train coordinates must be
// valid; caller owns the event record and this does not relocate or rehash it.
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


// Apply the retained four-cell coordinate offset and update the event at the resulting tile. Off-map results are
// ignored; native command admission validates representable offsets before entry.
void SetCityMapEventStageAtTileOffset(City *city, int x, int y) {
  y += 4;
  x += 4;
  TileIndex tile_index = MapTileXYOrInvalid(city, x, y);
  if (tile_index == INVALID_TILE) return;
  int overlay = ReadOverlay(city, tile_index);
  if (!IsCityEventOverlay(overlay))
    return;
  CityMapEvent *event = &city->events.event[CityEventOverlaySlot(overlay)];
  if (event->type == CITY_MAP_EVENT_HELICOPTER) {
    event->state = CITY_HELICOPTER_CRASH;
    PlayCitySound(city, tile_index, CITY_SOUND_HELICOPTER_STAGE);
  }
}

// Check disaster/timer mode and available units for the explicit emergency-service kind. Returns admission without
// moving units, editing overlays or consuming RNG; unsupported kinds are rejected.
bool CanDispatchCityService(const City *city, int service) {
  if (city->settings.timer_mode != CITY_TIMER_EVENT || !city->disasters.map_update_pending) return false;
  const CityEventRuntime *runtime = &city->event_runtime;
  switch (service) {
  case CITY_DISPATCH_POLICE: return runtime->police_unit_count > 0;
  case CITY_DISPATCH_FIRE: return runtime->fire_unit_count > 0;
  case CITY_DISPATCH_MILITARY: return runtime->military_unit_count > 0;
  default: return false;
  }
}

// Check whether the City retains any currently dispatched emergency units. Reads dispatch records only and does not
// recall units or remove their events.
bool HasCityDispatchedUnits(const City *city) {
  for (int slot = CITY_EVENT_SLOT_FIRST; slot < city->events.capacity; ++slot) {
    int type = city->events.event[slot].type;
    if (type == CITY_MAP_EVENT_POLICE || type == CITY_MAP_EVENT_FIRE || type == CITY_MAP_EVENT_MILITARY)
      return true;
  }
  return false;
}

// Prepare available emergency units and remove their previous map events. Requires initialized unit/event arrays;
// clears prior dispatch markers before installing available counts.
void InitializeCityEmergencyDispatchState(City *city) {
  static const u8 military_units[CITY_MILITARY_BASE_COUNT] = {0, 0, 5, 2, 3, 0};
  city->event_runtime.police_blocked_warning = 0;
  city->event_runtime.fire_blocked_warning = 0;
  city->event_runtime.military_blocked_warning = 0;
  int police = city->statistics.value[TK_POLICE_STATION];
  int fire = city->statistics.value[TK_FIRE_STATION];
  city->event_runtime.police_unit_count = police >> 3;
  city->event_runtime.fire_unit_count = fire >> 3;
  // Invalid unlock modes previously indexed beyond the original six stack words.
  city->event_runtime.military_unit_count =
      city->unlocks.military_base < CITY_MILITARY_BASE_COUNT ? military_units[city->unlocks.military_base] : 0;
  if (city->event_runtime.police_unit_count == 0 && city->event_runtime.fire_unit_count == 0 &&
      city->event_runtime.military_unit_count == 0) {
    city->event_runtime.military_unit_count = 1;
    StopCitySound(city->game, CITY_SOUND_BULLDOZER_WORK);
    PlayCitySound(city, INVALID_TILE, CITY_SOUND_CIVIC_PLACEMENT);
    ShowCityResourceAlertDialog(city, CITY_BITMAP_NOTICE_406, CITY_NOTICE_NATIONAL_GUARD_DEPLOYED);
  }
  city->unlocks.tools.slot[CITY_TOOL_GROUP_DISPATCH] = 0;
  if (city->event_runtime.military_unit_count != 0) {
    city->unlocks.tools.slot[CITY_TOOL_GROUP_DISPATCH] = CITY_TOOL_UNLOCK_MILITARY_DISPATCH;
    SelectCityDispatchTool(city, CITY_DISPATCH_MILITARY);
  }
  if (city->event_runtime.fire_unit_count != 0) {
    SelectCityDispatchTool(city, CITY_DISPATCH_FIRE);
    city->unlocks.tools.slot[CITY_TOOL_GROUP_DISPATCH] |= 2;
  }
  if (city->event_runtime.police_unit_count != 0) {
    SelectCityDispatchTool(city, CITY_DISPATCH_POLICE);
    city->unlocks.tools.slot[CITY_TOOL_GROUP_DISPATCH] |= 1;
  }
  city->event_runtime.police_index = 0;
  city->event_runtime.fire_index = 0;
  city->event_runtime.military_index = 0;
  for (int slot = CITY_EVENT_SLOT_FIRST; slot < city->events.capacity; ++slot) {
    int type = city->events.event[slot].type;
    if (type == CITY_MAP_EVENT_POLICE || type == CITY_MAP_EVENT_FIRE || type == CITY_MAP_EVENT_MILITARY)
      ClearCityMapEventSlot(city, slot);
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

// Dispatch one police, fire or military unit to a valid tile, recycling its old
// marker. The service must be supported; warnings retain their original ordering
// and callbacks may change City state before the dispatch continues.
void DispatchCityService(City *city, TileIndex tile_index, int service) {
  assert(tile_index < (u32)city->map_cell_count);
  assert(service >= CITY_DISPATCH_POLICE && service <= CITY_DISPATCH_MILITARY);
  CityPoint point = MapTilePt(city, tile_index);
  typedef struct {
    int empty_message, blocked_message, sound, event_type, capacity;
  } DispatchStyle;
  static const DispatchStyle kStyles[] = {
      {CITY_NOTICE_NO_POLICE_TO_DISPATCH, CITY_NOTICE_CANNOT_DISPATCH_POLICE_HERE,
          CITY_SOUND_POLICE_OR_HOSPITAL, CITY_MAP_EVENT_POLICE, CITY_CIVIC_DISPATCH_UNITS},
      {CITY_NOTICE_NO_FIRETRUCKS_TO_DISPATCH, CITY_NOTICE_CANNOT_DISPATCH_FIRETRUCKS_HERE,
          CITY_SOUND_FIRE_DISPATCH, CITY_MAP_EVENT_FIRE, CITY_CIVIC_DISPATCH_UNITS},
      {CITY_NOTICE_NO_MILITARY_TO_DISPATCH, CITY_NOTICE_CANNOT_DISPATCH_MILITARY_HERE,
          CITY_SOUND_MILITARY_DISPATCH, CITY_MAP_EVENT_MILITARY, CITY_MILITARY_DISPATCH_UNITS},
  };
  const DispatchStyle *style = &kStyles[service];
  CityEventRuntime *runtime = &city->event_runtime;
  bool police = service == CITY_DISPATCH_POLICE, fire = service == CITY_DISPATCH_FIRE;
  i16 *unit_count = police ? &runtime->police_unit_count : fire ? &runtime->fire_unit_count : &runtime->military_unit_count;
  i16 *unit_index = police ? &runtime->police_index : fire ? &runtime->fire_index : &runtime->military_index;
  u32 *blocked_warning = police ? &runtime->police_blocked_warning :
      fire ? &runtime->fire_blocked_warning : &runtime->military_blocked_warning;
  u16 *previous_y = police ? runtime->police_previous_y : fire ? runtime->fire_previous_y : runtime->military_previous_y;
  u16 *previous_x = police ? runtime->police_previous_x : fire ? runtime->fire_previous_x : runtime->military_previous_x;
  if (*unit_count == 0) {
    ShowCityMessage(city, style->empty_message);
    return;
  }
  if (!CanDispatchCityService(city, service)) return;
  if (HasTileWater(&city->tile[tile_index]) || ReadOverlay(city, tile_index) != 0) {
    PlayCitySound(city, tile_index, CITY_SOUND_PLACEMENT_REJECTED);
    if (*blocked_warning == 0) {
      ShowCityMessage(city, style->blocked_message);
      *blocked_warning = 1;
    }
    return;
  }

  PlayCitySound(city, tile_index, style->sound);
  // Normalize stock out-of-range marker indexing instead of corrupting state.
  if (++*unit_index > *unit_count ||
      *unit_index < 1 || *unit_index > style->capacity)
    *unit_index = 1;

  int index = *unit_index;
  int old_y = previous_y[index];
  int old_x = previous_x[index];
  const TileIndex old_index = MapTileXYOrInvalid(city, old_x, old_y);
  if (old_index != INVALID_TILE) {
    int overlay = ReadOverlay(city, old_index);
    if (IsCityEventOverlay(overlay)) {
      int slot = CityEventOverlaySlot(overlay);
      if (city->events.event[slot].type == style->event_type) {
        InvalidateCityMapEvent(city, slot);
        ClearCityMapEventSlot(city, slot);
        WriteOverlay(city, old_index, CITY_OVERLAY_NONE);
      }
    }
  }

  CreateCityMapEventAtTile(city, tile_index, style->event_type, 0);
  previous_y[index] = point.y;
  previous_x[index] = point.x;
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
    PlayCitySound(city, INVALID_TILE, CITY_SOUND_BUS_DEPOT);
  if (city->statistics.value[TK_POLICE_STATION] != 0 &&
      (int)ReadHistorySample(city, CITY_HISTORY_CRIME, 0) > 40 && (Sc2kCrtRand(city) & 0xFF) == 0)
    PlayCitySound(city, INVALID_TILE, CITY_SOUND_POLICE_OR_HOSPITAL);

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

// Materialize the waypoint when leaving the approach stage. Event must be an approaching airplane; transforms its
// stored runway reference once when leaving that stage.
void FinishCityAirplaneApproach(CityMapEvent *event) {
  if (event->type != CITY_MAP_EVENT_AIRPLANE ||
      (event->state & CITY_AIRPLANE_STAGE_MASK) != CITY_AIRPLANE_APPROACH) return;
  int x, y;
  ReadCityAirplaneApproachOffset(event, &x, &y);
  event->airplane.destination_y += y;
  event->airplane.destination_x += x;
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

typedef enum CityEventMoveMode {
  CITY_EVENT_MOVE_SKIP_MARKERS, CITY_EVENT_MOVE_BORDER_WATER, CITY_EVENT_MOVE_POWERED
} CityEventMoveMode;

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
  // Powered movement deliberately excludes the final row and column.
  if (x < 0 || y < 0 || x >= city->map_width - (mode == CITY_EVENT_MOVE_POWERED) ||
      y >= city->map_height - (mode == CITY_EVENT_MOVE_POWERED)) {
    if (mode != CITY_EVENT_MOVE_BORDER_WATER) ClearCityMapEventSlot(city, slot);
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
  if (mode != CITY_EVENT_MOVE_POWERED) event->previous_overlay = ReadCityEventUnderlay(city, tile);
  LinkCityMapEvent(city, slot);
  return true;
}

// Move one event by a fixed-point step and transfer its overlay marker. Requires a valid event slot; delegates
// fixed-point movement and hash transfer without allocating a new event.
void AdvanceCityMapEventPosition(City *city, int mode, int slot, int direction) {
  AdvanceCityEventByStep(city, slot, kCityMapEventSubtileSpeed[mode] * kCityMapEventSubtileXStep[direction],
      kCityMapEventSubtileSpeed[mode] * kCityMapEventSubtileYStep[direction],
      CITY_EVENT_TILE_FRACTION, CITY_EVENT_MOVE_SKIP_MARKERS);
}

// Advance one border-water event by a 12-unit sub-tile step; return 0 at the map edge. Requires a valid slot; returns
// whether the border-water event can continue after the step, including map-edge termination.
bool AdvanceCityMapEventPosition12(City *city, int slot) {
  int direction = city->events.event[slot].direction;
  return AdvanceCityEventByStep(city, slot, kCityMapEvent12StepX[direction], kCityMapEvent12StepY[direction],
      CITY_EVENT_SHIP_FRACTION, CITY_EVENT_MOVE_BORDER_WATER);
}

// Move a powered-tile event in 16-unit fixed-point steps. Requires a valid slot; returns whether the event's
// constrained step can continue and preserves the movement helper's edits.
bool AdvanceCityMapEventPosition16(City *city, int slot) {
  int direction = city->events.event[slot].direction;
  return AdvanceCityEventByStep(city, slot, kCityMapEvent16StepX[direction], kCityMapEvent16StepY[direction],
      CITY_EVENT_TILE_FRACTION, CITY_EVENT_MOVE_POWERED);
}

// Check the sailboat's water route and marina arrival. Requires a live sailboat slot; returns its route/arrival
// predicate without independently advancing the boat.
bool CheckCityEventAdjacentTile(City *city, int slot, int direction) {
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
  return ReadOverlay(city, tile_index) == 0 && HasTileWater(&city->tile[tile_index]);
}

// Update one sailboat, including its distress state. Slot must contain a sailboat; may change distress state, position
// and active counts or release its record.
void UpdateCityMapSailboat(City *city, int slot) {
  CityMapEvent *event = &city->events.event[slot];
  ++city->disasters.tile248_count;
  if (!(city->fixes & (1 << CITY_FIX_EXPANDED_EVENTS)) && city->disasters.tile248_count > 4) {
    ClearCityMapEventSlot(city, slot);
    return;
  }
  if (event->state != CITY_SAILBOAT_SAILING) {
    if (NextCityMapRandomModulo(city, 5) == 0)
      ClearCityMapEventSlot(city, slot);
    return;
  }
  if (NextCityMapRandomModulo(city, 4) != 0) {
    if (CheckCityEventAdjacentTile(city, slot, event->direction))
      AdvanceCityMapEventPosition16(city, slot);
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
    PlayCitySound(city, event_tile, CITY_SOUND_SAILBOAT_OR_ZOO);
  }
  event->direction = (u8)((event->direction + Sc2kCrtRandRange(city, 3) - 1) & 3);
}

// Occasionally turn an eight-way heading left or right using the City CRT generator. Direction must be supported and
// chance positive; the first draw gates whether the second turn-selection draw is consumed.
int RandomlyPerturbCityEventDirection(City *city, int direction, int chance) {
  if (Sc2kCrtRandRange(city, chance) == 0)
    return (Sc2kCrtRandRange(city, 3) + direction - 1) & CITY_HEADING_MASK;
  return direction;
}

// Choose the next eight-way turn toward a requested heading. Inputs must be supported headings; arithmetic wraps
// within the direction cycle without changing any City state.
int TurnCityEventDirectionToward(int direction, int target) {
  int difference = target - direction;
  bool advance_heading = difference >= 0 ? difference <= CITY_HEADING_COUNT / 2 : difference < -CITY_HEADING_COUNT / 2;
  return (direction + (advance_heading ? 1 : -1)) & CITY_HEADING_MASK;
}

// Compute the eight-way heading from the first coordinate pair toward the target pair. Coordinates must keep their
// integer differences representable; this read-only selector uses exact sign tests rather than the half-axis
// thresholds.
int ComputeCityEventDirection(int x, int y, int target_x, int target_y) {
  int dy = target_y - y;
  int dx = target_x - x;
  if (dy == 0)
    return dx < 0 ? CITY_HEADING_WEST : CITY_HEADING_EAST;
  if (dx == 0)
    return dy > 0 ? CITY_HEADING_SOUTH : CITY_HEADING_NORTH;
  if (dy > 0)
    return dx < 0 ? CITY_HEADING_SOUTH_WEST : CITY_HEADING_SOUTH_EAST;
  return dx < 0 ? CITY_HEADING_NORTH_WEST : CITY_HEADING_NORTH_EAST;
}

// Compute the eight-way heading from the first coordinate pair toward the second using half-axis thresholds.
// Coordinate differences must fit integer arithmetic; this read-only calculation consumes no random values.
int CityEventDirection(int destination_x, int destination_y, int source_x, int source_y) {
  int dy = source_y - destination_y;
  int dx = source_x - destination_x;
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
int SteerCityEventTowardTile(int direction, int x, int y, int target_x, int target_y) {
  int target = CityEventDirection(x, y, target_x, target_y);
  return target == direction ? direction : TurnCityEventDirectionToward(direction, target);
}

// Return the Manhattan distance between two coordinate pairs. Differences and their absolute sum must fit int; this
// calculation neither requires map admission nor changes any event state.
int CityEventManhattanDistance(int x, int y, int target_x, int target_y) {
  int dy = target_y - y;
  int dx = target_x - x;
  return (dy < 0 ? -dy : dy) + (dx < 0 ? -dx : dx);
}

// Probe the next directional event position for an arcology-or-later special building. Requires a valid event slot and
// heading; off-map targets return false and this does not move the event.
bool IsCityEventDirectionBlockedBySpecialTile(City *city, int slot, int direction) {
  CityMapEvent *event = &city->events.event[slot];
  CityPoint position = ReadCityMapEventPosition(city, event);
  int y = position.y + kCityEventDestinationYOffset[direction];
  int x = position.x + kCityEventDestinationXOffset[direction];
  TileIndex target = MapTileXYOrInvalid(city, x, y);
  return target != INVALID_TILE && IsTileKindArcology(ReadTile(city, target));
}

// Boats need level water. Native road/rail bridge middles and raised power lines leave water navigable;
// highway bridges retain their alternating passage pieces. Bridge heads and other occupants block passage.
bool IsCityBoatPassageTile(const City *city, TileIndex tile) {
  if (!IsMapTileValid(city, tile)) return false;
  const TileInfo *tp = &city->tile[tile];
  CityTileGeometry geometry = ReadCityTileGeometry(city, tile);
  if (geometry.water_surface != CITY_WATER_SURFACE_LEVEL) return false;

  if (IsTileKindExposedWater(tp->kind) || IsTileKindPlainPowerLine(tp->kind)) return true;
  return CityBridgeAllowsBoat(city, tile);
}

// Check the next directional water-route position and absence of an event overlay. Requires a valid slot/heading;
// off-map positions return true so the caller can finish its departure path.
bool CanCityEventAdvanceInDirection(City *city, int slot, int direction) {
  CityMapEvent *event = &city->events.event[slot];
  CityPoint position = ReadCityMapEventPosition(city, event);
  int y = position.y + kCityEventOverlayBlockYOffset[direction];
  int x = position.x + kCityEventOverlayBlockXOffset[direction];
  const TileIndex tile_index = MapTileXYOrInvalid(city, x, y);
  if (tile_index == INVALID_TILE) return true;
  return IsCityBoatPassageTile(city, tile_index) && !IsCityEventOrMarkerOverlay(ReadOverlay(city, tile_index));
}

// Search the fixed heading order when the current event direction encounters a special-building obstruction. Requires
// a valid event slot and direction; stores the first unblocked heading without drawing random values.
void FindNextValidCityEventDirection(City *city, int slot) {
  CityMapEvent *event = &city->events.event[slot];
  if (!IsCityEventDirectionBlockedBySpecialTile(city, slot, event->direction))
    return;
  int direction = event->direction;
  for (int index = 0; index < 7; ++index) {
    direction = (kCityEventDirectionSearchOrder[index] + event->direction) & 7;
    if (!IsCityEventDirectionBlockedBySpecialTile(city, slot, direction))
      break;
  }
  event->direction = (u8)direction;
}

// Stock collision thresholds: SMALLMED.DAT kinds 113–250, original height / 3.
// These are simulation constants; replacing or resizing artwork must not change collisions.
static const u8 kCityAirplaneCollisionHeights[TK_PLYMOUTH_ARCOLOGY - TK_RESIDENTIAL_FIRST - 1] = {
  [TK_LOWER_CLASS_HOMES_113 - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_LOWER_CLASS_HOMES_114 - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_LOWER_CLASS_HOMES_LAST - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_MIDDLE_CLASS_HOMES_FIRST - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_MIDDLE_CLASS_HOMES_117 - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_MIDDLE_CLASS_HOMES_118 - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_MIDDLE_CLASS_HOMES_LAST - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_LUXURY_HOMES_FIRST - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_LUXURY_HOMES_121 - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_LUXURY_HOMES_122 - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_LUXURY_HOMES_LAST - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_GAS_STATION_1X1_1 - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_BED_BREAKFAST_INN - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_CONVENIENCE_STORE - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_GAS_STATION_1X1_2 - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_SMALL_OFFICE_BUILDING - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_OFFICE_BUILDING_1X1 - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_WAREHOUSE_1X1_1 - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_CASSIDY_S_TOY_STORE - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_WAREHOUSE_1X1_2 - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_CHEMICAL_STORAGE - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_WAREHOUSE_1X1_3 - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_INDUSTRIAL_SUBSTATION - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_CONSTRUCTION_1X1_1 - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_CONSTRUCTION_1X1_2 - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_ABANDONED_BUILDING_1X1_1 - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_ABANDONED_BUILDING_1X1_2 - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_CHEAP_APARTMENTS - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_APARTMENTS_2X2_1 - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_APARTMENTS_2X2_2 - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_NICE_APARTMENTS_2X2_1 - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_NICE_APARTMENTS_2X2_2 - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_CONDOMINIUM_2X2_1 - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_CONDOMINIUM_2X2_2 - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_CONDOMINIUM_2X2_3 - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_SHOPPING_CENTER - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_GROCERY_STORE - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_OFFICE_BUILDING_2X2_1 - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_RESORT_HOTEL - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_OFFICE_BUILDING_2X2_2 - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_OFFICE_RETAIL - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_OFFICE_BUILDING_2X2_3 - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_OFFICE_BUILDING_2X2_4 - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_OFFICE_BUILDING_2X2_5 - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_OFFICE_BUILDING_2X2_6 - TK_RESIDENTIAL_FIRST - 1] = 7,
  [TK_WAREHOUSE_2X2 - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_CHEMICAL_PROCESSING_2X2 - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_FACTORY_2X2_1 - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_FACTORY_2X2_2 - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_FACTORY_2X2_3 - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_FACTORY_2X2_4 - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_FACTORY_2X2_5 - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_FACTORY_2X2_6 - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_CONSTRUCTION_2X2_1 - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_CONSTRUCTION_2X2_2 - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_CONSTRUCTION_2X2_3 - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_CONSTRUCTION_2X2_4 - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_ABANDONED_BUILDING_2X2_1 - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_ABANDONED_BUILDING_2X2_2 - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_ABANDONED_BUILDING_2X2_3 - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_ABANDONED_BUILDING_2X2_4 - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_LARGE_APARTMENT_BUILDING_3X3_1 - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_LARGE_APARTMENT_BUILDING_3X3_2 - TK_RESIDENTIAL_FIRST - 1] = 7,
  [TK_CONDOMINIUM_3X3_1 - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_CONDOMINIUM_3X3_2 - TK_RESIDENTIAL_FIRST - 1] = 8,
  [TK_OFFICE_PARK - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_OFFICE_TOWER_3X3_1 - TK_RESIDENTIAL_FIRST - 1] = 7,
  [TK_MINI_MALL - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_THEATRE_SQUARE - TK_RESIDENTIAL_FIRST - 1] = 8,
  [TK_DRIVE_IN_THEATER - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_OFFICE_TOWER_3X3_2 - TK_RESIDENTIAL_FIRST - 1] = 10,
  [TK_OFFICE_TOWER_3X3_3 - TK_RESIDENTIAL_FIRST - 1] = 11,
  [TK_PARKING_LOT - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_HISTORIC_OFFICE_BUILDING - TK_RESIDENTIAL_FIRST - 1] = 9,
  [TK_CORPORATE_HEADQUARTERS - TK_RESIDENTIAL_FIRST - 1] = 12,
  [TK_CHEMICAL_PROCESSING_3X3 - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_LARGE_FACTORY - TK_RESIDENTIAL_FIRST - 1] = 8,
  [TK_INDUSTRIAL_THINGAMAJIG - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_FACTORY_3X3 - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_LARGE_WAREHOUSE - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_WAREHOUSE_3X3 - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_CONSTRUCTION_3X3_1 - TK_RESIDENTIAL_FIRST - 1] = 7,
  [TK_CONSTRUCTION_3X3_2 - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_ABANDONED_BUILDING_3X3_1 - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_ABANDONED_BUILDING_3X3_2 - TK_RESIDENTIAL_FIRST - 1] = 8,
  [TK_HYDRO_POWER - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_WIND_POWER - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_GAS_POWER_PLANT - TK_RESIDENTIAL_FIRST - 1] = 7,
  [TK_OIL_POWER_PLANT - TK_RESIDENTIAL_FIRST - 1] = 9,
  [TK_NUCLEAR_POWER_PLANT - TK_RESIDENTIAL_FIRST - 1] = 7,
  [TK_SOLAR_POWER_PLANT - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_MICROWAVE_POWER_PLANT - TK_RESIDENTIAL_FIRST - 1] = 8,
  [TK_FUSION_POWER_PLANT - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_COAL_POWER_PLANT - TK_RESIDENTIAL_FIRST - 1] = 9,
  [TK_CITY_HALL - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_HOSPITAL - TK_RESIDENTIAL_FIRST - 1] = 7,
  [TK_POLICE_STATION - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_FIRE_STATION - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_MUSEUM - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_LARGE_PARK - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_SCHOOL - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_STADIUM - TK_RESIDENTIAL_FIRST - 1] = 7,
  [TK_PRISON - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_COLLEGE - TK_RESIDENTIAL_FIRST - 1] = 7,
  [TK_ZOO - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_STATUE - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_WATER_PUMP - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_RUNWAY_FIRST - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_RUNWAY_LAST - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_PIER - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_CRANE - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_CONTROL_TOWER_1X1_1 - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_CONTROL_TOWER_1X1_2 - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_WAREHOUSE_1X1_4 - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_BUILDING_1X1_1 - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_BUILDING_1X1_2 - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_TARMAC - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_F_15B - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_HANGAR_1X1 - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_SUBWAY_STATION - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_RADAR - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_WATER_TOWER - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_BUS_STATION - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_RAIL_STATION - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_PARKING_LOT_2X2_1 - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_PARKING_LOT_2X2_2 - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_LOADING_BAY - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_TOP_SECRET - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_CARGO_YARD - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_MAYORS_HOUSE - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_WATER_TREATMENT_PLANT - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_LIBRARY - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_HANGAR_3X3 - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_CHURCH - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_MARINA - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_MISSILE_SILO - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_DESALINATION_PLANT - TK_RESIDENTIAL_FIRST - 1] = 5,
};

// Advance one airplane through its recorded approach, flight and departure states. Requires a live airplane slot and
// consistent target data; may move, convert or clear the event and adjust active counters.
void UpdateCityMapAirplane(City *city, int slot) {
  CityMapEvent *event = &city->events.event[slot];
  CityPoint position = ReadCityMapEventPosition(city, event);
  ++city->disasters.type1_count;

  TileIndex event_tile = event->tile;
  int tile = ReadTile(city, event_tile);
  // Stock excludes the first house kind from building collision handling.
  if (IsTileKindBuilding(tile) && tile != TK_LOWER_CLASS_HOMES_FIRST &&
      ReadCityZoneKind(&city->tile[event_tile]) != ZONE_AIRPORT) {
    if (IsTileKindArcology(tile)) {
      FinishCityAirplaneApproach(event);
      event->type = CITY_MAP_EVENT_EXPLOSION;
      event->next_slot = CITY_DISASTER_CRASH;
      event->explosion.trigger_disaster = NextCityMapRandomModulo(city, 16) == 0;
      event->direction = CITY_EXPLOSION_START;
      return;
    }
    if (kCityAirplaneCollisionHeights[tile - TK_RESIDENTIAL_FIRST - 1] > event->height) {
      FinishCityAirplaneApproach(event);
      event->type = CITY_MAP_EVENT_EXPLOSION;
      event->next_slot = CITY_DISASTER_CRASH;
      event->explosion.trigger_disaster = 1;
      event->direction = CITY_EXPLOSION_START;
      return;
    }
  }

  switch (event->next_slot & CITY_AIRPLANE_STAGE_MASK) {
  case CITY_AIRPLANE_TAKEOFF:
    AdvanceCityMapEventPosition(city, CITY_MAP_EVENT_AIRPLANE, slot, event->direction);
    if (event->type == CITY_MAP_EVENT_UNUSED) return;
    position = ReadCityMapEventPosition(city, event);
    if (event->height == 0)
      PlayCitySound(city, event->tile, CITY_SOUND_AIRPLANE_TAKEOFF);
    if (event->height >= 14)
      event->next_slot = CITY_AIRPLANE_CRUISE;
    else
      ++event->height;
    break;
  case CITY_AIRPLANE_LANDING:
    AdvanceCityMapEventPosition(city, CITY_MAP_EVENT_AIRPLANE, slot, event->direction);
    if (event->type == CITY_MAP_EVENT_UNUSED) return;
    position = ReadCityMapEventPosition(city, event);
    --event->height;
    if (event->height == 0) {
      TileIndex landing_tile = event->tile;
      PlayCitySound(city, landing_tile, CITY_SOUND_AIRPLANE_LANDING);
      if (ReadTile(city, landing_tile) == TK_RUNWAY_FIRST) {
        ClearCityMapEventSlot(city, slot);
      } else {
        event->type = CITY_MAP_EVENT_EXPLOSION;
        event->next_slot = CITY_DISASTER_CRASH;
        event->explosion.trigger_disaster = 1;
        event->direction = CITY_EXPLOSION_START;
      }
    }
    break;
  case CITY_AIRPLANE_CRUISE:
    event->direction = (u8)RandomlyPerturbCityEventDirection(city, event->direction, 5);
    FindNextValidCityEventDirection(city, slot);
    AdvanceCityMapEventPosition(city, CITY_MAP_EVENT_AIRPLANE, slot, event->direction);
    if (event->type == CITY_MAP_EVENT_UNUSED) return;
    position = ReadCityMapEventPosition(city, event);
    break;
  case CITY_AIRPLANE_APPROACH: {
    int offset_x, offset_y;
    ReadCityAirplaneApproachOffset(event, &offset_x, &offset_y);
    int target_x = event->airplane.destination_x + offset_x;
    int target_y = event->airplane.destination_y + offset_y;
    int direction = ComputeCityEventDirection(position.x, position.y, target_x, target_y);
    event->direction = (u8)direction;
    FindNextValidCityEventDirection(city, slot);
    AdvanceCityMapEventPosition(city, CITY_MAP_EVENT_AIRPLANE, slot, event->direction);
    if (event->type == CITY_MAP_EVENT_UNUSED) return;
    position = ReadCityMapEventPosition(city, event);
    // An off-map waypoint cannot be a runway-alignment arrival.
    if (CityEventManhattanDistance(position.x, position.y, target_x, target_y) < 2 &&
        MapTileXYOrInvalid(city, target_x, target_y) != INVALID_TILE) {
      FinishCityAirplaneApproach(event);
      int orientation = event->next_slot >> 4;
      event->direction = (u8)TurnCityEventDirectionToward(direction, orientation);
      event->next_slot = 16 * orientation + CITY_AIRPLANE_ALIGN_RUNWAY;
      switch (orientation) {
      case CITY_HEADING_SOUTH_WEST:
        event->airplane.destination_x -= 6;
        break;
      case CITY_HEADING_SOUTH_EAST:
        event->airplane.destination_y += 6;
        break;
      case CITY_HEADING_NORTH_EAST:
        event->airplane.destination_x += 6;
        break;
      case CITY_HEADING_NORTH_WEST:
        event->airplane.destination_y -= 6;
        break;
      default:
        break;
      }
    }
    break;
  }
  case CITY_AIRPLANE_ALIGN_RUNWAY: {
    int direction =
        SteerCityEventTowardTile(event->direction, position.x, position.y,
            event->airplane.destination_x, event->airplane.destination_y);
    event->direction = (u8)direction;
    AdvanceCityMapEventPosition(city, CITY_MAP_EVENT_AIRPLANE, slot, direction);
    if (event->type == CITY_MAP_EVENT_UNUSED) return;
    position = ReadCityMapEventPosition(city, event);
    if (CityEventManhattanDistance(position.x, position.y,
            event->airplane.destination_x, event->airplane.destination_y) < 2) {
      int orientation = event->next_slot >> 4;
      event->direction = (u8)orientation;
      event->next_slot = CITY_AIRPLANE_LANDING;
      if (orientation == CITY_HEADING_SOUTH_WEST || orientation == CITY_HEADING_NORTH_EAST)
        MoveCityMapEvent(city, slot, MapTileXY(city, position.x, event->airplane.destination_y));
      else if (orientation == CITY_HEADING_SOUTH_EAST || orientation == CITY_HEADING_NORTH_WEST)
        MoveCityMapEvent(city, slot, MapTileXY(city, event->airplane.destination_x, position.y));
    }
    break;
  }
  case CITY_AIRPLANE_CRASH:
    if (event->height != 0) {
      --event->height;
      if (event->height == 8)
        PlayCitySound(city, event->tile, CITY_SOUND_AIRCRAFT_CRASH);
      int direction = event->direction;
      event->direction = (u8)((direction + 1) & 7);
      AdvanceCityMapEventPosition(city, CITY_MAP_EVENT_AIRPLANE, slot, direction);
      if (event->type == CITY_MAP_EVENT_UNUSED) return;
      position = ReadCityMapEventPosition(city, event);
    } else {
      event->type = CITY_MAP_EVENT_EXPLOSION;
      event->next_slot = CITY_DISASTER_CRASH;
      event->explosion.trigger_disaster = 1;
      event->direction = CITY_EXPLOSION_START;
    }
    break;
  default:
    break;
  }
}

// Advance one helicopter. Slot must contain a live helicopter; may update its target/motion or clear it after its
// final state.
void UpdateCityMapHelicopter(City *city, int slot) {
  CityAudioState *audio = city->game->audio;
  CityMapEvent *event = &city->events.event[slot];
  CityPoint position = ReadCityMapEventPosition(city, event);
  ++city->disasters.type2_count;

  if (IsTileKindArcology(ReadTile(city, event->tile))) {
    event->type = CITY_MAP_EVENT_EXPLOSION;
    event->next_slot = CITY_DISASTER_CRASH;
    event->explosion.trigger_disaster = 0;
    event->direction = CITY_EXPLOSION_START;
    return;
  }

  switch (event->next_slot) {
  case CITY_HELICOPTER_TAKEOFF:
    event->direction = (u8)((event->direction + 1) & 7);
    if (event->height >= 10)
      event->next_slot = CITY_HELICOPTER_FLY_TO_TARGET;
    else
      ++event->height;
    break;
  case CITY_HELICOPTER_FLY_TO_TARGET: {
    event->direction =
        SteerCityEventTowardTile(event->direction, position.x, position.y,
            event->helicopter.destination_x, event->helicopter.destination_y);
    FindNextValidCityEventDirection(city, slot);
    AdvanceCityMapEventPosition(city, CITY_MAP_EVENT_HELICOPTER, slot, event->direction);
    if (event->type == CITY_MAP_EVENT_UNUSED) return;
    position = ReadCityMapEventPosition(city, event);

    if (ReadTrafficDensity(city, MapTileXY(city, (position.x >> 1) * 2, (position.y >> 1) * 2)) >= 0xAA) {
      u32 tick_count = SimulationFrameTime(city);
      if ((i32)(tick_count - audio->helicopter_sound_deadline) > 0) {
        audio->helicopter_sound_deadline = tick_count + 5000;
        PlayCitySound(city, event->tile, CITY_SOUND_HELICOPTER_FLIGHT);
      }
    }

    if (CityEventManhattanDistance(position.x, position.y,
            event->helicopter.destination_x, event->helicopter.destination_y) < 2) {
      int y = (Sc2kCrtRand(city) & 0x3F) + city->geography.center_y - 32;
      int x = (Sc2kCrtRand(city) & 0x3F) + city->geography.center_x - 32;
      if (y < 0 || y >= city->map_height)
        y = (Sc2kCrtRand(city) & 0x3F) + 32;
      if (x < 0 || x >= city->map_width)
        x = (Sc2kCrtRand(city) & 0x3F) + 32;
      event->helicopter.destination_y = y;
      event->helicopter.destination_x = x;

      if ((Sc2kCrtRand(city) & 1) != 0) {
        TileIndex event_tile = event->tile;
        if (ReadTile(city, event_tile) == TK_CLEAR &&
            ReadTerrainCode(&city->tile[event_tile]) == CITY_TERRAIN_FLAT)
          event->next_slot = CITY_HELICOPTER_LANDING;
      }
    }
    break;
  }
  case CITY_HELICOPTER_LANDING:
    event->direction = (u8)((event->direction + 1) & 7);
    if (event->height <= 2)
      event->next_slot = CITY_HELICOPTER_ON_GROUND;
    else
      --event->height;
    break;
  case CITY_HELICOPTER_ON_GROUND:
    if (Sc2kCrtRandRange(city, 20) == 0)
      event->next_slot = CITY_HELICOPTER_TAKEOFF;
    break;
  case CITY_HELICOPTER_CRASH:
    event->direction = (u8)((event->direction + 2) & 7);
    if (event->height == 4)
      PlayCitySound(city, event->tile, CITY_SOUND_AIRCRAFT_CRASH);
    if (event->height <= 2) {
      event->type = CITY_MAP_EVENT_EXPLOSION;
      event->next_slot = CITY_DISASTER_HELICOPTER_CRASH;
      event->explosion.trigger_disaster = 1;
      event->direction = CITY_EXPLOSION_START;
    } else {
      --event->height;
    }
    break;
  default:
    break;
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
    direction = CityEventDirection(x, y, city->geography.center_x, city->geography.center_y);
    break;
  case CITY_MONSTER_APPLY_TILE_EFFECT: {
    if (Sc2kCrtRandRange(city, 25) == 0)
      event->state = CITY_MONSTER_RANDOM_MOVEMENT;
    event->monster.upper_pose = Sc2kCrtRand(city) & 0x7F;
    event->monster.lower_pose = Sc2kCrtRand(city) & 0x7F;
    direction = RandomlyPerturbCityEventDirection(city, event->direction, 5);
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
            RegradeCityTerrainCell(city, target);
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
    direction = RandomlyPerturbCityEventDirection(city, event->direction, 5);
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
