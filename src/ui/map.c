#include "drawing/maps/map_view.h"
#include "ui/frontend.h"
#include "ui/updates.h"
#include "ui/signs.h"
#include "drawing/drawing_internal.h"
#include "city/presentation.h"
#include "ui/map.h"
#include "game/errors.h"
#include "game/state.h"
#include "audio/state.h"
#include "city/state.h"
#include "drawing/state.h"
#include "assets/state.h"
#include "ui/state.h"

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

bool InvalidateCityMapDataPreview(City *city) {
  Drawing *drawing = city->game->drawing;
  if (!drawing->map_navigation_enabled || city->game->ui == NULL) {
    return false;
  }
  CityCanvasDesktop *desktop = &city->game->ui->desktop;
  int scale = drawing->ui_scale_applied > 0 ? drawing->ui_scale_applied : 100;
  bool result = true;
  for (int index = 0; index < desktop->count; ++index) {
    const Window *panel = &desktop->windows[index];
    if (panel->kind != CITY_CANVAS_MAP_DATA) continue;
    CityRect preview = Sc2kCanvasMapDataPreviewRect(panel);
    CityRect rect = {preview.left - panel->bounds.left, preview.top - panel->bounds.top,
        preview.right - panel->bounds.left, preview.bottom - panel->bounds.top};
    rect = (CityRect){rect.left * scale / 100, rect.top * scale / 100,
        (rect.right * scale + 99) / 100, (rect.bottom * scale + 99) / 100};
    result = InvalidateCityCanvasWindow(city->game, panel->window_id, &rect) && result;
  }
  return result;
}

// Redraw the city map; document attachment is comparison transport. Requires attached Drawing state; requests viewport
// refresh and does not rebuild or advance the simulation.
void RefreshCityDrawing(City *city) {
  RefreshCityMapViews(city->game);
}

// Mark a changed tile and notify Drawing so each viewport can damage its old or new extent. Tile must be valid or
// INVALID_TILE, which is ignored; other indices assert, and disabled/full-map tracking needs no extra notification.
void InvalidateTile(City *city, TileIndex tile) {
  if (tile == INVALID_TILE) return;
  assert(tile < city->map_cell_count);
  SimulationMapChanges *changes = &city->map_changes;
  if (!changes->enabled || changes->full) return;
  InvalidateCityInformation(city->game, CITY_UPDATE_MAP);
  changes->tiles_changed = true;
  Drawing *drawing = city->game ? city->game->drawing : NULL;
  if (drawing) PublishCityMapTileDamage(city, tile);
}

// Invalidate a rectangle beginning at a valid tile, clipping its right and bottom edges to the map. INVALID_TILE is
// ignored; dimensions must describe the intended nonnegative area, and each included tile uses InvalidateTile.
void InvalidateTileRange(City *city, TileIndex start, int width, int height) {
  if (start == INVALID_TILE) return;
  assert(start < city->map_cell_count);
  if (!city->map_changes.enabled || city->map_changes.full) return;
  width = IntMin(width, city->map_width - MapTileX(city, start));
  height = IntMin(height, city->map_height - MapTileY(city, start));
  for (int y = 0; y < height; ++y) {
    TileIndex row = start + y * city->map_width;
    for (int x = 0; x < width; ++x) InvalidateTile(city, row + x);
  }
}

// Record one coarse-layer change for viewport-specific redraw. Layer must be a supported CityMapChangeLayer; disabled
// tracking leaves damage state unchanged.
void InvalidateMapLayer(City *city, CityMapChangeLayer layer) {
  if (city->map_changes.enabled) {
    city->map_changes.invalidated_layers |= 1 << layer;
    InvalidateCityInformation(city->game, CITY_UPDATE_MAP | CITY_UPDATE_UTILITIES);
  }
}

// Mark the whole map dirty when damage tracking is enabled. This supersedes later per-tile work; use it only for
// changes whose drawing impact is actually map-wide.
void InvalidateMap(City *city) {
  if (city->map_changes.enabled) {
    city->map_changes.full = true;
    InvalidateCityInformation(city->game, CITY_UPDATE_MAP);
  }
}

