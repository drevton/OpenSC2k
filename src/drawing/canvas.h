#ifndef SC2K_CANVAS_H
#define SC2K_CANVAS_H

#include <stddef.h>

#include "drawing/types.h"

// Operation-local Drawing view of the final framebuffer; copies are independent.
typedef struct {
  u32 *pixels;
  i32 numerator, denominator, origin_x, origin_y;
  i32 width, height;
  CityRect clip;
  i32 offset_x, offset_y;
} CanvasRaster;

// Operation-local hit collection from the same rectangles used to draw buttons.
typedef struct {
  int x, y;
  bool found;
  CityRect bounds;
} CityCanvasButtonProbe;

typedef struct {
  u64 sprite_calls;
  bool active;
} CanvasDrawStats;

typedef struct Canvas {
  u32 *pixels;
  i32 width;
  i32 height;
  i32 stride;
  CityRect clip;
  CanvasRaster raster;
  u8 *palette_indices;
  int index_stride;
  u32 *palette_rows;
  CanvasDrawStats *draw_stats; // Borrowed frame counters; disabled outside preparation.
  const struct CityInterfaceFont *interface_font; // Borrowed UI glyph assets; NULL for map rasters.
  bool unscaled_bevels, scalable_font, font_antialias;
  const CityRect *pressed_button; // Borrowed bounds in this canvas's coordinates; NULL when no press applies.
  CityCanvasButtonProbe *button_probe; // Optional input-time hit collection with an empty paint clip.
  u32 *palette_composite_rows; // Palette dependencies requiring repaint after alpha compositing.
  u32 *palette_composite_bounds; // Packed left/right u16 bounds, valid when the corresponding mask is nonzero.
} Canvas;

// Scale signed coordinates by a positive ratio, with mathematical floor/ceiling rounding.
i32 CanvasScaleFloor(i32 value, i32 numerator, i32 denominator);
i32 CanvasScaleCeil(i32 value, i32 numerator, i32 denominator);
// Keep only fully covered pixels: round left/top up and right/bottom down. Fractional edges remain outside.
// The result may be empty or inverted; the caller decides whether it has any area.
CityRect CanvasScaleInnerRect(CityRect bounds, i32 numerator, i32 denominator);

// Borrow a logical canvas backed by physical framebuffer pixels for one synchronous paint callback.
// The callback uses canvas drawing operations; physical stride, palette planes and raster clip stay shared.
// Ratios must be in 1..1600. Invalid/missing targets or callbacks are no-ops; no pixel buffer is allocated.
void PaintScaledCanvas(Canvas *framebuffer, i32 numerator, i32 denominator, CanvasScaledPaint paint, void *context);

// UI resolves the active window before painting. Drawing only matches its borrowed press bounds.
bool CityCanvasButtonPressed(const Canvas *canvas, CityRect bounds);

// Index zero is fixed RGB. Collisions between indices modulo 32 are conservative.
static inline u32 CanvasPaletteIndexMask(int index) {
  return index ? (u32)1 << (index & 31) : 0;
}

static inline void RecordCanvasPaletteComposite(Canvas *canvas, int row, int left, int right, u32 buckets) {
  if (!buckets || !canvas->palette_composite_rows) return;
  if (canvas->palette_composite_bounds) {
    u32 previous = canvas->palette_composite_bounds[row];
    if (canvas->palette_composite_rows[row]) {
      left = IntMin(left, previous >> 16);
      right = IntMax(right, previous & 65535);
    }
    canvas->palette_composite_bounds[row] = (u32)left << 16 | IntMin(right, 65535);
  }
  canvas->palette_composite_rows[row] |= buckets;
}

#endif
