#include "ui/map_visibility.h"
#include "ui/map.h"

#include <string.h>

#include "assets/state.h"
#include "audio/state.h"
#include "base/time.h"
#include "city/events/events.h"
#include "city/maps/layers.h"
#include "city/presentation.h"
#include "city/state.h"
#include "drawing/maps/map_camera.h"
#include "drawing/maps/map_effects.h"
#include "drawing/maps/map_events.h"
#include "drawing/maps/map_render.h"
#include "drawing/maps/map_view.h"
#include "drawing/state.h"
#include "game/errors.h"
#include "game/state.h"
#include "ui/charts/charts.h"
#include "ui/dialogs/map_data.h"
#include "ui/map_views.h"
#include "ui/signs.h"
#include "ui/state.h"
#include "ui/text/text.h"
#include "ui/tools/tools.h"
#include "ui/updates.h"
#include "ui/windows/windows.h"

void ReadCityMapClientRect(Drawing *drawing, CityRect *rect) {
  *rect = drawing->client_rect;
}

bool InvalidateCityMap(Game *game, const CityRect *rect) {
  Drawing *drawing = game->drawing;
  if (!drawing->map_navigation_enabled || game->ui == NULL) {
    return false;
  }
  CityRect bounds;
  if (rect != NULL)
    bounds = *rect;
  else
    Sc2kGetCityMapClientRect(drawing, &bounds);
  // Map renderer coordinates become native window pixels at this boundary.
  int scale = drawing->map_pixel_scale > 0 ? drawing->map_pixel_scale : 1;
  bounds = (CityRect){bounds.left * scale, bounds.top * scale, bounds.right * scale, bounds.bottom * scale};
  return InvalidateCityCanvasWindow(game, 0, &bounds);
}

// Redraw the city map; document attachment is comparison transport. Requires attached Drawing state; requests viewport
// refresh and does not rebuild or advance the simulation.
void RefreshCityDrawing(City *city) {
  RefreshCityMapViews(city->game);
}

void ToggleCityMapLayer(Game *game, CityMapLayer layer) {
  CityMapDrawingState *map = &game->drawing->map_state;
  switch (layer) {
  case CITY_MAP_LAYER_BUILDINGS: map->buildings_enabled = !map->buildings_enabled; break;
  case CITY_MAP_LAYER_NETWORKS: map->networks_enabled = !map->networks_enabled; break;
  case CITY_MAP_LAYER_WATER: map->water_enabled = !map->water_enabled; break;
  case CITY_MAP_LAYER_TREES: map->trees_enabled = !map->trees_enabled; break;
  case CITY_MAP_LAYER_SIGNS: map->label_display = !map->label_display; break;
  case CITY_MAP_LAYER_VEHICLES:
    map->vehicles_enabled = !map->vehicles_enabled;
    if (!map->vehicles_enabled) ClearCityVehicleVisualization(game->drawing);
    break;
  default: return;
  }
  RefreshCityMapViews(game);
}

void RefreshCityMapViews(Game *game) {
  Drawing *drawing = game->drawing;
  InvalidateCityMapViews(drawing);
  MarkScreenDirty(&drawing->screen_damage, (CityRect){0, 0,
      drawing->framebuffer.width, drawing->framebuffer.height});
}

// Moving sprites retain old/new screen footprints in the normal frame renderer. Demolition may supply an unset
// last-disaster slot; reject it before reading the event array.
void InvalidateCityMapEvent(City *city, int slot) {
  if (slot < 0 || slot >= city->events.capacity) return;
  const CityMapEvent *event = &city->events.event[slot];
  if (event->type == CITY_MAP_EVENT_UNUSED) return;
  InvalidateTile(city, event->tile);
}

void RefreshCityMapTiles(City *city) {
  for (int layer = 0; layer < CITY_MAP_CHANGE_COUNT; ++layer) InvalidateMapLayer(city, layer);
}

void ReadCitySavedView(City *city, int *position_out, int *mode_out) {
  Drawing *drawing = city->game->drawing;
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
  *position_out = packed_position;
  *mode_out = mode;
}

void SetCityTileScanPresentation(City *city, int direction_mask) {
  if (city->game->drawing) city->game->drawing->map_state.tile_scan_mask = direction_mask;
}

