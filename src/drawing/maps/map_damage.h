#ifndef SC2K_DRAWING_MAP_DAMAGE_H
#define SC2K_DRAWING_MAP_DAMAGE_H

#include "drawing/types.h"
#include "drawing/maps/map_camera.h"
#include "drawing/maps/map_view.h"

typedef struct Drawing Drawing;
typedef struct SimulationMapChanges SimulationMapChanges;

// Map Damage

// Last published map values, owned by drawing and advanced after all viewports
// receive damage. Simulation writes never capture or update these arrays.
typedef struct {
  u8 *tiles, *flags, *zones, *terrain, *underground, *overlays, *heights;
  u16 *altitude;
  u8 *pollution, *traffic_density, *crime, *land_value;
  u8 *density, *police_coverage, *fire_coverage, *density_history;
} CityMapDrawingValues;

typedef struct {
  const City *city;
  bool valid;
  int width, height;
  u32 cell_count;
  void *storage;
  CityMapDrawingValues values;
  CityMapCamera regions[CITY_EXTRA_MAP_VIEW_COUNT + 1];
} CityMapDrawingSnapshot;

// An uninitialized snapshot or a different City requires full damage instead of layer comparison.
void FilterDrawingMapChanges(const City *city, SimulationMapChanges *changes,
    const CityMapDrawingSnapshot *snapshot);
// Reuse the backing allocation when dimensions match. Failure retains the previous allocation.
bool PrepareCityMapDrawingSnapshot(const City *city, CityMapDrawingSnapshot *snapshot);
bool PrepareCityMapDrawingStorage(const City *city);
void ReleaseCityMapDrawingStorage(Drawing *drawing);
// viewport is 0 for the main map or 1..CITY_EXTRA_MAP_VIEW_COUNT for an auxiliary camera.
// Advance snapshots after damage has been projected for every viewport; never changes City.
void UpdateDrawingMapSnapshot(const City *city, CityMapDrawingSnapshot *snapshot,
    int viewport, CityMapCamera region);

// Tile must be valid. Emit clipped rectangles in the supplied camera's logical map pixels.
void ProjectCityMapTileDamage(const City *city, TileIndex tile, CityMapCamera region,
    CityCanvasVisiblePaint mark, void *context);

// Project bulk simulation notifications into each viewport before acknowledging them.
// Partial updates require a valid snapshot for this City and its current dimensions.
// Both projection functions call mark synchronously; it records damage without modifying City or the camera.
void ProjectSimulationMapDamage(const City *city, const SimulationMapChanges *changes,
    const CityMapDrawingSnapshot *snapshot,
    CityMapCamera region, bool data_overlay, CityCanvasVisiblePaint mark, void *context);

#endif
