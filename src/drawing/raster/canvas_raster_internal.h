#ifndef SC2K_CANVAS_RASTER_INTERNAL_H
#define SC2K_CANVAS_RASTER_INTERNAL_H

#include <string.h>

#include "drawing/drawing.h"

// Small hot primitives stay inline so splitting painters does not add a
// function call for every pixel or span. Runtime raster state has one owner.

static inline i32 CanvasRasterEdge(i32 coordinate, i32 origin, i32 numerator, i32 denominator) {
  i64 product = ((i64)coordinate + origin) * numerator;
  return (i32)(product / denominator + (product % denominator > 0));
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
    const CanvasRaster *r = &canvas->raster;
    rect = (CityRect){CanvasRasterEdge(rect.left, r->origin_x, r->numerator, r->denominator) + r->offset_x,
        CanvasRasterEdge(rect.top, r->origin_y, r->numerator, r->denominator) + r->offset_y,
        CanvasRasterEdge(rect.right, r->origin_x, r->numerator, r->denominator) + r->offset_x,
        CanvasRasterEdge(rect.bottom, r->origin_y, r->numerator, r->denominator) + r->offset_y};
    rect = IntersectCityRects(rect, r->clip);
    rect = IntersectCityRects(rect, (CityRect){0, 0, r->width, r->height});
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

static inline void CanvasWrite4x4(u32 *destination, i32 stride, u32 color) {
  u32 block[4] = {color, color, color, color};
  memcpy(destination, block, 16);
  memcpy(destination + stride, block, 16);
  memcpy(destination + 2 * stride, block, 16);
  memcpy(destination + 3 * stride, block, 16);
}

static inline void CanvasWrite2x2(u32 *destination, i32 stride, u32 color) {
  u32 block[2] = {color, color};
  memcpy(destination, block, 8);
  memcpy(destination + stride, block, 8);
}

static inline void CanvasRasterFill(Canvas *canvas, CityRect rect, u32 color) {
  i32 width = rect.right - rect.left, height = rect.bottom - rect.top;
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
  for (i32 y = 0; y < height; ++y) {
    u32 *row = destination + (size_t)y * canvas->stride;
    i32 x = 0;
    for (; x + 4 <= width; x += 4) memcpy(row + x, block, 16);
    for (; x < width; ++x) row[x] = color;
  }
}

static inline void CanvasFill(Canvas *canvas, i32 left, i32 top, i32 right, i32 bottom, u32 color) {
  CanvasRasterFill(canvas, CanvasRasterRect(canvas, (CityRect){left, top, right, bottom}), color);
}

static inline void CanvasStore(Canvas *canvas, i32 x, i32 y, u32 color) {
  CanvasFill(canvas, x, y, x + 1, y + 1, color);
}

static inline void CanvasDimPixel(Canvas *canvas, i32 x, i32 y) {
  CityRect rect = CanvasRasterRect(canvas, (CityRect){x, y, x + 1, y + 1});
  for (i32 py = rect.top; py < rect.bottom; ++py)
    for (i32 px = rect.left; px < rect.right; ++px) {
      u32 *pixel = canvas->pixels + (size_t)py * canvas->stride + px;
      if (canvas->palette_indices)
        RecordCanvasPaletteComposite(canvas, py, px, px + 1,
            CanvasPaletteIndexMask(canvas->palette_indices[(size_t)py * canvas->index_stride + px]));
      if (canvas->palette_indices && (!canvas->palette_rows || canvas->palette_rows[py])) canvas->palette_indices[(size_t)py * canvas->index_stride + px] = 0;
      *pixel = ((*pixel & 0xFFFEFEFEu) >> 1) + 0xFF000000u;
    }
}

#endif
