#include "drawing/display.h"

#include <stdlib.h>

#include "city/engine/city_types.h"
#include "city/state.h"
#include "drawing/maps/sprite_cache.h"
#include "drawing/maps/map_cache.h"
#include "drawing/maps/map_animation.h"
#include "drawing/maps/map_effects.h"
#include "drawing/maps/map_events.h"
#include "drawing/maps/map_labels.h"
#include "drawing/maps/map_render.h"
#include "drawing/maps/map_view.h"
#include "drawing/sprites/surface.h"
#include "drawing/state.h"
#include "game/state.h"

// The target is already bound. rect selects logical map pixels; clip limits physical framebuffer writes.
static void PaintCityMapFragment(CityRect rect, CityRect clip, CityMapPaint *paint);

bool PrepareDrawingFramebuffer(Drawing *drawing, int width, int height) {
  if (width < 1 || height < 1 || (size_t)width > SIZE_MAX / height / sizeof(u32)) return false;
  if (drawing->framebuffer.width != width || drawing->framebuffer.height != height) {
    // Resize storage only at display changes. The other color plane is scroll scratch.
    size_t count = (size_t)width * height;
    u32 *color = calloc(count, sizeof(u32)), *scratch = malloc(count * sizeof(u32));
    u8 *indices = calloc(count, 1), *scratch_indices = malloc(count);
    u32 *rows = calloc(height, 3 * sizeof(*rows));
    if (!color || !scratch || !indices || !scratch_indices || !rows) {
      free(color); free(scratch); free(indices); free(scratch_indices); free(rows);
      return false;
    }
    free(drawing->framebuffer.pixels); free(drawing->scroll_colors); free(drawing->screen_indices);
    free(drawing->scroll_indices); free(drawing->animation_rows);
    drawing->scroll_colors = scratch; drawing->screen_indices = indices;
    drawing->scroll_indices = scratch_indices; drawing->animation_rows = rows;
    drawing->animation_composite_rows = rows + height;
    drawing->animation_composite_bounds = rows + 2 * height;
    drawing->framebuffer = (Canvas){.pixels = color, .width = width, .height = height, .stride = width,
        .clip = {0, 0, width, height}, .palette_indices = indices, .index_stride = width, .palette_rows = rows,
        .palette_composite_rows = rows + height, .palette_composite_bounds = rows + 2 * height};
    drawing->main_cache.valid = false;
    drawing->placement_painted_bounds = (CityRect){0};
    for (int i = 0; i < CITY_EXTRA_MAP_VIEW_COUNT; ++i) drawing->views[i].cache.valid = false;
    ResizeScreenDamage(&drawing->screen_damage, width, height);
    ResizeScreenDamage(&drawing->upload_damage, width, height);
    MarkScreenDirty(&drawing->screen_damage, (CityRect){0, 0, width, height});
  }
  drawing->map_width = (width + drawing->map_pixel_scale - 1) / drawing->map_pixel_scale;
  drawing->map_height = (height + drawing->map_pixel_scale - 1) / drawing->map_pixel_scale;
  drawing->framebuffer.draw_stats = drawing->show_render_stats ? &drawing->frame_stats.calls : NULL;
  drawing->screen_surface = (SpriteSurface){.canvas = drawing->framebuffer, .indices = drawing->screen_indices,
      .index_stride = width, .palette = drawing->colors, .row_flags = drawing->animation_rows,
      .animation_palette = drawing->animation_palette, .upload_damage = &drawing->upload_damage};
  return true;
}

void ReleaseDrawingFramebuffer(Drawing *drawing) {
  free(drawing->framebuffer.pixels);
  free(drawing->screen_indices); free(drawing->scroll_indices); free(drawing->scroll_colors);
  free(drawing->animation_rows);
  drawing->framebuffer = (Canvas){0};
  drawing->screen_surface = drawing->map_target = (SpriteSurface){0};
  drawing->screen_indices = drawing->scroll_indices = NULL;
  drawing->scroll_colors = NULL;
  drawing->animation_rows = drawing->animation_composite_rows = drawing->animation_composite_bounds = NULL;
  drawing->map_width = drawing->map_height = 0;
  drawing->placement_painted_bounds = (CityRect){0};
  drawing->main_cache.valid = false;
  for (int i = 0; i < CITY_EXTRA_MAP_VIEW_COUNT; ++i) drawing->views[i].cache.valid = false;
}

