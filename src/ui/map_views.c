#include "ui/windows/window_internal.h"
#include "ui/map_visibility.h"
#include "ui/map.h"
#include "ui/map_views.h"

#include <string.h>

#include "assets/state.h"
#include "audio/state.h"
#include "base/time.h"
#include "city/events/events.h"
#include "city/maps/layers.h"
#include "city/state.h"
#include "drawing/display.h"
#include "drawing/maps/map_cache.h"
#include "drawing/maps/map_animation.h"
#include "drawing/maps/map_effects.h"
#include "drawing/maps/map_events.h"
#include "drawing/maps/map_render.h"
#include "drawing/maps/map_view.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/raster/canvas_raster_internal.h"
#include "drawing/state.h"
#include "game/errors.h"
#include "game/state.h"
#include "ui/input.h"
#include "ui/menus/menu.h"
#include "ui/signs.h"
#include "ui/sdl.h"
#include "ui/state.h"
#include "ui/tools/cost_labels.h"
#include "ui/windows/windows.h"

// Map viewport controls share geometry between painting, input and hover help.

CityRect CityViewportContentBounds(const Window *window) {
  CityRect b = window->bounds;
  return (CityRect){b.left + 3, b.top + CITY_VIEWPORT_TOP, b.right - 3,
      window->shaded ? b.top + CITY_VIEWPORT_TOP : b.bottom - CITY_VIEWPORT_BOTTOM};
}

CityRect CityViewportControlBounds(const Window *window, CityViewportControl control) {
  CityRect b = window->bounds;
  if (control >= CITY_VIEWPORT_SHADE)
    return CityCaptionControlBounds(window, control - CITY_VIEWPORT_SHADE + CITY_CAPTION_SHADE);
  int top = b.bottom - CITY_VIEWPORT_BOTTOM + 2;
  int middle = b.left + 88 + ((b.right - b.left - 88) >> 1);
  switch (control) {
  case CITY_VIEWPORT_ZOOM_IN: return (CityRect){b.left, top, b.left + 22, top + 22};
  case CITY_VIEWPORT_ZOOM_OUT: return (CityRect){b.left + 22, top, b.left + 44, top + 22};
  case CITY_VIEWPORT_ROTATE_CLOCKWISE: return (CityRect){b.left + 44, top, b.left + 66, top + 22};
  case CITY_VIEWPORT_ROTATE_COUNTERCLOCKWISE: return (CityRect){b.left + 66, top, b.left + 88, top + 22};
  case CITY_VIEWPORT_MAIN_CENTER: return (CityRect){b.left + 88, top, middle, top + 22};
  case CITY_VIEWPORT_CENTER: return (CityRect){middle, top, b.right, top + 22};
  default: return (CityRect){0};
  }
}

CityViewportControl CityViewportControlAt(const Window *window, int x, int y) {
  for (int control = CITY_VIEWPORT_ZOOM_IN; control <= CITY_VIEWPORT_STICKY; ++control) {
    if (window->shaded && control < CITY_VIEWPORT_SHADE) continue;
    if (CanvasPointInRect(CityViewportControlBounds(window, control), x, y)) return control;
  }
  return CITY_VIEWPORT_CONTROL_NONE;
}

enum { CITY_SCROLL_PIXELS_PER_STEP = 20 };

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

static CityPoint CityMainMapCenter(const Drawing *drawing) {
  int shift = drawing->default_map_view->magnified_large_zoom ? 2 : 1;
  return (CityPoint){drawing->map_width >> shift, drawing->map_height >> shift};
}

// Exchange world-space centers without changing either camera's rotation.

static void CenterCityExtraMapView(City *city, CityExtraMapView *view) {
  Drawing *drawing = city->game->drawing;
  CityPoint center = CityMainMapCenter(drawing);
  CityPoint origin = TransferCityMapCameraCenter(city,
      (CityMapCamera){drawing->map_state.origin_x, drawing->map_state.origin_y,
          drawing->default_map_view->zoom, center.x * 2, center.y * 2, CityMapViewRotation(drawing->default_map_view)},
      (CityMapCamera){0, 0, CityViewportMapZoom(view), view->width, view->height, view->rotation});
  view->origin_x = origin.x;
  view->origin_y = origin.y;
  view->scroll_pixels = (CityPoint){0};
}

