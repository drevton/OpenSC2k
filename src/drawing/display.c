#include "city/engine/city_types.h"
#include "drawing/maps/map_view.h"
#include "game/application.h"
#include "base/time.h"
#include "game/application_internal.h"

#include <math.h>
#include <inttypes.h>
#include "game/errors.h"
#include "game/state.h"
#include "audio/state.h"
#include "city/state.h"
#include "drawing/state.h"
#include "assets/state.h"
#include "ui/state.h"
#include "drawing/maps/map_render.h"
#include "drawing/bitmaps.h"
#include "drawing/raster/canvas_raster_internal.h"
#include "drawing/drawing_internal.h"

static void PaintScreenShakeFragment(CityRect rect, void *context);
static void PaintScreenShakeScaled(Canvas *canvas, void *context);
static void PaintWindowsScaled(Canvas *canvas, void *context);

static bool PrepareScreenBuffer(City *city, CanvasSDL *display) {
  Drawing *drawing = city->game->drawing;
  int width = kDefaultCityCanvasWidth * drawing->map_pixel_scale, height = kDefaultCityCanvasHeight * drawing->map_pixel_scale;
  if (display && Sc2kSDLOutputSize(display, &width, &height)) return false;
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
    Sc2kDirtyMarkRect(&city->game->ui->desktop.dirty,
        (CityRect){0, 0, city->game->ui->desktop.canvas_width, city->game->ui->desktop.canvas_height});
  }
  drawing->map_width = (width + drawing->map_pixel_scale - 1) / drawing->map_pixel_scale;
  drawing->map_height = (height + drawing->map_pixel_scale - 1) / drawing->map_pixel_scale;
  drawing->framebuffer.draw_stats = drawing->show_render_stats ? &drawing->frame_stats.calls : NULL;
  drawing->screen_surface = (SpriteSurface){.canvas = drawing->framebuffer, .indices = drawing->screen_indices,
      .index_stride = width, .palette = drawing->colors, .row_flags = drawing->animation_rows,
      .animation_palette = drawing->animation_palette, .upload_damage = &drawing->upload_damage};
  return true;
}

// Loading artwork uses the shared row resampler and sprite drawer. Streaming
// avoids retaining a scaled full-screen copy of the backdrop.
static void DrawIntroImage(Game *game, Canvas *canvas, int id, int left, int top, int scale) {
  const RunSprite *image = game->resources->intro.images[id];
  CityRect bounds = {left, top, left + IntMax(1, image->width * scale / 65536),
      top + IntMax(1, image->height * scale / 65536)};
  if (!DrawCanvasSpriteImage(canvas, NULL, image,
      (CityRect){0, 0, image->width, image->height}, bounds, 0)) game->execution_failed = true;
}

static void DrawIntroBackdrop(Game *game, Canvas *canvas, CityRect area) {
  const RunSprite *image = game->resources->intro.images[CITY_INTRO_CITY_ART];
  int scale = IntMax((int)(((i64)canvas->width * 65536 + image->width - 1) / image->width),
      (int)(((i64)canvas->height * 65536 + image->height - 1) / image->height));
  Canvas clipped = *canvas;
  clipped.clip = IntersectCityRects(canvas->clip, area);
  DrawIntroImage(game, &clipped, CITY_INTRO_CITY_ART, (canvas->width - image->width * scale / 65536) / 2, 0, scale);
}

int CityStartupContentTop(int width, int height) {
  // The baker puts the 170-pixel identity at y=12 in this same background.
  return IntMax(width * 182 / 1024, height * 182 / 683) + 8;
}

static void DrawIntroProgress(Canvas *canvas, int left, int top, int width, int height, int percent) {
  double radius = height / 2.0;
  for (int y = IntMax(0, top - 1); y < IntMin(canvas->height, top + height + 1); ++y)
    for (int x = IntMax(0, left - 1); x < IntMin(canvas->width, left + width + 1); ++x) {
      double dx = fmax(left + radius - x - .5, x + .5 - (left + width - radius));
      double dy = y + .5 - (top + radius);
      double coverage = fmax(0, fmin(1, radius + .5 - hypot(fmax(0, dx), dy)));
      if (!coverage) continue;
      u32 color = x < left + width * percent / 100 ? 0xFFFCB030 : 0xFF67532B;
      size_t index = (size_t)y * canvas->stride + x;
      u32 background = canvas->pixels[index], result = 0xFF000000;
      for (int shift = 0; shift <= 16; shift += 8)
        result |= (u32)((color >> shift & 255) * coverage + (background >> shift & 255) * (1 - coverage) + .5)
            << shift;
      canvas->pixels[index] = result;
    }
}

