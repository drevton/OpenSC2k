#include "city/events/events.h"
#include "city/maps/layers.h"
#include "drawing/maps/map_view.h"
#include "base/time.h"
#include "game/application_internal.h"

#include "ui/input.h"
#include "ui/frontend.h"
#include "game/errors.h"
#include "game/state.h"
#include "audio/state.h"
#include "city/state.h"
#include "drawing/state.h"
#include "assets/state.h"
#include "ui/state.h"
#include "drawing/drawing_internal.h"
#include "drawing/maps/map_render.h"


static void MarkCitySpriteRect(CityRect rect, void *context) {
  CityMapPaint *paint = context;
  City *city = paint->city;
  Drawing *drawing = city->game->drawing;
  if (rect.left >= rect.right || rect.top >= rect.bottom) return;
  rect = CityMapScreenRect(rect, context);
  MarkScreenDirty(&drawing->screen_damage, rect);
  MarkScreenDirty(&drawing->world_damage, rect);
}

enum { CITY_COST_TEXT_DURATION_MS = 2000, CITY_COST_TEXT_FADE_MS = 500,
    CITY_COST_TEXT_WIDTH = 256, CITY_COST_TEXT_HEIGHT = 48 };

void ShowCityConstructionCost(City *city, TileIndex tile, i64 cost) {
  if (cost <= 0 || !IsMapTileValid(city, tile)) return;
  Drawing *drawing = city->game->drawing;
  int slot = 0;
  for (int i = 0; i < CITY_COST_TEXT_COUNT; ++i) {
    if (!drawing->cost_texts[i].active) { slot = i; break; }
    if (drawing->cost_texts[i].started < drawing->cost_texts[slot].started) slot = i;
  }
  CityCostText *text = &drawing->cost_texts[slot];
  *text = (CityCostText){.tile = tile, .started = MonotonicSeconds(), .active = true};
  drawing->cost_texts_active = true;
  drawing->cost_text_time = text->started;
  char currency[CITY_CURRENCY_TEXT_CAPACITY];
  FormatCityCurrency(city->game, currency, cost);
  snprintf(text->text, sizeof(text->text), "-%s", currency);
}

bool UpdateCityCostTexts(Drawing *drawing, double now) {
  bool dirty = false;
  drawing->cost_text_time = now;
  drawing->cost_texts_active = false;
  for (int i = 0; i < CITY_COST_TEXT_COUNT; ++i) {
    CityCostText *text = &drawing->cost_texts[i];
    if (!text->active) continue;
    double elapsed = (now - text->started) * 1000;
    int age = elapsed >= CITY_COST_TEXT_DURATION_MS ? CITY_COST_TEXT_DURATION_MS : elapsed > 0 ? (int)elapsed : 0;
    dirty |= age != text->age_ms || age == 0;
    text->age_ms = age;
    text->active = age < CITY_COST_TEXT_DURATION_MS;
    drawing->cost_texts_active |= text->active;
  }
  return dirty;
}

static CityRect CityCostTextBounds(City *city, const CityCostText *text, int *width, int *height) {
  Drawing *drawing = city->game->drawing;
  if (!text->active || !IsMapTileValid(city, text->tile)) return (CityRect){0};
  const CityInterfaceFont *font = &city->game->resources->interface_font;
  *width = IntMin(CITY_COST_TEXT_WIDTH, CityInterfaceTextWidth(font, text->text) + 2);
  *height = 10;
  if (font->ready) {
    const char *character = text->text;
    while (*character) {
      int code = ReadCityFontCharacter(&character);
      *height = IntMax(*height, font->glyphs[0][code].height + 1);
    }
  }
  *height = IntMin(CITY_COST_TEXT_HEIGHT, *height);
  int scale = 1 << drawing->default_map_view->zoom;
  CityPoint camera = CityMapViewTilePoint(city, text->tile);
  int x = camera.x, y = camera.y;
  const TileInfo *tp = &city->tile[text->tile];
  int elevation = drawing->map_state.events_suppressed ?
      ReadCityLandHeight(tp) : ReadCitySurfaceHeight(tp);
  int anchor_x = drawing->map_state.origin_x + (y - x + 1) * 4 * scale;
  int anchor_y = drawing->map_state.origin_y + (x + y - 1) * 2 * scale - elevation * 3 * scale;
  int ui_scale = drawing->options.scale;
  int pixel_width = (*width * ui_scale + 99) / 100;
  int pixel_height = (*height * ui_scale + 99) / 100;
  int left = drawing->target_x + anchor_x * drawing->target_scale / 100 - pixel_width / 2;
  // Round only the final physical-pixel position, keeping motion smooth at high UI scales.
  int rise = (text->age_ms * 24 * ui_scale + 50000) / 100000;
  int top = drawing->target_y + anchor_y * drawing->target_scale / 100 - pixel_height - 8 * ui_scale / 100 - rise;
  return (CityRect){left, top, left + pixel_width, top + pixel_height};
}