void OpenCityMapView(City *city) {
  Drawing *drawing = city->game->drawing;
  CityInterfaceState *ui = city->game->ui;
  for (int index = 0; index < CITY_EXTRA_MAP_VIEW_COUNT; ++index) {
    CityExtraMapView *view = &drawing->views[index];
    if (view->window_id != 0 && FindCityCanvasWindow(&ui->desktop, view->window_id) != NULL) continue;
    CityRect *animation_bounds = view->animations.bounds;
    int animation_capacity = view->animations.capacity;
    MapSpriteFootprints sprites = view->sprites;
    memset(view, 0, sizeof(*view));
    view->sprites = sprites;
    view->sprites.initialized = false;
    view->animations.bounds = animation_bounds;
    view->animations.capacity = animation_capacity;
    drawing->map_snapshot.regions[index + 1] = (CityMapCamera){0};
    static const char *const kViewportTitles[CITY_EXTRA_MAP_VIEW_COUNT] = {
      "Viewport 1", "Viewport 2", "Viewport 3", "Viewport 4",
      "Viewport 5", "Viewport 6", "Viewport 7", "Viewport 8"
    };
    view->rotation = CityMapViewRotation(drawing->default_map_view);
    view->zoom = IntMin(drawing->default_map_view->zoom + (drawing->default_map_view->magnified_large_zoom != 0),
        CityMapZoomLimit(drawing->map_pixel_scale * 100));
    int left = 90 + index * 12, top = 40 + index * 10;
    int slot = Sc2kCanvasDesktopOpen(&ui->desktop, (Window){.kind = CITY_CANVAS_MAP_VIEWPORT,
        .bounds = {left, top, left + CITY_VIEWPORT_WIDTH, top + CITY_VIEWPORT_HEIGHT},
        .title = kViewportTitles[index], .viewport_zoom = view->zoom});
    if (slot >= 0) {
      Window *window = &ui->desktop.windows[slot];
      view->window_id = window->window_id;
      ui->map_view_focus_window = window->window_id;
      SyncCityMapViewSize(city, view, window);
      CenterCityExtraMapView(city, view);
    }
    return;
  }
}

void PublishCityMapChanges(City *city) {
  RefreshCitySignEntries(city);
  Drawing *drawing = city->game->drawing;
  CityInterfaceState *ui = city->game->ui;
  CityMapPaint paint = {city, NULL};
  if (!PrepareCityMapDrawingStorage(city)) {
    SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    city->game->execution_failed = true;
    return;
  }
  ResizeScreenDamage(&drawing->screen_damage, drawing->framebuffer.width, drawing->framebuffer.height);
  int traffic_magnification = drawing->default_map_view->magnified_large_zoom ? 2 : 1;
  PopulateCityRoadTrafficView(city, 0, drawing->map_snapshot.valid ?
      drawing->map_snapshot.regions[0] : (CityMapCamera){0},
      (CityMapCamera){drawing->map_state.origin_x, drawing->map_state.origin_y, drawing->default_map_view->zoom,
          drawing->map_width / traffic_magnification, drawing->map_height / traffic_magnification,
          CityMapViewRotation(drawing->default_map_view)});
  for (int index = 0; index < CITY_EXTRA_MAP_VIEW_COUNT; ++index) {
    CityExtraMapView *view = &drawing->views[index];
    Window *window = FindCityCanvasWindow(&ui->desktop, view->window_id);
    if (window == NULL || !window->visible || window->shaded) continue;
    SyncCityMapViewSize(city, view, window);
    PopulateCityRoadTrafficView(city, index + 1, drawing->map_snapshot.valid ?
        drawing->map_snapshot.regions[index + 1] : (CityMapCamera){0},
        (CityMapCamera){view->origin_x, view->origin_y, CityViewportMapZoom(view),
            view->width, view->height, view->rotation});
  }
  FilterDrawingMapChanges(city, &city->map_changes, &drawing->map_snapshot);
  const SimulationMapChanges *changes = &city->map_changes;
  int geometry_layers = (1 << CITY_MAP_CHANGE_TILE) | (1 << CITY_MAP_CHANGE_ZONE) |
      (1 << CITY_MAP_CHANGE_TERRAIN) | (1 << CITY_MAP_CHANGE_ALTITUDE);
  if (changes->full || changes->tiles_changed || (changes->invalidated_layers & geometry_layers))
    RefreshCityRoadTrafficPoses(city);
  bool data = drawing->map_state.data_overlay_enabled && drawing->map_state.data_mode != CITY_DATA_MODE_NONE;
  int magnification = drawing->default_map_view->magnified_large_zoom ? 2 : 1;
  ProjectSimulationMapDamage(city, changes, &drawing->map_snapshot,
      (CityMapCamera){drawing->map_state.origin_x, drawing->map_state.origin_y, drawing->default_map_view->zoom,
          drawing->map_width / magnification, drawing->map_height / magnification,
          CityMapViewRotation(drawing->default_map_view)}, data, MarkCityMapRect, &paint);
  for (int index = 0; index < CITY_EXTRA_MAP_VIEW_COUNT; ++index) {
    CityExtraMapView *view = &drawing->views[index];
    Window *window = FindCityCanvasWindow(&ui->desktop, view->window_id);
    if (window == NULL || !window->visible || window->shaded) continue;
    SyncCityMapViewSize(city, view, window);
    ProjectSimulationMapDamage(city, changes, &drawing->map_snapshot,
        (CityMapCamera){view->origin_x, view->origin_y, CityViewportMapZoom(view),
            view->width, view->height, view->rotation}, data, MarkCityMapRect, &(CityMapPaint){city, view});
  }
  // Every registered view has received the batch; screen damage remains pending
  // until all viewport buffers and overlapping windows have been painted.
  UpdateDrawingMapSnapshot(city, &drawing->map_snapshot, 0,
      (CityMapCamera){drawing->map_state.origin_x, drawing->map_state.origin_y, drawing->default_map_view->zoom,
          drawing->map_width / magnification, drawing->map_height / magnification,
          CityMapViewRotation(drawing->default_map_view)});
  for (int index = 0; index < CITY_EXTRA_MAP_VIEW_COUNT; ++index) {
    CityExtraMapView *view = &drawing->views[index];
    Window *window = FindCityCanvasWindow(&ui->desktop, view->window_id);
    if (window == NULL || !window->visible || window->shaded) {
      drawing->map_snapshot.regions[index + 1] = (CityMapCamera){0};
      continue;
    }
    UpdateDrawingMapSnapshot(city, &drawing->map_snapshot, index + 1,
        (CityMapCamera){view->origin_x, view->origin_y, CityViewportMapZoom(view),
            view->width, view->height, view->rotation});
  }
  ClearSimulationMapChanges(&city->map_changes);
  drawing->world_damage = drawing->screen_damage;
}

