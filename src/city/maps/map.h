#ifndef SC2K_DOMAIN_SRC_CITY_MAPS_MAP_H
#define SC2K_DOMAIN_SRC_CITY_MAPS_MAP_H

#include "city/maps/map_types.h"
#include "city/maps/tile_storage.h"
#include "city/types.h"

// Queries read valid tiles and retain no pointers into City or presentation owners.
CityTileInformation ReadCityTileInformation(const City *city, TileIndex tile);
// Summarize the compatibility-indexed counters without narrowing their native 32-bit values. Output is borrowed
// caller storage; construction and abandoned categories retain their existing exclusion rules.
void CalculateCityLandUse(const CityStatistics *statistics, u32 acres[CITY_LAND_USE_COUNT],
    u16 percent[CITY_LAND_USE_COUNT]);

// Environment passes require exclusive use of City scratch until complete. A zero-initialized scan starts a pass;
// retain it between bounded row budgets. False means work remains, true means the derived layers are complete.
bool StepCityEnvironment(City *city, CityEnvironmentScan *scan, int rows);
void UpdateCityEnvironmentMaps(City *city);

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

// City owns these pending presentation notifications; they are not saved simulation data. Tile invalidations
// call the direct UI damage hook immediately. Bulk simulation passes notify layers once.
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
// Reset pending changes without changing enabled. Presentation clears after every view receives its batch;
// City also clears when replacing the map backing allocation.
void ClearSimulationMapChanges(SimulationMapChanges *changes);

// State

void RecountCityTileStatistics(City *city);
void RebuildLoadedCityMapState(City *city);
void RecountCityIndustryConnections(City *city);

void InitializeCityLandscape(City *city);

typedef struct CityTerrainMap CityTerrainMap;
void GenerateCityLandscape(City *city, const CityTerrainMap *terrain);
void AdvanceCityLandscape(City *city);

i64 UpdateCityMapTileStates(City *city);

#endif
