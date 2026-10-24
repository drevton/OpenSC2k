#ifndef SC2K_DOMAIN_SRC_DRAWING_MAPS_MAP_RENDER_H
#define SC2K_DOMAIN_SRC_DRAWING_MAPS_MAP_RENDER_H

#include "city/types.h"

typedef struct CityMapViewConfig CityMapViewConfig;

// Map traversal

// Conservative world-cell bounds for a screen clip, including sprites and labels.
CityRect VisibleCityMapTileBounds(const City *city, int origin_x, int origin_y, int zoom, int rotation, CityRect area);
// Prepare sprite bounds and event anchors, then paint clip-visible tiles in isometric order for the active map mode.
// Scratch storage follows the event pool, never map area. Animation capture uses scan_clip instead of the paint rect.
void PaintVisibleCityMapTiles(City *city, const CityMapViewConfig *view, int zoom);

// Overlay and tile painting at the prepared target. Tile must be valid; screen is its logical ground-sprite origin.
// Random effects advance the supplied City's RNG; normal frames supply a paint copy.
void DrawCityMapOverlay(City *city, TileIndex tile, int zoom, CityPoint screen);
// Paint the surface in the active map target. Zoom selects artwork; view supplies the camera configuration.
// Traversal derives tile ranges from the logical paint rectangle and preserves the caller's scan clip.
void DrawCityMapSurface(City *city, const CityMapViewConfig *view, int zoom);
void DrawCityMapData(City *city, int zoom);
// Paint the active underground view, binding its palette and preserving the caller's scan clip.
void DrawCityMapUnderground(City *city, int zoom);

#endif
