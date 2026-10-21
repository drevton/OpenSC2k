#include "city/engine/commands.h"
#include "ui/map.h"
#include "drawing/drawing_internal.h"


// Native cost text: the established canvas font in canvas or indexed map pixels.

void PresentCityZoneDragCost(City *city, Drawing *drawing, CityMapViewConfig *view, u32 cost, int x, int y) {
  const CityZoneCostPresentation presentation = {.view = view};
  DrawCityZoneCostLabel(city, &presentation, (int)cost, x, y);
}

int MeasureCityZoneCostText(const CityZoneCostPresentation *presentation, int cost, int zoom) {
  const char *text = presentation->text;
  return CanvasTextWidth(text) + 2;
}

static u8 ZoneCostPaletteIndex(const Drawing *drawing, u32 color) {
  int best_distance = INT32_MAX;
  int best_index = 0;
  for (int index = 0; index < 256; ++index) {
    const CityPaletteEntry *entry = &drawing->palette.current[index];
    int red = (int)entry->red - (int)(color & 255);
    int green = (int)entry->green - (int)((color >> 8) & 255);
    int blue = (int)entry->blue - (int)((color >> 16) & 255);
    int distance = red * red + green * green + blue * blue;
    if (distance < best_distance) { best_distance = distance; best_index = (u8)index; }
  }
  return best_index;
}

void DrawCityZoneCostText(Game *game, const CityZoneCostPresentation *presentation, int cost,
    int zoom,
    int x, int baseline_y, u32 color, const CityRect *bounds) {
  Drawing *drawing = game->drawing;
  const char *text = presentation->text;
  Canvas *canvas = presentation->canvas;
  if (canvas != NULL) {
    CityRect previous_clip = canvas->clip;
    canvas->clip = IntersectCityRects(previous_clip, *bounds);
    u32 argb = 0xFF000000u | ((color & 255) << 16) | (color & 0xFF00) | ((color >> 16) & 255);
    CanvasTextAt(canvas, x, baseline_y - 8, text, argb);
    canvas->clip = previous_clip;
    return;
  }
  CityMapViewConfig *view = presentation->view;
  if (view == NULL || view->pixels == NULL) {
    return;
  }
  u8 *pixels = view->pixels;
  u32 stride = view->paint_pixel_stride;
  int height = view->paint_mode_word, direction = -1;
  CityRect clip = IntersectCityRects(*bounds, (CityRect){0, 0, (int)stride, height});
  int ink = ZoneCostPaletteIndex(drawing, color);
  while (*text != '\0') {
    const CityCanvasGlyph *glyph = &kCanvasFont[ReadCityFontCharacter(&text)];
    for (int row = 0; row < glyph->height; ++row)
      for (int bit = 0; bit < glyph->width; ++bit) {
        int px = x + glyph->offset_x + bit, py = baseline_y - 8 + glyph->offset_y + row;
        if ((glyph->rows[row] & (1u << bit)) && px >= clip.left && px < clip.right &&
            py >= clip.top && py < clip.bottom) {
          int target_y = direction > 0 ? height - 1 - py : py;
          pixels[(u32)target_y * stride + (u32)px] = ink;
        }
      }
    x += glyph->advance;
  }
}

// Erase the last retained drag label through normal map damage.
void ClearCityZoneCostLabel(Drawing *drawing, const CityMapViewConfig *view) {
  InvalidateCityMapRect(drawing, view, drawing->map_state.zone_cost_label_rect);
  drawing->map_state.zone_cost_label_rect = (CityRect){0};
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
  int half_width = MeasureCityZoneCostText(presentation, cost, zoom) / 2;
  if (presentation->canvas == NULL && view->magnified_large_zoom) {
    // TileToScreen returns magnified coordinates; retained text uses map pixels.
    screen_x >>= 1;
    screen_y >>= 1;
  }
  CityRect bounds = {screen_x - half_width, screen_y - height,
      screen_x + half_width + 10, screen_y + height / 2};
  if (presentation->canvas == NULL) {
    InvalidateCityMapRect(drawing, view, drawing->map_state.zone_cost_label_rect);
    drawing->map_state.zone_cost_label_rect = bounds;
    drawing->map_state.zone_cost_label_cost = cost;
    CopyText(drawing->map_state.zone_cost_label_text, sizeof(drawing->map_state.zone_cost_label_text),
        presentation->text);
    drawing->map_state.zone_cost_label_position = (CityPoint){screen_x, screen_y};
    InvalidateCityMapRect(drawing, view, bounds);
    return;
  }
  const CityRect *rect = &bounds;
  static const i8 kOutlineX[8] = {2, 0, 2, 0, 2, 0, 1, 1};
  static const i8 kOutlineY[8] = {1, -1, -1, 1, 0, 0, 1, -1};
  for (int pass = 0; pass < 8; ++pass)
    DrawCityZoneCostText(city->game, presentation, cost, zoom, rect->left + kOutlineX[pass],
        height + rect->top + kOutlineY[pass], drawing->map_state.zone_cost_outline_color, rect);
  u32 color = AffordableCityCommandCost(city, cost) >= 0 ?
      drawing->map_state.zone_cost_affordable_color : drawing->map_state.zone_cost_unaffordable_color;
  DrawCityZoneCostText(city->game, presentation, cost, zoom, rect->left + 1, height + rect->top, color, rect);
}

// Canvas presentation is borrowed only for this draw.
void DrawCityCanvasZoneCostLabel(City *city, Canvas *canvas, CityMapViewConfig *view, int cost, const char *text,
    int screen_x, int screen_y) {
  const CityZoneCostPresentation presentation = {canvas, text, view};
  DrawCityZoneCostLabel(city, &presentation, cost, screen_x, screen_y);
}

void Sc2kShowZoneDragCost(City *city, CityMapViewConfig *view, u32 cost, int screen_x, int screen_y) {
  Drawing *drawing = city->game->drawing;
  PresentCityZoneDragCost(city, drawing, view, cost, screen_x, screen_y);
}