void RestoreCityVolcanoPresentation(City *city, u32 labels) {
  city->game->drawing->map_state.label_display = labels;
}

void StartCityEarthquakePresentation(City *city) {
  StartCityScreenShake(city->game->drawing);
}

void ShowCityConstructionCost(City *city, TileIndex tile, i64 cost) {
  if (cost <= 0 || !IsMapTileValid(city, tile)) return;
  double now = MonotonicSeconds();
  char currency[CITY_CURRENCY_TEXT_CAPACITY];
  FormatCityCurrency(city->game, currency, cost);
  StartCityConstructionCost(city->game->drawing, tile, currency, now);
}

void ShowCityDemolitionEffect(City *city, TileIndex tile, bool large) {
  if (!IsMapTileValid(city, tile)) return;
  int elevation = ReadCitySurfaceHeight(&city->tile[tile]);
  StartCityDemolitionEffect(city->game->drawing, tile, elevation, large);
}

void ApplyCitySavedViewPosition(City *city, int position) {
  CityLoadedMapView *view = &city->game->drawing->map_state.loaded_view;
  view->y = position == -1 ? 64 : (u16)position & 127;
  view->x = position == -1 ? 128 : (u16)(position >> 8);
}

void ApplyCitySavedViewMode(City *city, int mode) {
  city->game->drawing->map_state.loaded_view.zoom = mode;
}

void SetCityLoadedViewCenter(City *city, int x, int y) {
  CityLoadedMapView *view = &city->game->drawing->map_state.loaded_view;
  view->x = x;
  view->y = y;
}

void RestoreCityLoadedView(City *city) {
  CityLoadedMapView view = city->game->drawing->map_state.loaded_view;
  CenterCityMapViewAndResetOverlays(city, view.x, view.y, view.zoom);
}

CityMapViewConfig *BeginCitySaveView(City *city) {
  CityMapViewConfig *view = city->game->drawing->default_map_view;
  bool magnified = view && view->magnified_large_zoom;
  if (magnified) ZoomCityMapViewOut(city, view);
  return magnified ? view : NULL;
}

void EndCitySaveView(City *city, CityMapViewConfig *view) {
  if (view) ZoomCityMapViewIn(city, view);
}

void MarkCityMapSpriteDamage(CityRect rect, void *context) {
  CityMapPaint *paint = context;
  City *city = paint->city;
  Drawing *drawing = city->game->drawing;
  if (rect.left >= rect.right || rect.top >= rect.bottom) return;
  rect = CityMapScreenRect(rect, paint);
  MarkScreenDirty(&drawing->screen_damage, rect);
  MarkScreenDirty(&drawing->world_damage, rect);
}

void MarkCityMapRect(CityRect rect, void *context) {
  CityMapPaint *paint = context;
  City *city = paint->city;
  Drawing *drawing = city->game->drawing;
  if (rect.left >= rect.right || rect.top >= rect.bottom) return;
  MarkScreenDirty(&drawing->screen_damage, CityMapScreenRect(rect, paint));
}

void PublishCityMapTileDamage(City *city, TileIndex tile) {
  Drawing *drawing = city->game ? city->game->drawing : NULL;
  CityInterfaceState *ui = city->game ? city->game->ui : NULL;
  if (!drawing || !drawing->tile_damage_enabled || !ui) return;
  ResizeScreenDamage(&drawing->screen_damage, drawing->framebuffer.width, drawing->framebuffer.height);
  int magnification = drawing->default_map_view->magnified_large_zoom ? 2 : 1;
  ProjectCityMapTileDamage(city, tile,
      (CityMapCamera){drawing->map_state.origin_x, drawing->map_state.origin_y, drawing->default_map_view->zoom,
          drawing->map_width / magnification, drawing->map_height / magnification,
          CityMapViewRotation(drawing->default_map_view)},
      MarkCityMapRect, &(CityMapPaint){city, NULL});
  for (int index = 0; index < CITY_EXTRA_MAP_VIEW_COUNT; ++index) {
    CityExtraMapView *view = &drawing->views[index];
    Window *window = FindCityCanvasWindow(&ui->desktop, view->window_id);
    if (!window || !window->visible || window->shaded) continue;
    SyncCityMapViewSize(city, view, window);
    ProjectCityMapTileDamage(city, tile,
        (CityMapCamera){view->origin_x, view->origin_y, CityViewportMapZoom(view),
            view->width, view->height, view->rotation},
        MarkCityMapRect, &(CityMapPaint){city, view});
  }
}