bool BindCityMapTarget(const CityMapPaint *paint) {
  City *city = paint->city;
  Drawing *drawing = city->game->drawing;
  const CityInterfaceState *ui = city->game->ui;
  CityExtraMapView *view = paint->view;
  int magnification = drawing->default_map_view->magnified_large_zoom ? 2 : 1;
  int scale = drawing->map_pixel_scale * 100 * magnification;
  CityPoint position = {0};
  if (view) {
    CityRect bounds = Sc2kCanvasDesktopPixelBounds(&ui->desktop, view->window_id);
    position.x = bounds.left + (3 * drawing->options.scale + 99) / 100;
    position.y = bounds.top + (CITY_VIEWPORT_TOP * drawing->options.scale + 99) / 100;
  }
  CityPoint offset = view ? view->scroll_pixels : drawing->scroll_pixels;
  position.x += offset.x; position.y += offset.y;
  if (!view) position.x += drawing->screen_shake_offset * drawing->map_pixel_scale;
  return BindDrawingMapTarget(drawing, scale, position);
}

void PaintCityMapPixels(CityRect rect, void *context) {
  CityMapPaint *paint = context;
  City *city = paint->city;
  PaintDrawingMapPixels(rect, paint);
  if (!paint->view) DrawRetainedCityZoneCostLabel(city);
  // Composite toolbar/status controls after map painting instead of fragmenting
  // each repaint. Scroll caches continue to exclude these opaque UI pixels.
  CityCanvasDesktop *desktop = &city->game->ui->desktop;
  for (int overlay = 0; overlay < CITY_MENU_OVERLAY_COUNT; ++overlay) {
    if (overlay == 1) continue;
    Sc2kDirtyMarkRect(&desktop->window_damage,
        IntersectCityRects(rect, CityMenuPixelBounds(desktop, overlay)));
  }
}

void RepaintCityMapDamage(CityRect screen, void *context) {
  CityMapPaint *paint = context;
  City *city = paint->city;
  Drawing *drawing = city->game->drawing;
  CityInterfaceState *ui = city->game->ui;
  CityExtraMapView *view = paint->view;
  screen.left = IntMax(screen.left, 0); screen.top = IntMax(screen.top, 0);
  screen.right = IntMin(screen.right, drawing->framebuffer.width); screen.bottom = IntMin(screen.bottom, drawing->framebuffer.height);
  VisitCanvasMapRepaintPixels(&ui->desktop, view ? view->window_id : 0, screen, PaintCityMapPixels, context);
}

