#include "ui/map.h"

#include <string.h>

#include "city/presentation.h"
#include "city/state.h"
#include "drawing/maps/map_camera.h"
#include "drawing/maps/map_damage.h"
#include "drawing/maps/map_view.h"
#include "drawing/state.h"
#include "game/state.h"
#include "ui/charts/charts.h"
#include "ui/dialogs/map_data.h"
#include "ui/map_views.h"
#include "ui/state.h"
#include "ui/windows/windows.h"

// Camera controls change local presentation only; projection and picking stay in Drawing.

void ReadCityMapClientRect(Drawing *drawing, CityRect *rect) {
  *rect = drawing->client_rect;
}

// Initialize main-camera geometry, apply the requested zoom, then center at the supplied tile.
void InitializeCityMapView(Game *game, TileIndex location, int zoom) {
  RecalculateCityMapClientOrigins(game);
  game->drawing->default_map_view->zoom = zoom;
  InvalidateCityMapViews(game->drawing);
  CenterCityMapViewAtTile(game->city, NULL, MapTileX(game->city, location), MapTileY(game->city, location));
}

void RecalculateCityMapClientOrigins(Game *game) {
  City *city = game->city;
  Drawing *drawing = game->drawing;
  CityMapViewConfig *view = drawing->default_map_view;
  view->zoom = 0;
  GetCityMapClientRect(drawing, &view->view_rect);
  view->magnified_large_zoom = 0;
  int width = (int)((u32)view->view_rect.right - (u32)view->view_rect.left);
  int height = (int)((u32)view->view_rect.bottom - (u32)view->view_rect.top);
  drawing->map_state.save_x = width / 2;
  drawing->map_state.save_y = height / 2;
  drawing->map_state.origin_x = width / 2;
  drawing->map_state.origin_y = drawing->map_state.save_y - 216;
  UpdateCityMapViewScrollPosition(city, view);
}

void ReleaseCityMapMouseCapture(Game *game) {
  Drawing *drawing = game->drawing;
  CityMapViewConfig *view = drawing->default_map_view;
  view->pointer_drag_active = 0;
  view->pointer_command_stage = 0;
  ReleaseCityMapWindowPointer(game);
}

// Restore the pan position, redraw, and release the host mouse capture.
void EndCityMapPan(Game *game) {
  City *city = game->city;
  Drawing *drawing = game->drawing;
  CityMapViewConfig *view = drawing->default_map_view;
  int x;
  int y;
  if (view->magnified_large_zoom != 0) {
    x = drawing->map_state.pan_release_point.x >> 1;
    y = drawing->map_state.pan_release_point.y >> 1;
  } else {
    x = drawing->map_state.pan_release_point.x;
    y = drawing->map_state.pan_release_point.y;
  }
  OffsetCityMapOrigin(city, view, drawing->map_state.save_x - x, drawing->map_state.save_y - y);
  view->pointer_drag_active = 0;
  view->pointer_command_stage = 0;
  ReleaseCityMapWindowPointer(city->game);
  view->pan_capture_active = 0;
}

// Automatic centering follows this player's local preference.
void CenterCityMapOnEvent(City *city, int x, int y) {
  if (!city->game->ui->auto_goto_disabled) CenterCityMapViewAtTile(city, NULL, x, y);
}

