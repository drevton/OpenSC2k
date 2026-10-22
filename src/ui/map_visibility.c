#include "ui/map_views.h"
#include "ui/map_visibility.h"

#include "city/events/events.h"
#include "city/maps/layers.h"
#include "city/state.h"
#include "drawing/maps/map_events.h"
#include "drawing/maps/map_view.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/state.h"
#include "game/state.h"
#include "ui/dialogs/dialogs.h"
#include "ui/dialogs/scenario.h"
#include "ui/dialogs/spans.h"
#include "ui/menus/menu.h"
#include "ui/state.h"
#include "ui/windows/windows.h"

// Input admission includes dialogs; sound and vehicle queries only require visible map pixels.
bool IsCityMainMapInteractiveAt(City *city, int x, int y) {
  if (!city->game->city_active) return false;
  CityInterfaceState *ui = city->game->ui;
  Drawing *drawing = city->game->drawing;
  if (NativeSpanDialogWaiting(ui) || CityConnectionQuestionWaiting(ui) || CityMissionNoticeWaiting(ui) ||
      ui->startup_waiting ||
      NativeScenarioDialogWaiting(ui)) return false;
  if (x < 0 || y < 0 || x >= drawing->map_width || y >= drawing->map_height) return false;
  for (int overlay = 0; overlay < CITY_MENU_OVERLAY_COUNT; ++overlay) {
    CityRect b = CityMenuPixelBounds(&ui->desktop, overlay);
    int px = x * drawing->map_pixel_scale, py = y * drawing->map_pixel_scale;
    if (CanvasPointInRect(b, px, py)) return false;
  }
  for (int index = 0; index < ui->desktop.count; ++index) {
    Window *window = &ui->desktop.windows[index];
    CityRect bounds = Sc2kCanvasDesktopPixelBounds(&ui->desktop, window->window_id);
    bounds.right += (4 * drawing->options.scale + 99) / 100;
    bounds.bottom += (4 * drawing->options.scale + 99) / 100;
    bounds.left /= drawing->map_pixel_scale; bounds.top /= drawing->map_pixel_scale;
    bounds.right = (bounds.right + drawing->map_pixel_scale - 1) / drawing->map_pixel_scale;
    bounds.bottom = (bounds.bottom + drawing->map_pixel_scale - 1) / drawing->map_pixel_scale;
    if (window->visible && CanvasPointInRect(bounds, x, y))
      return false;
  }
  return true;
}

typedef struct {
  CityRect rects[CITY_MAP_CACHE_FRAGMENT_LIMIT];
  i32 count;
  i32 overflow;
  i32 magnification;
} MapFragments;

// Outward rounding may make adjacent exposed display pieces share a source
// pixel at an odd window edge. Subtract the existing union so scroll copies
// never move that pixel twice. These are rectangles, not raster storage.
static void CollectUnique(CityRect rect, MapFragments *fragments, i32 first) {
  for (i32 i = first; i < fragments->count; ++i) {
    CityRect cover = fragments->rects[i];
    CityRect overlap = IntersectCityRects(rect, cover);
    if (overlap.left >= overlap.right || overlap.top >= overlap.bottom) continue;
    if (rect.top < overlap.top)
      CollectUnique((CityRect){rect.left, rect.top, rect.right, overlap.top}, fragments, i + 1);
    if (overlap.bottom < rect.bottom)
      CollectUnique((CityRect){rect.left, overlap.bottom, rect.right, rect.bottom}, fragments, i + 1);
    if (rect.left < overlap.left)
      CollectUnique((CityRect){rect.left, overlap.top, overlap.left, overlap.bottom}, fragments, i + 1);
    if (overlap.right < rect.right)
      CollectUnique((CityRect){overlap.right, overlap.top, rect.right, overlap.bottom}, fragments, i + 1);
    return;
  }
  if (fragments->count == CITY_MAP_CACHE_FRAGMENT_LIMIT) {
    fragments->overflow = 1;
    return;
  }
  fragments->rects[fragments->count++] = rect;
}

static CityRect SourceRect(CityRect rect, i32 magnification) {
  return (CityRect){rect.left / magnification, rect.top / magnification,
      (rect.right + magnification - 1) / magnification,
      (rect.bottom + magnification - 1) / magnification};
}

