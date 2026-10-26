#ifndef SC2K_CANVAS_RASTER_INTERNAL_H
#define SC2K_CANVAS_RASTER_INTERNAL_H

#include <string.h>

#include "drawing/canvas.h"

// Small hot primitives stay inline so splitting painters does not add a
// function call for every pixel or span. Runtime raster state has one owner.

static inline int CanvasRasterEdge(int coordinate, int origin, int numerator, int denominator) {
  i64 product = ((i64)coordinate + origin) * numerator;
  return (int)(product / denominator + (product % denominator > 0));
}

static inline CanvasRaster CanvasRasterView(const Canvas *canvas) {
  if (canvas->raster.pixels == canvas->pixels)
    return canvas->raster;
  return (CanvasRaster){canvas->pixels, 1, 1, 0, 0,
      canvas->width, canvas->height, canvas->clip, 0, 0};
}

static inline CityRect CanvasRasterRect(const Canvas *canvas, CityRect rect) {
  rect = IntersectCityRects(rect, canvas->clip);
  rect = IntersectCityRects(rect, (CityRect){0, 0, canvas->width, canvas->height});
  if (canvas->raster.pixels == canvas->pixels) {
    const CanvasRaster *raster = &canvas->raster;
    rect = (CityRect){
        CanvasRasterEdge(rect.left, raster->origin_x, raster->numerator, raster->denominator) + raster->offset_x,
        CanvasRasterEdge(rect.top, raster->origin_y, raster->numerator, raster->denominator) + raster->offset_y,
        CanvasRasterEdge(rect.right, raster->origin_x, raster->numerator, raster->denominator) + raster->offset_x,
        CanvasRasterEdge(rect.bottom, raster->origin_y, raster->numerator, raster->denominator) + raster->offset_y};
    rect = IntersectCityRects(rect, raster->clip);
    rect = IntersectCityRects(rect, (CityRect){0, 0, raster->width, raster->height});
  }
  return rect;
}

// Image dimensions depend only on their logical size and scale. Rounding two
// absolute edges would make a bitmap grow/shrink as its position changes.
static inline CityRect CanvasRasterImageBounds(const Canvas *canvas, CityRect bounds) {
  CanvasRaster raster = CanvasRasterView(canvas);
  int left = CanvasRasterEdge(bounds.left, raster.origin_x, raster.numerator, raster.denominator) + raster.offset_x;
  int top = CanvasRasterEdge(bounds.top, raster.origin_y, raster.numerator, raster.denominator) + raster.offset_y;
  int width = CanvasRasterEdge(bounds.right - bounds.left, 0, raster.numerator, raster.denominator);
  int height = CanvasRasterEdge(bounds.bottom - bounds.top, 0, raster.numerator, raster.denominator);
  return (CityRect){left, top, left + width, top + height};
}

static inline void CanvasWrite4x4(u32 *destination, int stride, u32 color) {
  u32 block[4] = {color, color, color, color};
  memcpy(destination, block, sizeof(block));
  memcpy(destination + stride, block, sizeof(block));
  memcpy(destination + 2 * stride, block, sizeof(block));
  memcpy(destination + 3 * stride, block, sizeof(block));
}

static inline void CanvasWrite2x2(u32 *destination, int stride, u32 color) {
  u32 block[2] = {color, color};
  memcpy(destination, block, sizeof(block));
  memcpy(destination + stride, block, sizeof(block));
}

// Blend RGB with source coverage in 0..255. Ignore input upper bytes and return a zero upper byte.
// Each channel exactly matches (source * alpha + destination * (255 - alpha) + 127) / 255.
// Callers supply the output upper byte and maintain palette identities. This is not straight-alpha RGBA compositing.
static SC2K_FORCE_INLINE u32 BlendRGB(u32 source, u32 destination, int alpha) {
  int inverse = 255 - alpha;
  // Red and blue occupy separate 16-bit lanes. Each weighted sum fits u16,
  // so their multiplication and exact rounded division can share u32 work.
  u32 rb = (source & 0xFF00FF) * alpha + (destination & 0xFF00FF) * inverse + 0x800080;
  rb = (rb + ((rb >> 8) & 0xFF00FF)) >> 8 & 0xFF00FF;
  u32 green = (source >> 8 & 255) * alpha + (destination >> 8 & 255) * inverse + 128;
  green = (green + (green >> 8)) >> 8;
  return rb | green << 8;
}

// Write one already clipped physical pixel with coverage in 1..255; no transform or draw-call accounting.
// Full coverage copies color unchanged. Partial coverage blends RGB into an opaque result, recording the old
// palette identity before clearing it. Keep this inline for coverage and publication glyph inner loops.
static SC2K_FORCE_INLINE void CanvasRasterBlendPixel(Canvas *canvas, int x, int y, u32 color, int alpha) {
  size_t index = (size_t)y * canvas->stride + x;
  u32 value = color;
  if (alpha != 255) {
    u32 background = canvas->pixels[index];
    if (canvas->palette_indices)
      RecordCanvasPaletteComposite(canvas, y, x, x + 1,
          CanvasPaletteIndexMask(canvas->palette_indices[(size_t)y * canvas->index_stride + x]));
    value = 0xFF000000 | BlendRGB(color, background, alpha);
  }
  canvas->pixels[index] = value;
  if (canvas->palette_indices && (!canvas->palette_rows || canvas->palette_rows[y]))
    canvas->palette_indices[(size_t)y * canvas->index_stride + x] = 0;
}

// Fill an already clipped physical rectangle without transforms, blending or draw-call accounting. Backing pixels
// and optional palette-index rows must cover it; writing color clears tracked palette identities first.
static inline void CanvasRasterFill(Canvas *canvas, CityRect rect, u32 color) {
  int width = rect.right - rect.left, height = rect.bottom - rect.top;
  if (width <= 0 || height <= 0) return;
  if (canvas->palette_indices)
    for (int y = rect.top; y < rect.bottom; ++y)
      if (!canvas->palette_rows || canvas->palette_rows[y])
        memset(canvas->palette_indices + (size_t)y * canvas->index_stride + rect.left, 0, width);
  u32 *destination = canvas->pixels + (size_t)rect.top * canvas->stride + rect.left;
  // memcpy permits unaligned destinations and preserves aliasing rules. These
  // constant-size copies compile to wide stores without requiring SIMD ISA.
  u32 block[4] = {color, color, color, color};
  if (width == 4 && height == 4) {
    CanvasWrite4x4(destination, canvas->stride, color);
    return;
  }
  if (width == 2 && height == 2) {
    CanvasWrite2x2(destination, canvas->stride, color);
    return;
  }
  for (int y = 0; y < height; ++y) {
    u32 *row = destination + (size_t)y * canvas->stride;
    int x = 0;
    for (; x + 4 <= width; x += 4) memcpy(row + x, block, sizeof(block));
    for (; x < width; ++x) row[x] = color;
  }
}

static inline void CanvasFill(Canvas *canvas, int left, int top, int right, int bottom, u32 color) {
  CanvasRasterFill(canvas, CanvasRasterRect(canvas, (CityRect){left, top, right, bottom}), color);
}

static inline void CanvasStore(Canvas *canvas, int x, int y, u32 color) {
  CanvasFill(canvas, x, y, x + 1, y + 1, color);
}

#endif
