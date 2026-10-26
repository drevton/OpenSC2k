// Drawing owns effect visibility, presentation clocks, cost feedback and the fixed demolition pool.
// Game supplies clock samples and speed; UI resolves City requests before starting an effect.

#include "drawing/maps/map_sprites.h"
#include "drawing/sprites/surface.h"
#include "drawing/maps/map_effects.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "assets/state.h"
#include "city/engine/city_types.h"
#include "city/maps/terrain.h"
#include "city/state.h"
#include "drawing/maps/map_camera.h"
#include "drawing/maps/map_view.h"
#include "drawing/raster/canvas_raster_internal.h"
#include "drawing/raster/screen_damage.h"
#include "drawing/state.h"
#include "game/state.h"

int UseCityMapSpriteEffect(Drawing *drawing, CityMapSpriteEffect effect) {
  if (effect == CITY_MAP_EFFECT_WIND) {
    drawing->wind_power_visible = true;
    return drawing->wind_power_frame;
  }
  assert(effect == CITY_MAP_EFFECT_POWER_SMOKE);
  drawing->power_smoke_visible = true;
  return drawing->power_smoke_frame;
}

void StartCityScreenShake(Drawing *drawing) {
  drawing->screen_shake_active = true;
  drawing->screen_shake_started = -1;
}

bool UpdateCityScreenShake(Drawing *drawing, double now) {
  if (!drawing->screen_shake_active) return false;
  if (drawing->screen_shake_started < 0 || now < drawing->screen_shake_started)
    drawing->screen_shake_started = now;
  double elapsed = now - drawing->screen_shake_started;
  int frame = elapsed >= .12 ? 24 : elapsed / .005;
  drawing->screen_shake_active = frame < 24;
  int offset = drawing->screen_shake_active && !(frame & 1) ?
      4 << drawing->default_map_view->zoom : 0;
  if (offset == drawing->screen_shake_offset) return false;
  drawing->screen_shake_offset = offset;
  MarkScreenDirty(&drawing->screen_damage, (CityRect){0, 0, drawing->framebuffer.width, drawing->framebuffer.height});
  return true;
}

bool UpdateCityPowerAnimation(Drawing *drawing, int speed, double now) {
  double elapsed = now - drawing->power_smoke_time;
  drawing->power_smoke_time = now;
  if (drawing->power_smoke_speed != speed) {
    drawing->power_smoke_speed = speed;
    return false;
  }
  if (speed <= CITY_SPEED_PAUSED || elapsed <= 0) return false;
  double rate = CITY_POWER_SMOKE_FPS;
  if (speed == CITY_SPEED_SLOW) rate *= .5;
  else if (speed == CITY_SPEED_FAST) rate *= 2;
  else if (speed == CITY_SPEED_UNLIMITED) rate *= 4;
  drawing->power_smoke_phase += elapsed * rate;
  int frames = drawing->power_smoke_phase;
  drawing->power_smoke_phase -= frames;
  drawing->power_smoke_frame = (drawing->power_smoke_frame + frames) % CITY_POWER_SMOKE_FRAMES;
  int rotor = (drawing->power_smoke_frame * 4 + (int)(drawing->power_smoke_phase * 4)) % CITY_WIND_FRAMES;
  bool changed = (frames && drawing->power_smoke_visible) ||
      (rotor != drawing->wind_power_frame && drawing->wind_power_visible);
  drawing->wind_power_frame = rotor;
  return changed;
}

void StartCityDemolitionEffect(Drawing *drawing, TileIndex tile, int elevation, bool large) {
  if (!drawing->demolition_enabled || drawing->demolition_suppressed) return;
  int slot = 0, unused = -1;
  for (int i = 0; i < CITY_DEMOLITION_EFFECT_COUNT; ++i) {
    CityDemolitionEffect *effect = &drawing->demolition_effects[i];
    if (effect->active && effect->tile == tile) return;
    if (!effect->active && unused < 0) unused = i;
    if (effect->started < drawing->demolition_effects[slot].started) slot = i;
  }
  if (unused >= 0) slot = unused;
  // Presentation randomness never advances City's deterministic streams.
  drawing->demolition_random = drawing->demolition_random * 214013 + 2531011;
  drawing->demolition_effects[slot] = (CityDemolitionEffect){.tile = tile, .started = -1,
      .elevation = elevation, .flipped = drawing->demolition_random >> 16 & 1, .large = large, .active = true};
  drawing->demolition_active = true;
}

