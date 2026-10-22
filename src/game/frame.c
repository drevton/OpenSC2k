#include "game/frame.h"

#include <stdio.h>
#include <string.h>

#include "assets/state.h"
#include "base/time.h"
#include "city/state.h"
#include "drawing/display.h"
#include "drawing/maps/cost_labels.h"
#include "drawing/maps/map_cache.h"
#include "drawing/maps/map_animation.h"
#include "drawing/maps/map_effects.h"
#include "drawing/maps/map_events.h"
#include "drawing/maps/map_render.h"
#include "drawing/state.h"
#include "game/application.h"
#include "game/errors.h"
#include "game/graphics.h"
#include "game/palette.h"
#include "game/state.h"
#include "game/timing.h"
#include "ui/updates.h"
#include "ui/map_visibility.h"
#include "ui/cursor/cursor.h"
#include "ui/dialogs/dialogs.h"
#include "ui/dialogs/game_options.h"
#include "ui/map_views.h"
#include "ui/sdl.h"
#include "ui/startup/startup.h"
#include "ui/state.h"
#include "ui/tools/tools.h"
#include "ui/windows/tooltips.h"
#include "ui/windows/windows.h"

// Game sequences presentation updates before painting with a scoped City copy.
// Paint-time scalar changes stay in that copy; Drawing retains pixels and caches.

static bool PreserveCityViewportScrollSource(City *city) {
  Drawing *drawing = city->game->drawing;
  CityCanvasDesktop *desktop = &city->game->ui->desktop;
  for (int index = 0; index < CITY_EXTRA_MAP_VIEW_COUNT; ++index) {
    CityExtraMapView *view = &drawing->views[index];
    if (!view->cache.valid) continue;
    const Window *window = FindCityCanvasWindow(desktop, view->window_id);
    if (!window || !window->visible || window->shaded) continue;
    CityRect bounds = Sc2kCanvasDesktopPixelBounds(desktop, view->window_id);
    int scale = drawing->options.scale;
    int x = bounds.left + (3 * scale + 99) / 100 + view->scroll_pixels.x;
    int y = bounds.top + (CITY_VIEWPORT_TOP * scale + 99) / 100 + view->scroll_pixels.y;
    if (view->cache.target_x == x && view->cache.target_y == y &&
        view->cache.origin_x == view->origin_x && view->cache.origin_y == view->origin_y) continue;
    // All cameras share framebuffer storage. Preserve old viewport pixels before
    // the main map or another viewport repaints the source exposed by a drag.
    MapCachePixels pixels = CityMapCachePixels(drawing);
    SaveMapCacheScrollSource(&pixels);
    return true;
  }
  return false;
}

static void PaintWindowsScaled(Canvas *canvas, void *context) {
  City *city = context;
  DrawCityDesktopUI(city->game, &city->game->ui->desktop, canvas);
}

static void CompositeCityScreenDamage(CityRect rect, void *context) {
  City *city = context;
  Drawing *drawing = city->game->drawing;
  drawing->framebuffer.clip = rect;
  MarkScreenDirty(&drawing->upload_damage, rect);
  Sc2kCanvasPaintScaled(&drawing->framebuffer, drawing->options.scale, 100, PaintWindowsScaled, city);
  drawing->framebuffer.clip = (CityRect){0, 0, drawing->framebuffer.width, drawing->framebuffer.height};
}

static void PaintInactiveCityScreen(CityRect rect, void *context) {
  City *city = context;
  Drawing *drawing = city->game->drawing;
  Canvas canvas = drawing->framebuffer;
  canvas.clip = rect;
  DrawCityStartupBackdrop(city->game, &canvas, rect);
  CompositeCityScreenDamage(rect, context);
}

