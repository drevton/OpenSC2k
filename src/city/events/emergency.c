// Emergency service availability, dispatch markers, one-time warnings and recall belong to this module.
// Event pool/hash membership remains shared with moving vehicles; no separate marker allocation is retained.

#include "city/maps/labels.h"
#include "city/events/emergency.h"

#include <assert.h>

#include "assets/resource_types.h"
#include "game/audio.h"
#include "city/events/events.h"
#include "city/engine/commands.h"
#include "city/maps/layers.h"
#include "city/military.h"
#include "city/presentation.h"
#include "city/state.h"
#include "city/tools/types.h"

// Check disaster/timer mode and available units for the explicit emergency-service kind. Returns admission without
// moving units, editing overlays or consuming RNG; unsupported kinds are rejected.
bool CanDispatchCityService(const City *city, int service) {
  if (city->settings.timer_mode != CITY_TIMER_EVENT || !city->disasters.map_update_pending) return false;
  const CityEmergencyDispatchState *dispatch = &city->event_runtime.dispatch;
  switch (service) {
  case CITY_DISPATCH_POLICE: return dispatch->police_unit_count > 0;
  case CITY_DISPATCH_FIRE: return dispatch->fire_unit_count > 0;
  case CITY_DISPATCH_MILITARY: return dispatch->military_unit_count > 0;
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

// Refresh available units and tool unlocks, then remove previously dispatched events.
// If no local services are available, announce the guard before selecting its dispatch tool.
void InitializeCityEmergencyDispatchState(City *city) {
  static const u8 kMilitaryUnits[CITY_MILITARY_BASE_COUNT] = {
    [CITY_MILITARY_BASE_ARMY] = 5, [CITY_MILITARY_BASE_AIR_FORCE] = 2, [CITY_MILITARY_BASE_NAVY] = 3,
  };
  CityEmergencyDispatchState *dispatch = &city->event_runtime.dispatch;
  dispatch->police_blocked_warning = 0;
  dispatch->fire_blocked_warning = 0;
  dispatch->military_blocked_warning = 0;
  int police = city->statistics.value[TK_POLICE_STATION];
  int fire = city->statistics.value[TK_FIRE_STATION];
  dispatch->police_unit_count = police >> 3;
  dispatch->fire_unit_count = fire >> 3;
  // Invalid unlock modes previously indexed beyond the original six stack words.
  dispatch->military_unit_count =
      city->unlocks.military_base < CITY_MILITARY_BASE_COUNT ? kMilitaryUnits[city->unlocks.military_base] : 0;
  if (dispatch->police_unit_count == 0 && dispatch->fire_unit_count == 0 &&
      dispatch->military_unit_count == 0) {
    dispatch->military_unit_count = 1;
    StopCitySound(city->game, CITY_SOUND_BULLDOZER_WORK);
    PlayCitySound(city, INVALID_TILE, CITY_SOUND_CIVIC_PLACEMENT);
    ShowCityResourceAlertDialog(city, CITY_BITMAP_NOTICE_406, CITY_NOTICE_NATIONAL_GUARD_DEPLOYED);
  }
  city->unlocks.tools.slot[CITY_TOOL_GROUP_DISPATCH] = 0;
  if (dispatch->military_unit_count != 0) {
    city->unlocks.tools.slot[CITY_TOOL_GROUP_DISPATCH] = CITY_TOOL_UNLOCK_MILITARY_DISPATCH;
    SelectCityDispatchTool(city, CITY_DISPATCH_MILITARY);
  }
  if (dispatch->fire_unit_count != 0) {
    SelectCityDispatchTool(city, CITY_DISPATCH_FIRE);
    city->unlocks.tools.slot[CITY_TOOL_GROUP_DISPATCH] |= 2;
  }
  if (dispatch->police_unit_count != 0) {
    SelectCityDispatchTool(city, CITY_DISPATCH_POLICE);
    city->unlocks.tools.slot[CITY_TOOL_GROUP_DISPATCH] |= 1;
  }
  dispatch->police_index = 0;
  dispatch->fire_index = 0;
  dispatch->military_index = 0;
  for (int slot = CITY_EVENT_SLOT_FIRST; slot < city->events.capacity; ++slot) {
    int type = city->events.event[slot].type;
    if (type == CITY_MAP_EVENT_POLICE || type == CITY_MAP_EVENT_FIRE || type == CITY_MAP_EVENT_MILITARY)
      ClearCityMapEventSlot(city, slot);
  }
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
      [CITY_DISPATCH_POLICE] = {CITY_NOTICE_NO_POLICE_TO_DISPATCH, CITY_NOTICE_CANNOT_DISPATCH_POLICE_HERE,
          CITY_SOUND_POLICE_OR_HOSPITAL, CITY_MAP_EVENT_POLICE, CITY_CIVIC_DISPATCH_UNITS},
      [CITY_DISPATCH_FIRE] = {CITY_NOTICE_NO_FIRETRUCKS_TO_DISPATCH, CITY_NOTICE_CANNOT_DISPATCH_FIRETRUCKS_HERE,
          CITY_SOUND_FIRE_DISPATCH, CITY_MAP_EVENT_FIRE, CITY_CIVIC_DISPATCH_UNITS},
      [CITY_DISPATCH_MILITARY] = {CITY_NOTICE_NO_MILITARY_TO_DISPATCH, CITY_NOTICE_CANNOT_DISPATCH_MILITARY_HERE,
          CITY_SOUND_MILITARY_DISPATCH, CITY_MAP_EVENT_MILITARY, CITY_MILITARY_DISPATCH_UNITS},
  };
  const DispatchStyle *style = &kStyles[service];
  CityEmergencyDispatchState *dispatch = &city->event_runtime.dispatch;
  bool police = service == CITY_DISPATCH_POLICE, fire = service == CITY_DISPATCH_FIRE;
  i16 *unit_count = police ? &dispatch->police_unit_count :
      fire ? &dispatch->fire_unit_count : &dispatch->military_unit_count;
  i16 *unit_index = police ? &dispatch->police_index :
      fire ? &dispatch->fire_index : &dispatch->military_index;
  u32 *blocked_warning = police ? &dispatch->police_blocked_warning :
      fire ? &dispatch->fire_blocked_warning : &dispatch->military_blocked_warning;
  u16 *previous_y = police ? dispatch->police_previous_y :
      fire ? dispatch->fire_previous_y : dispatch->military_previous_y;
  u16 *previous_x = police ? dispatch->police_previous_x :
      fire ? dispatch->fire_previous_x : dispatch->military_previous_x;
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

// Recall the City's emergency units without changing vehicle/effect events or available service counts.
bool RecallCityDispatchedUnits(City *city) {
  bool recalled = false;
  for (int slot = CITY_EVENT_SLOT_FIRST; slot < city->events.capacity; ++slot) {
    int type = city->events.event[slot].type;
    if (type != CITY_MAP_EVENT_POLICE && type != CITY_MAP_EVENT_FIRE && type != CITY_MAP_EVENT_MILITARY) continue;
    InvalidateCityMapEvent(city, slot);
    ClearCityMapEventSlot(city, slot);
    recalled = true;
  }
  if (recalled) {
    CityEmergencyDispatchState *dispatch = &city->event_runtime.dispatch;
    dispatch->police_index = 0;
    dispatch->fire_index = 0;
    dispatch->military_index = 0;
  }
  return recalled;
}

// Resolve the shared event kind to a service and validate its availability. Execution acknowledges an attempt;
// DispatchCityService owns target rejection notices and may finish without creating a marker.
i64 CmdDispatchCityService(City *city, u32 flags, TileIndex tile, int event_type) {
  int service;
  switch (event_type) {
  case CITY_MAP_EVENT_POLICE: service = CITY_DISPATCH_POLICE; break;
  case CITY_MAP_EVENT_FIRE: service = CITY_DISPATCH_FIRE; break;
  case CITY_MAP_EVENT_MILITARY: service = CITY_DISPATCH_MILITARY; break;
  default: return CITY_COMMAND_COST_INVALID;
  }
  if (!IsMapTileValid(city, tile) || !CanDispatchCityService(city, service)) return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) return 0;
  DispatchCityService(city, tile, service);
  return 1;
}

// Recall existing emergency units according to the shared action contract. Payload fields are reserved and must be
// zero; TEST reports admission without removing event markers or changing dispatch state.
i64 CmdRecallDispatch(City *city, u32 flags, TileIndex tile,
    int reserved1, int reserved2, int reserved3) {
  int invalid = CITY_COMMAND_COST_INVALID;
  if (tile != INVALID_TILE || reserved1 || reserved2 || reserved3) return invalid;
  if (flags & CITY_COMMAND_TEST) return HasCityDispatchedUnits(city) ? 0 : invalid;
  return RecallCityDispatchedUnits(city) ? 1 : invalid;
}
