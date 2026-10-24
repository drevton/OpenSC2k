#include "drawing/frame_overlay.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "drawing/fonts/fonts.h"
#include "drawing/raster/screen_damage.h"
#include "drawing/state.h"

// Temporary upload overlays borrow Drawing's scroll scratch after all map scrolling and composition have finished.
// Preserve every covered rectangle before either overlay draws, then restore pixels even when upload fails.

typedef struct {
  Canvas *canvas;
  u32 *original;
  u32 sequence;
} FrameOverlayPixels;

static void TintRedrawRect(CityRect rect, void *context) {
  FrameOverlayPixels *tint = context;
  Canvas *canvas = tint->canvas;
  // Vary presentation colors without touching the simulation RNG.
  u32 seed = ++tint->sequence * 0x9E3779B9;
  int hue = (seed >> 16) % 1536, ramp = hue & 255;
  u32 color;
  switch (hue >> 8) {
  case 0: color = 0xFF0000 | ramp << 8; break;
  case 1: color = (255 - ramp) << 16 | 0xFF00; break;
  case 2: color = 0xFF00 | ramp; break;
  case 3: color = (255 - ramp) << 8 | 0xFF; break;
  case 4: color = ramp << 16 | 0xFF; break;
  default: color = 0xFF0000 | (255 - ramp); break;
  }
  u32 half_color = (color & 0xFEFEFE) >> 1;
  for (int y = rect.top; y < rect.bottom; ++y) {
    size_t offset = (size_t)y * canvas->stride + rect.left;
    u32 *pixels = canvas->pixels + offset;
    for (int x = 0; x < rect.right - rect.left; ++x)
      pixels[x] = (pixels[x] & 0xFF000000) | (((pixels[x] & 0xFEFEFE) >> 1) + half_color);
  }
}

static void SaveOverlayRect(CityRect rect, void *context) {
  const FrameOverlayPixels *tint = context;
  for (int y = rect.top; y < rect.bottom; ++y) {
    size_t offset = (size_t)y * tint->canvas->stride + rect.left;
    memcpy(tint->original + offset, tint->canvas->pixels + offset, (rect.right - rect.left) * sizeof(u32));
  }
}

// These pixels exist only during upload; neither debug overlay damages the map cache.
static CityRect RenderStatsBounds(const Drawing *drawing, const Canvas *canvas) {
  int scale = drawing->options.scale;
  int width = (260 * scale + 99) / 100, height = (71 * scale + 99) / 100;
  int left = IntMin(8, canvas->width), top = IntMin(40 * scale / 100, canvas->height);
  return IntersectCityRects((CityRect){left, top, left + width, top + height},
      (CityRect){0, 0, canvas->width, canvas->height});
}

static void DrawRenderStats(Drawing *drawing, const CityInterfaceFont *font, Canvas *canvas, CityRect bounds) {
  const DrawingFrameStats *stats = &drawing->frame_stats;
  int refresh = drawing->options.refresh_rate > 0 ? drawing->options.refresh_rate : 60;
  double budget = 1000000.0 / refresh, elapsed = stats->seconds * 1000000;
  u64 calls = stats->calls.sprite_calls;
  u64 visits = stats->tile_visits;
  u64 visible = stats->tile_visible_visits;
  u64 regions = stats->repaint_regions;
  Canvas panel = {.pixels = canvas->pixels, .stride = canvas->stride, .width = 260, .height = 71,
      .clip = {0, 0, 260, 71}, .scalable_font = true, .font_antialias = true};
  panel.raster = (CanvasRaster){.pixels = panel.pixels, .numerator = drawing->options.scale, .denominator = 100,
      .width = canvas->width, .height = canvas->height, .clip = bounds,
      .offset_x = bounds.left, .offset_y = bounds.top};
  DrawCityInterfaceTextBold(font, &panel, 2, 3, "F9  Render statistics", 0xFFFFD080, 8);
  char text[128];
  snprintf(text, sizeof(text), "Draw calls: %" PRIu64, calls);
  DrawCityInterfaceTextBold(font, &panel, 2, 15, text, 0xFFE8EEE8, 8);
  snprintf(text, sizeof(text), "Tile visits: %" PRIu64 " / %" PRIu64 " visible", visits, visible);
  DrawCityInterfaceTextBold(font, &panel, 2, 26, text, 0xFFE8EEE8, 8);
  snprintf(text, sizeof(text), "Map regions: %" PRIu64 "  F7 coalesce: %s", regions,
      drawing->disable_repaint_coalescing ? "off" : "on");
  DrawCityInterfaceTextBold(font, &panel, 2, 37, text, 0xFFE8EEE8, 8);
  snprintf(text, sizeof(text), "Draw CPU: %.0f us  (%.1f%%)", elapsed, elapsed * 100 / budget);
  DrawCityInterfaceTextBold(font, &panel, 2, 48, text, elapsed > budget ? 0xFFFF8080 : 0xFFE8EEE8, 8);
  snprintf(text, sizeof(text), "Budget: %.0f us @ %d Hz", budget, refresh);
  DrawCityInterfaceTextBold(font, &panel, 2, 59, text, 0xFFB8C4B8, 8);
}