static void CollectFragment(CityRect rect, void *context) {
  MapFragments *fragments = context;
  rect = SourceRect(rect, fragments->magnification);
  if (rect.left < rect.right && rect.top < rect.bottom)
    CollectUnique(rect, fragments, 0);
}

typedef struct {
  i32 magnification;
  CityCanvasVisiblePaint paint;
  void *context;
} MapSourcePaint;

static void PaintSource(CityRect rect, void *context) {
  MapSourcePaint *source = context;
  source->paint(SourceRect(rect, source->magnification), source->context);
}

static void VisitMapView(const CityCanvasDesktop *desktop, u32 window_id, CityRect area,
    CityCanvasVisiblePaint paint, void *context) {
  if (window_id == 0)
    Sc2kCanvasDesktopVisitMap(desktop, area, paint, context);
  else
    Sc2kCanvasDesktopVisitViewport(desktop, window_id, area, paint, context);
}

static void VisitCityMapViewSourceRects(const CityCanvasDesktop *desktop, u32 window_id, CityRect display_area,
    i32 magnification, CityCanvasVisiblePaint paint, void *context) {
  MapFragments fragments = {.magnification = magnification};
  VisitMapView(desktop, window_id, display_area, CollectFragment, &fragments);
  if (fragments.overflow) {
    MapSourcePaint source = {magnification, paint, context};
    VisitMapView(desktop, window_id, display_area, PaintSource, &source);
  } else
    for (i32 i = 0; i < fragments.count; ++i) paint(fragments.rects[i], context);
}

void DrawCityMapCache(NativeMapCache *cache, const CityCanvasDesktop *desktop, u32 window_id,
    i32 width, i32 height, i32 magnification, i32 origin_x, i32 origin_y,
    const MapCachePixels *pixels, CityCanvasVisiblePaint paint, void *context) {
  bool physical = pixels->physical_coordinates;
  MapFragments fragments = {.magnification = physical ? 1 : magnification};
  if (physical) VisitCanvasMapPixels(desktop, window_id, (CityRect){0, 0, width, height}, CollectFragment, &fragments);
  else VisitMapView(desktop, window_id, (CityRect){0, 0, width, height}, CollectFragment, &fragments);
  if (fragments.overflow) {
    cache->valid = 0;
    if (physical) VisitCanvasMapPixels(desktop, window_id, (CityRect){0, 0, width, height}, paint, context);
    else VisitCityMapViewSourceRects(desktop, window_id, (CityRect){0, 0, width, height},
        magnification, paint, context);
    return;
  }
  DrawMapCachePixels(cache, fragments.rects, fragments.count, magnification, origin_x, origin_y,
      pixels, paint, context);
}

typedef struct {
  int magnification;
  bool visible;
} MapSourceVisibility;

static void RoadCarVisibleFragment(CityRect rect, void *context) {
  MapSourceVisibility *visibility = context;
  rect = SourceRect(rect, visibility->magnification);
  if (rect.right > rect.left && rect.bottom > rect.top) visibility->visible = true;
}

bool IsCityRoadCarInView(const City *city, TileIndex tile, int height, CityMapCamera view, int view_index) {
  if (view.zoom < CITY_MAP_ZOOM_LARGE || view.width <= 0 || view.height <= 0) return false;
  const Drawing *drawing = city->game->drawing;
  u32 window_id = view_index ? drawing->views[view_index - 1].window_id : 0;
  if (view_index) {
    const Window *window = FindCityCanvasWindow(&city->game->ui->desktop, window_id);
    if (!window || !window->visible || window->shaded) return false;
  }
  CityPoint p = CityMapCameraPoint(city, view.rotation, MapTileX(city, tile), MapTileY(city, tile));
  int scale = 1 << view.zoom, margin = 64 * scale;
  int x = view.origin_x + (p.y - p.x + 1) * 4 * scale;
  int y = view.origin_y + (p.x + p.y) * 2 * scale -
      height * CityMapHeightStep(view.zoom) / 256;
  if (x < -margin || y < -margin || x >= view.width + margin || y >= view.height + margin) return false;
  int magnification = view_index ? 1 << IntMax(0, drawing->views[view_index - 1].zoom - 2) :
      drawing->default_map_view->magnified_large_zoom ? 2 : 1;
  CityRect vicinity = {(x - margin) * magnification, (y - margin) * magnification, (x + margin + 1) * magnification,
      (y + margin + 1) * magnification};
  // One nonempty source piece proves visibility; unioning the pieces cannot change that answer.
  MapSourceVisibility visibility = {.magnification = magnification};
  VisitMapView(&city->game->ui->desktop, window_id, vicinity, RoadCarVisibleFragment, &visibility);
  return visibility.visible;
}