// Center the main camera unless its host window is minimized. A NULL view selects the default.
int CenterCityMapViewAtTile(City *city, CityMapViewConfig *view, int x, int y) {
  Drawing *drawing = city->drawing;
  if (view == NULL) view = drawing->default_map_view;
  const CityInterfaceState *ui = city->game->ui;
  if (ui->main_window_iconic)
    return 0;

  bool magnified = false;

  if (view->magnified_large_zoom == 1) {
    view->magnified_large_zoom = 0;
    int saved_x = drawing->map_state.save_x;
    int saved_y = drawing->map_state.save_y;
    drawing->map_state.save_x = saved_x * 2;
    drawing->map_state.save_y = saved_y * 2;
    drawing->map_state.origin_x += saved_x;
    drawing->map_state.origin_y += saved_y;
    magnified = true;
  }

  CityPoint screen = CityMapTileToScreen(city, view, (CityPoint){x, y});
  drawing->map_state.origin_x = drawing->map_state.origin_x + drawing->map_state.save_x - screen.x;
  drawing->map_state.origin_y = drawing->map_state.origin_y + drawing->map_state.save_y - screen.y;

  if (magnified) {
    view->magnified_large_zoom = 1;
    int saved_x = drawing->map_state.save_x;
    int saved_y = drawing->map_state.save_y;
    drawing->map_state.save_x = saved_x >> 1;
    drawing->map_state.save_y = saved_y >> 1;
    drawing->map_state.origin_x -= drawing->map_state.save_x;
    drawing->map_state.origin_y -= drawing->map_state.save_y;
  }

  return UpdateCityMapViewScrollPosition(city, view);
}

// Increase map zoom around its saved screen center.
void ZoomCityMapViewIn(City *city, CityMapViewConfig *view) {
  Drawing *drawing = city->drawing;
  if (view->zoom + view->magnified_large_zoom >= CityMapZoomLimit(drawing->map_pixel_scale * 100))
    return;

  if (view->zoom == CITY_MAP_ZOOM_LARGE) {
    view->magnified_large_zoom = 1;
    drawing->map_state.save_x >>= 1;
    drawing->map_state.save_y >>= 1;
    drawing->map_state.origin_x = drawing->map_state.origin_x - drawing->map_state.save_x;
    drawing->map_state.origin_y = drawing->map_state.origin_y - drawing->map_state.save_y;
  } else {
    ++view->zoom;
    drawing->map_state.origin_x = 2 * drawing->map_state.origin_x - drawing->map_state.save_x;
    drawing->map_state.origin_y = 2 * drawing->map_state.origin_y - drawing->map_state.save_y;
  }

  GetCityMapClientRect(drawing, &view->view_rect);
  if (view->magnified_large_zoom == 1) {
    view->view_rect.left >>= 1;
    view->view_rect.top >>= 1;
    view->view_rect.right >>= 1;
    view->view_rect.bottom >>= 1;
  }
  UpdateCityMapViewScrollPosition(city, view);
  RefreshCityMapViews(city->game);
  RefreshCityMapDataPreview(city);
}

// Step out of magnified-large mode or reduce the map zoom by one.
void ZoomCityMapViewOut(City *city, CityMapViewConfig *view) {
  Drawing *drawing = city->drawing;
  if (view->zoom == CITY_MAP_ZOOM_SMALL)
    return;

  GetCityMapClientRect(drawing, &view->view_rect);
  if (view->magnified_large_zoom == 1) {
    int saved_x = drawing->map_state.save_x;
    int saved_y = drawing->map_state.save_y;
    drawing->map_state.save_x = saved_x * 2;
    drawing->map_state.save_y = saved_y * 2;
    drawing->map_state.origin_x += saved_x;
    drawing->map_state.origin_y += saved_y;
    view->magnified_large_zoom = 0;
  } else {
    --view->zoom;
    drawing->map_state.origin_x =
        ((drawing->map_state.origin_x - drawing->map_state.save_x) / 2 + drawing->map_state.save_x);
    drawing->map_state.origin_y =
        ((drawing->map_state.origin_y - drawing->map_state.save_y) / 2 + drawing->map_state.save_y);
  }

  UpdateCityMapViewScrollPosition(city, view);
  RefreshCityMapViews(city->game);
  RefreshCityMapDataPreview(city);
}

