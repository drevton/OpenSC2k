#ifndef SC2K_CITY_EVENTS_EMERGENCY_H
#define SC2K_CITY_EVENTS_EMERGENCY_H

#include "city/events/types.h"

// Read-only admission uses the City's disaster mode and available service count.
bool CanDispatchCityService(const City *city, int service);
bool HasCityDispatchedUnits(const City *city);
// Refresh available counts, warning gates and tool unlocks, and remove previously dispatched unit events.
void InitializeCityEmergencyDispatchState(City *city);
// Attempt placement on a valid tile for a supported service, reusing its previous marker.
// Rejections may show a notice without placing a unit; notices and sounds retain their ordering.
void DispatchCityService(City *city, TileIndex tile, int service);

// Shared commands carry an event kind (police/fire/military), independent of the selected local service tool.
// TEST checks availability and tile validity without probing occupancy. Execution acknowledges the dispatch attempt;
// a blocked target may show a warning without placing a marker.
i64 CmdDispatchCityService(City *city, u32 flags, TileIndex tile, int event_type);
// Recall requires INVALID_TILE and zero reserved payload. TEST reports whether units exist without removing them.
// Execution resets unit cursors only after recalling at least one emergency unit.
i64 CmdRecallDispatch(City *city, u32 flags, TileIndex tile, int reserved1, int reserved2, int reserved3);

#endif
