#ifndef SC2K_DOMAIN_SRC_DRAWING_MAPS_MAP_RENDER_H
#define SC2K_DOMAIN_SRC_DRAWING_MAPS_MAP_RENDER_H

#include "drawing/maps/map_camera.h"

// Map traversal

// Conservative world-cell bounds for a logical screen clip, including sprites and labels. Camera supplies origin,
// zoom and rotation; its viewport dimensions are unused. Refreshes Drawing's extent cache without preparing events,
// painting or scanning map cells. Returns a zero rectangle when the clamped tile range is empty.
CityRect VisibleCityMapTileBounds(const City *city, CityMapCamera camera, CityRect area);
// Prepare sprite bounds and event anchors, then paint clip-visible tiles in isometric order for the active map mode.
// Scratch storage follows the event pool, never map area. Animation capture uses scan_clip instead of the paint rect.
void PaintVisibleCityMapTiles(const City *city, const CityMapViewConfig *view, int zoom);

// Overlay and tile painting at the prepared target. Tile must be valid; screen is its logical ground-sprite origin.
// Random effects advance Drawing's painting stream; Game seeds it once before the frame's first camera.
void DrawCityMapOverlay(const City *city, TileIndex tile, int zoom, CityPoint screen);
// Paint the surface in the active map target. Zoom selects artwork; view supplies the camera configuration.
// Traversal derives tile ranges from the logical paint rectangle and preserves the caller's scan clip.
void DrawCityMapSurface(const City *city, const CityMapViewConfig *view, int zoom);
void DrawCityMapData(const City *city, int zoom);
// Paint the active underground view, binding its palette and preserving the caller's scan clip.
void DrawCityMapUnderground(const City *city, int zoom);

#endif