// Restore the map overlays, center the saved tile, and redraw.
int CenterCityMapViewAndResetOverlays(City *city, int x, int y, int zoom) {
  Drawing *drawing = city->drawing;
  CityMapViewConfig *view = drawing->default_map_view;
  // A stock save without a camera position supplies (128, 64), outside a
  // 128-wide map. Resolve that fallback before looking up tile elevation.
  if (!IsMapCoordinateValid(city, x, y)) {
    x = city->map_width / 2;
    y = city->map_height / 2;
  }
  static const u8 kEventDisplayFlags[5] = {0, 1, 1, 1, 1};
  static const CityMapOverlaySettings kOverlayDefaults = {
    .surface = {0, 1, 1, 1, 1},
    .underground = {0, 0, 1, 0, 0}
  };

  GetCityMapClientRect(drawing, &view->view_rect);
  if (view->magnified_large_zoom != 0)
    ZoomCityMapViewOut(city, view);
  view->magnified_large_zoom = 0;
  view->rotation = 0;
  view->zoom = zoom;

  for (int index = 0; index < 5; ++index)
    drawing->map_state.display_flags[index] = kEventDisplayFlags[index];
  RebuildCityToolbar(city);
  drawing->map_state.overlay_settings = kOverlayDefaults;

  CityCanvasDesktop *desktop = drawing->map_navigation_enabled ? &city->game->ui->desktop : NULL;
  if (desktop != NULL) {
    WindowIds windows = SnapshotCityWindowIds(desktop);
    for (int index = windows.count - 1; index >= 0; --index) {
      Window *window = FindCityCanvasWindow(desktop, windows.ids[index]);
      if (window && Sc2kCanvasChartCode(window->kind) != 0)
        Sc2kCanvasDesktopDismissWindow(desktop, window->window_id);
    }
  }

  InvalidateCityMapViews(drawing);
  return CenterCityMapViewAtTile(city, view, x, y);
}

void RotateCityMapView(City *city, CityMapViewConfig *view, bool clockwise) {
  Drawing *drawing = city->drawing;
  int rotation = (view->rotation + (clockwise ? 1 : 3)) & 3;
  CityPoint center = {drawing->map_state.save_x, drawing->map_state.save_y};
  CityPoint origin = TransferCityMapCameraCenter(city,
      (CityMapCamera){drawing->map_state.origin_x, drawing->map_state.origin_y, view->zoom,
          center.x * 2, center.y * 2, view->rotation},
      (CityMapCamera){0, 0, view->zoom, center.x * 2, center.y * 2, rotation});
  view->rotation = rotation;
  drawing->map_state.origin_x = origin.x;
  drawing->map_state.origin_y = origin.y;
  drawing->main_cache.valid = false;
  drawing->main_sprites.initialized = false;
  drawing->main_animations.count = 0;
  memset(drawing->cost_text_bounds[0], 0, sizeof(drawing->cost_text_bounds[0]));
  memset(drawing->demolition_bounds[0], 0, sizeof(drawing->demolition_bounds[0]));
  UpdateCityMapViewScrollPosition(city, view);
  InvalidateCityMapRect(drawing, view, view->view_rect);
}

void RotateCityMapViewport(City *city, u32 window_id, bool clockwise) {
  Drawing *drawing = city->drawing;
  for (int index = 0; index < CITY_EXTRA_MAP_VIEW_COUNT; ++index) {
    CityExtraMapView *view = &drawing->views[index];
    if (!window_id || view->window_id != window_id) continue;
    Window *window = FindCityCanvasWindow(&city->game->ui->desktop, window_id);
    if (!window) break;
    if (window->shaded) return;
    int rotation = (view->rotation + (clockwise ? 1 : 3)) & 3;
    int zoom = IntMin(view->zoom, CITY_MAP_ZOOM_LARGE);
    CityPoint center = {view->width / 2, view->height / 2};
    CityPoint origin = TransferCityMapCameraCenter(city,
        (CityMapCamera){view->origin_x, view->origin_y, zoom, center.x * 2, center.y * 2, view->rotation},
        (CityMapCamera){0, 0, zoom, center.x * 2, center.y * 2, rotation});
    view->origin_x = origin.x;
    view->origin_y = origin.y;
    view->rotation = rotation;
    view->cache.valid = false;
    view->sprites.initialized = false;
    view->animations.count = 0;
    memset(drawing->cost_text_bounds[index + 1], 0, sizeof(drawing->cost_text_bounds[index + 1]));
    memset(drawing->demolition_bounds[index + 1], 0, sizeof(drawing->demolition_bounds[index + 1]));
    Sc2kDirtyMarkRect(&city->game->ui->desktop.dirty, window->bounds);
    return;
  }
  RotateCityMapView(city, drawing->default_map_view, clockwise);
}