bool RenderCityMapViews(City *city, bool preserved_source) {
  Drawing *drawing = city->game->drawing;
  CityInterfaceState *ui = city->game->ui;
  CityMapViewConfig saved_view = *drawing->default_map_view;
  int saved_x = drawing->map_state.origin_x, saved_y = drawing->map_state.origin_y;
  SpriteSurface saved_target = drawing->map_target;
  int saved_scale = drawing->target_scale, saved_cache = drawing->target_cache;
  int saved_target_x = drawing->target_x, saved_target_y = drawing->target_y;
  bool success = true;
  for (int index = 0; index < CITY_EXTRA_MAP_VIEW_COUNT; ++index) {
    CityExtraMapView *view = &drawing->views[index];
    Window *window = FindCityCanvasWindow(&ui->desktop, view->window_id);
    if (window == NULL || !window->visible || window->shaded) {
      view->cache.valid = false;
      continue;
    }
    SyncCityMapViewSize(city, view, window);
    drawing->default_map_view->rotation = view->rotation;
    drawing->default_map_view->zoom = CityViewportMapZoom(view);
    drawing->default_map_view->magnified_large_zoom = view->zoom == CITY_CANVAS_MAP_ZOOM_MAGNIFIED;
    drawing->default_map_view->view_rect = (CityRect){0, 0, view->width, view->height};
    drawing->map_state.origin_x = view->origin_x;
    drawing->map_state.origin_y = view->origin_y;
    CityMapPaint paint = {city, view};
    if (!BindCityMapTarget(&paint)) { success = false; goto restore; }
    PrepareCityMapSpriteDamage(&paint, MarkCityMapSpriteDamage);
    CityRect effect_clip = ClipCityMapViewportPixels(
        (CityRect){0, 0, drawing->framebuffer.width, drawing->framebuffer.height}, &paint);
    PrepareCityMapEffectDamage(&paint, effect_clip);
    if (drawing->disable_map_cache) view->cache.valid = 0;
    bool retained = view->cache.valid;
    MapCachePixels pixels = CityMapCachePixels(drawing);
    pixels.preserved_source = preserved_source;
    DrawCityMapCache(&view->cache, &ui->desktop, view->window_id,
        drawing->framebuffer.width, drawing->framebuffer.height, 1, view->origin_x, view->origin_y,
        &pixels, PaintCityMapPixels, &paint);
    if (retained) {
      VisitScreenRepaintDamage(&drawing->world_damage, RepaintCityMapDamage, &paint,
          !drawing->disable_repaint_coalescing);
    }
    FinishCityMapAnimations(city, drawing);
  }
restore:
  *drawing->default_map_view = saved_view;
  drawing->map_state.origin_x = saved_x;
  drawing->map_state.origin_y = saved_y;
  drawing->map_target = saved_target;
  drawing->target_scale = saved_scale;
  drawing->target_cache = saved_cache;
  drawing->target_x = saved_target_x;
  drawing->target_y = saved_target_y;
  return success && GetGameError(city->game) == 0;
}

static void DirtyCityMapView(City *city, CityExtraMapView *view) {
  CityInterfaceState *ui = city->game->ui;
  Window *window = FindCityCanvasWindow(&ui->desktop, view->window_id);
  if (window != NULL) Sc2kDirtyMarkRect(&ui->desktop.dirty, window->bounds);
}

static void SetCityMapViewZoom(City *city, CityExtraMapView *view, int zoom) {
  Drawing *drawing = city->game->drawing;
  zoom = IntMin(zoom, CityMapZoomLimit(drawing->map_pixel_scale * 100));
  if (view->zoom == zoom) return;
  Window *window = FindCityCanvasWindow(&city->game->ui->desktop, view->window_id);
  if (window->shaded) return;
  int previous = 1 << CityViewportMapZoom(view);
  int x = (view->width >> 1) - view->origin_x;
  int y = (view->height >> 1) - view->origin_y;
  view->zoom = zoom;
  SyncCityMapViewSize(city, view, window);
  int scale = 1 << CityViewportMapZoom(view);
  view->origin_x = (view->width >> 1) - x * scale / previous;
  view->origin_y = (view->height >> 1) - y * scale / previous;
  SyncCityMapViewSize(city, view, window);
  view->scroll_pixels = (CityPoint){0};
  view->cache.valid = false;
}

// A display pixel-density change can make the current map zoom too large.
void UpdateCityMapZoomLimits(Game *game) {
  Drawing *drawing = game->drawing;
  CityMapViewConfig *main = drawing->default_map_view;
  int limit = CityMapZoomLimit(drawing->map_pixel_scale * 100);
  while (main->zoom + main->magnified_large_zoom > limit)
    ZoomCityMapViewOut(game->city, main);
  limit = CityMapZoomLimit(drawing->map_pixel_scale * 100);
  for (int index = 0; index < CITY_EXTRA_MAP_VIEW_COUNT; ++index) {
    CityExtraMapView *view = &drawing->views[index];
    Window *window = FindCityCanvasWindow(&game->ui->desktop, view->window_id);
    if (!window || window->shaded || view->zoom <= limit) continue;
    SetCityMapViewZoom(game->city, view, limit);
    DirtyCityMapView(game->city, view);
  }
}

