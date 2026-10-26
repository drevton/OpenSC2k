#ifndef SC2K_DOMAIN_SRC_CITY_MAPS_MAP_H
#define SC2K_DOMAIN_SRC_CITY_MAPS_MAP_H

#include "city/types.h"

typedef struct TileInfo TileInfo;

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

// Map change storage is defined in map_types.h and borrowed by the invalidation API.
typedef struct SimulationMapChanges SimulationMapChanges;

void InvalidateMapLayer(City *city, CityMapChangeLayer layer);
// Accept a valid tile or INVALID_TILE (no change); other indices violate the contract.
void InvalidateTile(City *city, TileIndex tile);
// Same start contract as InvalidateTile. Clip the rectangle at the right/bottom edges without wrapping rows.
void InvalidateTileRange(City *city, TileIndex start, int width, int height);
void InvalidateMap(City *city);
// Reset pending changes without changing enabled. Presentation clears after every view receives its batch;
// City also clears when replacing the map backing allocation.
void ClearSimulationMapChanges(SimulationMapChanges *changes);

// State

// Select the fixed counter slot used by economy and environment tables. Requires a supported native kind;
// unfinished stock imports retain their source key until native map storage is published. No state changes.
int CityTileStatisticsKey(const City *city, int kind);
// Read the surface counter slot of a borrowed City tile record, including construction and wet power-line projection.
// Bridge underpasses are counted separately with the kind query. Does not change counters or tile metadata.
int ReadCityTileStatisticsKey(const City *city, const TileInfo *tp);
void RecountCityTileStatistics(City *city);
void RebuildLoadedCityMapState(City *city);

// Initialize missing landscape metadata; retain existing seed/tree choices. Rebuild power connections either way.
void InitializeCityLandscape(City *city);

typedef struct CityTerrainMap CityTerrainMap;
typedef struct CityTerrainSettings CityTerrainSettings;

// Prepare preview backing, copy/downsample live terrain and finish derived metadata. City is read-only.
// False means preview backing could not be prepared.
bool ReadCityTerrainPreview(const City *city, CityTerrainMap *map, const CityTerrainSettings *settings);
// Install generated or imported terrain after the caller's City reset. Map dimensions must match allocated storage.
// Failure sets Game's allocation error and preserves earlier tile writes; success includes statistics/RNG/climate.
bool InstallCityTerrainMap(City *city, const CityTerrainMap *map, const CityTerrainSettings *settings);

void AdvanceCityLandscape(City *city);

#endif
