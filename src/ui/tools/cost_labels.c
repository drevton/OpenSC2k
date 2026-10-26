#include "ui/tools/cost_labels.h"

#include "base/time.h"
#include "city/engine/commands.h"
#include "city/state.h"
#include "drawing/maps/map_effects.h"
#include "drawing/state.h"
#include "ui/text/text.h"

// UI formats the drag price and selects affordability styling. Drawing owns retained text, rasterization and damage.

// Currency formatting and affordability are UI policy; Drawing receives the resolved text and style.
void DrawCityZoneCostLabel(const City *city, const CityZoneCostPresentation *presentation, int cost,
    int screen_x, int screen_y) {
  char currency[CITY_CURRENCY_TEXT_CAPACITY];
  CityZoneCostPresentation formatted = *presentation;
  if (formatted.text == NULL) {
    FormatCityCurrency(city->game, currency, cost);
    formatted.text = currency;
  }
  bool affordable = presentation->canvas && AffordableCityCommandCost(city, cost) >= 0;
  PaintCityZoneCostLabel(city->drawing, &formatted, cost, (CityPoint){screen_x, screen_y}, affordable);
}

// Canvas presentation is borrowed only for this draw.
void DrawCityCanvasZoneCostLabel(const City *city, Canvas *canvas, CityMapViewConfig *view, int cost, const char *text,
    int screen_x, int screen_y) {
  const CityZoneCostPresentation presentation = {canvas, text, view};
  DrawCityZoneCostLabel(city, &presentation, cost, screen_x, screen_y);
}

// The main map's repaint wrapper composes its retained label after terrain, signs and transient effects.
void DrawRetainedCityZoneCostLabel(const City *city) {
  Drawing *drawing = city->drawing;
  bool affordable = AffordableCityCommandCost(city, drawing->map_state.zone_cost_label_cost) >= 0;
  PaintRetainedCityZoneCostLabel(drawing, affordable);
}

// Present a completed construction charge using host time and the current currency convention.
void ShowCityConstructionCost(const City *city, TileIndex tile, i64 cost) {
  if (cost <= 0 || !IsMapTileValid(city, tile)) return;
  double now = MonotonicSeconds();
  char currency[CITY_CURRENCY_TEXT_CAPACITY];
  FormatCityCurrency(city->game, currency, cost);
  StartCityConstructionCost(city->drawing, tile, currency, now);
}