// Clear accumulated tile, layer and full-map damage after its consumer has processed it. The enabled policy is
// preserved; the supplied change record remains caller-owned.
void ClearSimulationMapChanges(SimulationMapChanges *changes) {
  changes->tiles_changed = false;
  changes->invalidated_layers = 0;
  changes->full = false;
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

void ApplyCitySavedViewPosition(City *city, int position) {
  CitySavedViewState *view = &city->game->drawing->map_state.saved_view;
  view->y = position == -1 ? 64 : (u16)position & 127;
  view->x = position == -1 ? 128 : (u16)(position >> 8);
}

void ApplyCitySavedViewMode(City *city, int mode) {
  city->game->drawing->map_state.saved_view.mode = mode;
}

void SetCityLoadedViewCenter(City *city, int x, int y) {
  CitySavedViewState *view = &city->game->drawing->map_state.saved_view;
  view->x = x;
  view->y = y;
}

void RestoreCityLoadedView(City *city) {
  CitySavedViewState view = city->game->drawing->map_state.saved_view;
  CenterCityMapViewAndResetOverlays(city, view.x, view.y, view.mode);
}

void *BeginCitySaveView(City *city) {
  CityMapViewConfig *view = city->game->drawing->default_map_view;
  bool magnified = view && view->magnified_large_zoom;
  if (magnified) ZoomCityMapViewOut(city, view);
  return magnified ? view : NULL;
}

void EndCitySaveView(City *city, void *saved_view) {
  CityMapViewConfig *view = saved_view;
  if (view) ZoomCityMapViewIn(city, view);
}

// Use the same viewport boundary as VisitCanvasMapPixels. Effect footprints
// outside this window must not damage another camera's retained pixels.
CityRect ClipCityMapViewportPixels(CityRect rect, CityMapPaint *paint) {
  if (!paint->view) return rect;
  CityCanvasDesktop *desktop = &paint->city->game->ui->desktop;
  const Window *window = FindCityCanvasWindow(desktop, paint->view->window_id);
  if (!window || !window->visible || window->shaded) return (CityRect){0};
  CityRect b = Sc2kCanvasDesktopPixelBounds(desktop, window->window_id);
  int scale = desktop->pixel_state.scale ? desktop->pixel_state.scale : 100;
  CityRect content = {b.left + (3 * scale + 99) / 100, b.top + (CITY_VIEWPORT_TOP * scale + 99) / 100,
      b.right - (3 * scale + 99) / 100,
      b.bottom - ((window->viewport_pixels ? 3 : CITY_VIEWPORT_BOTTOM) * scale + 99) / 100};
  return IntersectCityRects(rect, content);
}

int CityViewportMapZoom(const CityExtraMapView *view) {
  return IntMin(view->zoom, CITY_MAP_ZOOM_LARGE);
}

int CityViewportMagnification(const CityExtraMapView *view) {
  return view->zoom == CITY_CANVAS_MAP_ZOOM_MAGNIFIED ? 2 : 1;
}

// Update logical viewport dimensions and constrain its camera after resizing, rotation or zoom changes.
void SyncCityMapViewSize(const City *city, CityExtraMapView *view, Window *window) {
  const Drawing *drawing = city->game->drawing;
  CityRect content = CityViewportContentBounds(window);
  int scale = drawing->map_pixel_scale * 100 * CityViewportMagnification(view);
  int width = (content.right - content.left) * drawing->options.scale;
  int height = (content.bottom - content.top) * drawing->options.scale;
  view->width = (width + scale - 1) / scale;
  view->height = (height + scale - 1) / scale;
  CityPoint origin = ClampCityMapCameraOrigin(city,
      (CityMapCamera){view->origin_x, view->origin_y, CityViewportMapZoom(view),
          view->width, view->height, view->rotation});
  if (origin.x != view->origin_x) view->scroll_pixels.x = 0;
  if (origin.y != view->origin_y) view->scroll_pixels.y = 0;
  view->origin_x = origin.x;
  view->origin_y = origin.y;
  window->viewport_zoom = view->zoom;
}

CityRect CityMapScreenRect(CityRect rect, void *context) {
  CityMapPaint *paint = context;
  City *city = paint->city;
  Drawing *drawing = city->game->drawing;
  CityInterfaceState *ui = city->game->ui;
  CityExtraMapView *view = paint->view;
  if (view == NULL) {
    int scale = drawing->map_pixel_scale * (drawing->default_map_view->magnified_large_zoom ? 2 : 1);
    rect = (CityRect){rect.left * scale, rect.top * scale, rect.right * scale, rect.bottom * scale};
  } else {
    CityRect pixels = Sc2kCanvasDesktopPixelBounds(&ui->desktop, view->window_id);
    int ui_scale = drawing->options.scale;
    int scale = drawing->map_pixel_scale * 100 * CityViewportMagnification(view);
    int left = pixels.left + (3 * ui_scale + 99) / 100;
    int top = pixels.top + (CITY_VIEWPORT_TOP * ui_scale + 99) / 100;
    rect = (CityRect){left + rect.left * scale / 100, top + rect.top * scale / 100,
        left + (rect.right * scale + 99) / 100, top + (rect.bottom * scale + 99) / 100};
  }
  CityPoint offset = view ? view->scroll_pixels : drawing->scroll_pixels;
  rect.left += offset.x; rect.right += offset.x;
  rect.top += offset.y; rect.bottom += offset.y;
  return ClipCityMapViewportPixels(rect, paint);
}

void MarkCityMapRect(CityRect rect, void *context) {
  CityMapPaint *paint = context;
  City *city = paint->city;
  Drawing *drawing = city->game->drawing;
  if (rect.left >= rect.right || rect.top >= rect.bottom) return;
  MarkScreenDirty(&drawing->screen_damage, CityMapScreenRect(rect, context));
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

static void MarkCityRoadCarDamage(const RoadCar *car, CityMapCamera camera, CityMapPaint *paint) {
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
