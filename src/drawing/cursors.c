#include "drawing/cursors.h"
#include "drawing/sprites/surface.h"

#include <string.h>

#include "city/maps/buildings.h"
#include "city/state.h"
#include "drawing/maps/map_sprites.h"
#include "drawing/maps/tables.h"
#include "drawing/sprites/preparation.h"
#include "drawing/state.h"

void InvalidateCityCursorCache(CityCursorCache *cache) {
  cache->state = CURSOR_CACHE_EMPTY;
  ++cache->revision;
}

// Keep a custom gold pointer even if host graphics cannot be loaded. This
// small geometric fallback contains no duplicated sprite pixel data.
const u32 *PrepareCityFallbackCursor(CityCursorCache *cache) {
  u32 *pixels = cache->tools[0];
  if (cache->state != CURSOR_CACHE_FALLBACK) {
    memset(pixels, 0, sizeof(cache->tools[0]));
    for (i32 y = 0; y < 40; ++y)
      for (i32 x = 0; x < 40; ++x) {
        i32 triangle = x <= 31 - y && y <= 31;
        i32 stem = y >= 15 && y <= 36 && x >= y - 12 && x <= y - 4;
        if (!triangle && !stem) continue;
        i32 edge = x < 2 || y < 2 || (triangle && x >= 29 - y) ||
            (stem && (x <= y - 11 || x >= y - 5 || y >= 35));
        pixels[y * SC2K_CURSOR_ASSET_SOURCE_SIZE + x] =
            edge ? 0xFF101010u : 0xFFFCD400u;
      }
    cache->state = CURSOR_CACHE_FALLBACK;
  }
  return pixels;
}

static u32 CursorPixelOver(u32 source, u32 destination) {
  int alpha = source >> 24, background = ((destination >> 24) * (255 - alpha) + 127) / 255;
  int combined = alpha + background;
  if (!combined) return 0;
  u32 color = (u32)combined << 24;
  for (int shift = 0; shift < 24; shift += 8)
    color |= (u32)(((source >> shift & 255) * alpha + (destination >> shift & 255) * background +
        combined / 2) / combined) << shift;
  return color;
}

void ComposeCityCursorIcon(u32 pixels[SC2K_CURSOR_ASSET_SPRITE_PIXELS], const CityCursorAssets *artwork,
    const u32 icon[SC2K_CURSOR_ASSET_ICON_PIXELS]) {
  memcpy(pixels, artwork->pixels + SC2K_CURSOR_ASSET_BADGE_OFFSET,
      SC2K_CURSOR_ASSET_SPRITE_PIXELS * sizeof(*pixels));
  for (i32 y = 0; y < 40; ++y)
    for (i32 x = 0; x < 40; ++x)
      pixels[(y + 18) * 72 + x + 16] = CursorPixelOver(icon[y * 40 + x], pixels[(y + 18) * 72 + x + 16]);
  for (i32 i = 0; i < 72 * 72; ++i)
    pixels[i] = CursorPixelOver(artwork->pixels[SC2K_CURSOR_ASSET_ARROW_OFFSET + i], pixels[i]);
}

void ComposeCityZoneCursorIcon(u32 pixels[SC2K_CURSOR_ASSET_SPRITE_PIXELS], const CityCursorAssets *artwork,
    u32 color, bool dense) {
  u32 icon[40 * 40] = {0};
  for (i32 y = 7; y < 33; ++y) {
    i32 half = y < 20 ? y - 7 : 32 - y;
    for (i32 x = 20 - half; x <= 20 + half; ++x)
      icon[y * 40 + x] = (x == 20 - half || x == 20 + half ||
          (dense && y % 4 == 0)) ? 0xFF303030u : color;
  }
  ComposeCityCursorIcon(pixels, artwork, icon);
}

static bool DecodeNativeMapIcon(const RunSprite *sprite, const u32 palette[256],
    const u32 *animation_palette, u32 *icon, int size) {
  if (!sprite) return false;
  memset(icon, 0, size * size * sizeof(*icon));
  int divisor = IntMax(sprite->width, sprite->height);
  int width = IntMax(1, sprite->width * (size - 4) / divisor);
  int height = IntMax(1, sprite->height * (size - 4) / divisor);
  RunSprite *prepared = ResizeRunSprite(sprite, width, height);
  if (!prepared) return false;
  bool copied = CopyRunSpriteRgba(prepared, icon + (size - height) / 2 * size + (size - width) / 2,
      size, palette, animation_palette);
  FreeRunSprite(prepared);
  return copied;
}