bool WriteDrawingScreenshot(const Drawing *drawing, FILE *file, bool physical) {
  const Canvas *canvas = &drawing->framebuffer;
  int step = physical ? 1 : drawing->map_pixel_scale;
  // Include a final partial logical pixel, but sample only within the physical framebuffer.
  int output_width = (canvas->width - 1) / step + 1;
  int output_height = (canvas->height - 1) / step + 1;
  bool ok = fprintf(file, "P6\n%d %d\n255\n", output_width, output_height) > 0;
  u8 rgb[256 * 3];
  for (int y = 0; ok && y < output_height; ++y) {
    const u32 *row = canvas->pixels + (size_t)y * step * canvas->stride;
    for (int first = 0; ok && first < output_width;) {
      int count = IntMin(output_width - first, sizeof(rgb) / 3);
      for (int x = 0; x < count; ++x) {
        u32 color = row[(first + x) * step];
        rgb[3 * x] = color >> 16;
        rgb[3 * x + 1] = color >> 8;
        rgb[3 * x + 2] = color;
      }
      size_t bytes = count * 3;
      ok = fwrite(rgb, 1, bytes, file) == bytes;
      first += count;
    }
  }
  return ok;
}

bool BindDrawingMapTarget(Drawing *drawing, int scale, CityPoint position) {
  int slot = FindMapSpriteScale(&drawing->sprite_cache, scale);
  if (slot < 0) return false;
  drawing->map_target = drawing->screen_surface;
  // Alpha-blended map pixels keep their rendered RGB until an ordinary repaint.
  // Only opaque indexed pixels participate in palette animation.
  drawing->map_target.canvas.palette_composite_rows = NULL;
  drawing->map_target.canvas.palette_composite_bounds = NULL;
  drawing->target_scale = scale; drawing->target_cache = slot;
  drawing->target_x = position.x; drawing->target_y = position.y;
  return true;
}

MapCachePixels CityMapCachePixels(Drawing *drawing) {
  return (MapCachePixels){.surface = &drawing->map_target,
      .scratch_colors = drawing->scroll_colors, .scratch_indices = drawing->scroll_indices,
      .scale = drawing->target_scale, .x = drawing->target_x, .y = drawing->target_y,
      .physical_coordinates = true};
}

void PaintDrawingMapPixels(CityRect rect, CityMapPaint *paint) {
  const City *city = paint->city;
  Drawing *drawing = city->drawing;
  int scale = drawing->target_scale, x = drawing->target_x, y = drawing->target_y;
  int left = (rect.left - x) * 100, top = (rect.top - y) * 100;
  int right = (rect.right - x) * 100, bottom = (rect.bottom - y) * 100;
  CityRect source = {left / scale - (left % scale < 0), top / scale - (top % scale < 0),
      right / scale + (right % scale > 0), bottom / scale + (bottom % scale > 0)};
  MarkScreenDirty(&drawing->screen_damage, rect);
  PaintCityMapFragment(source, rect, paint);
}

void PrepareCityMapSpriteDamage(CityMapPaint *paint, CityCanvasVisiblePaint mark) {
  const City *city = paint->city;
  Drawing *drawing = city->drawing;
  MapSpriteFootprints *sprites = paint->view ? &paint->view->sprites : &drawing->main_sprites;
  PublishCityMapEventDamage(city, sprites, mark, paint);
  CityMapAnimationFootprints *previous = paint->view ? &paint->view->animations : &drawing->main_animations;
  BeginCityMapAnimationCapture(drawing);
  // The empty paint rectangle prevents rasterization. The normal scan rectangle
  // selects nearby effects in painter order without visiting the whole map.
  CityRect clip = {drawing->target_x, drawing->target_y, drawing->target_x, drawing->target_y};
  PaintCityMapFragment((CityRect){0}, clip, paint);
  EndCityMapAnimationCapture(drawing);
  PublishCityMapAnimationDamage(city, previous, mark, paint);
}

