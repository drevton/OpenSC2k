#include "ui/map_saved_view.h"

#include "city/presentation.h"
#include "city/state.h"
#include "drawing/maps/map_camera.h"
#include "drawing/maps/map_view.h"
#include "drawing/state.h"
#include "game/state.h"
#include "ui/map.h"

// Saved camera presentation and stock coordinate packing. The stock diagonal scan retains
// its fixed map size and byte-packed result at this file/diagnostic boundary.

static CityPoint PickCitySavedViewDiagonalPoint(const City *city, int diagonal_x, int diagonal_y, int zoom) {
  int step = 4 << zoom, elevation_step = CityMapHeightStep(zoom);
  int diagonal[] = {diagonal_x, diagonal_y};
  int coordinate[] = {diagonal_x / step, diagonal_y / step};
  int edge[] = {step * coordinate[0], step * coordinate[1]};
  int size[] = {CITY_MAP_SIZE, CITY_MAP_SIZE};
  int limit = CITY_MAP_SIZE;
  int first_axis = diagonal_x >= diagonal_y ? 0 : 1;
  CityPoint chosen = {coordinate[0], coordinate[1]};
  for (int count = 0; count < limit; ++count) {
    for (int turn = 0; turn < 2; ++turn) {
      int axis = first_axis ^ turn;
      edge[axis] += step;
      if (++coordinate[axis] >= size[axis]) return chosen;
      if (edge[axis] - elevation_step * CityMapTileElevation(city, coordinate[0], coordinate[1]) < diagonal[axis])
        chosen = (CityPoint){coordinate[0], coordinate[1]};
    }
  }
  return chosen;
}

static int CityMapDiagonalToLegacyTile(const City *city, int diagonal_x, int diagonal_y, int zoom) {
  CityPoint point = PickCitySavedViewDiagonalPoint(city, diagonal_x, diagonal_y, zoom);
  return point.y + 256 * point.x;
}

// Inverse projection for small map tiles.
int CityMapDiagonalToTileSmall(const City *city, int diagonal_x, int diagonal_y) {
  return CityMapDiagonalToLegacyTile(city, diagonal_x, diagonal_y, 0);
}

// Inverse projection for medium map tiles.
int CityMapDiagonalToTileMedium(const City *city, int diagonal_x, int diagonal_y) {
  return CityMapDiagonalToLegacyTile(city, diagonal_x, diagonal_y, 1);
}

// Inverse projection for large map tiles.
int CityMapDiagonalToTileLarge(const City *city, int diagonal_x, int diagonal_y) {
  return CityMapDiagonalToLegacyTile(city, diagonal_x, diagonal_y, 2);
}

// Stock saved-view fields pack world x in the high byte and world y in the low byte.
int CityMapScreenToTile(const City *city, const CityMapViewConfig *view, int screen_x, int screen_y) {
  TileIndex tile = PickCityMapTile(city, view, screen_x, screen_y);
  return IsMapTileValid(city, tile) ? (MapTileX(city, tile) << 8) | MapTileY(city, tile) : -1;
}

CitySavedViewFields ReadCitySavedView(const City *city) {
  Drawing *drawing = city->drawing;
  int point = drawing->map_state.save_x;
  int packed_position;
  int mode;
  if (drawing->default_map_view != NULL) {
    const CityMapViewConfig *view = drawing->default_map_view;
    if (drawing->options.save_view_position) {
      int x = (view->view_rect.left + view->view_rect.right) / 2;
      int y = (view->view_rect.top + view->view_rect.bottom) / 2;
      packed_position = CityMapScreenToTile(city, view, x, y);
    } else {
      packed_position = CityMapScreenToTile(city, view, point, point);
    }
    mode = (i16)drawing->default_map_view->zoom;
  } else if (drawing->options.save_view_position) {
    packed_position = 128;
    mode = CITY_MAP_ZOOM_SMALL;
  } else {
    packed_position = CityMapDiagonalToTileSmall(city, point + 6, point + 8);
    mode = CITY_MAP_ZOOM_MEDIUM;
  }
  return (CitySavedViewFields){packed_position, mode};
}

void ApplyCitySavedViewPosition(City *city, int position) {
  CityLoadedMapView *view = &city->drawing->map_state.loaded_view;
  view->y = position == -1 ? 64 : (u16)position & 127;
  view->x = position == -1 ? 128 : (u16)(position >> 8);
}

void ApplyCitySavedViewMode(City *city, int mode) {
  city->drawing->map_state.loaded_view.zoom = mode;
}

void SetCityLoadedViewCenter(City *city, int x, int y) {
  CityLoadedMapView *view = &city->drawing->map_state.loaded_view;
  view->x = x;
  view->y = y;
}

void RestoreCityLoadedView(City *city) {
  CityLoadedMapView view = city->drawing->map_state.loaded_view;
  CenterCityMapViewAndResetOverlays(city, view.x, view.y, view.zoom);
}

CityMapViewConfig *BeginCitySaveView(City *city) {
  CityMapViewConfig *view = city->drawing->default_map_view;
  bool magnified = view && view->magnified_large_zoom;
  if (magnified) ZoomCityMapViewOut(city, view);
  return magnified ? view : NULL;
}

void EndCitySaveView(City *city, CityMapViewConfig *view) {
  if (view) ZoomCityMapViewIn(city, view);
}