void PanCityMapWithScroll(City *city, CityExtraMapView *view, const CanvasSDLEvent *event) {
  CityInterfaceState *ui = city->game->ui;
  i64 *remainder_x = view ? &view->scroll_x : &ui->scroll_remainder_x;
  i64 *remainder_y = view ? &view->scroll_y : &ui->scroll_remainder_y;
  i64 x = event->scroll.precise ? event->scroll.pixels_x : (i64)event->x * CITY_SCROLL_PIXELS_PER_STEP;
  i64 y = event->scroll.precise ? event->scroll.pixels_y : (i64)event->y * CITY_SCROLL_PIXELS_PER_STEP;
  if (!event->scroll.precise) {
    if (event->scroll_flipped) { x = -x; y = -y; }
    y = -y;
  }
  *remainder_x += x;
  *remainder_y += y;
  int dx = (int)(*remainder_x / 1024), dy = (int)(*remainder_y / 1024);
  *remainder_x %= 1024;
  *remainder_y %= 1024;
  if (dx || dy) OffsetCityMapPixels(city, view, dx, dy);
}

void OffsetCityMapPixels(City *city, CityExtraMapView *view, int dx, int dy) {
  Drawing *drawing = city->game->drawing;
  CityPoint *pixels = view ? &view->scroll_pixels : &drawing->scroll_pixels;
  int scale = view ? drawing->map_pixel_scale * 100 * CityViewportMagnification(view) :
      drawing->map_pixel_scale * (drawing->default_map_view->magnified_large_zoom ? 2 : 1) * 100;
  int x = pixels->x + dx, y = pixels->y + dy;
  int logical_x = x * 100 / scale, logical_y = y * 100 / scale;
  int origin_x = view ? view->origin_x : drawing->map_state.origin_x;
  int origin_y = view ? view->origin_y : drawing->map_state.origin_y;
  pixels->x = x - ((origin_x + logical_x) * scale / 100 - origin_x * scale / 100);
  pixels->y = y - ((origin_y + logical_y) * scale / 100 - origin_y * scale / 100);
  if (view) {
    CityPoint origin = ClampCityMapCameraOrigin(city,
        (CityMapCamera){view->origin_x + logical_x, view->origin_y + logical_y, CityViewportMapZoom(view),
            view->width, view->height, view->rotation});
    if (origin.x != view->origin_x + logical_x) pixels->x = 0;
    if (origin.y != view->origin_y + logical_y) pixels->y = 0;
    view->origin_x = origin.x;
    view->origin_y = origin.y;
    DirtyCityMapView(city, view);
  } else if (logical_x || logical_y) {
    OffsetCityMapOrigin(city, drawing->default_map_view, logical_x, logical_y);
  }
}

static void ApplyCityViewportControl(City *city, CityExtraMapView *view, CityViewportControl control) {
  Drawing *drawing = city->game->drawing;
  CityInterfaceState *ui = city->game->ui;
  Window *window = FindCityCanvasWindow(&ui->desktop, view->window_id);
  switch (control) {
  case CITY_VIEWPORT_ZOOM_IN: SetCityMapViewZoom(city, view, view->zoom + 1); break;
  case CITY_VIEWPORT_ZOOM_OUT: SetCityMapViewZoom(city, view, view->zoom - 1); break;
  case CITY_VIEWPORT_ROTATE_CLOCKWISE: RotateCityMapViewport(city, view->window_id, true); break;
  case CITY_VIEWPORT_ROTATE_COUNTERCLOCKWISE: RotateCityMapViewport(city, view->window_id, false); break;
  case CITY_VIEWPORT_MAIN_CENTER: {
    CityPoint center = CityMainMapCenter(drawing);
    CityPoint origin = TransferCityMapCameraCenter(city,
        (CityMapCamera){view->origin_x, view->origin_y, CityViewportMapZoom(view),
            view->width, view->height, view->rotation},
        (CityMapCamera){0, 0, drawing->default_map_view->zoom, center.x * 2, center.y * 2,
            CityMapViewRotation(drawing->default_map_view)});
    drawing->scroll_pixels = (CityPoint){0};
    OffsetCityMapOrigin(city, drawing->default_map_view,
        origin.x - drawing->map_state.origin_x, origin.y - drawing->map_state.origin_y);
    break;
  }
  case CITY_VIEWPORT_CENTER: CenterCityExtraMapView(city, view); break;
  case CITY_VIEWPORT_SHADE:
    Sc2kDirtyMarkRect(&ui->desktop.dirty, window->bounds);
    if (window->shaded) {
      window->bounds.bottom = window->bounds.top + window->restore_height;
    } else {
      window->restore_height = window->bounds.bottom - window->bounds.top;
      window->bounds.bottom = window->bounds.top + 14;
    }
    window->shaded = !window->shaded;
    view->cache.valid = false;
    break;
  case CITY_VIEWPORT_DEFAULT_SIZE:
    Sc2kDirtyMarkRect(&ui->desktop.dirty, window->bounds);
    window->shaded = false;
    ResetCityWindowSize(window);
    break;
  case CITY_VIEWPORT_STICKY: window->sticky = !window->sticky; break;
  default: break;
  }
  if (!window->shaded) SyncCityMapViewSize(city, view, window);
  DirtyCityMapView(city, view);
}