void DrawCityCostTexts(void *context) {
  City *city = ((CityMapPaint *)context)->city;
  Drawing *drawing = city->game->drawing;
  Canvas *canvas = &drawing->map_target.canvas;
  CityRect clip = IntersectCityRects(canvas->clip, (CityRect){0, 0, canvas->width, canvas->height});
  u32 background[CITY_COST_TEXT_WIDTH * 5 * 8];
  for (int i = 0; i < CITY_COST_TEXT_COUNT; ++i) {
    const CityCostText *text = &drawing->cost_texts[i];
    int width, height;
    CityRect bounds = CityCostTextBounds(city, text, &width, &height);
    CityRect visible = IntersectCityRects(bounds, clip);
    if (visible.left >= visible.right || visible.top >= visible.bottom) continue;
    Canvas label = *canvas;
    label.raster = CanvasRasterView(canvas);
    label.raster.numerator = drawing->options.scale;
    label.raster.denominator = 100;
    label.raster.origin_x = label.raster.origin_y = 0;
    label.raster.offset_x = bounds.left;
    label.raster.offset_y = bounds.top;
    label.raster.clip = visible;
    label.width = width;
    label.height = height;
    label.clip = (CityRect){0, 0, width, height};
    label.scalable_font = !drawing->options.sprite_font;
    label.font_antialias = drawing->options.font_antialias;
    int alpha = text->age_ms < CITY_COST_TEXT_DURATION_MS - CITY_COST_TEXT_FADE_MS ? 255 :
        (CITY_COST_TEXT_DURATION_MS - text->age_ms) * 255 / CITY_COST_TEXT_FADE_MS;
    if (alpha == 255) {
      DrawCityInterfaceText(&city->game->resources->interface_font, &label, 0, 0, text->text, 0xFFFF6060, true);
      continue;
    }
    // Blend the tail against the freshly painted map in small row bands. The font
    // still selects its cached 1x/2x/4x detail from the final display scale.
    int band_width = visible.right - visible.left;
    for (int top = visible.top; top < visible.bottom; top += 8) {
      int bottom = IntMin(top + 8, visible.bottom);
      for (int y = top; y < bottom; ++y)
        memcpy(background + (y - top) * band_width,
            canvas->pixels + (size_t)y * canvas->stride + visible.left, band_width * sizeof(u32));
      label.raster.clip = (CityRect){visible.left, top, visible.right, bottom};
      DrawCityInterfaceText(&city->game->resources->interface_font, &label, 0, 0, text->text, 0xFFFF6060, true);
      for (int y = top; y < bottom; ++y)
        for (int x = visible.left; x < visible.right; ++x) {
          size_t pixel = (size_t)y * canvas->stride + x;
          u32 before = background[(y - top) * band_width + x - visible.left];
          u32 color = canvas->pixels[pixel], value = 0xFF000000;
          if (before == color) continue;
          for (int shift = 0; shift <= 16; shift += 8)
            value |= (((color >> shift & 255) * alpha + (before >> shift & 255) * (255 - alpha) + 127) / 255)
                << shift;
          canvas->pixels[pixel] = value;
        }
    }
  }
}

static int DemolitionSpriteId(const Drawing *drawing, const CityDemolitionEffect *effect) {
  int frame = effect->frame + (effect->large ? 0 : CITY_DEMOLITION_LARGE_FRAMES);
  return MAP_DEMOLITION_SPRITE_FIRST + drawing->default_map_view->zoom * CITY_DEMOLITION_FRAME_COUNT + frame;
}