bool DrawCityLoadingScreen(Game *game, CanvasSDL *display, CityIntroStage stage, int completed, int total, bool initial) {
  Drawing *drawing = game->drawing;
  int width = drawing->framebuffer.width, height = drawing->framebuffer.height;
  if (!PrepareScreenBuffer(game->city, display) || Sc2kSDLConfigureFramebuffer(display, 100)) return false;
  Canvas canvas = drawing->framebuffer;
  const CityIntroArt *art = &game->resources->intro;
  assert(art->images[0] && stage >= 0 && stage < CITY_INTRO_STAGE_COUNT);
  initial |= width != canvas.width || height != canvas.height;
  // Both screens share the still backdrop and the same baked game identity.
  int scale = IntClamp(IntMin(canvas.width * 65536 / 704, canvas.height * 65536 / 404), 16384, 98304);
  int panel_width = 620 * scale / 65536, panel_height = 160 * scale / 65536;
  int left = (canvas.width - panel_width) / 2;
  int top = (CityStartupContentTop(canvas.width, canvas.height) + canvas.height - panel_height) / 2;
  CityRect panel = {left, top, left + panel_width, top + panel_height};
  CityRect dirty = initial ? canvas.clip : panel;
  DrawIntroBackdrop(game, &canvas, dirty);
  DrawCanvasFrame(&canvas, panel, 0xFF97835B, 0xFFD3BB93, 0xFF674F2B, true);
  int label = stage;
  DrawIntroImage(game, &canvas, label, (canvas.width - (int)art->images[label]->width * scale / 65536) / 2,
      top + 32 * scale / 65536, scale);
  int percent = total > 0 ? (int)((i64)IntClamp(completed, 0, total) * 100 / total) : 0;
  int bar_width = 360 * scale / 65536;
  DrawIntroProgress(&canvas, (canvas.width - bar_width) / 2, top + 82 * scale / 65536,
      bar_width, IntMax(3, 4 * scale / 65536), percent);
  char text[8]; snprintf(text, sizeof(text), "%d%%", percent);
  int percent_width = 0;
  for (const char *c = text; *c; ++c) {
    int image = CITY_INTRO_DIGIT_FIRST + (*c == '%' ? 10 : *c - '0');
    percent_width += art->images[image]->width * scale / 65536;
  }
  int x = (canvas.width - percent_width) / 2;
  for (const char *c = text; *c; ++c) {
    int image = CITY_INTRO_DIGIT_FIRST + (*c == '%' ? 10 : *c - '0');
    DrawIntroImage(game, &canvas, image, x, top + 108 * scale / 65536, scale);
    x += art->images[image]->width * scale / 65536;
  }
  if (game->execution_failed) return false;
  ClearScreenDamage(&drawing->upload_damage);
  MarkScreenDirty(&drawing->upload_damage, dirty);
  return Sc2kSDLPresentFramebuffer(display, &drawing->framebuffer, &drawing->upload_damage, (CityRect){0}) == 0;
}

