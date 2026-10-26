#ifndef SC2K_CITY_MAPS_BUILDINGS_H
#define SC2K_CITY_MAPS_BUILDINGS_H

#include "city/engine/command_types.h"
#include "city/maps/tile_storage.h"

typedef struct CityTileGeometry CityTileGeometry;
typedef struct Canvas Canvas;

// Buildings are identified by their actual tile kind, independent of toolbar layout.
// Base purchase prices also supply plant renewal and corrected power-plant/arcology asset values.
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
// Stored map-relative facing of a valid tile. Non-building occupants use the City's default placement facing.
// Known building records can decode their terrain payload directly when that fallback is not needed.
int ReadBuildingOrientation(const City *city, TileIndex tile);
// Complete valid rectangles only; initialize links after kinds exist. Facing uses stored map coordinates.
void InitializeCityBuildingFootprint(City *city, TileIndex top_left, int width, int height, int facing);
// Northwest native anchor of a valid building tile; broken or mismatched links return INVALID_TILE.
// Highways use their domain anchor; other occupants return the supplied tile. Read-only, with no allocation.
TileIndex GetBuildingStartTile(const City *city, TileIndex tile);
// Square side length in tiles, independent of facing. The small park returns one; other non-building kinds return zero.
int GetBuildingFootprintSize(int kind);
SC2K_PRESERVE_MOST bool IsCityBuildingCorner(const City *city, TileIndex tile, int corner);
// Rotation-relative CityFootprintCornerMask bits in the low nibble; zero for invalid continuation links.
SC2K_PRESERVE_MOST int CityBuildingCornerMask(const City *city, TileIndex tile);

// Unsupported categories read as stage zero. Writes preserve unrelated altitude bits, counters and targeted damage.
int ReadCityBuildingConstructionStage(const TileInfo *tp);
int CityBuildingConstructionKind(const TileInfo *tp);
bool SetCityBuildingConstructionStage(City *city, TileIndex tile, int stage);
// Apply a stage to the complete valid footprint; malformed anchors are ignored.
void SetCityBuildingFootprintStage(City *city, TileIndex tile, int stage);
void AdvanceCityBuildingConstruction(City *city, TileIndex tile);
// Design cutoff for newly placed wind turbines; stored tiles retain their selected era.
enum { CITY_WIND_MODERN_YEAR = 1980 };
// Requires a valid tile. Checks and installs the footprint without payment, updating records, metadata and damage.
// Sizes 1/2 use the picked tile as top-left; sizes 3/4 extend one tile north and west of it.
// Returns success or a negative gameplay error.
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

// Plant smoke profile (oil, nuclear or coal), or -1 for kinds without smoke. Shared by drawing and damage bounds.
int CityBuildingSmokeIndex(int kind);
enum { CITY_BUILDING_PREVIEW_WIDTH = 144, CITY_BUILDING_PREVIEW_HEIGHT = 150 };
// kind must have a prepared construction-chooser preview; orientation is the resolved artwork facing in 0..3.
// UI resolves tool selection and camera/placement rotation before this draw, which does not read the active tool.
// City supplies artwork policy/year without mutation. False reports a missing prepared sprite to the caller.
bool DrawCityBuildingPreview(const City *city, Canvas *canvas, CityRect bounds, TileKind kind, int orientation);
// Fit the building's installed artwork into a caller-owned 40x40 icon, including the wind rotor/year variant.
// Missing artwork or preparation failure returns false; temporary sprites are released. City remains read-only.
bool DecodeCityBuildingCursorIcon(const City *city, TileKind kind, const u32 palette[256], u32 *icon);
// Requires bound Drawing metrics. sprite is an unzoomed catalog-cache slot, not a tile kind
// or resource image ID; zoom is 0..2. Return logical width and top-to-ground distance before target/display scaling.
// Native buildings require installed artwork and include every view, construction and smoke/rotor extents.
// Other sprites use their cached metrics.
// Read-only: no preparation, painting, allocation or camera dependence.
CityPoint CityBuildingSpriteExtent(const City *city, int sprite, int zoom);
// Draw shared/custom building artwork in the caller's existing fallback phase. Sprite already includes zoom.
void DrawCityBuildingFallback(const City *city, TileIndex tile, int sprite, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry);
typedef enum CityBuildingSelectionPhase {
  CITY_BUILDING_SELECTION_AFTER, CITY_BUILDING_SELECTION_BEFORE
} CityBuildingSelectionPhase;
// Options come from the active Drawing view; this changes pixels/selection only, never City state.
void DrawCityBuildingSelection(const City *city, TileIndex tile, CityBuildingSelectionPhase phase, u32 options);

#endif
