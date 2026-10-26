#include "drawing/maps/cost_labels.h"

#include "base/text.h"
#include "drawing/fonts/fonts.h"
#include "drawing/maps/map_view.h"
#include "drawing/raster/canvas_raster_internal.h"
#include "drawing/state.h"

// The caller has selected a canvas target; retained labels return before reaching this raster path.
void DrawCityZoneCostCanvasText(Canvas *canvas, const char *text, int x, int baseline_y,
    u32 color, const CityRect *bounds) {
  CityRect previous_clip = canvas->clip;
  canvas->clip = IntersectCityRects(previous_clip, *bounds);
  u32 argb = 0xFF000000u | ((color & 255) << 16) | (color & 0xFF00) | ((color >> 16) & 255);
  CanvasTextAt(canvas, x, baseline_y - 8, text, argb);
  canvas->clip = previous_clip;
}

// Erase the last retained drag label through normal map damage.
void ClearCityZoneCostLabel(Drawing *drawing, const CityMapViewConfig *view) {
  InvalidateCityMapRect(drawing, view, drawing->map_state.zone_cost_label_rect);
  drawing->map_state.zone_cost_label_rect = (CityRect){0};
}

// Copy formatted text into Drawing's retained label, damaging both its old and new bounds.
static void RetainCityZoneCostLabel(Drawing *drawing, const CityMapViewConfig *view, CityRect bounds, int cost,
    const char *text, CityPoint position) {
  InvalidateCityMapRect(drawing, view, drawing->map_state.zone_cost_label_rect);
  drawing->map_state.zone_cost_label_rect = bounds;
  drawing->map_state.zone_cost_label_cost = cost;
  CopyText(drawing->map_state.zone_cost_label_text, sizeof(drawing->map_state.zone_cost_label_text), text);
  drawing->map_state.zone_cost_label_position = position;
  InvalidateCityMapRect(drawing, view, bounds);
}

void PaintCityZoneCostLabel(Drawing *drawing, const CityZoneCostPresentation *presentation, int cost,
    CityPoint position, bool affordable) {
  const CityMapViewConfig *view = presentation->view;
  int height = drawing->map_state.zone_cost_font_heights[view->zoom];
  int half_width = (CanvasTextWidth(presentation->text) + 2) / 2;
  if (presentation->canvas == NULL && view->magnified_large_zoom) {
    // TileToScreen returns magnified coordinates; retained text uses map pixels.
    position.x >>= 1;
    position.y >>= 1;
  }
  CityRect bounds = {position.x - half_width, position.y - height,
      position.x + half_width + 10, position.y + height / 2};
  if (presentation->canvas == NULL) {
    RetainCityZoneCostLabel(drawing, view, bounds, cost, presentation->text, position);
    return;
  }
  static const i8 kOutlineX[8] = {2, 0, 2, 0, 2, 0, 1, 1};
  static const i8 kOutlineY[8] = {1, -1, -1, 1, 0, 0, 1, -1};
  for (int pass = 0; pass < 8; ++pass)
    DrawCityZoneCostCanvasText(presentation->canvas, presentation->text, bounds.left + kOutlineX[pass],
        height + bounds.top + kOutlineY[pass], drawing->map_state.zone_cost_outline_color, &bounds);
  u32 color = affordable ? drawing->map_state.zone_cost_affordable_color :
      drawing->map_state.zone_cost_unaffordable_color;
  DrawCityZoneCostCanvasText(presentation->canvas, presentation->text, bounds.left + 1,
      height + bounds.top, color, &bounds);
}

void PaintRetainedCityZoneCostLabel(Drawing *drawing, bool affordable) {
  CityRect label = drawing->map_state.zone_cost_label_rect;
  if (label.left >= label.right || label.top >= label.bottom) return;
  int magnification = drawing->default_map_view->magnified_large_zoom ? 2 : 1;
  Canvas canvas = drawing->map_target.canvas;
  canvas.raster = CanvasRasterView(&canvas);
  canvas.raster.numerator = drawing->target_scale;
  canvas.raster.denominator = 100;
  canvas.raster.origin_x = canvas.raster.origin_y = 0;
  canvas.raster.offset_x = drawing->target_x;
  canvas.raster.offset_y = drawing->target_y;
  canvas.width = drawing->map_width / magnification;
  canvas.height = drawing->map_height / magnification;
  canvas.clip = (CityRect){0, 0, canvas.width, canvas.height};
  const CityZoneCostPresentation presentation = {
    .canvas = &canvas, .text = drawing->map_state.zone_cost_label_text, .view = drawing->default_map_view,
  };
  PaintCityZoneCostLabel(drawing, &presentation, drawing->map_state.zone_cost_label_cost,
      drawing->map_state.zone_cost_label_position, affordable);
}
