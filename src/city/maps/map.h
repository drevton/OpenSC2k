#ifndef SC2K_DOMAIN_SRC_CITY_MAPS_MAP_H
#define SC2K_DOMAIN_SRC_CITY_MAPS_MAP_H

#include "city/maps/tile_storage.h"
#include "city/maps/map_types.h"
#include "city/engine/input.h"
#include "city/types.h"

// Queries read valid tiles and retain no pointers into City or presentation owners.
CityTileInformation ReadCityTileInformation(const City *city, TileIndex tile);
void CalculateCityLandUse(const i16 *tile_counts, u16 acres[CITY_LAND_USE_COUNT],
    u16 percent[CITY_LAND_USE_COUNT]);

// Native terrain bits 0..3 always store ground shape, including submerged beds.
// Kind stores water presence; flags retain salinity. Water occupants retain their underlying water kind in terrain.
// Heights are map levels. A dry tile has no water height, even if ALTM contains
// stale bits or a building stage. Callers must supply a valid tile.

// Water presence comes from TileKind; dry tiles may retain TF_SALTWATER.
typedef enum CityWaterKind {
  CITY_WATER_NONE = 0,
  CITY_WATER_FRESH = 1,
  CITY_WATER_SALT = 2
} CityWaterKind;

typedef enum CityWaterSurface {
  CITY_WATER_SURFACE_NONE,
  CITY_WATER_SURFACE_LEVEL,
  CITY_WATER_SURFACE_SHORE,
  CITY_WATER_SURFACE_RAPIDS,
  CITY_WATER_SURFACE_UNSUPPORTED
} CityWaterSurface;

typedef struct {
  int land_height, land_shape;
  int water_height;
  int surface_height, surface_shape;
  CityWaterKind water;
  CityWaterSurface water_surface;
} CityTileGeometry;

// Resolve surface shapes and water classification. For individual stored fields, use the layer accessors;
// ReadCitySurfaceHeight reads just the surface elevation without decoding shoreline geometry.
CityTileGeometry ReadCityTileGeometry(const City *city, TileIndex tile);
int CityTerrainSlopeAt(const City *city, TileIndex tile);
// Lowest height shared by the two adjacent records along a north/east/south/west edge.
int CityTerrainEdgeHeight(const TileInfo *tp, const TileInfo *neighbor, int direction);
// Native terrain edits preserve the supplied water level and existing salinity.
// Dry land newly covered by water borrows salinity from neighboring water.
// keep_rapids preserves existing sloped river water; landscaping passes false
// so a raised river bed at the water plane becomes dry instead.
// Updates clear/water/river/bank kinds while retaining river behavior independently of salinity.
// Their statistics counter is shared; the caller owns old/new invalidation.
void SetCityTerrainWater(City *city, TileIndex tile, int water_height, int land_shape, bool keep_rapids);

// Changes

typedef enum CityMapChangeLayer {
  CITY_MAP_CHANGE_TILE,
  CITY_MAP_CHANGE_FLAGS,
  CITY_MAP_CHANGE_ZONE,
  CITY_MAP_CHANGE_TERRAIN,
  CITY_MAP_CHANGE_UNDERGROUND,
  CITY_MAP_CHANGE_OVERLAY,
  CITY_MAP_CHANGE_ALTITUDE,
  CITY_MAP_CHANGE_POLLUTION,
  CITY_MAP_CHANGE_TRAFFIC_DENSITY,
  CITY_MAP_CHANGE_CRIME,
  CITY_MAP_CHANGE_LAND_VALUE,
  CITY_MAP_CHANGE_DENSITY,
  CITY_MAP_CHANGE_POLICE_COVERAGE,
  CITY_MAP_CHANGE_FIRE_COVERAGE,
  CITY_MAP_CHANGE_DENSITY_HISTORY,
  CITY_MAP_CHANGE_COUNT
} CityMapChangeLayer;

// Presentation notifications are not saved simulation data. Tile invalidations
// immediately mark screen regions; bulk simulation passes notify layers once.
typedef struct {
  u16 invalidated_layers;
  bool tiles_changed;
  bool full;
  bool enabled;
} SimulationMapChanges;

void InvalidateMapLayer(City *city, CityMapChangeLayer layer);
// Accept a valid tile or INVALID_TILE (no change); other indices violate the contract.
void InvalidateTile(City *city, TileIndex tile);
// Same start contract as InvalidateTile. Clip the rectangle at the right/bottom edges without wrapping rows.
void InvalidateTileRange(City *city, TileIndex start, int width, int height);
void RefreshCityTileClimates(City *city, TileIndex tile);
void InvalidateMap(City *city);
// Only the presentation publisher clears the shared batch, after notifying every view.
void ClearSimulationMapChanges(SimulationMapChanges *changes);

// Input

// Labels

void ClearCityOverlayNames(City *city);
void ClearOverlayAssociation(City *city, int overlay);
int FindFreeOverlayNameSlot(City *city);
bool CanEditCityMapSign(const City *city, TileIndex tile);
bool CityTileHasFacilityLabel(const City *city, TileIndex tile);
// A leading byte 1..31 selects a native sign color; plain names retain default green.
int CitySignTextColor(const char *text);
const char *CitySignTextName(const char *text);
i64 CmdRenameCitySign(City *city, u32 flags, TileIndex tile, int slot, const u8 *text, int length);
i64 CmdMoveCitySign(City *city, u32 flags, TileIndex tile, TileIndex destination, int slot);
void EditCityOverlayNameAtTile(City *city, TileIndex tile);
i64 ApplyCityMapLabelChoice(City *city, TileIndex tile, const CityNameCommandInput *state, int answer);
void ApplyCityFacilityLabelChoice(City *city, TileIndex tile, const CityNameCommandInput *state, int answer);
void EditCityOverlayLabelAtCell(City *city, TileIndex tile);
void ApplyCityStadiumTeamChoice(City *city, TileIndex tile, int team, const char *name);
void AcceptCityStadiumTeamDialog(City *city, TileIndex tile, int team, const char *name);

// State

void RecountCityTileStatistics(City *city);
void ScanCityMapPhase20(City *city);
void RebuildLoadedCityMapState(City *city);
void RecountCityIndustryConnections(City *city);

void InitializeCityLandscape(City *city);

typedef struct CityTerrainMap CityTerrainMap;
void GenerateCityLandscape(City *city, const CityTerrainMap *terrain);
void AdvanceCityLandscape(City *city);

i64 UpdateCityMapTileStates(City *city);

#endif
