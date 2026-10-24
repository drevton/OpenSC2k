#include "drawing/raster/canvas_primitives.h"

#include <stdlib.h>

#include "drawing/fonts/fonts.h"
#include "drawing/raster/canvas_raster_internal.h"

const u32 canvas_text = 0xFF101010u;
const u32 canvas_title = 0xFF97835Bu;
const u32 canvas_face = 0xFF838583;
const u32 canvas_light = 0xFFA8A8A8;
const u32 canvas_dark = 0xFF636562;
const u32 canvas_edge = 0xFF636562;

i32 CanvasScaleFloor(i32 value, i32 numerator, i32 denominator) {
  i64 product = (i64)value * numerator;
  return (i32)(product / denominator - (product % denominator < 0));
}

i32 CanvasScaleCeil(i32 value, i32 numerator, i32 denominator) {
  return -CanvasScaleFloor(-value, numerator, denominator);
}

CityRect CanvasScaleInnerRect(CityRect b, i32 numerator, i32 denominator) {
  return (CityRect){CanvasScaleCeil(b.left, numerator, denominator),
                    CanvasScaleCeil(b.top, numerator, denominator),
                    CanvasScaleFloor(b.right, numerator, denominator),
                    CanvasScaleFloor(b.bottom, numerator, denominator)};
}

void PaintScaledCanvas(Canvas *framebuffer, i32 numerator, i32 denominator,
    CanvasScaledPaint paint, void *context) {
  if (!framebuffer || !framebuffer->pixels || !paint || numerator < 1 || numerator > 1600 ||
      denominator < 1 || denominator > 1600 || framebuffer->width < 1 || framebuffer->height < 1)
    return;
  Canvas logical = *framebuffer;
  logical.raster = (CanvasRaster){framebuffer->pixels, numerator, denominator, 0, 0,
      framebuffer->width, framebuffer->height, framebuffer->clip, 0, 0};
  logical.width = (i32)(((i64)framebuffer->width * denominator + numerator - 1) / numerator);
  logical.height = (i32)(((i64)framebuffer->height * denominator + numerator - 1) / numerator);
  logical.clip = (CityRect){0, 0, logical.width, logical.height};
  paint(&logical, context);
}