bool PollCityMapViewControls(City *city) {
  CityInterfaceState *ui = city->game->ui;
  if (!ui->viewport_pressed_window) return false;
  Window *window = FindCityCanvasWindow(&ui->desktop, ui->viewport_pressed_window);
  if (window && (!ui->viewport_press_released ||
      (u32)((u32)(u64)(MonotonicSeconds() * 1000) - ui->viewport_release_time) < 150)) return false;
  if (window) {
    window->viewport_pressed = CITY_VIEWPORT_CONTROL_NONE;
    Sc2kDirtyMarkRect(&ui->desktop.dirty, window->bounds);
  }
  ui->viewport_pressed_window = 0;
  ui->viewport_press_released = false;
  return true;
}

static void DrawCityViewportWindow(Game *game, Canvas *canvas, const Window *window) {
  const u32 face = canvas_face, light = canvas_light, dark = canvas_dark, ink = 0xFF101010;
  CityRect b = window->bounds, content = CityViewportContentBounds(window), clip = canvas->clip;
  if (!window->shaded) {
    // Map pixels already occupy the framebuffer. Only the surrounding chrome is filled.
    CityRect edges[] = {{b.left, b.top + 14, b.right, content.top},
        {b.left, content.top, content.left, content.bottom},
        {content.right, content.top, b.right, content.bottom},
        {b.left, content.bottom, b.right, b.bottom}};
    for (int i = 0; i < 4; ++i) {
      canvas->clip = IntersectCityRects(clip, edges[i]);
      DrawCanvasFrame(canvas, (CityRect){b.left, b.top + 14, b.right, b.bottom}, face, light, dark, true);
      DrawCanvasFrame(canvas, (CityRect){content.left - 1, content.top - 1, content.right + 1, content.bottom + 1},
          face, light, dark, false);
    }
    canvas->clip = clip;
  }
  DrawCityWindowCaption(canvas, b, window->title, face, light, dark, 33);
  for (int control = CITY_VIEWPORT_ZOOM_IN; control <= CITY_VIEWPORT_STICKY; ++control) {
    if (window->shaded && control < CITY_VIEWPORT_SHADE) continue;
    CityRect rect = CityViewportControlBounds(window, control);
    bool disabled = (control == CITY_VIEWPORT_ZOOM_IN &&
        window->viewport_zoom >= CityMapZoomLimit(game->drawing->map_pixel_scale * 100)) ||
        (control == CITY_VIEWPORT_ZOOM_OUT && window->viewport_zoom == CITY_CANVAS_MAP_ZOOM_SMALL);
    bool pressed = window->viewport_pressed == control ||
        (control == CITY_VIEWPORT_SHADE && window->shaded) ||
        (control == CITY_VIEWPORT_STICKY && window->sticky);
    if (disabled) DrawCanvasFrame(canvas, rect, face, light, dark, !pressed);
    else pressed |= DrawCanvasButtonFrame(canvas, rect, face, light, dark, !pressed);
    if (control <= CITY_VIEWPORT_ZOOM_OUT) {
      DrawCityToolbarIcon(game, canvas, rect,
          control == CITY_VIEWPORT_ZOOM_IN ? CITY_TOPBAR_ZOOM_IN : CITY_TOPBAR_ZOOM_OUT, disabled, false);
    } else if (control <= CITY_VIEWPORT_ROTATE_COUNTERCLOCKWISE) {
      DrawCityToolbarIcon(game, canvas, rect, CITY_TOPBAR_ROTATE, false,
          control == CITY_VIEWPORT_ROTATE_COUNTERCLOCKWISE);
    } else if (control <= CITY_VIEWPORT_CENTER) {
      CityRect label = {rect.left + pressed, rect.top + pressed, rect.right + pressed, rect.bottom + pressed};
      DrawCityInterfaceTextCentered(canvas->interface_font, canvas, label,
          control == CITY_VIEWPORT_MAIN_CENTER ? "Change main view" : "Change viewport", ink, false, 10);
    } else {
      DrawCityWindowControlIcon(canvas, rect, control == CITY_VIEWPORT_SHADE ? CITY_WINDOW_ICON_SHADE :
          control == CITY_VIEWPORT_DEFAULT_SIZE ? CITY_WINDOW_ICON_DEFAULT_SIZE : CITY_WINDOW_ICON_STICKY, pressed);
    }
  }
  if (!window->shaded) {
    DrawCanvasFrame(canvas, (CityRect){b.left, b.bottom - 12, b.right - 12, b.bottom}, face, light, dark, true);
    CityRect grip = {b.right - 12, b.bottom - 12, b.right, b.bottom};
    DrawCanvasFrame(canvas, grip, face, light, dark, true);
    for (int offset = 3; offset <= 9; offset += 3) {
      DrawCanvasLine(canvas, b.right - offset, b.bottom - 3, b.right - 3, b.bottom - offset, ink, 1);
      DrawCanvasLine(canvas, b.right - offset, b.bottom - 2, b.right - 2, b.bottom - offset, light, 1);
    }
  }
}

