#include "ui/tools/cost_labels.h"

#include "city/engine/commands.h"
#include "city/state.h"
#include "drawing/fonts/fonts.h"
#include "drawing/maps/cost_labels.h"
#include "drawing/raster/canvas_raster_internal.h"
#include "drawing/state.h"
#include "game/state.h"
#include "ui/text/text.h"

// UI formats the drag price and selects affordability styling. Drawing owns retained text, rasterization and damage.

static void PresentCityZoneDragCost(City *city, CityMapViewConfig *view, u32 cost, int x, int y) {
  const CityZoneCostPresentation presentation = {.view = view};
  DrawCityZoneCostLabel(city, &presentation, (int)cost, x, y);
}

// Original cost bounds, eight outline passes and cash-dependent foreground.
// Font selection/measurement/rasterization are canvas host services, not CDC emulation.
void DrawCityZoneCostLabel(City *city, const CityZoneCostPresentation *presentation, int cost,
    int screen_x, int screen_y) {
  Drawing *drawing = city->game->drawing;
  const CityMapViewConfig *view = presentation->view;
  int zoom = view->zoom;
  int height = drawing->map_state.zone_cost_font_heights[zoom];
  char currency[CITY_CURRENCY_TEXT_CAPACITY];
  CityZoneCostPresentation formatted = *presentation;
  if (formatted.text == NULL) {
    FormatCityCurrency(city->game, currency, cost);
    formatted.text = currency;
  }
  presentation = &formatted;
  int half_width = (CanvasTextWidth(presentation->text) + 2) / 2;
  if (presentation->canvas == NULL && view->magnified_large_zoom) {
    // TileToScreen returns magnified coordinates; retained text uses map pixels.
    screen_x >>= 1;
    screen_y >>= 1;
  }
  CityRect bounds = {screen_x - half_width, screen_y - height,
      screen_x + half_width + 10, screen_y + height / 2};
  if (presentation->canvas == NULL) {
    RetainCityZoneCostLabel(drawing, view, bounds, cost, presentation->text, (CityPoint){screen_x, screen_y});
    return;
  }
  const CityRect *rect = &bounds;
  static const i8 kOutlineX[8] = {2, 0, 2, 0, 2, 0, 1, 1};
  static const i8 kOutlineY[8] = {1, -1, -1, 1, 0, 0, 1, -1};
  for (int pass = 0; pass < 8; ++pass)
    DrawCityZoneCostCanvasText(presentation->canvas, presentation->text, rect->left + kOutlineX[pass],
        height + rect->top + kOutlineY[pass], drawing->map_state.zone_cost_outline_color, rect);
  u32 color = AffordableCityCommandCost(city, cost) >= 0 ?
      drawing->map_state.zone_cost_affordable_color : drawing->map_state.zone_cost_unaffordable_color;
  DrawCityZoneCostCanvasText(presentation->canvas, presentation->text, rect->left + 1, height + rect->top, color, rect);
}

// Canvas presentation is borrowed only for this draw.
void DrawCityCanvasZoneCostLabel(City *city, Canvas *canvas, CityMapViewConfig *view, int cost, const char *text,
    int screen_x, int screen_y) {
  const CityZoneCostPresentation presentation = {canvas, text, view};
  DrawCityZoneCostLabel(city, &presentation, cost, screen_x, screen_y);
}

// The main map's repaint wrapper composes its retained label after terrain, signs and transient effects.
void DrawRetainedCityZoneCostLabel(City *city) {
  Drawing *drawing = city->game->drawing;
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
  CityPoint position = drawing->map_state.zone_cost_label_position;
  DrawCityCanvasZoneCostLabel(city, &canvas, drawing->default_map_view,
      drawing->map_state.zone_cost_label_cost, drawing->map_state.zone_cost_label_text, position.x, position.y);
}

void Sc2kShowZoneDragCost(City *city, CityMapViewConfig *view, u32 cost, int screen_x, int screen_y) {
  PresentCityZoneDragCost(city, view, cost, screen_x, screen_y);
}
