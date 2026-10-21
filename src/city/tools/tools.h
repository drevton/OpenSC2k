#ifndef SC2K_DOMAIN_SRC_CITY_TOOLS_TOOLS_H
#define SC2K_DOMAIN_SRC_CITY_TOOLS_TOOLS_H

#include "city/maps/tile_storage.h"
#include "city/tools/types.h"
#include "city/types.h"
#include "city/engine/command_types.h"
#include "city/infrastructure/infrastructure.h"

TileIndex FindLowerTerrainNeighbor(const City *city, TileIndex tile);
i64 EditCityTerrainCell(City *city, TileIndex tile, bool raise, int tool);
i64 CmdLevelTerrainTile(City *city, u32 flags, TileIndex tile, int height, int options);
i64 CmdStretchTerrainTile(City *city, u32 flags, TileIndex tile, int height, int options);
enum { CITY_STREAM_DEFAULT_LENGTH = 64 };
i64 CmdPlaceStream(City *city, u32 flags, TileIndex tile, int length);

// Demolition

// The start is the northwest tile. Unresolved footprints have INVALID_TILE and zero dimensions.
CityTileArea ResolveBulldozeFootprint(const City *city, TileIndex tile);
// Mutate an admitted structure, including connected tunnels/bridges/runways/piers and footprint metadata.
// Requires a valid tile; performs damage/sound/effects but no admission, pricing or payment.
void BulldozeCityTile(City *city, TileIndex tile, bool animate);
// Tool execution checks admission and pays once before applying demolition. Returns success or a negative error;
// an error after payment retains the original partial effects (for example a tree-removal protest).
i64 ApplyCityBulldozerAtTile(City *city, TileIndex tile, int options);
bool CanBulldozeCityTile(const City *city, TileIndex tile, bool underground, bool alt);
int GetCityBulldozeTileError(const City *city, TileIndex tile, bool underground, bool alt);
bool IsCityBulldozeGroundNoop(const TileInfo *tp, bool underground);
i64 GetCityBulldozeTileCost(const City *city, TileIndex tile, bool underground);

// Read-only quotes for explicit tool variants; no active UI selection is consulted.
CityCommandCost GetCityConstructionCost(const City *city, TileIndex start, TileIndex end, int group, int variant);
CityCommandCost PriceCityConstruction(const City *city, TileIndex start, TileIndex end, int group, int variant);
CityInfrastructureSpan GetCityConstructionSpan(const City *city, TileIndex start, TileIndex end, int group, int variant);

// Placement/terrain operations returning i64 use negative gameplay errors and nonnegative success values.

extern const i16 kCityZoneCosts[ZONE_RESERVED_FIRST];
// Shared zoning admission, including flat dry trees. Does not remove trees or modify their local metadata.
bool CanZoneCityTile(const TileInfo *tp);
// Retained command drag over valid tile endpoints. Reset surcharge/count, count eligible new zoning cells and
// invalidate cells entering or leaving the rectangle. Uses the live construction zone; no payment, map writes or RNG.
void UpdateCityZoneRectanglePreview(City *city, TileIndex origin, TileIndex previous, TileIndex next);
CityCommandCost GetCityZoneRectangleCost(const City *city, TileIndex start, TileIndex end, int zone);
i64 PlaceCityZoneRectangleAt(City *city, TileIndex start, TileIndex end, int zone);
// Apply prepared City.construction.zone_code/cost to valid endpoints, using its live values after any callbacks.
// The caller owns pricing/payment. Returns the zoning result without collecting pointer input or charging again.
i64 ApplyPreparedCityZoneRectangle(City *city, TileIndex start, TileIndex end);

// Placement

// Read-only construction admission and pricing, shared by commands and previews.
bool CanPlaceCityToolCell(const City *city, TileIndex tile, int group, int variant);
int CityToolZoneCode(int group, int variant);
i64 CalculateCityZoneCost(const City *city, TileIndex start, TileIndex end, int zone, int unit_cost);
bool CityZoneRectangleContainsZone(const City *city, TileIndex start, TileIndex end, int zone);
int GetCityFacilityTile(int group, int variant);

// Tables

enum {
  CITY_TOOL_COST_UNAVAILABLE = 0xFFFFFFFF,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE = 255,
  CITY_TOOL_FOOTPRINT_GROUP_COUNT = CITY_TOOL_GROUP_QUERY
};

// Immutable simulation data, available without loading an executable image.
extern const u8 kCityToolFootprintSizes[CITY_TOOL_FOOTPRINT_GROUP_COUNT * CITY_TOOL_VARIANT_CAPACITY];
extern const u32 kCityToolCosts[CITY_TOOL_GROUP_COUNT * CITY_TOOL_VARIANT_CAPACITY];
// Square building sizes shared by demolition, stock codecs and native footprints.
extern const u8 kZoneTerrainSurchargeFlags[16];
extern const u8 kPowerPlantToolVariants[TK_COAL_POWER_PLANT - TK_GAS_POWER_PLANT + 1];

// Terrain

i64 MarkTerrainRaisePath(City *city, TileIndex tile, bool allow_water, TileIndex origin);
i64 RaiseMarkedTerrain(City *city, TileIndex tile);
bool CanRaiseTerrainCell(const City *city, int x, int y);
i64 LowerCityTerrainCell(City *city, TileIndex tile_index, bool charge, int tool);
int ClassifyCardinalDragDirection(int x, int y, int previous_x, int previous_y);
i64 RaiseCityTerrainCell(City *city, TileIndex tile_index, int tool);
i64 StepCityTerrainTowardHeight(City *city, TileIndex tile_index, int target_height,
    int tool);
void RegradeCityTerrainCell(City *city, TileIndex tile);
// Refresh after structure removal. Native recalculation preserves dry ground and existing water levels;
// terrain-edit commands apply their own sea-level policy when committing changed vertices.
void RecalculateTerrainCell(City *city, TileIndex tile_index);
void RebuildCityTerrainCell(City *city, TileIndex tile_index);
void RebalanceAdjacentTerrainElevations(City *city, TileIndex tile_index);
void RefreshTerrainNeighborhood(City *city, TileIndex tile_index);

i64 ApplyBulldozerTile(City *city, TileIndex tile, int options, int tool);

#endif