static void MarkCityRoadCarDamage(const RoadCar *car, CityMapCamera camera, const CityMapPaint *paint) {
  Drawing *drawing = paint->city->game->drawing;
  CityExtraMapView *view = paint->view;
  int magnification = view ? CityViewportMagnification(view) :
      drawing->default_map_view->magnified_large_zoom ? 2 : 1;
  CityRect rect = CityRoadCarPixelBounds(paint->city, car, camera, drawing->map_pixel_scale * magnification);
  if (rect.left >= rect.right || rect.top >= rect.bottom) return;
  CityPoint origin = view ? view->scroll_pixels : drawing->scroll_pixels;
  if (view) {
    CityRect pixels = Sc2kCanvasDesktopPixelBounds(&paint->city->game->ui->desktop, view->window_id);
    int ui_scale = drawing->options.scale;
    origin.x += pixels.left + (3 * ui_scale + 99) / 100;
    origin.y += pixels.top + (CITY_VIEWPORT_TOP * ui_scale + 99) / 100;
  } else origin.x += drawing->screen_shake_offset * drawing->map_pixel_scale;
  rect.left += origin.x; rect.right += origin.x;
  rect.top += origin.y; rect.bottom += origin.y;
  MarkScreenDirty(&drawing->screen_damage, ClipCityMapViewportPixels(rect, paint));
}

void PublishCityRoadCarDamage(City *city, const RoadCar *previous, const RoadCar *current) {
  Drawing *drawing = city->game->drawing;
  CityInterfaceState *ui = city->game->ui;
  if (!drawing->tile_damage_enabled || !ui) return;
  ResizeScreenDamage(&drawing->screen_damage, drawing->framebuffer.width, drawing->framebuffer.height);
  int magnification = drawing->default_map_view->magnified_large_zoom ? 2 : 1;
  CityMapCamera camera = {drawing->map_state.origin_x, drawing->map_state.origin_y, drawing->default_map_view->zoom,
      drawing->map_width / magnification, drawing->map_height / magnification,
      CityMapViewRotation(drawing->default_map_view)};
  CityMapPaint paint = {city, NULL};
  MarkCityRoadCarDamage(previous, camera, &paint);
  MarkCityRoadCarDamage(current, camera, &paint);
  for (int index = 0; index < CITY_EXTRA_MAP_VIEW_COUNT; ++index) {
    CityExtraMapView *view = &drawing->views[index];
    Window *window = FindCityCanvasWindow(&ui->desktop, view->window_id);
    if (!window || !window->visible || window->shaded) continue;
    SyncCityMapViewSize(city, view, window);
    camera = (CityMapCamera){view->origin_x, view->origin_y, CityViewportMapZoom(view),
        view->width, view->height, view->rotation};
    paint.view = view;
    MarkCityRoadCarDamage(previous, camera, &paint);
    MarkCityRoadCarDamage(current, camera, &paint);
  }
}

void HideCitySimulationPanels(City *city) {
  HideCityChartPanel(city, CITY_PANEL_HISTORY);
  HideCityChartPanel(city, CITY_PANEL_DEMOGRAPHICS);
  HideCityMapDataDialog(city);
  HideCityChartPanel(city, CITY_PANEL_NEIGHBORS);
  HideCityChartPanel(city, CITY_PANEL_INDUSTRY);
}

// Camera controls change local presentation only; projection and picking stay in Drawing.

// Center the main camera unless its host window is minimized. A NULL view selects the default.
int CenterCityMapViewAtTile(City *city, CityMapViewConfig *view, int x, int y) {
  Drawing *drawing = city->game->drawing;
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
  Drawing *drawing = city->game->drawing;
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
  Drawing *drawing = city->game->drawing;
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
  Drawing *drawing = city->game->drawing;
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
  Drawing *drawing = city->game->drawing;
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
  Drawing *drawing = city->game->drawing;
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
  Drawing *drawing = city->game->drawing;
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
  Drawing *drawing = city->game->drawing;
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
