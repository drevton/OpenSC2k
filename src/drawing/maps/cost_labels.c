#include "drawing/maps/cost_labels.h"

#include "base/text.h"
#include "drawing/fonts/fonts.h"
#include "drawing/maps/map_render.h"
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
void RetainCityZoneCostLabel(Drawing *drawing, const CityMapViewConfig *view, CityRect bounds, int cost,
    const char *text, CityPoint position) {
  InvalidateCityMapRect(drawing, view, drawing->map_state.zone_cost_label_rect);
  drawing->map_state.zone_cost_label_rect = bounds;
  drawing->map_state.zone_cost_label_cost = cost;
  CopyText(drawing->map_state.zone_cost_label_text, sizeof(drawing->map_state.zone_cost_label_text), text);
  drawing->map_state.zone_cost_label_position = position;
  InvalidateCityMapRect(drawing, view, bounds);
}