// Handles this window's painting and input; paint coordinates are local to the window.
bool MapViewportWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    Canvas *canvas = event->canvas;
    if (!window->viewport_pixels) DrawCityViewportWindow(game, canvas, window);
    else {
      CanvasPanelFrame(canvas, window->bounds, window->title);
      int left = IntMax(0, canvas->clip.left - window->bounds.left - 3);
      int top = IntMax(0, canvas->clip.top - window->bounds.top - 17);
      int right = IntMin(window->viewport_width, canvas->clip.right - window->bounds.left - 3);
      int bottom = IntMin(window->viewport_height, canvas->clip.bottom - window->bounds.top - 17);
      for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
          CanvasStore(canvas, window->bounds.left + 3 + x, window->bounds.top + 17 + y,
              window->viewport_pixels[y * window->viewport_stride + x]);
    }
    return true;
  }
  if (event->kind == WINDOW_DESTROY) {
    CityInterfaceState *ui = game->ui;
    if (ui->panning_view && ui->panning_view->window_id == window->window_id) {
      ui->panning_view = NULL;
      ui->pan_button = 0;
    }
    if (ui->map_view_focus_window == window->window_id) ui->map_view_focus_window = 0;
    if (ui->viewport_pressed_window == window->window_id) ui->viewport_pressed_window = 0;
    return true;
  }
  if (event->kind != WINDOW_INPUT && event->kind != WINDOW_ZOOM) return false;
  City *city = game->city;
  Drawing *drawing = game->drawing;
  CityInterfaceState *ui = game->ui;
  CityExtraMapView *view = NULL;
  for (int i = 0; i < CITY_EXTRA_MAP_VIEW_COUNT; ++i)
    if (drawing->views[i].window_id == window->window_id) view = &drawing->views[i];
  if (!view) return false;
  Window *target = FindCityCanvasWindow(&ui->desktop, window->window_id);
  if (event->kind == WINDOW_ZOOM) {
    if (!window->shaded && CanvasPointInRect(CityViewportContentBounds(window), event->x, event->y)) {
      SyncCityMapViewSize(city, view, target);
      SetCityMapViewZoom(city, view,
          IntClamp(view->zoom + event->zoom_steps, CITY_CANVAS_MAP_ZOOM_SMALL, CITY_CANVAS_MAP_ZOOM_MAGNIFIED));
      DirtyCityMapView(city, view);
    }
    return true;
  }
  const CanvasSDLEvent *input = event->input;
  if (input->kind == CANVAS_SDL_KEY_DOWN) {
    if (HandleCityPlacementRotationKey(game, input->key)) return true;
    CityHotkey key = input->key;
    if (!target->shaded) SyncCityMapViewSize(city, view, target);
    if (key == CITY_KEY_ESCAPE) {
      if (ui->panning_view == view) {
        ui->panning_view = NULL;
        ui->pan_button = 0;
      }
      if (!target->sticky) Sc2kCanvasDesktopDismissWindow(&ui->desktop, target->window_id);
      return true;
    }
    if (key == CITY_KEY_LEFT) OffsetCityMapPixels(city, view, 40 * drawing->map_pixel_scale, 0);
    else if (key == CITY_KEY_RIGHT) OffsetCityMapPixels(city, view, -40 * drawing->map_pixel_scale, 0);
    else if (key == CITY_KEY_UP) OffsetCityMapPixels(city, view, 0, 20 * drawing->map_pixel_scale);
    else if (key == CITY_KEY_DOWN) OffsetCityMapPixels(city, view, 0, -20 * drawing->map_pixel_scale);
    else return false;
    DirtyCityMapView(city, view);
    return true;
  }
  if (input->kind == CANVAS_SDL_SCROLL) {
    CanvasSDLEvent scroll = *input;
    if (scroll.scroll_flipped) { scroll.x = -scroll.x; scroll.y = -scroll.y; }
    if (ui->scrollwheel_mode == CITY_SCROLLWHEEL_OFF) return true;
    if (ui->scrollwheel_mode == CITY_SCROLLWHEEL_ZOOM) scroll.ctrl = 1;
    if (target->shaded) return true;
    if (scroll.ctrl) {
      if (scroll.y) {
        SyncCityMapViewSize(city, view, target);
        SetCityMapViewZoom(city, view, IntClamp(view->zoom + (scroll.y > 0 ? 1 : -1),
            CITY_CANVAS_MAP_ZOOM_SMALL, CITY_CANVAS_MAP_ZOOM_MAGNIFIED));
        DirtyCityMapView(city, view);
      }
      return true;
    }
    PanCityMapWithScroll(city, view, input);
    return true;
  }
  if (input->kind == CANVAS_SDL_FOCUS_LOST || input->kind == CANVAS_SDL_POINTER_LEAVE) {
    if (ui->panning_view == view) { ui->panning_view = NULL; ui->pan_button = 0; }
    if (ui->viewport_pressed_window == window->window_id && !ui->viewport_press_released) {
      ui->viewport_press_released = true;
      ui->viewport_release_time = (u32)(u64)(MonotonicSeconds() * 1000);
    }
    return true;
  }
  if (input->kind == CANVAS_SDL_POINTER_UP) {
    if (ui->panning_view == view && input->button == ui->pan_button) {
      ui->panning_view = NULL; ui->pan_button = 0;
    }
    if (ui->viewport_pressed_window == window->window_id && !ui->viewport_press_released) {
      ui->viewport_press_released = true;
      ui->viewport_release_time = (u32)(u64)(MonotonicSeconds() * 1000);
    }
    return true;
  }
  int x, y;
  if (input->kind != CANVAS_SDL_POINTER_DOWN && input->kind != CANVAS_SDL_POINTER_MOVE) return false;
  CityInterfacePixels(game, input, &x, &y);
  if (input->kind == CANVAS_SDL_POINTER_MOVE) {
    if (ui->panning_view == view && ui->pan_button) {
      OffsetCityMapPixels(city, view, x - ui->pan_pointer.x, y - ui->pan_pointer.y);
      ui->pan_pointer = (CityPoint){x, y};
      return true;
    }
    return ui->viewport_pressed_window == window->window_id && !ui->viewport_press_released;
  }
  if (HandleCityMapSignInput(city, view, input)) return true;
  bool right = input->button == CANVAS_SDL_BUTTON_RIGHT;
  if ((!right && input->button != CANVAS_SDL_BUTTON_LEFT) || ui->pointer_stroke_count || ui->pointer_query_capture)
    return false;
  ui->map_view_focus_window = window->window_id;
  CityViewportControl control = CityViewportControlAt(window, event->x, event->y);
  if (control != CITY_VIEWPORT_CONTROL_NONE) {
    if (right) return false;
    if ((control == CITY_VIEWPORT_ZOOM_IN && view->zoom >= CityMapZoomLimit(drawing->map_pixel_scale * 100)) ||
        (control == CITY_VIEWPORT_ZOOM_OUT && view->zoom == CITY_CANVAS_MAP_ZOOM_SMALL)) return true;
    Window *previous = FindCityCanvasWindow(&ui->desktop, ui->viewport_pressed_window);
    if (previous) {
      previous->viewport_pressed = CITY_VIEWPORT_CONTROL_NONE;
      Sc2kDirtyMarkRect(&ui->desktop.dirty, previous->bounds);
    }
    target->viewport_pressed = control;
    ui->viewport_pressed_window = window->window_id;
    ui->viewport_press_released = false;
    PlayCityButtonBeep(game);
    if (!window->shaded) SyncCityMapViewSize(city, view, target);
    ApplyCityViewportControl(city, view, control);
    return true;
  }
  if (window->shaded || !CanvasPointInRect(CityViewportContentBounds(window), event->x, event->y)) return false;
  ui->panning_view = view;
  ui->pan_button = input->button;
  ui->pan_pointer = (CityPoint){x, y};
  return true;
}

