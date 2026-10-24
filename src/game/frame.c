#include "game/frame.h"

#include <string.h>

#include "assets/state.h"
#include "base/time.h"
#include "city/state.h"
#include "drawing/display.h"
#include "drawing/frame_overlay.h"
#include "drawing/maps/cost_labels.h"
#include "drawing/maps/map_effects.h"
#include "drawing/maps/map_vehicles.h"
#include "drawing/maps/map_view.h"
#include "drawing/maps/road_cars.h"
#include "drawing/state.h"
#include "game/application.h"
#include "game/errors.h"
#include "game/graphics.h"
#include "game/palette.h"
#include "game/state.h"
#include "game/timing.h"
#include "ui/cursor/cursor.h"
#include "ui/dialogs/game_options.h"
#include "ui/map_views.h"
#include "ui/sdl.h"
#include "ui/state.h"
#include "ui/tools/placement.h"
#include "ui/tools/tools.h"
#include "ui/updates.h"
#include "ui/windows/windows.h"

// Game sequences presentation updates before painting with a scoped City copy.
// Paint-time scalar changes stay in that copy; Drawing retains pixels and caches.

static bool PrepareCityCanvasFrame(City *city, CanvasSDL *display) {
  Drawing *drawing = city->drawing;
  CityInterfaceState *ui = city->game->ui;
  if (!display) display = ui->display;
  // Failed loaders may leave partial stock records. Draw only the desktop
  // until a successful load or a new terrain session activates the City.
  if (!city->game->city_active) {
    SyncCityInterface(city->game, display, 1);
    if (!PrepareCityScreenBuffer(city->game, display)) return false;
    ComposeCityStartupFrame(city);
    return true;
  }
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
  CityMapPaint paint_context = {.city = &paint_city};
  if (!PrepareCityMapSprites(city->game) || !BindCityMapTarget(&paint_context)) return false;
  double snapshotted = (drawing->benchmark_redraw || drawing->profile_redraw) ? MonotonicSeconds() : 0;
  ClearGameError(city->game);
  if (drawing->disable_map_cache) drawing->main_cache.valid = 0;
  PrepareCityMapSpriteDamage(&paint_context, MarkCityMapSpriteDamage);
  PrepareCityMapEffectDamage(&paint_context,
      (CityRect){0, 0, drawing->framebuffer.width, drawing->framebuffer.height});
  UpdateCityPlacementPreview(city);
  // The main target is bound here, including any screen-shake offset.
  const NativeMapCache *cache = &drawing->main_cache;
  if (cache->valid &&
      (cache->origin_x != drawing->map_state.origin_x || cache->origin_y != drawing->map_state.origin_y ||
          cache->target_x != drawing->target_x || cache->target_y != drawing->target_y))
    InvalidateCityMapRect(drawing, drawing->default_map_view, drawing->map_state.zone_cost_label_rect);
  PublishCityPlacementDamage(city);
  if (!RenderCityMapViews(&paint_city)) return false;
  double mapped = (drawing->benchmark_redraw || drawing->profile_redraw) ? MonotonicSeconds() : 0;
  ComposeCityDesktopFrame(city);
  if (drawing->benchmark_redraw || drawing->profile_redraw) {
    ++drawing->profiled_redraws;
    drawing->snapshot_seconds += snapshotted - started;
    drawing->map_seconds += mapped - snapshotted;
    drawing->canvas_seconds += MonotonicSeconds() - mapped;
  }
  return true;
}

bool PrepareCityCanvas(City *city, CanvasSDL *display) {
  Drawing *drawing = city->drawing;
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
  Drawing *drawing = city->drawing;
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
  Drawing *drawing = city->drawing;
  const CityInterfaceState *ui = city->game->ui;
  const CityCanvasDesktop *desktop = &ui->desktop;
  const NativeMapCache *cache = &drawing->main_cache;
  if (!city->game->city_active)
    return cache->valid || desktop->dirty.count || desktop->window_damage.count ||
        drawing->screen_damage.bounds.left < drawing->screen_damage.bounds.right;
  PublishCityMapChanges(city);
  // Compare pending scroll before preparation rebinds the main target.
  return ui->placement_dirty || desktop->dirty.count || desktop->window_damage.count ||
      !cache->valid || cache->origin_x != drawing->map_state.origin_x ||
      cache->origin_y != drawing->map_state.origin_y ||
      cache->target_x != drawing->scroll_pixels.x || cache->target_y != drawing->scroll_pixels.y ||
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