bool PrepareCityMapSpriteSources(Game *game) {
  Drawing *drawing = game->drawing;
  CityInterfaceState *ui = game->ui;
  CityResourceState *assets = game->resources;
  if (drawing->sprite_revision != assets->sprite_revision) {
    if (ui->tree_picker.drawing) InvalidateCityWindowBitmaps(&ui->tree_picker.drawing->base);
    for (int i = 0; i < CITY_STARTUP_WINDOW_LIMIT; ++i)
      if (ui->startup_windows[i].layout.drawing)
        InvalidateCityWindowBitmaps(&ui->startup_windows[i].layout.drawing->base);
    FreeMapSpriteCache(&drawing->sprite_cache);
    drawing->sprite_cache.assets = assets;
    for (int id = 0; id < CITY_MAP_SPRITE_COUNT; ++id) {
      int slot = id % 500;
      int resource = slot < assets->map_tile_sprite_count ? TileSpriteResourceIndex(assets, slot, 0) : slot;
      const CityLandscapeSprite *landscape = GetResourceSprite(assets, resource);
      if (landscape && landscape->sprite) {
        if (!InstallResourceSprite(&drawing->sprite_cache, assets, id, resource, 1 << (2 - id / 500))) return false;
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
  }
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

bool PrepareCityMapSprites(Game *game) {
  Drawing *drawing = game->drawing;
  if (!PrepareCityMapSpriteSources(game)) return false;
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

bool BindCityMapTarget(void *context) {
  CityMapPaint *paint = context;
  City *city = paint->city;
  Drawing *drawing = city->game->drawing;
  CityInterfaceState *ui = city->game->ui;
  CityExtraMapView *view = paint->view;
  int magnification = drawing->default_map_view->magnified_large_zoom ? 2 : 1;
  int scale = drawing->map_pixel_scale * 100 * magnification;
  int slot = FindMapSpriteScale(&drawing->sprite_cache, scale);
  if (slot < 0) return false;
  drawing->map_target = drawing->screen_surface;
  // Alpha-blended map pixels keep their rendered RGB until an ordinary repaint.
  // Only opaque indexed pixels participate in palette animation.
  drawing->map_target.canvas.palette_composite_rows = NULL;
  drawing->map_target.canvas.palette_composite_bounds = NULL;
  drawing->target_scale = scale; drawing->target_cache = slot;
  drawing->target_x = drawing->target_y = 0;
  if (view) {
    CityRect bounds = Sc2kCanvasDesktopPixelBounds(&ui->desktop, view->window_id);
    drawing->target_x = bounds.left + (3 * drawing->options.scale + 99) / 100;
    drawing->target_y = bounds.top + (CITY_VIEWPORT_TOP * drawing->options.scale + 99) / 100;
  }
  CityPoint offset = view ? view->scroll_pixels : drawing->scroll_pixels;
  drawing->target_x += offset.x; drawing->target_y += offset.y;
  if (!view) drawing->target_x += drawing->screen_shake_offset * drawing->map_pixel_scale;
  return true;
}

MapCachePixels CityMapCachePixels(Drawing *drawing) {
  return (MapCachePixels){&drawing->map_target, drawing->scroll_colors, drawing->scroll_indices,
      drawing->target_scale, drawing->target_x, drawing->target_y, true};
}

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

void PaintCityMapPixels(CityRect rect, void *context) {
  CityMapPaint *paint = context;
  City *city = paint->city;
  Drawing *drawing = city->game->drawing;
  int scale = drawing->target_scale, x = drawing->target_x, y = drawing->target_y;
  int left = (rect.left - x) * 100, top = (rect.top - y) * 100;
  int right = (rect.right - x) * 100, bottom = (rect.bottom - y) * 100;
  CityRect source = {left / scale - (left % scale < 0), top / scale - (top % scale < 0),
      right / scale + (right % scale > 0), bottom / scale + (bottom % scale > 0)};
  drawing->target_clip = rect; drawing->target_physical_clip = true;
  PaintCityMapFragment(source, context);
  drawing->target_physical_clip = false;
  // Composite toolbar/status controls after map painting instead of fragmenting
  // each repaint. Scroll caches continue to exclude these opaque UI pixels.
  CityCanvasDesktop *desktop = &city->game->ui->desktop;
  for (int overlay = 0; overlay < CITY_MENU_OVERLAY_COUNT; ++overlay) {
    if (overlay == 1) continue;
    Sc2kDirtyMarkRect(&desktop->window_damage,
        IntersectCityRects(rect, CityMenuPixelBounds(desktop, overlay)));
  }
}

void PaintCityMapFragment(CityRect rect, void *context) {
  CityMapPaint *paint_context = context;
  City *city = paint_context->city;
  Drawing *drawing = city->game->drawing;
  if (drawing->target_physical_clip) MarkScreenDirty(&drawing->screen_damage, drawing->target_clip);
  else MarkCityMapRect(rect, context);
  int background = drawing->options.black_background ? 0 : 0x7B;
  int scale = drawing->target_scale, x = drawing->target_x, y = drawing->target_y;
  drawing->map_target.canvas.clip = (CityRect){x + rect.left * scale / 100, y + rect.top * scale / 100,
      x + (rect.right * scale + 99) / 100, y + (rect.bottom * scale + 99) / 100};
  if (drawing->target_physical_clip) drawing->map_target.canvas.clip = drawing->target_clip;
  MarkScreenDirty(&drawing->upload_damage, drawing->map_target.canvas.clip);
  if (city->game->debug_city) {
    const u32 debug_background[] = {0xFFFF00FF};
    SpriteSurface surface = drawing->map_target;
    surface.palette = debug_background;
    FillSpriteSurface(&surface, surface.canvas.clip, 0);
  } else FillSpriteSurface(&drawing->map_target, drawing->map_target.canvas.clip, background);
  i32 magnification = drawing->default_map_view->magnified_large_zoom ? 2 : 1;
  drawing->map_state.scan_clip = paint_context->view != NULL ?
      (CityRect){0, 0, paint_context->view->width, paint_context->view->height} :
      (CityRect){0, 0, drawing->map_width / magnification, drawing->map_height / magnification};
  // Include the partial logical pixels at the physical viewport edges.
  --drawing->map_state.scan_clip.left; --drawing->map_state.scan_clip.top;
  ++drawing->map_state.scan_clip.right; ++drawing->map_state.scan_clip.bottom;
  drawing->map_state.paint_parameters = (CityMapPaintParameters){rect, 0, (u32)kDefaultCityCanvasWidth,
      drawing->map_framebuffer, drawing->map_state.sprite_catalog};
  drawing->paint_placement = !paint_context->view;
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
  DrawCityDemolitionEffects(context);
  DrawCityCostTexts(context);
  CityRect label = drawing->map_state.zone_cost_label_rect;
  if (!paint_context->view && label.left < label.right && label.top < label.bottom) {
    Canvas canvas = drawing->map_target.canvas;
    canvas.raster = CanvasRasterView(&canvas);
    canvas.raster.numerator = drawing->target_scale;
    canvas.raster.denominator = 100;
    canvas.raster.origin_x = canvas.raster.origin_y = 0;
    canvas.raster.offset_x = drawing->target_x;
    canvas.raster.offset_y = drawing->target_y;
    canvas.width = drawing->map_width / magnification;
    canvas.height = drawing->map_height / magnification;
    canvas.clip = (CityRect){0, 0, canvas.width, canvas.height};
    CityPoint position = drawing->map_state.zone_cost_label_position;
    DrawCityCanvasZoneCostLabel(city, &canvas, drawing->default_map_view,
        drawing->map_state.zone_cost_label_cost, drawing->map_state.zone_cost_label_text, position.x, position.y);
  }
  SetCityMapLabelStyle(drawing, false);
}

static void PaintScreenShakeFragment(CityRect rect, void *context) {
  NativeScreenShakePaint *paint = context;
  paint->canvas.clip = rect;
  Sc2kCanvasBlitCitySurface(&paint->canvas, paint->view, paint->destination, paint->source,
      paint->city->game->drawing->colors);
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

static void DrawCityRenderStats(City *city, Canvas *canvas, CityRect bounds) {
  Drawing *drawing = city->game->drawing;
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
  const CityInterfaceFont *font = &city->game->resources->interface_font;
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

int PresentCityDesktop(City *city, CanvasSDL *display) {
  UpdateCityToolCursor(city);
  Drawing *drawing = city->game->drawing;
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
  if (drawing->show_render_stats) DrawCityRenderStats(city, &canvas, stats);
  CityRect debug = drawing->show_redraw_rects ? drawing->world_damage.bounds : (CityRect){0};
  if (drawing->show_render_stats)
    debug = debug.left < debug.right && debug.top < debug.bottom ? UnionCityRects(debug, stats) : stats;
  // Current and previous bounds are uploaded even on frames without map damage.
  CityRect restore = drawing->redraw_rects_bounds;
  if (restore.left >= restore.right || restore.top >= restore.bottom) restore = debug;
  else if (debug.left < debug.right && debug.top < debug.bottom) restore = UnionCityRects(restore, debug);
  int result = Sc2kSDLPresentFramebuffer(display, &canvas,
      &drawing->upload_damage, restore);
  if (drawing->show_redraw_rects) VisitScreenDamage(&drawing->world_damage, RestoreCityRedrawRect, &tint);
  if (drawing->show_render_stats) RestoreCityRedrawRect(stats, &tint);
  if (!result) {
    drawing->redraw_rects_bounds = debug;
    drawing->frame_stats.pending = false;
    ClearScreenDamage(&drawing->upload_damage);
    ClearScreenDamage(&drawing->world_damage);
  }
  return result;
}

static void PaintScreenShakeScaled(Canvas *canvas, void *context) {
  NativeScreenShakePaint *paint = context;
  paint->canvas = *canvas;
  Sc2kCanvasDesktopVisitMap(&paint->city->game->ui->desktop,
      (CityRect){0, 0, kDefaultCityCanvasWidth, kDefaultCityCanvasHeight}, PaintScreenShakeFragment, paint);
}

static void PaintWindowsScaled(Canvas *canvas, void *context) {
  City *city = context;
  DrawCityDesktopUI(city->game, &city->game->ui->desktop, canvas);
}

void PresentCityScreenShake(City *city, CityMapViewConfig *view, CityRect destination, CityRect source) {
  Drawing *drawing = city->game->drawing;
  if (!drawing->framebuffer.pixels) return;
  NativeScreenShakePaint paint = {.city = city, .view = view, .destination = destination, .source = source};
  Sc2kCanvasPaintScaled(&drawing->framebuffer, drawing->map_pixel_scale, 1, PaintScreenShakeScaled, &paint);
  Sc2kCanvasPaintScaled(&drawing->framebuffer, drawing->options.scale, 100, PaintWindowsScaled, city);
  MarkScreenDirty(&drawing->upload_damage, (CityRect){0, 0, drawing->framebuffer.width, drawing->framebuffer.height});
  if (drawing->shake_display != NULL && PresentCityDesktop(city, drawing->shake_display) != 0)
    city->game->execution_failed = 1;
  // The following synchronous map-update request must retain this shifted frame.
  drawing->shake_frame_presented = true;
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
  if (drawing->shake_frame_presented) {
    drawing->shake_frame_presented = false;
    return !city->game->execution_failed;
  }
  if (city->settings.speed == CITY_SPEED_PAUSED && city->game->idle_state == (u32)CITY_IDLE_PLAY &&
      !PrepareCityCanvasDamage(city))
    return true;
  // Publish dirty city tiles and compose all visible map views at this exact
  // animation boundary, before its caller pumps timers or starts another frame.
  if (!RedrawCityCanvas(city, city->game->drawing->shake_display)) {
    city->game->execution_failed = 1;
    return false;
  }
  return true;
}

void SetCityPaletteDialogState(City *city, bool paused) {
  if (paused) EnterCityPaletteModalState(city);
  else LeaveCityPaletteModalState(city);
}

int CountCityCanvasWindows(const CityInterfaceState *ui) {
  i32 count = 0;
  for (i32 index = 0; index < ui->desktop.count; ++index)
    count += ui->desktop.windows[index].kind != CITY_CANVAS_TOOL_STATUS &&
        ui->desktop.windows[index].kind != CITY_CANVAS_MAP_VIEWPORT;
  return count;
}

static void CompositeCityScreenDamage(CityRect rect, void *context) {
  City *city = context;
  Drawing *drawing = city->game->drawing;
  drawing->framebuffer.clip = rect;
  MarkScreenDirty(&drawing->upload_damage, rect);
  Sc2kCanvasPaintScaled(&drawing->framebuffer, drawing->options.scale, 100, PaintWindowsScaled, city);
  drawing->framebuffer.clip = (CityRect){0, 0, drawing->framebuffer.width, drawing->framebuffer.height};
}

bool UpdateCityWindowTimers(Game *game, double now) {
  SyncCityFireSoundPause(game);
  // Retained dialog callers pump their own input. Window presentation timers
  // must keep running there even while the outer game loop is suspended.
  bool dirty = PollCityWindowHighlights(&game->ui->desktop, (u32)(u64)(now * 1000));
  dirty |= PollCityButtonFeedback(game, now);
  dirty |= UpdateCityTooltip(game, now);
  dirty |= UpdateCityTextCarets(game, now);
  dirty |= UpdateCityJukeboxTitle(game->ui, (u32)(u64)(now * 1000));
  CityCanvasDesktop *desktop = &game->ui->desktop;
  bool closed = false;
  for (int index = desktop->count - 1; index >= 0; --index) {
    Window *window = &desktop->windows[index];
    if (window->kind == CITY_CANVAS_COMMAND_ERROR) {
      // OpenTTD's default error duration is five real-time seconds.
      if (!game->ui->command_error_close_at) game->ui->command_error_close_at = now + 5;
      if (now >= game->ui->command_error_close_at) {
        Sc2kCanvasDesktopDismissWindow(desktop, window->window_id);
        game->ui->command_error_close_at = 0;
        closed = true;
      }
      continue;
    }
    if (window->newspaper && window->newspaper->close_at && now >= window->newspaper->close_at) {
      Sc2kCanvasDesktopDismissWindow(desktop, window->window_id);
      closed = true;
    }
  }
  dirty |= UpdateCityNewspaperOpening(game, now);
  if (closed) ApplicationUpdateDialogPause(game->city);
  return dirty || closed;
}

// Match OpenTTD's 27 ms palette ticks and independent dark-water/glitter cycles.
// This Drawing clock stops while paused and never advances City simulation.
// Recolor opaque indexed pixels directly; blended map pixels retain their rendered RGB.
bool AnimateCityCanvasPalette(Game *game, double now) {
  Drawing *drawing = game->drawing;
  if (!drawing->framebuffer.pixels) return false;
  u32 intervals = PollGameInterval(now, CITY_WATER_PALETTE_TICK_MS / 1000.0, &drawing->next_water_palette_tick);
  drawing->water_palette_counter += 8 * intervals;
  u32 water_changed = 0;
  int water_phase = ((u16)(drawing->water_palette_counter * 320) * SPRITE_WATER_COLORS) >> 16;
  for (int color = 0; color < SPRITE_WATER_COLORS; ++color) {
    u32 value = game->resources->landscape_water_colors[(color + water_phase) % SPRITE_WATER_COLORS];
    int id = SPRITE_ANIMATION_WATER_FIRST + color;
    if (drawing->animation_palette[id] != value) water_changed |= CanvasPaletteIndexMask(id);
    drawing->animation_palette[id] = value;
  }
  // OpenTTD palette.cpp: fifteen glitter colors, five entries spaced three
  // steps apart, advancing at EXTR(128, 15), separately from EXTR(320, 5).
  static const u32 kWaterGlitterCycle[] = {
    0xD8F4FC, 0xACD0E0, 0x84ACC4, 0x6484A8,
    0x486490, 0x486490, 0x486490, 0x486490,
    0x486490, 0x486490, 0x486490, 0x486490,
    0x6484A8, 0x84ACC4, 0xACD0E0
  };
  int glitter_phase = ((u16)(drawing->water_palette_counter * 128) * 15) >> 16;
  for (int color = 0; color < SPRITE_WATER_GLITTER_COLORS; ++color) {
    u32 value = kWaterGlitterCycle[(glitter_phase + color * 3) % 15];
    int id = SPRITE_ANIMATION_WATER_FIRST + SPRITE_WATER_COLORS + color;
    if (drawing->animation_palette[id] != value) water_changed |= CanvasPaletteIndexMask(id);
    drawing->animation_palette[id] = value;
  }
  u32 palette_changed = 0;
  for (int id = 1; id < SPRITE_ANIMATION_WATER_FIRST; ++id) {
    u32 color = drawing->colors[SpriteAnimationSource(id)] & 0xFFFFFF;
    if (drawing->animation_palette[id] != color) palette_changed |= CanvasPaletteIndexMask(id);
    drawing->animation_palette[id] = color;
  }
  u32 changed_buckets = palette_changed | water_changed;
  // Water cycles only update opaque indexed pixels. Blended map edges remain
  // fixed RGB; interface bitmaps retain their ordinary global-palette dependencies.
  if (palette_changed && drawing->animation_composite_rows) {
    for (int y = 0; y < drawing->framebuffer.height;) {
      if (!(drawing->animation_composite_rows[y] & palette_changed)) { ++y; continue; }
      int first = y, left = drawing->framebuffer.width, right = 0;
      do {
        u32 bounds = drawing->animation_composite_bounds[y];
        left = IntMin(left, bounds >> 16);
        right = IntMax(right, bounds & 65535);
        drawing->animation_composite_rows[y++] = 0;
      } while (y < drawing->framebuffer.height && (drawing->animation_composite_rows[y] & palette_changed));
      CityRect rect = {left, first, right, y};
      MarkScreenDirty(&drawing->screen_damage, rect);
      Sc2kDirtyMarkRect(&game->ui->desktop.window_damage, rect);
    }
  }
  if (palette_changed) InvalidateCityWindowPaletteBitmaps(drawing, palette_changed);
  memcpy(drawing->resolved_palette, drawing->colors, sizeof(drawing->colors));
  if (palette_changed) return AnimateSpriteSurfaceChanges(&drawing->screen_surface, changed_buckets);
  return water_changed && AnimateSpriteSurfaceWater(&drawing->screen_surface, water_changed);
}

static void PaintInactiveCityScreen(CityRect rect, void *context) {
  City *city = context;
  Drawing *drawing = city->game->drawing;
  Canvas canvas = drawing->framebuffer;
  canvas.clip = rect;
  DrawIntroBackdrop(city->game, &canvas, rect);
  CompositeCityScreenDamage(rect, context);
}

static bool PrepareInactiveCityCanvas(City *city, CanvasSDL *display) {
  Drawing *drawing = city->game->drawing;
  CityCanvasDesktop *desktop = &city->game->ui->desktop;
  SyncCityInterface(city->game, display, 1);
  if (!PrepareScreenBuffer(city, display)) return false;
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

// Merge moving-preview fragments only when the added physical pixel work
// stays below both the relative limit and the fixed 64-by-64 pixel allowance.
static void MarkCityPlacementDamage(Drawing *drawing, CityRect previous, CityRect current) {
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

static bool PrepareCityCanvasFrame(City *city, CanvasSDL *display) {
  Drawing *drawing = city->game->drawing;
  CityInterfaceState *ui = city->game->ui;
  if (!display) display = drawing->ui_display;
  // Failed loaders may leave partial stock records. Draw only the desktop
  // until a successful load or a new terrain session activates the City.
  if (!city->game->city_active) return PrepareInactiveCityCanvas(city, display);
  PrepareCityInformation(city->game);
  SampleGameTime(city->game, MonotonicSeconds());
  double ui_time = ReadGameTime(city->game, GAME_TIME_REALTIME);
  double world_time = ReadGameTime(city->game, GAME_TIME_UNPAUSED);
  UpdateCityVehicleAnimation(city, world_time);
  UpdateCityRoadTraffic(city, (u32)(u64)(world_time * 256));
  UpdateCityPowerAnimation(city, world_time);
  drawing->power_smoke_visible = drawing->wind_power_visible = false;
  UpdateCityCostTexts(drawing, ui_time);
  UpdateCityDemolitionEffects(drawing, ui_time);
  SyncCityInterface(city->game, display, 1);
  if (!PrepareScreenBuffer(city, display)) return false;
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
  PublishCityMainEventDamage(&paint_city);
  PrepareCityMapAnimations(&drawing->main_animations, &paint_context);
  PrepareCityCostTextDamage(&paint_context);
  PrepareCityDemolitionDamage(&paint_context);
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
  DrawMapCachePixels(drawing, &drawing->main_cache, &ui->desktop, 0, NULL, 0, drawing->framebuffer.width, drawing->framebuffer.height,
      magnification, drawing->map_state.origin_x, drawing->map_state.origin_y, &pixels, PaintCityMapPixels, &paint_context);
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

bool RedrawCityCanvas(City *city, CanvasSDL *display) {
  if (!display) display = city->game->drawing->ui_display;
  return PrepareCityCanvas(city, display) && (!display || PresentCityDesktop(city, display) == 0);
}