static CityRect DemolitionBounds(const City *city, const CityDemolitionEffect *effect) {
  const Drawing *drawing = city->game->drawing;
  if (!effect->active || !IsMapTileValid(city, effect->tile)) return (CityRect){0};
  int id = DemolitionSpriteId(drawing, effect), slot = drawing->target_cache;
  const RunSprite *sprite = drawing->sprite_cache.sprites[slot][id];
  if (!sprite) return (CityRect){0};
  CityPoint camera = CityMapViewTilePoint(city, effect->tile);
  int x = camera.x, y = camera.y;
  int zoom = drawing->default_map_view->zoom;
  int anchor_x = drawing->map_state.origin_x + (y - x + 1) * (4 << zoom);
  int anchor_y = drawing->map_state.origin_y + (x + y - 1) * (2 << zoom) -
      effect->elevation * CityMapHeightStep(zoom);
  int left = drawing->target_x + anchor_x * drawing->target_scale / 100 +
      drawing->sprite_cache.offsets_x[slot][id];
  int top = drawing->target_y + anchor_y * drawing->target_scale / 100 -
      drawing->sprite_cache.baselines[slot][id];
  return (CityRect){left, top, left + sprite->width, top + sprite->height};
}

static void PrepareCityMapEffectDamage(CityMapPaint *paint, bool demolition) {
  Drawing *drawing = paint->city->game->drawing;
  int viewport = paint->view ? (int)(paint->view - drawing->views) + 1 : 0;
  NativeMapCache *cache = paint->view ? &paint->view->cache : &drawing->main_cache;
  int dx = (drawing->map_state.origin_x - cache->origin_x) * drawing->target_scale / 100 +
      drawing->target_x - cache->target_x;
  int dy = (drawing->map_state.origin_y - cache->origin_y) * drawing->target_scale / 100 +
      drawing->target_y - cache->target_y;
  int count = demolition ? CITY_DEMOLITION_EFFECT_COUNT : CITY_COST_TEXT_COUNT;
  CityRect *stored = demolition ? drawing->demolition_bounds[viewport] : drawing->cost_text_bounds[viewport];
  for (int i = 0; i < count; ++i) {
    CityRect previous = stored[i], bounds;
    int demolition_state = 0;
    if (demolition) {
      const CityDemolitionEffect *effect = &drawing->demolition_effects[i];
      demolition_state = effect->frame | effect->flipped << 5 | effect->large << 6;
      bounds = DemolitionBounds(paint->city, effect);
      if (!dx && !dy && !memcmp(&previous, &bounds, sizeof(bounds)) &&
          drawing->demolition_draw_states[viewport][i] == demolition_state) continue;
    }
    if (previous.left < previous.right && previous.top < previous.bottom) {
      CityRect moved = {previous.left + dx, previous.top + dy, previous.right + dx, previous.bottom + dy};
      previous = ClipCityMapViewportPixels(previous, paint);
      moved = ClipCityMapViewportPixels(moved, paint);
      MarkScreenDirty(&drawing->screen_damage, previous);
      MarkScreenDirty(&drawing->world_damage, previous);
      MarkScreenDirty(&drawing->screen_damage, moved);
      MarkScreenDirty(&drawing->world_damage, moved);
    }
    if (!demolition) {
      int width, height;
      bounds = CityCostTextBounds(paint->city, &drawing->cost_texts[i], &width, &height);
    }
    CityRect visible = ClipCityMapViewportPixels(bounds, paint);
    MarkScreenDirty(&drawing->screen_damage, visible);
    MarkScreenDirty(&drawing->world_damage, visible);
    stored[i] = bounds;
    if (demolition) drawing->demolition_draw_states[viewport][i] = demolition_state;
  }
}

void PrepareCityCostTextDamage(void *context) {
  PrepareCityMapEffectDamage(context, false);
}

void PrepareCityDemolitionDamage(void *context) {
  PrepareCityMapEffectDamage(context, true);
}

void DrawCityDemolitionEffects(void *context) {
  City *city = ((CityMapPaint *)context)->city;
  Drawing *drawing = city->game->drawing;
  for (int i = 0; i < CITY_DEMOLITION_EFFECT_COUNT; ++i) {
    const CityDemolitionEffect *effect = &drawing->demolition_effects[i];
    if (!effect->active) continue;
    CityRect bounds = DemolitionBounds(city, effect);
    if (bounds.left >= bounds.right || bounds.top >= bounds.bottom) continue;
    const RunSprite *sprite = drawing->sprite_cache.sprites[drawing->target_cache][DemolitionSpriteId(drawing, effect)];
    if (sprite) DrawRunSprite(&drawing->map_target, sprite, bounds.left, bounds.top,
        SPRITE_COPY | (effect->flipped ? SPRITE_FLIPPED : 0), NULL);
  }
}

