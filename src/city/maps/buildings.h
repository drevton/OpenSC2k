#ifndef SC2K_CITY_MAPS_BUILDINGS_H
#define SC2K_CITY_MAPS_BUILDINGS_H

#include "city/engine/command_types.h"
#include "city/maps/tile_storage.h"
#include "city/maps/terrain.h"

// Buildings are identified by their actual tile kind, independent of toolbar layout.
typedef struct {
  u32 cost;
  u8 size, reward; // Zero reward means none; otherwise the availability index plus one.
} CityBuildingDefinition;
extern const CityBuildingDefinition kCityBuildings[TK_COUNT];
// Building growth states shared by the zone and tile lookup tables.
// Values 0–2 identify developed, construction and abandoned zone buildings.
typedef enum CityBuildingGrowthState {
  CITY_BUILDING_DEVELOPED = 0, CITY_BUILDING_CONSTRUCTION = 1, CITY_BUILDING_ABANDONED = 2,
} CityBuildingGrowthState;

extern const u16 kTilePopulationSize[TK_COUNT - TK_RESIDENTIAL_FIRST];
extern const u8 kTileZoneCategory[TK_COUNT - TK_RESIDENTIAL_FIRST];
extern const u8 kTilePollutionContribution[TK_COUNT - TK_RESIDENTIAL_FIRST];
extern const u8 kTileDensityContribution[TK_HYDRO_POWER - TK_RESIDENTIAL_FIRST];

// Footprint lookup subtracts the first building kind; it also covers non-purchasable growth buildings.
extern const u8 kCityBuildingFootprintSize[TK_COUNT - TK_RESIDENTIAL_FIRST];
// Complete valid rectangles only; initialize links after kinds exist. Facing uses stored map coordinates.
void InitializeCityBuildingFootprint(City *city, TileIndex top_left, int width, int height, int facing);
// Top-left native anchor; broken continuation links return INVALID_TILE. Existing highway callers retain their anchor.
TileIndex GetBuildingStartTile(const City *city, TileIndex tile);
CityPoint GetBuildingFootprintDimensions(int kind, int orientation);
SC2K_PRESERVE_MOST bool IsCityBuildingCorner(const City *city, TileIndex tile, int corner);
SC2K_PRESERVE_MOST int CityBuildingCornerMask(const City *city, TileIndex tile, const u8 *zones, int stride);

// Unsupported categories read as stage zero. Writes preserve unrelated altitude bits, counters and targeted damage.
int ReadCityBuildingConstructionStage(const TileInfo *tp);
int CityBuildingConstructionKind(const TileInfo *tp);
bool SetCityBuildingConstructionStage(City *city, TileIndex tile, int stage);
// Apply a stage to the complete valid footprint; malformed anchors are ignored.
void SetCityBuildingFootprintStage(City *city, TileIndex tile, int stage);
void AdvanceCityBuildingConstruction(City *city, TileIndex tile);
// Design cutoff for newly placed wind turbines; stored tiles retain their selected era.
enum { CITY_WIND_MODERN_YEAR = 1980 };
// Square footprint admission: zero is allowed, one is blocked, other values identify a rejection notice.
// For sizes 1/2 the picked tile is the top-left; sizes 3/4 extend one tile north and west of it.
int GetCityFacilityPlacementError(const City *city, TileIndex tile, int kind, int size);
// Requires a valid tile. Checks and installs the footprint without payment, updating records, metadata and damage.
// Returns success or a negative gameplay error; uses the same picked-tile convention as the admission query.
i64 PlaceSpecialZoneFootprint(City *city, TileIndex tile, u8 kind, int size);
// Quote admission without RNG or callbacks. Placement remains unpaid; the command pays before the finishing phase.
CityCommandCost GetCityBuildingCost(const City *city, TileIndex tile, int kind);
// Command admission includes kind, tile and optional facing (-1 means automatic). TEST quotes without mutation.
// Placement installs the footprint unpaid; DoCityCommand pays before calling FinishCityBuildingCommand.
i64 CmdPlaceBuilding(City *city, u32 flags, TileIndex tile, int kind, int orientation, int reserved);
void FinishCityBuildingCommand(City *city, TileIndex tile, int kind, int orientation);
i64 PlaceCityBuilding(City *city, TileIndex tile, int kind);
void FinishCityBuildingPlacement(City *city, TileIndex tile, int kind);
i64 PlaceCityWaterPowerFacility(City *city, TileIndex tile);

// Remove the connected runway or pier/crane component, using the existing shared route queue. Start must be valid
// and kind captured before removal. Preserves dequeue/neighbor order, rubble RNG and per-cell effects, without payment.
// The caller owns the final sound and damage covering the selected footprint.
void RemoveCityConnectedRunwayOrPier(City *city, TileIndex start, int kind, bool animate);

// Sprite source selection is independent of native kind storage and preserves explicit artwork overrides.
int CityBuildingSpriteFirst(int kind);
int CityBuildingSprite(const City *city, int kind, int orientation);
int CityBuildingSmokeIndex(int kind);
// Bounds accept a source layout, include every view/effect, and remain in map coordinates before target scaling.
CityPoint CityBuildingSpriteExtent(const City *city, int layout, int zoom);
// Draw shared/custom building artwork in the caller's existing fallback phase. Sprite already includes zoom.
void DrawCityBuildingFallback(City *city, TileIndex tile, int sprite, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry);
typedef enum CityBuildingSelectionPhase {
  CITY_BUILDING_SELECTION_AFTER, CITY_BUILDING_SELECTION_BEFORE
} CityBuildingSelectionPhase;
// Options come from the active Drawing view; this changes pixels/selection only, never City state.
void DrawCityBuildingSelection(City *city, TileIndex tile, CityBuildingSelectionPhase phase, u32 options);

#endif