static bool DecodeCityMapIcon(const Drawing *drawing, int sprite_id, const u32 palette[256], u32 *icon, int size) {
  if ((unsigned)sprite_id >= MAP_SPRITE_COUNT || size <= 4) return false;
  return DecodeNativeMapIcon(drawing->sprite_cache.base[sprite_id], palette, drawing->animation_palette, icon, size);
}

bool DecodeCityBuildingCursorIcon(const City *city, TileKind kind, const u32 palette[256],
    u32 icon[SC2K_CURSOR_ASSET_ICON_PIXELS]) {
  int art = CityBuildingSprite(city, kind, 0);
  int sprite = CITY_SPRITE_CATALOG_LARGE_BASE + kCityMapKindSprites[kind];
  if (art >= 0) {
    // Wind previews include the rotor, which is a separate animated layer on the map.
    if (kind == TK_WIND_POWER)
      art = CITY_LANDSCAPE_WIND_PREVIEW_FIRST +
          (city->geography.start_year + city->clock.year >= CITY_WIND_MODERN_YEAR ? CITY_CIVIC_VIEWS : 0);
    sprite = MAP_LANDSCAPE_SPRITE_FIRST + 2 * MAP_LANDSCAPE_SPRITES_PER_ZOOM + art - CITY_LANDSCAPE_GROUND_FIRST;
  }
  return DecodeCityMapIcon(city->drawing, sprite, palette, icon, 40);
}

bool DecodeCityInterfaceCursorIcon(InterfaceSpriteSource source, int height,
    u32 icon[SC2K_CURSOR_ASSET_ICON_PIXELS]) {
  RunSprite *reference = ReferenceRunSprite(source.image, source.image->palette, source.flipped);
  RunSprite *prepared = reference ? ResizeRunSprite(reference, 40, height) : NULL;
  FreeRunSprite(reference);
  memset(icon, 0, SC2K_CURSOR_ASSET_ICON_PIXELS * sizeof(*icon));
  bool copied = prepared && CopyRunSpriteRgba(prepared, icon + (40 - height) / 2 * 40, 40, NULL, NULL);
  FreeRunSprite(prepared);
  return copied;
}

CityRect Sc2kCanvasCursorBounds(i32 x, i32 y, i32 scale) {
  if (scale < 100 || scale > 500) return (CityRect){x, y, x, y};
  i32 size = (SC2K_CURSOR_SOURCE_SIZE * scale + SC2K_CURSOR_SCALE_DENOMINATOR - 1) /
      SC2K_CURSOR_SCALE_DENOMINATOR;
  return (CityRect){x, y, x + size, y + size};
}

RunSprite *PrepareCityCursorSprite(const u32 pixels[SC2K_CURSOR_ASSET_SPRITE_PIXELS], int size) {
  RunSprite *source = EncodeRgbaRunSprite(pixels,
      SC2K_CURSOR_SOURCE_SIZE, SC2K_CURSOR_SOURCE_SIZE, SC2K_CURSOR_SOURCE_SIZE);
  if (!source) return NULL;
  RunSprite *prepared = ResizeRunSprite(source, size, size);
  FreeRunSprite(source);
  return prepared;
}

// Normal play installs SDL cursors. Diagnostic software captures use the same
// prepared image and sprite drawer, including clipping and partial alpha.
bool DrawCityCursorSprite(Canvas *canvas, const u32 pixels[SC2K_CURSOR_ASSET_SPRITE_PIXELS],
    int x, int y, int scale) {
  int size = Sc2kCanvasCursorBounds(0, 0, scale).right;
  RunSprite *sprite = PrepareCityCursorSprite(pixels, size);
  if (!sprite) return false;
  SpriteSurface surface = {.canvas = *canvas, .indices = canvas->palette_indices,
      .index_stride = canvas->index_stride, .row_flags = canvas->palette_rows};
  DrawRunSprite(&surface, sprite, x, y, SPRITE_COPY, NULL);
  FreeRunSprite(sprite);
  return true;
}