void PublishCityEventDamage(MapSpriteFootprints *sprites, void *context) {
  CityMapPaint *paint = context;
  City *city = paint->city;
  Drawing *drawing = city->game->drawing;
  if (!PrepareCityEventDrawingStorage(city)) return;
  int phase = city->event_runtime.animation_phase;
  int dx = drawing->map_state.origin_x - sprites->origin_x;
  int dy = drawing->map_state.origin_y - sprites->origin_y;
  for (int slot = CITY_EVENT_SLOT_FIRST; slot < city->events.capacity; ++slot) {
    CityMapEvent canonical = city->events.event[slot];
    canonical.hash_next = 0;
    canonical.coordinate_flags &= ~CITY_EVENT_HASH_LINKED;
    CityMapEvent *event = &canonical;
    CityRect bounds = {0};
    bool random = event->type == CITY_MAP_EVENT_EXPLOSION || event->type == CITY_MAP_EVENT_MONSTER ||
        event->type == CITY_MAP_EVENT_TORNADO;
    bool rotor_changed = sprites->helicopter_frame != drawing->vehicles.helicopter_frame &&
        ((event->type == CITY_MAP_EVENT_HELICOPTER && event->next_slot != CITY_HELICOPTER_ON_GROUND) ||
            event->type == CITY_MAP_EVENT_MAXIS_MAN);
    CityPoint position = ReadCityMapEventPosition(city, event);
    if (!random && event->type != CITY_MAP_EVENT_UNUSED && IsMapTileValid(city, event->tile))
      bounds = Sc2kCityMapEventBounds(city, drawing->default_map_view->zoom, position.x, position.y, slot);
    if (!sprites->initialized || sprites->animation_phase != phase || rotor_changed ||
        memcmp(event, &sprites->events[slot], sizeof(*event)) ||
        memcmp(&bounds, &sprites->event_bounds[slot], sizeof(bounds))) {
      CityRect previous = sprites->event_bounds[slot];
      if (previous.left < previous.right && previous.top < previous.bottom &&
          sprites->zoom == drawing->default_map_view->zoom) {
        previous.left += dx; previous.right += dx;
        previous.top += dy; previous.bottom += dy;
      }
      MarkCitySpriteRect(previous, context);
      MarkCitySpriteRect(bounds, context);
    }
    sprites->event_bounds[slot] = bounds;
    sprites->events[slot] = *event;
  }
  sprites->animation_phase = phase;
  sprites->helicopter_frame = drawing->vehicles.helicopter_frame;
  sprites->origin_x = drawing->map_state.origin_x;
  sprites->origin_y = drawing->map_state.origin_y;
  sprites->zoom = drawing->default_map_view->zoom;
  sprites->initialized = true;
}

void PublishCityMainEventDamage(City *city) {
  Drawing *drawing = city->game->drawing;
  CityMapPaint paint = {city, NULL};
  PublishCityEventDamage(&drawing->main_sprites, &paint);
}

void PrepareCityMapAnimations(CityMapAnimationFootprints *previous, void *context) {
  CityMapPaint *paint = context;
  City *city = paint->city;
  Drawing *drawing = city->game->drawing;
  SimulationRandomState before = city->random;
  BeginCityMapAnimationCapture(drawing);
  // The empty paint rectangle prevents rasterization. The normal scan rectangle
  // selects nearby effects in painter order without visiting the whole map.
  PaintCityMapFragment((CityRect){0}, context);
  EndCityMapAnimationCapture(city, drawing);
  city->random = before;
  PublishCityMapAnimationDamage(city, drawing->default_map_view, previous, MarkCitySpriteRect, context);
  ReplayCityMapAnimations(drawing);
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

void RepaintCityMapDamage(CityRect screen, void *context) {
  City *city = ((CityMapPaint *)context)->city;
  Drawing *drawing = city->game->drawing;
  CityInterfaceState *ui = city->game->ui;
  CityExtraMapView *view = ((CityMapPaint *)context)->view;
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
    PublishCityEventDamage(&view->sprites, &paint);
    PrepareCityMapAnimations(&view->animations, &paint);
    PrepareCityCostTextDamage(&paint);
    PrepareCityDemolitionDamage(&paint);
    if (drawing->disable_map_cache) view->cache.valid = 0;
    bool retained = view->cache.valid;
    MapCachePixels pixels = CityMapCachePixels(drawing);
    pixels.preserved_source = preserved_source;
    DrawMapCachePixels(drawing, &view->cache, &ui->desktop, view->window_id, NULL, 0,
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
      ui->pointer_stroke_count || ui->pointer_query_capture || !IsCityMapVisibleAt(city, input->x, input->y))
    return false;
  ui->map_view_focus_window = 0;
  ui->pan_button = input->button;
  ui->pan_pointer = (CityPoint){x, y};
  return true;
}
