#ifndef SC2K_DOMAIN_SRC_CITY_TOOLS_TOOLS_H
#define SC2K_DOMAIN_SRC_CITY_TOOLS_TOOLS_H

#include "city/engine/command_types.h"
#include "city/maps/tile_storage.h"
#include "city/tools/types.h"
#include "city/types.h"

// Enable or disable terrain protection for structures. Validate Boolean/zero reserved values; TEST has no effects.
i64 CmdSetTerrainProtection(City *city, u32 flags, int enabled, int reserved1, int reserved2);

// Raw terrain quote validates tile/tool. Native planning uses resolved terrain options; legacy pricing ignores them.
// Does not change map, treasury, RNG or presentation.
CityCommandCost GetCityTerrainCost(const City *city, TileIndex tile, bool raise, int tool, int options);
i64 EditCityTerrainCell(City *city, TileIndex tile, bool raise, int tool);
i64 CmdLevelTerrainTile(City *city, u32 flags, TileIndex tile, int height, int options);
i64 CmdStretchTerrainTile(City *city, u32 flags, TileIndex tile, int height, int options);

// Demolition

// Retained tools select a cost-table entry explicitly. Check admission and pay once before applying demolition;
// return success or a negative error. A late error retains payment and prior edits (for example a tree protest).
i64 ApplyBulldozerTile(City *city, TileIndex tile, int options, int cost_index);
// Read-only admission uses resolved layer/alt choices; invalid tiles are rejected as nothing to remove.
// City is mutable only to dispatch the module callback with CITY_COMMAND_TEST; these queries have no effects.
bool CanBulldozeCityTile(City *city, TileIndex tile, bool underground, bool alt);
int GetCityBulldozeTileError(City *city, TileIndex tile, bool underground, bool alt);
// Quote a valid tile without mutation, payment, RNG or invalidation. Admission is checked separately.
i64 GetCityBulldozeTileCost(const City *city, TileIndex tile, bool underground);

// Structural removal command. Validate tile and animate (0/1); TEST returns zero without editing.
// Execution removes the structure without tool admission or payment and reports completion through Game's error record.
i64 CmdBulldoze(City *city, u32 flags, TileIndex tile, int animate);
// Paid single-cell tool command with resolved layer/alt/effect options. TEST checks admission and quotes its price;
// execution checks admission, charges once and restores the prior local effect suppression after the edit.
// A late error retains payment and prior edits. TEST callbacks cannot pump input or replace map storage.
i64 CmdApplyBulldozerTool(City *city, u32 flags, TileIndex tile, int options);
// Rectangle TEST quotes the current cells; execution checks and pays each live edit after earlier footprint changes.
// If no cells are admitted, the quote retains the last specific blocker in row order,
// or CITY_COMMAND_COST_NOTHING_TO_REMOVE when there is none.
// Execution may open a tree-removal notice. TEST is read-only; invalid endpoints/options return a negative error.
i64 CmdBulldozeRect(City *city, u32 flags, TileIndex start, TileIndex end, int options);

// Placement/terrain operations returning i64 use negative gameplay errors and nonnegative success values.

extern const i16 kCityZoneCosts[ZONE_RESERVED_FIRST];
// Retained command drag over valid tile endpoints. Reset surcharge/count, count eligible new zoning cells and
// invalidate cells entering or leaving the rectangle. Uses the live construction zone; no payment, map writes or RNG.
void UpdateCityZoneRectanglePreview(City *city, TileIndex origin, TileIndex previous, TileIndex next);
// Validate resolved zoning endpoints and quote in TEST mode; execution preserves the prepared zoning/payment path.
i64 CmdPlaceZoneRect(City *city, u32 flags, TileIndex tile, int zone, TileIndex end);
// Apply prepared City.construction.zone_code/cost to valid endpoints, using its live values after any callbacks.
// The caller owns pricing/payment. Returns the zoning result without collecting pointer input or charging again.
i64 ApplyPreparedCityZoneRectangle(City *city, TileIndex start, TileIndex end);

// Placement

// Surface eligibility shared by zoning quotes, rectangle previews and retained admission. Reads tp only;
// rejects water, transport, buildings, radioactive waste, parks, slopes and military zones.
bool CanZoneCityTile(const TileInfo *tp);
i64 CalculateCityZoneCost(const City *city, TileIndex start, TileIndex end, int zone, int unit_cost);
// Read the zones in an inclusive rectangle without editing construction state; invalid endpoints return false.
bool CityZoneRectangleContainsZone(const City *city, TileIndex start, TileIndex end, int zone);

// Tables

enum { CITY_TOOL_COST_UNAVAILABLE = 0xFFFFFFFF };

// Immutable simulation data, available without loading an executable image.
extern const u32 kCityToolCosts[CITY_TOOL_GROUP_COUNT * CITY_TOOL_VARIANT_CAPACITY];

// Terrain

i64 MarkTerrainRaisePath(City *city, TileIndex tile, bool allow_water, TileIndex origin);
i64 RaiseMarkedTerrain(City *city, TileIndex tile);
i64 LowerCityTerrainCell(City *city, TileIndex tile_index, bool charge, int tool);
i64 RaiseCityTerrainCell(City *city, TileIndex tile_index, int tool);
i64 StepCityTerrainTowardHeight(City *city, TileIndex tile_index, int target_height,
    int tool);

// Shared command admission is read-only in TEST mode; execution retains the family placement/payment order.
i64 CmdRaiseOrStepTerrain(City *city, u32 flags, TileIndex tile,
    bool raise, int options, int terrain_options, int reserved);
i64 CmdLowerTerrainTile(City *city, u32 flags, TileIndex tile,
    int options, int terrain_options, int reserved);

#endif
