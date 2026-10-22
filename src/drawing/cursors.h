#ifndef SC2K_DRAWING_CURSORS_H
#define SC2K_DRAWING_CURSORS_H

#include "assets/cursors.h"
#include "drawing/canvas.h"

// Bounded Drawing storage for composed tool cursors; UI verifies its slot mapping against this capacity.
enum { CITY_CURSOR_TOOL_CAPACITY = 71 };

// Prepared cursor pixels use the same tool indices as UI selection and SDL cursor slots.
// The cache belongs to Drawing; source badge and arrow pixels belong to Assets.
typedef struct {
  u32 tools[CITY_CURSOR_TOOL_CAPACITY][SC2K_CURSOR_ASSET_SPRITE_PIXELS];
  u8 initialized;
  u32 revision; // Source replacement or pixel composition invalidates SDL's resized cursors.
} CityCursorCache;

// Compact pointer artwork: three source pixels span one logical interface pixel.
// The arrow tip is the hotspot (0,0); its position is never quantized by scale.
enum { SC2K_CURSOR_SOURCE_SIZE = SC2K_CURSOR_ASSET_SOURCE_SIZE, SC2K_CURSOR_SCALE_DENOMINATOR = 300 };

// Compose into caller-owned cache pixels using borrowed badge, arrow and icon artwork.
void ComposeCityCursorIcon(u32 pixels[SC2K_CURSOR_ASSET_SPRITE_PIXELS], const CityCursorAssets *artwork,
    const u32 icon[SC2K_CURSOR_ASSET_ICON_PIXELS]);
// The caller owns the prepared sprite and releases it with FreeRunSprite.
struct RunSprite *PrepareCityCursorSprite(const u32 pixels[SC2K_CURSOR_ASSET_SPRITE_PIXELS], int size);
CityRect Sc2kCanvasCursorBounds(i32 x, i32 y, i32 scale);
// The canvas must have pixels and scale must be in 100..500. Returns false if sprite preparation fails.
bool DrawCityCursorSprite(Canvas *canvas, const u32 pixels[SC2K_CURSOR_ASSET_SPRITE_PIXELS],
    int x, int y, int scale);

#endif
