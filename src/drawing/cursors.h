#ifndef SC2K_DRAWING_CURSORS_H
#define SC2K_DRAWING_CURSORS_H

#include "assets/cursors.h"
#include "drawing/canvas.h"
#include "drawing/sprites/sources.h"

typedef struct Drawing Drawing;

// Bounded Drawing storage for composed tool cursors; UI verifies its slot mapping against this capacity.
enum { CITY_CURSOR_TOOL_CAPACITY = 71 };

typedef enum CursorCacheState {
  CURSOR_CACHE_EMPTY, CURSOR_CACHE_ARTWORK, CURSOR_CACHE_FALLBACK
} CursorCacheState;

// Prepared cursor pixels use the same tool indices as UI selection and SDL cursor slots.
// The cache belongs to Drawing; source badge and arrow pixels belong to Assets.
typedef struct {
  u32 tools[CITY_CURSOR_TOOL_CAPACITY][SC2K_CURSOR_ASSET_SPRITE_PIXELS];
  u8 state; // CursorCacheState; fallback uses only tools[0], with the other tool images unavailable.
  u32 revision; // Source replacement or pixel composition invalidates SDL's resized cursors.
} CityCursorCache;

// Source replacement/release makes all composed images unavailable and invalidates host-sized copies.
// Keep the inline pixel backing; the next request recomposes artwork or the fallback.
void InvalidateCityCursorCache(CityCursorCache *cache);

// Compact pointer artwork: three source pixels span one logical interface pixel.
// The arrow tip is the hotspot (0,0); its position is never quantized by scale.
enum { SC2K_CURSOR_SOURCE_SIZE = SC2K_CURSOR_ASSET_SOURCE_SIZE, SC2K_CURSOR_SCALE_DENOMINATOR = 300 };

// Without source artwork, reuse the first tool slot for the geometric gold pointer. The returned pixels remain
// borrowed until this cache is invalidated or rebuilt; loaded artwork must recompose the tool set afterwards.
const u32 *PrepareCityFallbackCursor(CityCursorCache *cache);

// Compose into caller-owned cache pixels using borrowed badge, arrow and icon artwork.
void ComposeCityCursorIcon(u32 pixels[SC2K_CURSOR_ASSET_SPRITE_PIXELS], const CityCursorAssets *artwork,
    const u32 icon[SC2K_CURSOR_ASSET_ICON_PIXELS]);
// Compose the zone diamond in the supplied color, with stripes for dense zoning, using the same badge and arrow.
void ComposeCityZoneCursorIcon(u32 pixels[SC2K_CURSOR_ASSET_SPRITE_PIXELS], const CityCursorAssets *artwork,
    u32 color, bool dense);
// Fit a valid map-cache slot into a cleared 40x40 cursor icon, using the supplied palette and Drawing animation colors.
// Invalid slots and missing sources return false without writing pixels. Preparation failure leaves the cleared icon.
// Temporary sprites are released; no cache or simulation state changes.
bool DecodeCityMapCursorIcon(const Drawing *drawing, int sprite_id, const u32 palette[256],
    u32 icon[SC2K_CURSOR_ASSET_ICON_PIXELS]);
// Decode a selected interface image into a cleared 40x40 icon, centered at width 40 and height in 1..40.
// Source is borrowed. Failure releases temporary references and leaves the cleared icon; no cache is retained.
bool DecodeCityInterfaceCursorIcon(InterfaceSpriteSource source, int height,
    u32 icon[SC2K_CURSOR_ASSET_ICON_PIXELS]);
// The caller owns the prepared sprite and releases it with FreeRunSprite.
struct RunSprite *PrepareCityCursorSprite(const u32 pixels[SC2K_CURSOR_ASSET_SPRITE_PIXELS], int size);
CityRect Sc2kCanvasCursorBounds(i32 x, i32 y, i32 scale);
// The canvas must have pixels and scale must be in 100..500. Returns false if sprite preparation fails.
bool DrawCityCursorSprite(Canvas *canvas, const u32 pixels[SC2K_CURSOR_ASSET_SPRITE_PIXELS],
    int x, int y, int scale);

#endif