static bool PrepareInactiveCityCanvas(City *city, CanvasSDL *display) {
  Drawing *drawing = city->game->drawing;
  CityCanvasDesktop *desktop = &city->game->ui->desktop;
  SyncCityInterface(city->game, display, 1);
  if (!PrepareCityScreenBuffer(city->game, display)) return false;
  if (drawing->main_cache.valid)
    MarkScreenDirty(&drawing->screen_damage, drawing->framebuffer.clip);
  drawing->main_cache.valid = false;
  drawing->map_target.canvas.clip = (CityRect){0};
  drawing->placement_painted_bounds = (CityRect){0};
  desktop->placement.count = 0;
  for (int index = 0; index < desktop->dirty.count; ++index) {
    MarkScreenDirty(&drawing->screen_damage, CityCanvasDirtyPixelBounds(desktop, desktop->dirty.rects[index]));
  }
  for (int index = 0; index < desktop->window_damage.count; ++index)
    MarkScreenDirty(&drawing->screen_damage, desktop->window_damage.rects[index]);
  VisitScreenDamage(&drawing->screen_damage, PaintInactiveCityScreen, city);
  if (drawing->show_redraw_rects) drawing->world_damage = drawing->screen_damage;
  else ClearScreenDamage(&drawing->world_damage);
  ClearScreenDamage(&drawing->screen_damage);
  Sc2kDirtyClear(&desktop->dirty);
  Sc2kDirtyClear(&desktop->window_damage);
  city->game->ui->placement_dirty = false;
  return true;
}