void RotateFocusedCityMapView(City *city, bool clockwise) {
  u32 window = city->game->ui->map_view_focus_window;
  RotateCityMapViewport(city, window, clockwise);
}

// Remove the scrollbar strips from the host client rectangle, preserving observed client-rectangle reads.
int GetCityMapClientRect(Drawing *drawing, CityRect *rect) {
  ReadCityMapClientRect(drawing, rect);
  rect->bottom = (int)((u32)rect->bottom - (u32)drawing->map_state.scroll_height);
  rect->right = (int)((u32)rect->right - (u32)drawing->map_state.scroll_width);
  return rect->bottom;
}

// Constrain the main camera and update both scrollbar positions from its logical viewport and rotated map dimensions.
int UpdateCityMapViewScrollPosition(City *city, CityMapViewConfig *view) {
  Drawing *drawing = city->drawing;
  CityMapCamera camera = {drawing->map_state.origin_x, drawing->map_state.origin_y, view->zoom,
      drawing->map_state.save_x * 2, drawing->map_state.save_y * 2, view->rotation};
  CityPoint origin = ClampCityMapCameraOrigin(city, camera);
  if (origin.x != camera.origin_x) drawing->scroll_pixels.x = 0;
  if (origin.y != camera.origin_y) drawing->scroll_pixels.y = 0;
  drawing->map_state.origin_x = origin.x;
  drawing->map_state.origin_y = origin.y;
  CityRect bounds = CityMapOriginBounds(city, camera);
  view->horizontal_scroll_position = (bounds.right - origin.x) * 100 / (bounds.right - bounds.left);
  view->vertical_scroll_position = (bounds.bottom - origin.y) * 100 / (bounds.bottom - bounds.top);
  return view->vertical_scroll_position;
}

// Optionally preserve stock camera alignment for compatibility.
void MaskCityMapOriginForZoom(Drawing *drawing, CityMapViewConfig *view) {
  if (!(drawing->options.fixes & (1 << DRAWING_FIX_PIXEL_SCROLLING)))
    drawing->map_state.origin_x &= -CityMapOriginAlignment(view->zoom);
}

// Offset the main map, constrain its extents and refresh the map-data preview.
void OffsetCityMapOrigin(City *city, CityMapViewConfig *view, int delta_x, int delta_y) {
  Drawing *drawing = city->drawing;
  CityRect bounds = CityMapOriginBounds(city, (CityMapCamera){0, 0, view->zoom,
      drawing->map_state.save_x * 2, drawing->map_state.save_y * 2, view->rotation});
  i64 x = (i64)drawing->map_state.origin_x + delta_x;
  i64 y = (i64)drawing->map_state.origin_y + delta_y;
  drawing->map_state.origin_x = x < bounds.left ? bounds.left : x > bounds.right ? bounds.right : (int)x;
  drawing->map_state.origin_y = y < bounds.top ? bounds.top : y > bounds.bottom ? bounds.bottom : (int)y;
  MaskCityMapOriginForZoom(drawing, view);
  UpdateCityMapViewScrollPosition(city, view);
  RefreshCityMapDataPreview(city);
}