static void RestoreOverlayRect(CityRect rect, void *context) {
  const FrameOverlayPixels *tint = context;
  for (int y = rect.top; y < rect.bottom; ++y) {
    size_t offset = (size_t)y * tint->canvas->stride + rect.left;
    memcpy(tint->canvas->pixels + offset, tint->original + offset, (rect.right - rect.left) * sizeof(u32));
  }
}

DrawingFrameOverlay BeginDrawingFrameOverlay(Drawing *drawing, const CityInterfaceFont *font) {
  Canvas canvas = drawing->framebuffer;
  // Scrolling has finished. Preserve the union of both debug overlays in its
  // existing scratch plane before either overlay writes overlapping pixels.
  FrameOverlayPixels tint = {&canvas, drawing->scroll_colors, drawing->redraw_tint_sequence};
  CityRect stats = drawing->show_render_stats ? RenderStatsBounds(drawing, &canvas) : (CityRect){0};
  if (drawing->show_redraw_rects)
    VisitScreenDamage(&drawing->world_damage, SaveOverlayRect, &tint);
  if (drawing->show_render_stats) SaveOverlayRect(stats, &tint);
  if (drawing->show_redraw_rects) {
    VisitScreenDamage(&drawing->world_damage, TintRedrawRect, &tint);
    drawing->redraw_tint_sequence = tint.sequence;
  }
  if (drawing->show_render_stats) DrawRenderStats(drawing, font, &canvas, stats);
  CityRect debug = drawing->show_redraw_rects ? drawing->world_damage.bounds : (CityRect){0};
  if (drawing->show_render_stats)
    debug = debug.left < debug.right && debug.top < debug.bottom ? UnionCityRects(debug, stats) : stats;
  // Current and previous bounds are uploaded even on frames without map damage.
  CityRect restore = drawing->redraw_rects_bounds;
  if (restore.left >= restore.right || restore.top >= restore.bottom) restore = debug;
  else if (debug.left < debug.right && debug.top < debug.bottom) restore = UnionCityRects(restore, debug);
  return (DrawingFrameOverlay){.statistics = stats, .current = debug, .upload = restore};
}

void EndDrawingFrameOverlay(Drawing *drawing, const DrawingFrameOverlay *overlay, bool uploaded) {
  Canvas canvas = drawing->framebuffer;
  FrameOverlayPixels tint = {.canvas = &canvas, .original = drawing->scroll_colors};
  if (drawing->show_redraw_rects) VisitScreenDamage(&drawing->world_damage, RestoreOverlayRect, &tint);
  if (drawing->show_render_stats) RestoreOverlayRect(overlay->statistics, &tint);
  if (uploaded) {
    drawing->redraw_rects_bounds = overlay->current;
    drawing->frame_stats.pending = false;
    ClearScreenDamage(&drawing->upload_damage);
    ClearScreenDamage(&drawing->world_damage);
  }
}