bool CanvasPointInRect(CityRect rect, i32 x, i32 y) {
  return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

static bool CanvasBlitIndexedBlocks(Canvas *canvas, const u8 *indices,
    i32 source_stride, const u32 *palette, CityRect area);

// Half-pixel artwork preserves the diagonal pin and shade silhouette at larger UI sizes.
// These small monochrome masks draw directly into the final framebuffer, like the close glyph.
void DrawCityWindowControlIcon(Canvas *canvas, CityRect bounds, CityWindowControlIcon icon, bool pressed) {
  pressed |= RegisterCityCanvasButton(canvas, bounds);
  static const u16 kControlPixels[][16] = {
    {0, 0, 0x7FFE, 0x7FFE, 0x0180, 0x03C0, 0x07E0, 0x0FF0,
     0x1FF8, 0x3FFC, 0x7FFE, 0x7FFE, 0, 0, 0, 0},
    {0, 0, 0, 0, 0x0FF0, 0x0FF0, 0x0FF0, 0x0FF0,
     0x0FF0, 0x0FF0, 0x0FF0, 0x0FF0, 0, 0, 0, 0},
    {0x0C00, 0x1E00, 0x3F00, 0x7F80, 0xFFFE, 0xFFFE, 0x7FFC, 0x3FF8,
     0x1FF0, 0x0FE0, 0x0FD0, 0x0FB8, 0x0F1C, 0x0E0E, 0x0C07, 0x0003}
  };
  CanvasRaster raster = CanvasRasterView(canvas);
  Canvas pixels = *canvas;
  pixels.raster = raster;
  pixels.raster.origin_x *= 2; pixels.raster.origin_y *= 2;
  pixels.raster.denominator *= 2;
  pixels.width *= 2; pixels.height *= 2;
  pixels.clip = (CityRect){canvas->clip.left * 2, canvas->clip.top * 2,
      canvas->clip.right * 2, canvas->clip.bottom * 2};
  int left = (bounds.left + (bounds.right - bounds.left - 8) / 2 + pressed) * 2;
  int top = (bounds.top + (bounds.bottom - bounds.top - 8) / 2 + pressed) * 2;
  for (int row = 0; row < 16; ++row)
    for (int column = 0; column < 16; ++column)
      if (kControlPixels[icon][row] & (1 << column))
        CanvasFill(&pixels, left + column, top + row, left + column + 1, top + row + 1, 0xFF101010);
}

// Width is in the caller's coordinate space; scoped canvas scaling expands
// the stroke into physical pixels along with its endpoints. Square end caps.
void DrawCanvasLine(Canvas *canvas, int x0, int y0, int x1, int y1, u32 color, int width) {
  if (width <= 0) return;
  i64 dx = llabs((i64)x1 - x0), dy = -llabs((i64)y1 - y0), error = dx + dy;
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int before = width >> 1, after = width - before;
  for (;;) {
    CanvasFill(canvas, x0 - before, y0 - before, x0 + after, y0 + after, color);
    if (x0 == x1 && y0 == y1) break;
    i64 twice = 2 * error;
    if (twice >= dy) { error += dy; x0 += sx; }
    if (twice <= dx) { error += dx; y0 += sy; }
  }
}

void Sc2kCanvasBlitIndexed(Canvas *canvas, const u8 *indices, i32 source_stride,
    const u32 *palette, CityRect area) {
  area = IntersectCityRects(area, canvas->clip);
  area = IntersectCityRects(area, (CityRect){0, 0, canvas->width, canvas->height});
  if (area.left >= area.right || area.top >= area.bottom) return;
  if (CanvasBlitIndexedBlocks(canvas, indices, source_stride, palette, area)) return;
  for (i32 y = area.top; y < area.bottom; ++y)
    for (i32 x = area.left; x < area.right; ++x)
      CanvasStore(canvas, x, y, palette[indices[y * source_stride + x]]);
}

static CityRect CanvasCloseRect(CityRect bounds) {
  return (CityRect){bounds.left, bounds.top, bounds.left + 11, bounds.top + 14};
}

bool CanvasCloseAt(CityRect bounds, i32 x, i32 y) {
  CityRect close = CanvasCloseRect(bounds);
  return x >= close.left && x < close.right && y >= close.top && y < close.bottom;
}

static void DrawCaptionBorder(Canvas *canvas, CityRect b, u32 face, u32 light, u32 dark, bool lowered) {
  DrawCanvasFrame(canvas, b, face, light, dark, !lowered);
}

void DrawCityWindowCaption(Canvas *canvas, CityRect bounds, const char *title,
    u32 face, u32 light, u32 dark, int controls) {
  CityRect close = CanvasCloseRect(bounds);
  DrawCanvasButtonFrame(canvas, close, face, light, dark, true);
  DrawCityInterfaceClose(canvas->interface_font, canvas, close, 0xFF101010);
  CityRect caption = {close.right, bounds.top, bounds.right - controls, bounds.top + 14};
  DrawCaptionBorder(canvas, caption, face, light, dark, false);
  DrawCaptionBorder(canvas, (CityRect){caption.left + 1, caption.top + 1,
      caption.right - 1, caption.bottom - 1}, face, light, dark, true);
  CityRect clip = canvas->clip;
  canvas->clip.left = IntMax(clip.left, caption.left + 2);
  canvas->clip.right = IntMin(clip.right, caption.right - 2);
  DrawCityInterfaceTextCentered(canvas->interface_font, canvas, caption, title, 0xFFFCFCFC, true, 10);
  canvas->clip = clip;
}

void CanvasPanelFrame(Canvas *canvas, CityRect bounds, const char *title) {
  CanvasBeveledBox(canvas, bounds.left, bounds.top + 14, bounds.right, bounds.bottom, canvas_face, 1);
  DrawCityWindowCaption(canvas, bounds, title ? title : "", canvas_title, 0xFFD3BB93, 0xFF674F2B, 0);
}

void CanvasBeveledBox(Canvas *canvas, i32 left, i32 top, i32 right, i32 bottom,
    u32 fill, i32 raised) {
  u32 light = fill == canvas_title ? 0xFFD3BB93u : canvas_light;
  u32 dark = fill == canvas_title ? 0xFF674F2Bu : canvas_dark;
  if (raised && fill == canvas_light)
    fill = canvas_face;
  DrawCanvasFrame(canvas, (CityRect){left, top, right, bottom}, fill, light, dark, raised != 0);
}

bool CityCanvasButtonPressed(const Canvas *canvas, CityRect b) {
  const CityRect *press = canvas->pressed_button;
  return press && b.left == press->left && b.top == press->top &&
      b.right == press->right && b.bottom == press->bottom;
}

bool RegisterCityCanvasButton(Canvas *canvas, CityRect b) {
  if (canvas->button_probe && CanvasPointInRect(b, canvas->button_probe->x, canvas->button_probe->y)) {
    canvas->button_probe->bounds = b;
    canvas->button_probe->found = true;
  }
  return CityCanvasButtonPressed(canvas, b);
}

bool DrawCanvasButtonFrame(Canvas *canvas, CityRect b, u32 fill, u32 light, u32 dark, bool raised) {
  bool pressed = RegisterCityCanvasButton(canvas, b);
  DrawCanvasFrame(canvas, b, fill, light, dark, raised && !pressed);
  return pressed;
}

bool DrawCanvasButtonBox(Canvas *canvas, i32 left, i32 top, i32 right, i32 bottom, u32 fill, i32 raised) {
  u32 light = fill == canvas_title ? 0xFFD3BB93 : canvas_light;
  u32 dark = fill == canvas_title ? 0xFF674F2B : canvas_dark;
  if (raised && fill == canvas_light) fill = canvas_face;
  return DrawCanvasButtonFrame(canvas, (CityRect){left, top, right, bottom}, fill, light, dark, raised != 0);
}

// Borders can stay one physical pixel while the control's rectangle and content scale.
void DrawCanvasFrame(Canvas *canvas, CityRect b, u32 fill, u32 light, u32 dark, bool raised) {
  Canvas physical;
  if (canvas->unscaled_bevels) {
    CanvasRaster r = CanvasRasterView(canvas);
    physical = *canvas;
    physical.clip = CanvasRasterRect(canvas, canvas->clip);
    physical.width = r.width; physical.height = r.height;
    physical.raster = (CanvasRaster){0};
    b = (CityRect){CanvasRasterEdge(b.left, r.origin_x, r.numerator, r.denominator) + r.offset_x,
        CanvasRasterEdge(b.top, r.origin_y, r.numerator, r.denominator) + r.offset_y,
        CanvasRasterEdge(b.right, r.origin_x, r.numerator, r.denominator) + r.offset_x,
        CanvasRasterEdge(b.bottom, r.origin_y, r.numerator, r.denominator) + r.offset_y};
    canvas = &physical;
  }
  CanvasFill(canvas, b.left, b.top, b.right, b.bottom, fill);
  // Corner ownership follows OpenTTD's DrawFrameRect for both bevel states.
  if (raised) {
    CanvasFill(canvas, b.left, b.top, b.left + 1, b.bottom - 1, light);
    CanvasFill(canvas, b.left + 1, b.top, b.right - 1, b.top + 1, light);
    CanvasFill(canvas, b.right - 1, b.top, b.right, b.bottom - 1, dark);
    CanvasFill(canvas, b.left, b.bottom - 1, b.right, b.bottom, dark);
  } else {
    CanvasFill(canvas, b.left, b.top, b.left + 1, b.bottom, dark);
    CanvasFill(canvas, b.left + 1, b.top, b.right, b.top + 1, dark);
    CanvasFill(canvas, b.right - 1, b.top + 1, b.right, b.bottom - 1, light);
    CanvasFill(canvas, b.left + 1, b.bottom - 1, b.right, b.bottom, light);
  }
}

void DrawCityCheckbox(Canvas *canvas, CityRect bounds, bool checked, u32 face, u32 light, u32 dark) {
  bool pressed = RegisterCityCanvasButton(canvas, bounds);
  DrawCanvasFrame(canvas, bounds, checked ? 0xFF68AC34 : face, light, dark, false);
  int thumb_width = (bounds.right - bounds.left) * 3 / 10;
  if (checked) bounds.left = bounds.right - thumb_width;
  else bounds.right = bounds.left + thumb_width;
  DrawCanvasFrame(canvas, bounds, 0xFFE8B810, 0xFFFCF880, 0xFFB87818, !pressed);
}

// Canvas Raster
// Drawing subsystem: canvas raster.

// Indexed opaque runs at integer 2x/4x avoid per-pixel coordinate division and
// clipping. A run with a partial physical edge falls back to CanvasStore.
static bool CanvasBlitIndexedBlocks(Canvas *canvas, const u8 *indices,
    i32 source_stride, const u32 *palette, CityRect area) {
  const CanvasRaster *r = &canvas->raster;
  if (r->pixels != canvas->pixels || r->numerator % r->denominator) return false;
  i32 scale = r->numerator / r->denominator;
  if (scale != 2 && scale != 4) return false;
  i32 left = (area.left + r->origin_x) * scale + r->offset_x, top = (area.top + r->origin_y) * scale + r->offset_y;
  i32 right = (area.right + r->origin_x) * scale + r->offset_x, bottom = (area.bottom + r->origin_y) * scale + r->offset_y;
  if (left < 0 || top < 0 || right > r->width || bottom > r->height ||
      left < r->clip.left || top < r->clip.top || right > r->clip.right || bottom > r->clip.bottom)
    return false;
  if (canvas->palette_indices) for (int y = top; y < bottom; ++y)
    if (!canvas->palette_rows || canvas->palette_rows[y])
      memset(canvas->palette_indices + (size_t)y * canvas->index_stride + left, 0, right - left);
  for (i32 y = area.top; y < area.bottom; ++y) {
    const u8 *source = indices + (size_t)y * source_stride + area.left;
    u32 *row = canvas->pixels + (size_t)((y + r->origin_y) * scale + r->offset_y) * canvas->stride + left;
    if (scale == 4) {
      for (i32 x = area.left; x < area.right; ++x, row += 4)
        CanvasWrite4x4(row, canvas->stride, palette[*source++]);
    } else {
      for (i32 x = area.left; x < area.right; ++x, row += 2)
        CanvasWrite2x2(row, canvas->stride, palette[*source++]);
    }
  }
  return true;
}