// This presentation clock is real time, including when City simulation is paused.
bool UpdateCityDemolitionEffects(Drawing *drawing, double now) {
  bool dirty = false;
  drawing->demolition_active = false;
  for (int i = 0; i < CITY_DEMOLITION_EFFECT_COUNT; ++i) {
    CityDemolitionEffect *effect = &drawing->demolition_effects[i];
    if (!effect->active) continue;
    if (effect->started < 0 || now < effect->started) { effect->started = now; dirty = true; }
    int count = effect->large ? CITY_DEMOLITION_LARGE_FRAMES : CITY_DEMOLITION_SMALL_FRAMES;
    double elapsed = now - effect->started;
    int frame = elapsed >= count * 0.12 ? count : (int)(elapsed / 0.12);
    dirty |= frame != effect->frame;
    effect->frame = frame;
    effect->active = frame < count;
    drawing->demolition_active |= effect->active;
  }
  return dirty;
}

enum { CITY_COST_TEXT_DURATION_MS = 2000, CITY_COST_TEXT_FADE_MS = 500,
    CITY_COST_TEXT_WIDTH = 256, CITY_COST_TEXT_HEIGHT = 48 };

void StartCityConstructionCost(Drawing *drawing, TileIndex tile, const char *currency, double now) {
  int slot = 0;
  for (int i = 0; i < CITY_COST_TEXT_COUNT; ++i) {
    if (!drawing->cost_texts[i].active) { slot = i; break; }
    if (drawing->cost_texts[i].started < drawing->cost_texts[slot].started) slot = i;
  }
  CityCostText *text = &drawing->cost_texts[slot];
  *text = (CityCostText){.tile = tile, .started = now, .active = true};
  drawing->cost_texts_active = true;
  drawing->cost_text_time = text->started;
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

typedef struct {
  CityRect bounds;
  int width, height;
} CityCostTextLayout;

// Logical label dimensions and physical framebuffer bounds come from the same measurement.
static CityCostTextLayout LayoutCityCostText(const City *city, const CityCostText *text) {
  const Drawing *drawing = city->drawing;
  if (!text->active || !IsMapTileValid(city, text->tile)) return (CityCostTextLayout){0};
  const CityInterfaceFont *font = &city->game->resources->interface_font;
  int width = 2, height = 10;
  if (font->ready) {
    const char *character = text->text;
    while (*character) {
      const CityInterfaceGlyph *glyph = &font->glyphs[0][ReadCityFontCharacter(&character)];
      width += glyph->advance;
      height = IntMax(height, glyph->height + 1);
    }
  } else width += CanvasTextWidth(text->text);
  width = IntMin(CITY_COST_TEXT_WIDTH, width);
  height = IntMin(CITY_COST_TEXT_HEIGHT, height);
  int scale = 1 << drawing->default_map_view->zoom;
  CityPoint camera = CityMapViewTilePoint(city, text->tile);
  int x = camera.x, y = camera.y;
  const TileInfo *tp = &city->tile[text->tile];
  int elevation = drawing->map_state.events_suppressed ?
      ReadCityLandHeight(tp) : ReadCitySurfaceHeight(tp);
  int anchor_x = drawing->map_state.origin_x + (y - x + 1) * 4 * scale;
  int anchor_y = drawing->map_state.origin_y + (x + y - 1) * 2 * scale - elevation * 3 * scale;
  int ui_scale = drawing->options.scale;
  int pixel_width = (width * ui_scale + 99) / 100;
  int pixel_height = (height * ui_scale + 99) / 100;
  int left = drawing->target_x + anchor_x * drawing->target_scale / 100 - pixel_width / 2;
  // Round only the final physical-pixel position, keeping motion smooth at high UI scales.
  int rise = (text->age_ms * 24 * ui_scale + 50000) / 100000;
  int top = drawing->target_y + anchor_y * drawing->target_scale / 100 - pixel_height - 8 * ui_scale / 100 - rise;
  return (CityCostTextLayout){.bounds = {left, top, left + pixel_width, top + pixel_height},
      .width = width, .height = height};
}

void DrawCityCostTexts(const City *city) {
  Drawing *drawing = city->drawing;
  Canvas *canvas = &drawing->map_target.canvas;
  CityRect clip = IntersectCityRects(canvas->clip, (CityRect){0, 0, canvas->width, canvas->height});
  u32 background[CITY_COST_TEXT_WIDTH * 5 * 8];
  for (int i = 0; i < CITY_COST_TEXT_COUNT; ++i) {
    const CityCostText *text = &drawing->cost_texts[i];
    CityCostTextLayout layout = LayoutCityCostText(city, text);
    CityRect bounds = layout.bounds;
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
    label.width = layout.width;
    label.height = layout.height;
    label.clip = (CityRect){0, 0, layout.width, layout.height};
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
          u32 color = canvas->pixels[pixel];
          if (before == color) continue;
          canvas->pixels[pixel] = 0xFF000000 | BlendRGB(color, before, alpha);
        }
    }
  }
}

