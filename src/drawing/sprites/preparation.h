#ifndef SC2K_DRAWING_SPRITES_PREPARATION_H
#define SC2K_DRAWING_SPRITES_PREPARATION_H

#include "drawing/sprites/sprites.h"
#include "drawing/types.h"

typedef struct {
  u32 rgba; // Numeric 0xAARRGGBB, independent of host byte order.
  u8 index, kind; // Explicit indexed mode permits opaque static source index zero.
} SpritePixel;

// Batch resampling reuses one row buffer. Zero-initialize before first use and borrow exclusively per call;
// separate preparation workers use separate workspaces. Returned images never borrow this storage.
typedef struct {
  SpritePixel *pixels;
  size_t capacity;
} SpritePreparationWorkspace;

// Keep indexed identities and the nearest footprint; COLORS smooths existing RGB/alpha samples only.
// IMAGE also resolves static palette samples and filters them with premultiplied alpha weights.
typedef enum SpriteFilter { SPRITE_FILTER_NEAREST, SPRITE_FILTER_COLORS, SPRITE_FILTER_IMAGE } SpriteFilter;

// Resample a nonempty source region using positive per-axis ratios. Content starts at nonnegative coordinates;
// space beyond the source is transparent. Unpaletted indexed sources require the supplied 256-color palette.
// Source rows must be validated. The result owns one reference, including an exact-size retained source.
// NULL reports size/allocation failure; workspace capacity may grow on either outcome and remains caller-owned.
RunSprite *ResampleRunSprite(SpritePreparationWorkspace *workspace, const RunSprite *source,
    CityRect content, CityPoint numerator, CityPoint denominator, const u32 *palette, SpriteFilter filter);
// Free row scratch and reset the workspace. Does not affect previously returned sprites; safe for an empty workspace.
void FreeSpritePreparationWorkspace(SpritePreparationWorkspace *workspace);

// Encoding and fixed image conversion

// Encode borrowed pixels with nearest-neighbor integer expansion; scale must be positive and stride is in pixels.
// Fractional resizing uses ResampleRunSprite. Returns one owned reference,
// or NULL for missing pixels, invalid dimensions/stride/scale, packed-size limits or allocation failure.
RunSprite *EncodeRunSprite(const SpritePixel *pixels, int width, int height, int stride, int scale);
// Fixed UI images use the same resampler as map sprites. Source is borrowed; the result owns one reference.
// Positive dimensions are required. NULL reports size/allocation failure; temporary row scratch is released.
RunSprite *ResizeRunSprite(const RunSprite *source, int width, int height);
// Host cursor and generated previews share the normal run encoder/decoder.
RunSprite *EncodeRgbaRunSprite(const u32 *pixels, int width, int height, int stride);
bool CopyRunSpriteRgba(const RunSprite *source, u32 *pixels, int stride,
    const u32 *palette, const u32 *animation_palette);

// Canvas images and reusable preparations

// One source region prepared at final raster dimensions. Zero-initialize before first use;
// the slot owns both source and prepared references.
typedef struct SpriteImage {
  RunSprite *source, *prepared;
  CityRect content;
} SpriteImage;
// Release both references and clear the slot; safe to repeat after initialization.
void ReleaseSpriteImage(SpriteImage *image);
// Draw a non-NULL full sprite into logical canvas bounds through the existing clip/raster transform; retain no cache.
bool DrawCanvasRunSprite(Canvas *canvas, const RunSprite *sprite, CityRect bounds, u32 flags);
// A non-NULL cache slot prepares a source region at its final raster dimensions and retains both references.
// Missing source or empty clip is a no-op; preparation failure leaves the previous cache intact.
// ReleaseSpriteImage releases the slot. Use DrawCanvasRunSprite to stream a full image without a cache.
bool DrawCanvasSpriteImage(Canvas *canvas, SpriteImage *image, const RunSprite *source,
    CityRect content, CityRect bounds, u32 flags);

// One generated bitmap retained as a run image. Zero-initialize before first use; free sprite when retiring the cache.
typedef struct SpriteBitmapCache {
  RunSprite *sprite;
  bool valid;
} SpriteBitmapCache;

typedef struct {
  CityRect bounds;
  void (*build)(Canvas *pixels, const void *context); // Paint fixed RGB/alpha pixels; called only on a cache miss.
  const void *context;
} CanvasBitmapPaint;
// Build borrows a zeroed raster and its context; neither is retained. Set cache->valid to false when content changes.
// Failed allocation/encoding preserves the previous cache; empty clips skip the builder.
bool DrawCachedCanvasBitmap(Canvas *canvas, SpriteBitmapCache *cache, const CanvasBitmapPaint *paint);

#endif
