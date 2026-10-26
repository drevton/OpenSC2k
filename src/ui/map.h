#ifndef SC2K_DOMAIN_SRC_UI_MAP_H
#define SC2K_DOMAIN_SRC_UI_MAP_H

#include "city/types.h"
#include "drawing/maps/map_view.h"

typedef struct Drawing Drawing;
typedef struct CityMapViewConfig CityMapViewConfig;

typedef struct City City;
typedef struct Game Game;

// Change a local display layer. Disabling vehicles clears their visualization; a change refreshes every map view.
void ToggleCityMapLayer(Game *game, CityMapLayer layer);

// Map Navigation

void SetCityMapNavigationPresentation(Game *game, bool enabled, bool interactive);
// Initialize/reset the main camera's client geometry and origin; initialization then applies the requested zoom
// and centers on a valid tile. Both update presentation only; initialization invalidates the map caches.
void InitializeCityMapView(Game *game, TileIndex location, int zoom);
void RecalculateCityMapClientOrigins(Game *game);
// Clear the main view's command drag state and release host capture. Pan completion also restores its saved
// origin through the normal scroll/damage path before clearing pan capture; neither executes a City tool.
void ReleaseCityMapMouseCapture(Game *game);
void EndCityMapPan(Game *game);
// Raw host bounds for navigation/input. Update the supplied storage synchronously;
// diagnostic host reads may run callbacks, including when storage is a view's rectangle.
void ReadCityMapClientRect(Drawing *drawing, CityRect *rect);
// Read the host client area without scrollbar strips; return the resulting bottom coordinate.
i32 GetCityMapClientRect(Drawing *drawing, CityRect *rect);
// Map renderer rectangles are scaled into native window pixels before queuing UI damage.
bool InvalidateCityMap(Game *game, const CityRect *rect);
bool CaptureCityMapPointer(Game *game);
int ReleaseCityMapWindowPointer(Game *game);

// Main-camera controls preserve the existing zoom, scroll and preview refresh ordering.
// NULL selects the default view only for centering; the other operations require a supplied view.
i32 CenterCityMapViewAtTile(City *city, CityMapViewConfig *view, int x, int y);
void ZoomCityMapViewOut(City *city, CityMapViewConfig *view);
void ZoomCityMapViewIn(City *city, CityMapViewConfig *view);
void OffsetCityMapOrigin(City *city, CityMapViewConfig *view, int delta_x, int delta_y);
// These controls use the bound main-camera origin and supplied view configuration. Rotation discards its retained
// pixels, sprites, animation and label bounds; scroll updates return the vertical scrollbar percentage.
void RotateCityMapView(City *city, CityMapViewConfig *view, bool clockwise);
i32 UpdateCityMapViewScrollPosition(City *city, CityMapViewConfig *view);
void MaskCityMapOriginForZoom(Drawing *drawing, CityMapViewConfig *view);
// An unknown/zero viewport ID falls back to the main camera. Shaded viewports retain their rotation.
void RotateCityMapViewport(City *city, u32 window_id, bool clockwise);
void RotateFocusedCityMapView(City *city, bool clockwise);

#endif
