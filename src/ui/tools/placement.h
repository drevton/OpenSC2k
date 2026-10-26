#ifndef SC2K_UI_TOOLS_PLACEMENT_H
#define SC2K_UI_TOOLS_PLACEMENT_H

#include "city/types.h"

typedef struct CityMapViewConfig CityMapViewConfig;

// Retained highway drag between valid endpoints and a required borrowed view. Admit compact/legacy planning,
// prepare the drawing clip, update City's highway counters/damage, then reset the clip. No placement, payment or RNG.
void UpdateCityHighwayDragPreview(City *city, CityMapViewConfig *view, TileIndex start, TileIndex end);

// UI owns the committed selection and reusable scratch. Cells use City TileIndex values; UI resolves the
// sorted selection during tile painting and Drawing supplies marker bounds and pixels.
typedef struct {
  TileIndex tile;
} CityCanvasPlacementCell;

typedef struct {
  CityCanvasPlacementCell *cells, *scratch; // Committed cells are sorted by TileIndex.
  // Paired UI-owned arrays share capacity. Appending may replace both; commit reads only the prepared scratch prefix.
  int count, capacity;
  CityRect bounds;
  int origin_x, origin_y;
  u32 zoom, magnified, underground;
  bool point;
} CityCanvasPlacementSelection;

// Pick in main-map logical pixels. Level Terrain selects a terrain point; other tools select the tile surface.
TileIndex PickCityPlacementTile(const City *city, int x, int y);
// Resolve hover/drag feedback into the retained selection. City and its RNG remain read-only;
// allocation failure clears the visible preview through the existing empty-selection path.
void UpdateCityPlacementPreview(const City *city);
// Return the selection's physical framebuffer bounds, including pending scroll; empty when there are no cells.
CityRect CityPlacementPixelBounds(const City *city);
// Publish damage for the last painted preview and current selection after the main target is bound.
// Include the previous preview's scroll destination; do not replace painted bounds or clear placement_dirty.
void PublishCityPlacementDamage(const City *city);

#endif