static int DemolitionSpriteId(const Drawing *drawing, const CityDemolitionEffect *effect) {
  int frame = effect->frame + (effect->large ? 0 : CITY_DEMOLITION_LARGE_FRAMES);
  return MAP_DEMOLITION_SPRITE_FIRST + drawing->default_map_view->zoom * CITY_DEMOLITION_FRAME_COUNT + frame;
}

static CityRect DemolitionBounds(const City *city, const CityDemolitionEffect *effect) {
  const Drawing *drawing = city->drawing;
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

void DrawCityDemolitionEffects(const City *city) {
  Drawing *drawing = city->drawing;
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

// A copied scroll can leave an effect at both its previous screen position and its translated position.
// Damage both before drawing the current footprint, always clipped to this viewport.
static void MarkCityMapEffectDamage(Drawing *drawing, CityRect previous, CityRect bounds,
    CityPoint scroll, CityRect viewport) {
  if (previous.left < previous.right && previous.top < previous.bottom) {
    CityRect moved = {previous.left + scroll.x, previous.top + scroll.y,
        previous.right + scroll.x, previous.bottom + scroll.y};
    previous = IntersectCityRects(previous, viewport);
    moved = IntersectCityRects(moved, viewport);
    MarkScreenDirty(&drawing->screen_damage, previous);
    MarkScreenDirty(&drawing->world_damage, previous);
    MarkScreenDirty(&drawing->screen_damage, moved);
    MarkScreenDirty(&drawing->world_damage, moved);
  }
  CityRect visible = IntersectCityRects(bounds, viewport);
  MarkScreenDirty(&drawing->screen_damage, visible);
  MarkScreenDirty(&drawing->world_damage, visible);
}

void PrepareCityMapEffectDamage(const CityMapPaint *paint, CityRect viewport) {
  Drawing *drawing = paint->city->drawing;
  int index = paint->view ? (int)(paint->view - drawing->views) + 1 : 0;
  const NativeMapCache *cache = paint->view ? &paint->view->cache : &drawing->main_cache;
  CityPoint scroll = {
      (drawing->map_state.origin_x - cache->origin_x) * drawing->target_scale / 100 +
          drawing->target_x - cache->target_x,
      (drawing->map_state.origin_y - cache->origin_y) * drawing->target_scale / 100 +
          drawing->target_y - cache->target_y};
  for (int i = 0; i < CITY_COST_TEXT_COUNT; ++i) {
    CityRect bounds = LayoutCityCostText(paint->city, &drawing->cost_texts[i]).bounds;
    MarkCityMapEffectDamage(drawing, drawing->cost_text_bounds[index][i], bounds, scroll, viewport);
    drawing->cost_text_bounds[index][i] = bounds;
  }
  for (int i = 0; i < CITY_DEMOLITION_EFFECT_COUNT; ++i) {
    const CityDemolitionEffect *effect = &drawing->demolition_effects[i];
    int state = effect->frame | effect->flipped << 5 | effect->large << 6;
    CityRect bounds = DemolitionBounds(paint->city, effect);
    CityRect previous = drawing->demolition_bounds[index][i];
    if (!scroll.x && !scroll.y && !memcmp(&previous, &bounds, sizeof(bounds)) &&
        drawing->demolition_draw_states[index][i] == state) continue;
    MarkCityMapEffectDamage(drawing, previous, bounds, scroll, viewport);
    drawing->demolition_bounds[index][i] = bounds;
    drawing->demolition_draw_states[index][i] = state;
  }
}
