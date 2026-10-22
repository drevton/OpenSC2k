#include "drawing/cursors.h"

#include <string.h>

#include "drawing/sprites/sprites.h"

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
