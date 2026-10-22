#include "drawing/display.h"

#include <inttypes.h>
#include <stdlib.h>

#include "assets/state.h"
#include "city/engine/city_types.h"
#include "city/state.h"
#include "drawing/bitmaps.h"
#include "drawing/maps/map_cache.h"
#include "drawing/maps/map_animation.h"
#include "drawing/maps/map_effects.h"
#include "drawing/maps/map_events.h"
#include "drawing/maps/map_render.h"
#include "drawing/maps/map_view.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/raster/canvas_raster_internal.h"
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

bool InstallDrawingMapSpriteSources(Drawing *drawing, const CityAssets *assets) {
  FreeMapSpriteCache(&drawing->sprite_cache);
  drawing->sprite_cache.assets = assets;
  for (int id = 0; id < CITY_MAP_SPRITE_COUNT; ++id) {
    int slot = id % CITY_MAP_SPRITES_PER_ZOOM;
    int resource = slot < assets->map_tile_sprite_count ? TileSpriteResourceIndex(assets, slot, 0) : slot;
    const CityLandscapeSprite *landscape = GetResourceSprite(assets, resource);
    if (landscape && landscape->sprite) {
      if (!InstallResourceSprite(&drawing->sprite_cache, assets, id, resource,
          1 << (2 - id / CITY_MAP_SPRITES_PER_ZOOM))) return false;
    }
  }
  for (int id = CITY_LANDSCAPE_SELECTION_FIRST; id < CITY_LANDSCAPE_SELECTION_END; ++id) {
    const CityLandscapeSprite *selection = &assets->landscape[id];
    if (!InstallLandscapeSprite(&drawing->sprite_cache,
        MAP_SELECTION_SPRITE_FIRST + id - CITY_LANDSCAPE_SELECTION_FIRST, selection->sprite,
        selection->scale, selection->baseline, 0, false)) return false;
  }
  for (int zoom = 0; zoom < 3; ++zoom) {
    for (int slope = 0; slope < MAP_SELECTION_SPRITE_SHAPES; ++slope) {
      const CityLandscapeSprite *water = &assets->landscape[CITY_LANDSCAPE_SLOPED_WATER_FIRST + slope];
      int cached = MAP_SLOPED_WATER_SPRITE_FIRST + zoom * MAP_SELECTION_SPRITE_SHAPES + slope;
      if (!InstallLandscapeSprite(&drawing->sprite_cache, cached, water->sprite,
          water->scale << (2 - zoom), water->baseline, water->offset_x, false)) return false;
    }
    const CityLandscapeSprite *point = &assets->landscape[CITY_LANDSCAPE_POINT + zoom];
    if (!InstallLandscapeSprite(&drawing->sprite_cache, MAP_POINT_SPRITE_FIRST + zoom, point->sprite,
        point->scale, point->baseline, point->offset_x, false)) return false;
    for (int id = CITY_LANDSCAPE_GROUND_FIRST; id < CITY_LANDSCAPE_POINT; ++id) {
      int cached = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM +
          id - CITY_LANDSCAPE_GROUND_FIRST;
      if (!InstallResourceSprite(&drawing->sprite_cache, assets, cached, id, 1 << (2 - zoom))) return false;
    }
    for (int id = CITY_LANDSCAPE_RIVER_SIDE_FIRST; id < CITY_LANDSCAPE_SPRITE_COUNT; ++id) {
      int resource = LandscapeSpriteResourceIndex(assets, id);
      const CityLandscapeSprite *bank = GetResourceSprite(assets, resource);
      if (!bank || !bank->sprite) continue;
      int cached = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM +
          id - CITY_LANDSCAPE_GROUND_FIRST;
      if (!InstallResourceSprite(&drawing->sprite_cache, assets, cached, resource, 1 << (2 - zoom))) return false;
    }
  }
  for (int frame = 0; frame < CITY_DEMOLITION_FRAME_COUNT; ++frame) {
    const CityLandscapeSprite *effect = &assets->demolition[frame];
    if (!InstallLandscapeSprite(&drawing->sprite_cache,
        MAP_DEMOLITION_SPRITE_FIRST + 2 * CITY_DEMOLITION_FRAME_COUNT + frame, effect->sprite,
        effect->scale, effect->baseline, effect->offset_x, false)) return false;
  }
  drawing->sprite_revision = assets->sprite_revision;
  InvalidateCityMapViews(drawing);
  return true;
}

