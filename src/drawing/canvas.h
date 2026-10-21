#ifndef SC2K_CANVAS_H
#define SC2K_CANVAS_H

#include <stddef.h>

#include "city/types.h"

// Text-block alignment used by the software canvas, not GDI flags.
typedef enum CityCanvasTextAlignment {
  CITY_CANVAS_TEXT_LEFT = 0, CITY_CANVAS_TEXT_CENTER = 1, CITY_CANVAS_TEXT_RIGHT = 2,
} CityCanvasTextAlignment;

enum {
  CITY_TEXT_UPDATE_POSITION = 0x001,
  CITY_TEXT_RIGHT_UPDATE_POSITION = 0x003,
};

typedef void (*CityCanvasVisiblePaint)(CityRect area, void *context);

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
  const struct CityButtonFeedback *button_feedback; // Borrowed UI presentation state.
  CityCanvasButtonProbe *button_probe; // Optional input-time hit collection with an empty paint clip.
  u32 button_window_id;
  u32 *palette_composite_rows; // Palette dependencies requiring repaint after alpha compositing.
  u32 *palette_composite_bounds; // Packed left/right u16 bounds, valid when the corresponding mask is nonzero.
} Canvas;

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