static void PaintCityMapFragment(CityRect rect, CityRect clip, CityMapPaint *paint) {
  const City *city = paint->city;
  Drawing *drawing = city->drawing;
  int background = drawing->options.black_background ? 0 : 0x7B;
  drawing->map_target.canvas.clip = clip;
  MarkScreenDirty(&drawing->upload_damage, drawing->map_target.canvas.clip);
  if (city->game->debug_city) {
    const u32 debug_background[] = {0xFFFF00FF};
    SpriteSurface surface = drawing->map_target;
    surface.palette = debug_background;
    FillSpriteSurface(&surface, surface.canvas.clip, 0);
  } else FillSpriteSurface(&drawing->map_target, drawing->map_target.canvas.clip, background);
  i32 magnification = drawing->default_map_view->magnified_large_zoom ? 2 : 1;
  drawing->map_state.scan_clip = paint->view != NULL ?
      (CityRect){0, 0, paint->view->width, paint->view->height} :
      (CityRect){0, 0, drawing->map_width / magnification, drawing->map_height / magnification};
  // Include the partial logical pixels at the physical viewport edges.
  --drawing->map_state.scan_clip.left; --drawing->map_state.scan_clip.top;
  ++drawing->map_state.scan_clip.right; ++drawing->map_state.scan_clip.bottom;
  drawing->map_state.paint_rect = rect;
  drawing->paint_placement = !paint->view;
  drawing->sign_count = 0;
  SetCityMapLabelStyle(drawing, true);
  u64 regions = drawing->frame_stats.repaint_regions;
  if (drawing->map_state.data_overlay_enabled != 0 && drawing->map_state.data_mode != CITY_DATA_MODE_NONE)
    DrawCityMapData(city, ((const CityMapViewConfig *)drawing->default_map_view)->zoom);
  else if (drawing->map_state.events_suppressed != 0)
    DrawCityMapUnderground(city, ((const CityMapViewConfig *)drawing->default_map_view)->zoom);
  else
    DrawCityMapSurface(city, drawing->default_map_view, drawing->default_map_view->zoom);
  // Depth sorting counts its final subdivisions. Direct overlay/topography
  // paints still repaint one map region even though they do not sort sprites.
  if (drawing->frame_stats.calls.active && drawing->animation.mode != ANIMATION_CAPTURE &&
      drawing->frame_stats.repaint_regions == regions)
    ++drawing->frame_stats.repaint_regions;
  drawing->paint_placement = false;
  DrawCapturedCitySigns(city);
  DrawCityDemolitionEffects(city);
  DrawCityCostTexts(city);
  SetCityMapLabelStyle(drawing, false);
}

// Merge moving-preview fragments only when the added physical pixel work
// stays below both the relative limit and the fixed 64-by-64 pixel allowance.
void MarkCityPlacementDamage(Drawing *drawing, CityRect previous, CityRect current) {
  enum { kMaximumExtraPixels = 4096 };
  CityRect screen = {0, 0, drawing->framebuffer.width, drawing->framebuffer.height};
  previous = IntersectCityRects(previous, screen);
  current = IntersectCityRects(current, screen);
  if (previous.left < previous.right && previous.top < previous.bottom &&
      current.left < current.right && current.top < current.bottom) {
    CityRect combined = UnionCityRects(previous, current), overlap = IntersectCityRects(previous, current);
    i64 pixels = (i64)(previous.right - previous.left) * (previous.bottom - previous.top) +
        (i64)(current.right - current.left) * (current.bottom - current.top) -
        (i64)IntMax(0, overlap.right - overlap.left) * IntMax(0, overlap.bottom - overlap.top);
    i64 joined = (i64)(combined.right - combined.left) * (combined.bottom - combined.top);
    if (joined * 4 <= pixels * 5 && joined - pixels <= kMaximumExtraPixels) {
      MarkScreenDirty(&drawing->screen_damage, combined);
      return;
    }
  }
  MarkScreenDirty(&drawing->screen_damage, previous);
  MarkScreenDirty(&drawing->screen_damage, current);
}