// The background map has no floating-window controls. Its right-button pan is
// independent of viewport capture, which is handled by MapViewportWindowProcedure.
bool HandleCityMainMapPan(City *city, const CanvasSDLEvent *input) {
  CityInterfaceState *ui = city->game->ui;
  if (ui->panning_view) return false;
  if (input->kind == CANVAS_SDL_POINTER_LEAVE || input->kind == CANVAS_SDL_FOCUS_LOST ||
      (input->kind == CANVAS_SDL_POINTER_UP && input->button == ui->pan_button)) {
    bool held = ui->pan_button != 0;
    ui->pan_button = 0;
    return held;
  }
  int x, y;
  CityInterfacePixels(city->game, input, &x, &y);
  if (ui->pan_button) {
    if (input->kind == CANVAS_SDL_POINTER_MOVE) {
      OffsetCityMapPixels(city, NULL, x - ui->pan_pointer.x, y - ui->pan_pointer.y);
      ui->pan_pointer = (CityPoint){x, y};
    }
    return true;
  }
  if (input->kind != CANVAS_SDL_POINTER_DOWN || input->button != CANVAS_SDL_BUTTON_RIGHT ||
      ui->pointer_stroke_count || ui->pointer_query_capture || !IsCityMainMapInteractiveAt(city, input->x, input->y))
    return false;
  ui->map_view_focus_window = 0;
  ui->pan_button = input->button;
  ui->pan_pointer = (CityPoint){x, y};
  return true;
}