int CityMapSpriteScales(const Drawing *drawing, int scales[2]) {
  int scale = drawing->map_pixel_scale * 100;
  int maximum = CITY_MAP_MAX_TILE_WIDTH * 100 / (8 << CITY_MAP_ZOOM_LARGE);
  scales[0] = scale;
  if (scale * 2 > maximum) return 1;
  scales[1] = scale * 2;
  return 2;
}

bool PrepareDrawingMapSprites(Drawing *drawing) {
  // Every ordinary zoom, magnified zoom and auxiliary viewport is ready
  // before drawing. Only a display pixel-density change replaces this set.
  drawing->sprite_cache.pixel_scale = drawing->map_pixel_scale * 100;
  int scales[2], count = CityMapSpriteScales(drawing, scales);
  bool missing = false;
  for (int i = 0; i < count; ++i)
    if (FindMapSpriteScale(&drawing->sprite_cache, scales[i]) < 0) missing = true;
  if (missing) {
    FreeMapSpriteScales(&drawing->sprite_cache);
    for (int i = 0; i < count; ++i)
      if (PrepareMapSpriteScale(&drawing->sprite_cache, scales[i], drawing->colors) < 0)
        return false;
  }
  return PrepareMapBuildingPreviews(&drawing->sprite_cache,
      (CITY_BUILDING_PREVIEW_WIDTH * drawing->options.scale + 99) / 100,
      (CITY_BUILDING_PREVIEW_HEIGHT * drawing->options.scale + 99) / 100, drawing->colors);
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
  return (MapCachePixels){&drawing->map_target, drawing->scroll_colors, drawing->scroll_indices,
      drawing->target_scale, drawing->target_x, drawing->target_y, true};
}

void PaintDrawingMapPixels(CityRect rect, CityMapPaint *paint) {
  City *city = paint->city;
  Drawing *drawing = city->game->drawing;
  int scale = drawing->target_scale, x = drawing->target_x, y = drawing->target_y;
  int left = (rect.left - x) * 100, top = (rect.top - y) * 100;
  int right = (rect.right - x) * 100, bottom = (rect.bottom - y) * 100;
  CityRect source = {left / scale - (left % scale < 0), top / scale - (top % scale < 0),
      right / scale + (right % scale > 0), bottom / scale + (bottom % scale > 0)};
  MarkScreenDirty(&drawing->screen_damage, rect);
  PaintCityMapFragment(source, rect, paint);
}

void PrepareCityMapSpriteDamage(CityMapPaint *paint, CityCanvasVisiblePaint mark) {
  City *city = paint->city;
  Drawing *drawing = city->game->drawing;
  MapSpriteFootprints *sprites = paint->view ? &paint->view->sprites : &drawing->main_sprites;
  PublishCityMapEventDamage(city, sprites, mark, paint);
  CityMapAnimationFootprints *previous = paint->view ? &paint->view->animations : &drawing->main_animations;
  SimulationRandomState before = city->random;
  BeginCityMapAnimationCapture(drawing);
  // The empty paint rectangle prevents rasterization. The normal scan rectangle
  // selects nearby effects in painter order without visiting the whole map.
  CityRect clip = {drawing->target_x, drawing->target_y, drawing->target_x, drawing->target_y};
  PaintCityMapFragment((CityRect){0}, clip, paint);
  EndCityMapAnimationCapture(city, drawing);
  city->random = before;
  PublishCityMapAnimationDamage(city, drawing->default_map_view, previous, mark, paint);
  ReplayCityMapAnimations(drawing);
}

