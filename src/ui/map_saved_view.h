#ifndef SC2K_UI_MAP_SAVED_VIEW_H
#define SC2K_UI_MAP_SAVED_VIEW_H

#include "city/types.h"

typedef struct CityMapViewConfig CityMapViewConfig;

// Stock saved-view and diagnostic adapters preserve the original 128-cell diagonal scan and byte packing.
// Results pack x above y (x * 256 + y). These queries leave City and camera state unchanged.
int CityMapDiagonalToTileSmall(const City *city, int diagonal_x, int diagonal_y);
int CityMapDiagonalToTileMedium(const City *city, int diagonal_x, int diagonal_y);
int CityMapDiagonalToTileLarge(const City *city, int diagonal_x, int diagonal_y);
// Convert this view's canvas point through native picking, then pack the stock saved position; invalid hits return -1.
int CityMapScreenToTile(const City *city, const CityMapViewConfig *view, int screen_x, int screen_y);

#endif