CityRect ClipCityMapViewportPixels(CityRect rect, const CityMapPaint *paint) {
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

CityRect CityMapScreenRect(CityRect rect, const CityMapPaint *paint) {
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

static void MarkSoundVisible(CityRect rect, void *context) {
  (void)rect;
  *(bool *)context = true;
}

static bool FindCityMapSoundView(City *city, TileIndex tile, int airplane, u8 *pan) {
  Game *game = city->game;
  Drawing *drawing = game->drawing;
  CityInterfaceState *ui = game->ui;
  if (!game->city_active ||
      !ui->main_window_available || ui->main_window_iconic) return false;
  if (airplane >= 0 && (!drawing->map_state.vehicles_enabled || drawing->map_state.events_suppressed ||
      (drawing->map_state.data_overlay_enabled && drawing->map_state.data_mode != CITY_DATA_MODE_NONE))) return false;
  const CityMapViewConfig *main = drawing->default_map_view;
  for (int index = -1; index < CITY_EXTRA_MAP_VIEW_COUNT; ++index) {
    CityExtraMapView *view = index < 0 ? NULL : &drawing->views[index];
    if (view && !view->window_id) continue;
    int zoom = view ? CityViewportMapZoom(view) : main->zoom;
    CityMapCamera camera = view ?
        (CityMapCamera){view->origin_x, view->origin_y, zoom, view->width, view->height, view->rotation} :
        (CityMapCamera){drawing->map_state.origin_x, drawing->map_state.origin_y, zoom,
            drawing->map_width, drawing->map_height, CityMapViewRotation(main)};
    CityMapPaint paint = {city, view};
    CityRect bounds;
    if (airplane >= 0) bounds = CityAirplaneBounds(city, airplane, camera);
    else {
      CityPoint point = CityMapCameraPoint(city, camera.rotation, MapTileX(city, tile), MapTileY(city, tile));
      int x = camera.origin_x + (4 << zoom) * (point.y - point.x + 1);
      int y = camera.origin_y + (2 << zoom) * (point.x + point.y) -
          CityMapHeightStep(zoom) * ReadCitySurfaceHeight(&city->tile[tile]);
      bounds = (CityRect){x, y, x + 1, y + 1};
    }
    bounds = CityMapScreenRect(bounds, &paint);
    bool visible = false;
    VisitCanvasMapPixels(&ui->desktop, view ? view->window_id : 0, bounds, MarkSoundVisible, &visible);
    if (visible) {
      if (pan) {
        CityRect viewport = CityMapScreenRect((CityRect){0, 0, camera.width, camera.height}, &paint);
        int width = viewport.right - viewport.left;
        if (width > 0) *pan = IntClamp(128 +
            ((bounds.left + bounds.right) / 2 - (viewport.left + viewport.right) / 2) * 256 / width, 0, 255);
      }
      return true;
    }
  }
  return false;
}

bool IsCityAirplaneVisible(City *city, int slot) {
  return FindCityMapSoundView(city, city->events.event[slot].tile, slot, NULL);
}

bool IsCityMapSoundVisible(City *city, TileIndex tile) {
  return IsMapTileValid(city, tile) && FindCityMapSoundView(city, tile, -1, NULL);
}

u8 CityMapSoundPan(City *city, TileIndex tile) {
  u8 pan = 128;
  if (!IsMapTileValid(city, tile)) return pan;
  int airplane = -1;
  for (int slot = FirstCityMapEventOnTile(city, tile); slot >= 0; slot = NextCityMapEventOnTile(city, slot)) {
    if (city->events.event[slot].type == CITY_MAP_EVENT_AIRPLANE) { airplane = slot; break; }
  }
  FindCityMapSoundView(city, tile, airplane, &pan);
  return pan;
}