static bool PrepareCityCanvasFrame(City *city, CanvasSDL *display) {
  Drawing *drawing = city->game->drawing;
  CityInterfaceState *ui = city->game->ui;
  if (!display) display = ui->display;
  // Failed loaders may leave partial stock records. Draw only the desktop
  // until a successful load or a new terrain session activates the City.
  if (!city->game->city_active) return PrepareInactiveCityCanvas(city, display);
  PrepareCityInformation(city->game);
  SampleGameTime(city->game, MonotonicSeconds());
  double ui_time = ReadGameTime(city->game, GAME_TIME_REALTIME);
  double world_time = ReadGameTime(city->game, GAME_TIME_UNPAUSED);
  UpdateCityVehicleAnimation(city, world_time);
  UpdateCityRoadTraffic(city, (u32)(u64)(world_time * 256));
  UpdateCityPowerAnimation(drawing, city->settings.speed, world_time);
  drawing->power_smoke_visible = drawing->wind_power_visible = false;
  UpdateCityCostTexts(drawing, ui_time);
  UpdateCityDemolitionEffects(drawing, ui_time);
  SyncCityInterface(city->game, display, 1);
  if (!PrepareCityScreenBuffer(city->game, display)) return false;
  AnimateCityCanvasPalette(city->game, world_time);
  UpdateCityToolStatus(city);
  UpdateCityToolbar(city->game);
  PublishCityMapChanges(city);
  double started = (drawing->benchmark_redraw || drawing->profile_redraw) ? MonotonicSeconds() : 0;
  City paint_city = *city;
  paint_city.map_changes.enabled = false;
  CityMapPaint paint_context = {&paint_city, NULL};
  if (!PrepareCityMapSprites(city->game) || !BindCityMapTarget(&paint_context)) return false;
  double snapshotted = (drawing->benchmark_redraw || drawing->profile_redraw) ? MonotonicSeconds() : 0;
  ClearGameError(city->game);
  if (drawing->disable_map_cache) drawing->main_cache.valid = 0;
  PrepareCityMapSpriteDamage(&paint_context, MarkCityMapSpriteDamage);
  PrepareCityMapEffectDamage(&paint_context,
      (CityRect){0, 0, drawing->framebuffer.width, drawing->framebuffer.height});
  UpdateCityPlacementPreview(city);
  if (drawing->main_cache.valid &&
      (drawing->main_cache.origin_x != drawing->map_state.origin_x ||
          drawing->main_cache.origin_y != drawing->map_state.origin_y ||
          drawing->main_cache.target_x != drawing->target_x || drawing->main_cache.target_y != drawing->target_y))
    InvalidateCityMapRect(drawing, drawing->default_map_view, drawing->map_state.zone_cost_label_rect);
  // Ignore intermediate hover positions that never reached the framebuffer.
  // Only the last painted preview and the final pointer position need damage.
  CityRect previous = drawing->placement_painted_bounds;
  if (ui->placement_dirty) {
    CityRect bounds = ui->desktop.placement.bounds;
    int scale = drawing->map_pixel_scale;
    CityRect current = ui->desktop.placement.count ?
        (CityRect){bounds.left * scale + drawing->scroll_pixels.x, bounds.top * scale + drawing->scroll_pixels.y,
            bounds.right * scale + drawing->scroll_pixels.x, bounds.bottom * scale + drawing->scroll_pixels.y} :
        (CityRect){0};
    MarkCityPlacementDamage(drawing, previous, current);
    if (drawing->main_cache.valid && previous.left < previous.right && previous.top < previous.bottom) {
      int magnification = drawing->default_map_view->magnified_large_zoom ? 2 : 1;
      int dx = (drawing->map_state.origin_x - drawing->main_cache.origin_x) * scale * magnification;
      int dy = (drawing->map_state.origin_y - drawing->main_cache.origin_y) * scale * magnification;
      dx += drawing->target_x - drawing->main_cache.target_x;
      dy += drawing->target_y - drawing->main_cache.target_y;
      if (dx || dy) MarkCityPlacementDamage(drawing,
          (CityRect){previous.left + dx, previous.top + dy, previous.right + dx, previous.bottom + dy}, current);
    }
  }
  // Sprite footprints come from native render scratch.
  drawing->world_damage = drawing->screen_damage;
  // Requests are preserved for every camera. From here, screen_damage records
  // painted pixels for F8; scroll copies only contribute to upload_damage.
  ClearScreenDamage(&drawing->screen_damage);
  i32 magnification = drawing->default_map_view->magnified_large_zoom ? 2 : 1;
  bool retained = drawing->main_cache.valid;
  bool preserved_source = PreserveCityViewportScrollSource(city);
  MapCachePixels pixels = CityMapCachePixels(drawing);
  pixels.preserved_source = preserved_source;
  DrawCityMapCache(&drawing->main_cache, &ui->desktop, 0,
      drawing->framebuffer.width, drawing->framebuffer.height,
      magnification, drawing->map_state.origin_x, drawing->map_state.origin_y,
      &pixels, PaintCityMapPixels, &paint_context);
  if (retained) VisitScreenRepaintDamage(&drawing->world_damage, RepaintCityMapDamage, &paint_context,
      !drawing->disable_repaint_coalescing);
  FinishCityMapAnimations(&paint_city, drawing);
  i32 error = GetGameError(city->game);
  bool rendered = RenderCityMapViews(&paint_city, preserved_source);
  // Between frames, command-time paint requests must not overwrite retained pixels.
  drawing->map_target.canvas.clip = (CityRect){0};
  if (!rendered) return false;
  double mapped = (drawing->benchmark_redraw || drawing->profile_redraw) ? MonotonicSeconds() : 0;
  if (error) {
    fprintf(stderr, "Map drawing failed: reconstruction error %d\n", error);
    return false;
  }
  Sc2kCanvasSetMapZoom(&ui->desktop, drawing->default_map_view->zoom +
      (drawing->default_map_view->magnified_large_zoom != 0));
  ClearScreenDamage(&drawing->world_damage);
  for (int index = 0; index < ui->desktop.dirty.count; ++index) {
    CityRect rect = CityCanvasDirtyPixelBounds(&ui->desktop, ui->desktop.dirty.rects[index]);
    MarkScreenDirty(&drawing->screen_damage, rect);
    MarkScreenDirty(&drawing->world_damage, rect);
  }
  for (int index = 0; index < ui->desktop.window_damage.count; ++index) {
    MarkScreenDirty(&drawing->screen_damage, ui->desktop.window_damage.rects[index]);
    MarkScreenDirty(&drawing->world_damage, ui->desktop.window_damage.rects[index]);
  }
  VisitScreenDamage(&drawing->world_damage, CompositeCityScreenDamage, city);
  drawing->framebuffer.clip = (CityRect){0, 0, drawing->framebuffer.width, drawing->framebuffer.height};
  CityRect placement = ui->desktop.placement.bounds;
  int scale = drawing->map_pixel_scale;
  drawing->placement_painted_bounds = ui->desktop.placement.count ?
      (CityRect){placement.left * scale + drawing->scroll_pixels.x, placement.top * scale + drawing->scroll_pixels.y,
          placement.right * scale + drawing->scroll_pixels.x, placement.bottom * scale + drawing->scroll_pixels.y} :
      (CityRect){0};
  if (drawing->show_redraw_rects) drawing->world_damage = drawing->screen_damage;
  else ClearScreenDamage(&drawing->world_damage);
  ClearScreenDamage(&drawing->screen_damage);
  Sc2kDirtyClear(&ui->desktop.dirty);
  Sc2kDirtyClear(&ui->desktop.window_damage);
  ui->placement_dirty = 0;
  if (drawing->benchmark_redraw || drawing->profile_redraw) {
    ++drawing->profiled_redraws;
    drawing->snapshot_seconds += snapshotted - started;
    drawing->map_seconds += mapped - snapshotted;
    drawing->canvas_seconds += MonotonicSeconds() - mapped;
  }
  return true;
}