static void PaintCityMapFragment(CityRect rect, CityRect clip, CityMapPaint *paint) {
  City *city = paint->city;
  Drawing *drawing = city->game->drawing;
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

typedef struct {
  Canvas *canvas;
  u32 *original;
  u32 sequence;
} NativeRedrawTint;

static void TintCityRedrawRect(CityRect rect, void *context) {
  NativeRedrawTint *tint = context;
  Canvas *canvas = tint->canvas;
  // Vary presentation colors without touching the simulation RNG.
  u32 seed = ++tint->sequence * 0x9E3779B9;
  int hue = (seed >> 16) % 1536, ramp = hue & 255;
  u32 color;
  switch (hue >> 8) {
  case 0: color = 0xFF0000 | ramp << 8; break;
  case 1: color = (255 - ramp) << 16 | 0xFF00; break;
  case 2: color = 0xFF00 | ramp; break;
  case 3: color = (255 - ramp) << 8 | 0xFF; break;
  case 4: color = ramp << 16 | 0xFF; break;
  default: color = 0xFF0000 | (255 - ramp); break;
  }
  u32 half_color = (color & 0xFEFEFE) >> 1;
  for (int y = rect.top; y < rect.bottom; ++y) {
    size_t offset = (size_t)y * canvas->stride + rect.left;
    u32 *pixels = canvas->pixels + offset;
    for (int x = 0; x < rect.right - rect.left; ++x)
      pixels[x] = (pixels[x] & 0xFF000000) | (((pixels[x] & 0xFEFEFE) >> 1) + half_color);
  }
}

static void PreserveCityDebugRect(CityRect rect, void *context) {
  const NativeRedrawTint *tint = context;
  for (int y = rect.top; y < rect.bottom; ++y) {
    size_t offset = (size_t)y * tint->canvas->stride + rect.left;
    memcpy(tint->original + offset, tint->canvas->pixels + offset, (rect.right - rect.left) * sizeof(u32));
  }
}

// These pixels exist only during upload; neither debug overlay damages the map cache.
static CityRect CityRenderStatsBounds(const Drawing *drawing, const Canvas *canvas) {
  int scale = drawing->options.scale;
  int width = (260 * scale + 99) / 100, height = (71 * scale + 99) / 100;
  int left = IntMin(8, canvas->width), top = IntMin(40 * scale / 100, canvas->height);
  return IntersectCityRects((CityRect){left, top, left + width, top + height},
      (CityRect){0, 0, canvas->width, canvas->height});
}

static void DrawCityRenderStats(Drawing *drawing, const CityInterfaceFont *font, Canvas *canvas, CityRect bounds) {
  const DrawingFrameStats *stats = &drawing->frame_stats;
  int refresh = drawing->options.refresh_rate > 0 ? drawing->options.refresh_rate : 60;
  double budget = 1000000.0 / refresh, elapsed = stats->seconds * 1000000;
  u64 calls = stats->calls.sprite_calls;
  u64 visits = stats->tile_visits;
  u64 visible = stats->tile_visible_visits;
  u64 regions = stats->repaint_regions;
  Canvas panel = {.pixels = canvas->pixels, .stride = canvas->stride, .width = 260, .height = 71,
      .clip = {0, 0, 260, 71}, .scalable_font = true, .font_antialias = true};
  panel.raster = (CanvasRaster){.pixels = panel.pixels, .numerator = drawing->options.scale, .denominator = 100,
      .width = canvas->width, .height = canvas->height, .clip = bounds,
      .offset_x = bounds.left, .offset_y = bounds.top};
  DrawCityInterfaceTextBold(font, &panel, 2, 3, "F9  Render statistics", 0xFFFFD080, 8);
  char text[128];
  snprintf(text, sizeof(text), "Draw calls: %" PRIu64, calls);
  DrawCityInterfaceTextBold(font, &panel, 2, 15, text, 0xFFE8EEE8, 8);
  snprintf(text, sizeof(text), "Tile visits: %" PRIu64 " / %" PRIu64 " visible", visits, visible);
  DrawCityInterfaceTextBold(font, &panel, 2, 26, text, 0xFFE8EEE8, 8);
  snprintf(text, sizeof(text), "Map regions: %" PRIu64 "  F7 coalesce: %s", regions,
      drawing->disable_repaint_coalescing ? "off" : "on");
  DrawCityInterfaceTextBold(font, &panel, 2, 37, text, 0xFFE8EEE8, 8);
  snprintf(text, sizeof(text), "Draw CPU: %.0f us  (%.1f%%)", elapsed, elapsed * 100 / budget);
  DrawCityInterfaceTextBold(font, &panel, 2, 48, text, elapsed > budget ? 0xFFFF8080 : 0xFFE8EEE8, 8);
  snprintf(text, sizeof(text), "Budget: %.0f us @ %d Hz", budget, refresh);
  DrawCityInterfaceTextBold(font, &panel, 2, 59, text, 0xFFB8C4B8, 8);
}

static void RestoreCityRedrawRect(CityRect rect, void *context) {
  const NativeRedrawTint *tint = context;
  for (int y = rect.top; y < rect.bottom; ++y) {
    size_t offset = (size_t)y * tint->canvas->stride + rect.left;
    memcpy(tint->canvas->pixels + offset, tint->original + offset, (rect.right - rect.left) * sizeof(u32));
  }
}

DrawingFrameOverlay BeginDrawingFrameOverlay(Drawing *drawing, const CityInterfaceFont *font) {
  Canvas canvas = drawing->framebuffer;
  // Scrolling has finished. Preserve the union of both debug overlays in its
  // existing scratch plane before either overlay writes overlapping pixels.
  NativeRedrawTint tint = {&canvas, drawing->scroll_colors, drawing->redraw_tint_sequence};
  CityRect stats = drawing->show_render_stats ? CityRenderStatsBounds(drawing, &canvas) : (CityRect){0};
  if (drawing->show_redraw_rects)
    VisitScreenDamage(&drawing->world_damage, PreserveCityDebugRect, &tint);
  if (drawing->show_render_stats) PreserveCityDebugRect(stats, &tint);
  if (drawing->show_redraw_rects) {
    VisitScreenDamage(&drawing->world_damage, TintCityRedrawRect, &tint);
    drawing->redraw_tint_sequence = tint.sequence;
  }
  if (drawing->show_render_stats) DrawCityRenderStats(drawing, font, &canvas, stats);
  CityRect debug = drawing->show_redraw_rects ? drawing->world_damage.bounds : (CityRect){0};
  if (drawing->show_render_stats)
    debug = debug.left < debug.right && debug.top < debug.bottom ? UnionCityRects(debug, stats) : stats;
  // Current and previous bounds are uploaded even on frames without map damage.
  CityRect restore = drawing->redraw_rects_bounds;
  if (restore.left >= restore.right || restore.top >= restore.bottom) restore = debug;
  else if (debug.left < debug.right && debug.top < debug.bottom) restore = UnionCityRects(restore, debug);
  return (DrawingFrameOverlay){.statistics = stats, .current = debug, .upload = restore};
}

void EndDrawingFrameOverlay(Drawing *drawing, const DrawingFrameOverlay *overlay, bool uploaded) {
  Canvas canvas = drawing->framebuffer;
  NativeRedrawTint tint = {.canvas = &canvas, .original = drawing->scroll_colors};
  if (drawing->show_redraw_rects) VisitScreenDamage(&drawing->world_damage, RestoreCityRedrawRect, &tint);
  if (drawing->show_render_stats) RestoreCityRedrawRect(overlay->statistics, &tint);
  if (uploaded) {
    drawing->redraw_rects_bounds = overlay->current;
    drawing->frame_stats.pending = false;
    ClearScreenDamage(&drawing->upload_damage);
    ClearScreenDamage(&drawing->world_damage);
  }
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
