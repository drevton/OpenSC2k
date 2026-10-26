#include "drawing/newspaper.h"

#include <math.h>
#include <stdlib.h>

#include "drawing/raster/canvas_raster_internal.h"
#include "drawing/state.h"

void ReleaseCityNewspaperOpening(Drawing *drawing) {
  free(drawing->newspaper_opening.pixels);
  drawing->newspaper_opening = (Canvas){0};
  drawing->newspaper_opening_window = 0;
  drawing->newspaper_opening_bounds = (CityRect){0};
}

bool ResetCityNewspaperOpening(Drawing *drawing, int width, int height) {
  ReleaseCityNewspaperOpening(drawing);
  u32 *storage = width > 0 && height > 0 && (size_t)width <= SIZE_MAX / height / sizeof(u32) ?
      calloc((size_t)width * height, sizeof(u32)) : NULL;
  if (!storage) return false;
  drawing->newspaper_opening = (Canvas){.pixels = storage, .width = width, .height = height, .stride = width};
  return true;
}

CityRect CityNewspaperOpeningBounds(CityRect bounds, double scale, double angle) {
  double width = (bounds.right - bounds.left) * scale, height = (bounds.bottom - bounds.top) * scale;
  double x = (bounds.left + bounds.right) * 0.5, y = (bounds.top + bounds.bottom) * 0.5;
  double extent_x = (fabs(cos(angle)) * width + fabs(sin(angle)) * height) * 0.5;
  double extent_y = (fabs(sin(angle)) * width + fabs(cos(angle)) * height) * 0.5;
  return (CityRect){(int)floor(x - extent_x) - 1, (int)floor(y - extent_y) - 1,
      (int)ceil(x + extent_x) + 1, (int)ceil(y + extent_y) + 1};
}

void PaintCityNewspaperOpening(Drawing *drawing, Canvas *canvas, CityRect pixels, double scale, double angle) {
  CanvasRaster raster = CanvasRasterView(canvas);
  const Canvas *image = &drawing->newspaper_opening;
  int width = image->width, height = image->height;
  CityRect area = CityNewspaperOpeningBounds(pixels, scale, angle);
  drawing->newspaper_opening_bounds = area;
  area = IntersectCityRects(area, raster.clip);
  area = IntersectCityRects(area, (CityRect){0, 0, raster.width, raster.height});
  double inverse_cos = cos(angle) / scale, inverse_sin = sin(angle) / scale;
  double center_x = (pixels.left + pixels.right) * 0.5, center_y = (pixels.top + pixels.bottom) * 0.5;
  int step_x = (int)llround(inverse_cos * 65536), step_y = (int)llround(-inverse_sin * 65536);
  // Increment inverse coordinates along each scanline; no trigonometry or divisions per pixel.
  for (int y = area.top; y < area.bottom; ++y) {
    i64 sx = (i64)llround((width * 0.5 + inverse_cos * (area.left + 0.5 - center_x) +
        inverse_sin * (y + 0.5 - center_y)) * 65536);
    i64 sy = (i64)llround((height * 0.5 - inverse_sin * (area.left + 0.5 - center_x) +
        inverse_cos * (y + 0.5 - center_y)) * 65536);
    for (int x = area.left; x < area.right; ++x, sx += step_x, sy += step_y) {
      int source_x = (int)(sx >> 16), source_y = (int)(sy >> 16);
      if ((u32)source_x >= (u32)width || (u32)source_y >= (u32)height) continue;
      size_t index = (size_t)y * canvas->stride + x;
      canvas->pixels[index] = image->pixels[(size_t)source_y * width + source_x];
      if (canvas->palette_indices && (!canvas->palette_rows || canvas->palette_rows[y]))
        canvas->palette_indices[(size_t)y * canvas->index_stride + x] = 0;
    }
  }
}