bool PrepareCityCanvas(City *city, CanvasSDL *display) {
  Drawing *drawing = city->game->drawing;
  bool measure = drawing->show_render_stats;
  DrawingFrameStats previous = drawing->frame_stats;
  drawing->frame_stats = (DrawingFrameStats){.calls.active = measure, .pending = measure};
  double started = measure ? MonotonicSeconds() : 0;
  bool result = PrepareCityCanvasFrame(city, display);
  if (measure) drawing->frame_stats.seconds = MonotonicSeconds() - started;
  drawing->frame_stats.calls.active = false;
  // Keep the last map repaint visible through input-only, UI and palette frames.
  // Replacing it with zero map work hides one-shot measurements such as F6.
  if (measure && city->game->city_active && !drawing->frame_stats.repaint_regions)
    drawing->frame_stats = previous;
  return result;
}

int PresentCityDesktop(City *city, CanvasSDL *display) {
  UpdateCityToolCursor(city);
  Drawing *drawing = city->game->drawing;
  DrawingFrameOverlay overlay = BeginDrawingFrameOverlay(drawing, &city->game->resources->interface_font);
  int result = Sc2kSDLPresentFramebuffer(display, &drawing->framebuffer, &drawing->upload_damage, overlay.upload);
  EndDrawingFrameOverlay(drawing, &overlay, result == 0);
  return result;
}

bool RedrawCityCanvas(City *city, CanvasSDL *display) {
  if (!display) display = city->game->ui->display;
  return PrepareCityCanvas(city, display) && (!display || PresentCityDesktop(city, display) == 0);
}

// Publish explicit invalidations before deciding whether retained pixels need work.
bool PrepareCityCanvasDamage(City *city) {
  PrepareCityInformation(city->game);
  Drawing *drawing = city->game->drawing;
  CityInterfaceState *ui = city->game->ui;
  if (!city->game->city_active)
    return drawing->main_cache.valid || ui->desktop.dirty.count || ui->desktop.window_damage.count ||
        drawing->screen_damage.bounds.left < drawing->screen_damage.bounds.right;
  PublishCityMapChanges(city);
  return ui->placement_dirty || ui->desktop.dirty.count || ui->desktop.window_damage.count ||
      !drawing->main_cache.valid || drawing->main_cache.origin_x != drawing->map_state.origin_x ||
      drawing->main_cache.origin_y != drawing->map_state.origin_y ||
      drawing->main_cache.target_x != drawing->scroll_pixels.x ||
      drawing->main_cache.target_y != drawing->scroll_pixels.y ||
      drawing->screen_damage.bounds.left < drawing->screen_damage.bounds.right ||
      memcmp(drawing->resolved_palette, drawing->colors, sizeof(drawing->colors)) != 0;
}

bool ApplicationPresentMap(Game *game) {
  City *city = game->city;
  Drawing *drawing = game->drawing;
  if (!drawing->framebuffer.pixels)
    return true;
  if (city->settings.speed == CITY_SPEED_PAUSED && game->idle_state == (u32)CITY_IDLE_PLAY &&
      !PrepareCityCanvasDamage(city))
    return true;
  // Publish dirty city tiles and compose all visible map views at this exact
  // animation boundary, before its caller pumps timers or starts another frame.
  if (!RedrawCityCanvas(city, game->ui->display)) {
    game->execution_failed = true;
    return false;
  }
  return true;
}
