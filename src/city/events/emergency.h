#ifndef SC2K_CITY_EVENTS_EMERGENCY_H
#define SC2K_CITY_EVENTS_EMERGENCY_H

#include "city/types.h"

// Shared resolved service IDs; these are independent of the locally selected dispatch tool.
typedef enum CityDispatchService {
  CITY_DISPATCH_POLICE = 0, CITY_DISPATCH_FIRE = 1, CITY_DISPATCH_MILITARY = 2,
} CityDispatchService;

// Unit markers use slots 1..count, including the final one-based slot.
enum { CITY_CIVIC_DISPATCH_UNITS = 30, CITY_MILITARY_DISPATCH_UNITS = 5 };

typedef struct CityEmergencyDispatchState {
  i16 police_unit_count;
  i16 fire_unit_count;
  i16 military_unit_count;
  i16 police_index;
  i16 fire_index;
  i16 military_index;
  u32 police_blocked_warning;
  u32 fire_blocked_warning;
  u32 military_blocked_warning;
  u16 police_previous_x[CITY_CIVIC_DISPATCH_UNITS + 1];
  u16 police_previous_y[CITY_CIVIC_DISPATCH_UNITS + 1];
  u16 fire_previous_x[CITY_CIVIC_DISPATCH_UNITS + 1];
  u16 fire_previous_y[CITY_CIVIC_DISPATCH_UNITS + 1];
  u16 military_previous_x[CITY_MILITARY_DISPATCH_UNITS + 1];
  u16 military_previous_y[CITY_MILITARY_DISPATCH_UNITS + 1];
} CityEmergencyDispatchState;

// Read-only admission uses the City's disaster mode and available service count.
bool CanDispatchCityService(const City *city, int service);
bool HasCityDispatchedUnits(const City *city);
// Refresh available counts, warning gates and tool unlocks, and remove previously dispatched unit events.
void InitializeCityEmergencyDispatchState(City *city);
// Attempt placement on a valid tile for a supported service, reusing its previous marker.
// Rejections may show a notice without placing a unit; notices and sounds retain their ordering.
void DispatchCityService(City *city, TileIndex tile, int service);
// Remove all emergency unit events. Reset unit cursors only when at least one unit was recalled.
bool RecallCityDispatchedUnits(City *city);

// Shared commands carry an event kind (police/fire/military), independent of the selected local service tool.
// TEST checks availability and tile validity without probing occupancy. Execution acknowledges the dispatch attempt;
// a blocked target may show a warning without placing a marker.
i64 CmdDispatchCityService(City *city, u32 flags, TileIndex tile, int event_type);
// Recall requires INVALID_TILE and zero reserved payload. TEST reports whether units exist without removing them.
i64 CmdRecallDispatch(City *city, u32 flags, TileIndex tile, int reserved1, int reserved2, int reserved3);

#endif
