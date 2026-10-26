#ifndef SC2K_UI_MAP_SAVED_VIEW_H
#define SC2K_UI_MAP_SAVED_VIEW_H

#include "city/types.h"

typedef struct CityMapViewConfig CityMapViewConfig;

// Stock saved positions pack x above y (x * 256 + y), not native TileIndex values.
// Both queries leave City and camera state unchanged.

// Preserve the original 128-cell diagonal scan. Zoom must be CITY_MAP_ZOOM_SMALL through CITY_MAP_ZOOM_LARGE;
// diagonal coordinates use that zoom's map pixels. The result retains the scan's rounding and boundary behavior.
int CityMapDiagonalToSavedPosition(const City *city, int diagonal_x, int diagonal_y, int zoom);
// Pick this view's canvas point, then pack the stock saved position; invalid hits return -1.
int CityMapScreenToSavedPosition(const City *city, const CityMapViewConfig *view, int screen_x, int screen_y);

#endif
