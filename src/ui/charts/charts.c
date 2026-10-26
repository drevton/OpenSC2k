#include "ui/windows/window_internal.h"
#include "base/text.h"
#include "ui/charts/charts.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "assets/catalog.h"
#include "assets/resource_types.h"
#include "assets/state.h"
#include "audio/state.h"
#include "base/byte_order.h"
#include "city/economy/history.h"
#include "city/economy/types.h"
#include "city/engine/city_types.h"
#include "city/presentation.h"
#include "city/state.h"
#include "drawing/bitmaps.h"
#include "drawing/fonts/fonts.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/raster/canvas_raster_internal.h"
#include "drawing/state.h"
#include "game/errors.h"
#include "game/state.h"
#include "ui/commands.h"
#include "ui/dialogs/map_data.h"
#include "ui/map.h"
#include "ui/startup/startup.h"
#include "ui/sdl.h"
#include "ui/state.h"
#include "ui/text/text.h"
#include "ui/tools/tools.h"
#include "ui/tools/types.h"
#include "ui/windows/windows.h"

static const char *const kIndustryNames[] = {
  "Metals and Mining",
  "Textiles",
  "Petrochemicals",
  "Food Production",
  "Construction",
  "Automotive",
  "Aerospace",
  "Finance",
  "Media",
  "Electronic Equipment",
  "Tourism",
};
static const char *const kDemographicCaptions[] = {
  "Population",
  "Health",
  "Education",
};

// Chart Canvas
// Canvas presentation of chart data, using shared interface fonts and software drawing.

static const i32 kChartCodes[CITY_CANVAS_CHART_COUNT] = {
  [CITY_CANVAS_CHART_HISTORY] = CITY_PANEL_HISTORY,
  [CITY_CANVAS_CHART_DEMOGRAPHICS] = CITY_PANEL_DEMOGRAPHICS,
  [CITY_CANVAS_CHART_NEIGHBORS] = CITY_PANEL_NEIGHBORS,
  [CITY_CANVAS_CHART_INDUSTRY] = CITY_PANEL_INDUSTRY,
};
enum { HISTORY_WINDOW_WIDTH = 520, HISTORY_WINDOW_HEIGHT = 216,
    HISTORY_LEGEND_WIDTH = 208, HISTORY_ROW_HEIGHT = 12, HISTORY_PERIOD_HEIGHT = 16,
    POPULATION_WINDOW_WIDTH = 380, POPULATION_WINDOW_HEIGHT = 234, POPULATION_TAB_HEIGHT = 18,
    INDUSTRY_WINDOW_WIDTH = 420, INDUSTRY_WINDOW_HEIGHT = 246, INDUSTRY_ROW_HEIGHT = 16,
    NEIGHBOR_TILE_WIDTH = CITY_NEIGHBOR_ART_WIDTH / CITY_NEIGHBOR_ART_SCALE,
    NEIGHBOR_TILE_HEIGHT = CITY_NEIGHBOR_ART_HEIGHT / CITY_NEIGHBOR_ART_SCALE,
    NEIGHBOR_MAP_TOP = 44, NEIGHBOR_MAP_MARGIN = 6,
    NEIGHBOR_WINDOW_WIDTH = 2 * NEIGHBOR_TILE_WIDTH + 2 * NEIGHBOR_MAP_MARGIN,
    NEIGHBOR_WINDOW_HEIGHT = NEIGHBOR_MAP_TOP + 2 * NEIGHBOR_TILE_HEIGHT + 8 };
static const u32 kHistoryColors[CITY_HISTORY_SERIES_COUNT] = {
  0xFFF0F0F0, 0xFF80C8FF, 0xFFFFA000, 0xFFFFFF40,
  0xFFB8B8B8, 0xFFB090E0, 0xFF88C840, 0xFFFF6060,
  0xFFFFD080, 0xFF40D8E8, 0xFF60D898, 0xFFFF90C0,
  0xFFFF60FF, 0xFF6098F0, 0xFFD0B080, 0xFFB8E860
};

typedef struct { int x, y; } CityChartPoint;
typedef struct {
  CityChartPoint point[20];
  u32 latest;
  i32 count;
} CityHistoryPlot;

// Fill caller-owned plot coordinates and scale from a selected history series and period. Rejects invalid dimensions,
// series, periods and NULL output; a successful plot borrows no City storage.
static bool BuildCityHistoryPlot(const City *city, int series, int period, int width, int height,
    CityHistoryPlot *plot) {
  if (plot == NULL || series < 0 || series >= CITY_HISTORY_SERIES_COUNT || period < 0 ||
      period > CITY_HISTORY_PERIOD_LAST || width < 4 || height < 4 || width > 32767 || height > 32767)
    return false;
  int maximum = (int)city->history_maxima.value[series];
  if (maximum == 0)
    return false;
  const CityHistoryRow *row = &city->history[series];
  // Plot each band from its oldest sample to its newest.
  int last = period == CITY_HISTORY_PERIOD_YEAR ? CITY_HISTORY_MONTHLY_FIRST + CITY_HISTORY_MONTHLY_COUNT - 1 :
      period == CITY_HISTORY_PERIOD_TEN_YEARS ? CITY_HISTORY_HALF_YEAR_FIRST + CITY_HISTORY_HALF_YEAR_COUNT - 1 :
      CITY_HISTORY_FIVE_YEAR_FIRST + CITY_HISTORY_FIVE_YEAR_COUNT - 1;
  plot->count = period == CITY_HISTORY_PERIOD_YEAR ? CITY_HISTORY_MONTHLY_COUNT :
      period == CITY_HISTORY_PERIOD_TEN_YEARS ? CITY_HISTORY_HALF_YEAR_COUNT : CITY_HISTORY_FIVE_YEAR_COUNT;
  // The final point uses five sixths of the available width.
  int x_divisor = period == CITY_HISTORY_PERIOD_YEAR ? 6 * (CITY_HISTORY_MONTHLY_COUNT - 1) :
      period == CITY_HISTORY_PERIOD_TEN_YEARS ? 6 * (CITY_HISTORY_HALF_YEAR_COUNT - 1) :
      6 * (CITY_HISTORY_FIVE_YEAR_COUNT - 1);
  for (int index = 0; index < plot->count; ++index) {
    u32 sample = row->sample[last - index];
    int product = (int)((u32)(height - 3) * sample);
    if (product == INT_MIN && maximum == -1)
      return false; // Reject division overflow in a malformed graph scale.
    int offset = product / maximum;
    plot->point[index] = (CityChartPoint){(width - 1) * 5 * index / x_divisor, height - 2 - offset};
  }
  plot->latest = row->sample[last - plot->count + 1];
  return true;
}

static CityRect HistoryControlBounds(const Window *window, int control) {
  CityRect b = window->bounds;
  int legend_left = b.right - HISTORY_LEGEND_WIDTH;
  if (control >= CITY_CONTROL_HISTORY_SERIES_FIRST && control <= CITY_CONTROL_HISTORY_SERIES_LAST) {
    int top = b.top + 20 + (control - CITY_CONTROL_HISTORY_SERIES_FIRST) * HISTORY_ROW_HEIGHT;
    return (CityRect){legend_left, top, b.right - 4, top + HISTORY_ROW_HEIGHT};
  }
  if (control >= CITY_CONTROL_HISTORY_PERIOD_FIRST && control <= CITY_CONTROL_HISTORY_PERIOD_LAST) {
    int period = control - CITY_CONTROL_HISTORY_PERIOD_FIRST;
    int width = legend_left - b.left - 8;
    return (CityRect){b.left + 4 + period * width / 3, b.bottom - HISTORY_PERIOD_HEIGHT - 2,
        b.left + 4 + (period + 1) * width / 3, b.bottom - 2};
  }
  if (control == CITY_CONTROL_HISTORY_PLOT)
    return (CityRect){b.left + 4, b.top + 20, legend_left - 4, b.bottom - HISTORY_PERIOD_HEIGHT - 18};
  if (control == CITY_CONTROL_HISTORY_LEGEND)
    return (CityRect){b.left + 4, b.bottom - HISTORY_PERIOD_HEIGHT - 18,
        legend_left - 4, b.bottom - HISTORY_PERIOD_HEIGHT - 2};
  return (CityRect){0};
}
static CityRect DemographicControlBounds(const Window *window, int control) {
  CityRect b = window->bounds;
  if (control >= CITY_CONTROL_POPULATION_GRAPH && control <= CITY_CONTROL_EDUCATION_GRAPH) {
    int mode = control - CITY_CONTROL_POPULATION_GRAPH, width = b.right - b.left;
    return (CityRect){b.left + mode * width / 3, b.top + 14,
        b.left + (mode + 1) * width / 3, b.top + 14 + POPULATION_TAB_HEIGHT};
  }
  if (control == CITY_CONTROL_DEMOGRAPHIC_CHART)
    return (CityRect){b.left + 34, b.top + 58, b.right - 6, b.bottom - 30};
  return (CityRect){0};
}

static CityRect IndustryControlBounds(const Window *window, int control) {
  CityRect b = window->bounds;
  if (control >= CITY_CONTROL_INDUSTRY_ACCOUNTS && control <= CITY_CONTROL_INDUSTRY_DEMAND) {
    int mode = control - CITY_CONTROL_INDUSTRY_ACCOUNTS, width = b.right - b.left;
    return (CityRect){b.left + mode * width / 3, b.top + 14,
        b.left + (mode + 1) * width / 3, b.top + 14 + POPULATION_TAB_HEIGHT};
  }
  if (control == CITY_CONTROL_INDUSTRY_CHART)
    return (CityRect){b.left + 122, b.top + 56, b.right - 62, b.top + 56 + CITY_INDUSTRY_COUNT * INDUSTRY_ROW_HEIGHT};
  return (CityRect){0};
}

static i32 ChartSlot(i32 code) {
  for (i32 i = 0; i < CITY_CANVAS_CHART_COUNT; ++i) if (kChartCodes[i] == code) return i;
  return -1;
}
const CityCanvasChartControl *Sc2kCanvasChartControl(const CityChartState *charts, i32 code, i32 id) {
  i32 slot = ChartSlot(code);
  if (slot < 0) return NULL;
  const CityCanvasChartDialog *dialog = &charts->resources.dialog[slot];
  for (i32 i = 0; i < dialog->control_count; ++i)
    if (dialog->control[i].id == id) return &dialog->control[i];
  return NULL;
}
i32 Sc2kCanvasChartCode(WindowKind kind) {
  return kind == CITY_CANVAS_HISTORY ? CITY_PANEL_HISTORY : kind == CITY_CANVAS_DEMOGRAPHICS ? CITY_PANEL_DEMOGRAPHICS :
      kind == CITY_CANVAS_NEIGHBORS ? CITY_PANEL_NEIGHBORS : kind == CITY_CANVAS_INDUSTRY ? CITY_PANEL_INDUSTRY : 0;
}
static void DrawChartLine(Canvas *canvas, i32 x0, i32 y0, i32 x1, i32 y1, u32 color) {
  i32 dx = x1 >= x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
  i32 dy = y1 >= y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
  i32 error = dx + dy;
  for (;;) {
    CanvasStore(canvas, x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    i32 twice = 2 * error;
    if (twice >= dy) { error += dy; x0 += sx; }
    if (twice <= dx) { error += dx; y0 += sy; }
  }
}
static void DrawHistoryLegend(Canvas *canvas, const Window *window) {
  const CityChartState *charts = &window->city->game->ui->charts;
  for (int series = 0; series < CITY_HISTORY_SERIES_COUNT; ++series) {
    CityRect row = HistoryControlBounds(window, CITY_CONTROL_HISTORY_SERIES_FIRST + series);
    bool selected = (window->policy_flags & (1 << series)) != 0;
    DrawCanvasButtonFrame(canvas, row, canvas_title, 0xFFD3BB93, 0xFF674F2B, !selected);
    CityRect swatch = {row.left + 2, row.top + 2, row.left + 10, row.bottom - 2};
    DrawCanvasFrame(canvas, swatch, kHistoryColors[series], 0xFF101010, 0xFF101010, false);
    const CityCanvasChartControl *item = Sc2kCanvasChartControl(charts, CITY_PANEL_HISTORY,
        CITY_CONTROL_HISTORY_SERIES_FIRST + series);
    char value[32];
    FormatGroupedInteger(value, sizeof(value), window->city->history[series].sample[0]);
    int value_width = CityInterfaceTextWidth(canvas->interface_font, value);
    u32 ink = selected ? 0xFFFCFCFC : canvas_text;
    CityRect clip = canvas->clip;
    canvas->clip = IntersectCityRects(clip,
        (CityRect){row.left + 12, row.top + 1, row.right - value_width - 6, row.bottom - 1});
    DrawCityInterfaceText(canvas->interface_font, canvas, row.left + 12, row.top + 1,
        item ? item->text : "", ink, selected);
    canvas->clip = IntersectCityRects(clip, row);
    DrawCityInterfaceText(canvas->interface_font, canvas, row.right - value_width - 3, row.top + 1,
        value, ink, selected);
    canvas->clip = clip;
  }
  static const char *const kPeriods[CITY_HISTORY_PERIOD_COUNT] = {"1 year", "10 years", "100 years"};
  for (int period = 0; period < CITY_HISTORY_PERIOD_COUNT; ++period) {
    CityRect button = HistoryControlBounds(window, CITY_CONTROL_HISTORY_PERIOD_FIRST + period);
    bool selected = window->data_map_selection == period;
    DrawCanvasButtonFrame(canvas, button, canvas_title, 0xFFD3BB93, 0xFF674F2B, !selected);
    DrawCityInterfaceTextCentered(canvas->interface_font, canvas, button, kPeriods[period],
        canvas_text, false, 10);
  }
}

static void DrawHistory(Canvas *canvas, const Window *window) {
  CityRect rect = HistoryControlBounds(window, CITY_CONTROL_HISTORY_PLOT);
  CityRect axis = HistoryControlBounds(window, CITY_CONTROL_HISTORY_LEGEND);
  DrawCanvasFrame(canvas, rect, 0xFF202020, 0xFF101010, 0xFF101010, false);
  CityRect previous = canvas->clip;
  canvas->clip = IntersectCityRects(previous, rect);
  int width = rect.right - rect.left, height = rect.bottom - rect.top;
  // Series retain their original independent scales; the shared grid has no Y labels.
  int count = window->data_map_selection == CITY_HISTORY_PERIOD_YEAR ? 12 : 20;
  int label_step = window->data_map_selection == CITY_HISTORY_PERIOD_YEAR ? 2 : 4;
  int plot_width = width - 4, plot_height = height - 4;
  for (int i = 1; i < 8; ++i) {
    int y = rect.top + i * (height - 1) / 8;
    CanvasFill(canvas, rect.left + 1, y, rect.right - 1, y + 1, 0xFF383838);
  }
  for (int i = 0; i < count; ++i) {
    int x = rect.left + 2 + i * (plot_width - 1) / (count - 1);
    CanvasFill(canvas, x, rect.top + 1, x + 1, rect.bottom - 1, 0xFF383838);
  }
  for (int series = 0; series < CITY_HISTORY_SERIES_COUNT; ++series) {
    if (!(window->policy_flags & (1 << series))) continue;
    CityHistoryPlot plot;
    if (!BuildCityHistoryPlot(window->city, series, window->data_map_selection, plot_width, plot_height, &plot))
      continue;
    // The stock plot leaves its final sixth for numeric labels. The legend now
    // owns those values, so spread the same sample points across the whole plot.
    int last_x = plot.point[plot.count - 1].x;
    for (int i = 0; i < plot.count; ++i) {
      int x = rect.left + 2 + (last_x ? plot.point[i].x * (plot_width - 1) / last_x : 0);
      int y = rect.top + 2 + plot.point[i].y;
      if (i) {
        int old_x = rect.left + 2 + (last_x ? plot.point[i - 1].x * (plot_width - 1) / last_x : 0);
        DrawChartLine(canvas, old_x, rect.top + 2 + plot.point[i - 1].y, x, y, kHistoryColors[series]);
      }
      CanvasFill(canvas, x - 1, y - 1, x + 2, y + 2, kHistoryColors[series]);
    }
  }
  canvas->clip = IntersectCityRects(previous, axis);
  for (int i = 0; i < count; i += label_step) {
    char label[16];
    if (window->data_map_selection == CITY_HISTORY_PERIOD_YEAR) {
      CopyText(label, sizeof(label), window->city->game->resources->calendar_names.abbreviated[
          (window->city->clock.month + i + 1) % CITY_MONTH_COUNT]);
    } else {
      int year = window->city->clock.year + window->city->geography.start_year;
      int years_ago = window->data_map_selection == CITY_HISTORY_PERIOD_TEN_YEARS ? (19 - i) / 2 : (19 - i) * 5;
      snprintf(label, sizeof(label), "%d", year - years_ago);
    }
    int x = rect.left + 2 + i * (plot_width - 1) / (count - 1);
    int label_width = CityInterfaceTextWidth(canvas->interface_font, label);
    x = IntClamp(x - label_width / 2, axis.left, axis.right - label_width);
    DrawCityInterfaceText(canvas->interface_font, canvas, x, axis.top + 2, label, canvas_text, false);
  }
  canvas->clip = previous;
  DrawHistoryLegend(canvas, window);
}

static void DrawDemographics(Canvas *canvas, const Window *window) {
  static const char *const kModes[CITY_DEMOGRAPHIC_MODE_COUNT] = {"Population", "Health", "Education"};
  static const u32 kColors[CITY_DEMOGRAPHIC_MODE_COUNT] = {0xFF80C8FF, 0xFF60D898, 0xFFFFD080};
  CityDemographicValues values;
  int mode = window->data_map_selection;
  if (!ReadCityDemographicValues(window->city, mode, &values)) return;
  for (int tab = 0; tab < CITY_DEMOGRAPHIC_MODE_COUNT; ++tab) {
    CityRect button = DemographicControlBounds(window, CITY_CONTROL_POPULATION_GRAPH + tab);
    bool selected = tab == mode;
    DrawCanvasButtonFrame(canvas, button, canvas_title, 0xFFD3BB93, 0xFF674F2B, !selected);
    DrawCityInterfaceTextCentered(canvas->interface_font, canvas, button, kModes[tab],
        selected ? 0xFFFCFCFC : canvas_text, selected, 10);
  }
  char summary[128], population[32];
  if (mode == CITY_DEMOGRAPHIC_POPULATION) {
    FormatGroupedInteger(population, sizeof(population), window->city->indicators.population);
    snprintf(summary, sizeof(summary), "Population: %s   Workforce: %d%%", population, values.summary);
  } else if (mode == CITY_DEMOGRAPHIC_HEALTH) {
    snprintf(summary, sizeof(summary), "Life expectancy: %d years", values.summary);
  } else {
    snprintf(summary, sizeof(summary), "Education quotient: %d", values.summary);
  }
  CityRect b = window->bounds;
  int summary_y = b.top + 14 + POPULATION_TAB_HEIGHT + 8;
  DrawCanvasFrame(canvas, (CityRect){b.left + 6, summary_y, b.left + 14, summary_y + 8},
      kColors[mode], 0xFF101010, 0xFF101010, false);
  DrawCityInterfaceText(canvas->interface_font, canvas, b.left + 19, summary_y - 1, summary, canvas_text, false);

  // Population uses actual percentages; the other modes retain their existing cohort metrics.
  int samples[CITY_AGE_COHORT_COUNT], maximum = mode == CITY_DEMOGRAPHIC_POPULATION ? 2000 : 100;
  for (int i = 0; i < CITY_AGE_COHORT_COUNT; ++i) {
    if (mode == CITY_DEMOGRAPHIC_POPULATION) {
      u32 total = window->city->indicators.population;
      samples[i] = total ? (u64)window->city->age_cohorts.value[i] * 10000 / total : 0;
    } else samples[i] = IntMax(0, values.value[i]);
    maximum = IntMax(maximum, samples[i]);
  }
  int step = mode == CITY_DEMOGRAPHIC_POPULATION ? 2000 : 20;
  maximum = (maximum + step - 1) / step * step;
  CityRect plot = DemographicControlBounds(window, CITY_CONTROL_DEMOGRAPHIC_CHART);
  DrawCanvasFrame(canvas, plot, 0xFF202020, 0xFF101010, 0xFF101010, false);
  int width = plot.right - plot.left - 4, height = plot.bottom - plot.top - 4;
  for (int row = 0; row <= 4; ++row) {
    int y = plot.bottom - 2 - row * height / 4;
    CanvasFill(canvas, plot.left + 1, y, plot.right - 1, y + 1, 0xFF383838);
    char label[24];
    if (mode == CITY_DEMOGRAPHIC_POPULATION) snprintf(label, sizeof(label), "%d%%", maximum * row / 400);
    else snprintf(label, sizeof(label), "%d", maximum * row / 4);
    int label_width = CityInterfaceTextWidth(canvas->interface_font, label);
    DrawCityInterfaceText(canvas->interface_font, canvas, plot.left - label_width - 4, y - 4,
        label, canvas_text, false);
  }
  CityRect clip = canvas->clip;
  canvas->clip = IntersectCityRects(clip, plot);
  for (int i = 0; i < CITY_AGE_COHORT_COUNT; ++i) {
    int left = plot.left + 2 + i * width / CITY_AGE_COHORT_COUNT,
        right = plot.left + 2 + (i + 1) * width / CITY_AGE_COHORT_COUNT;
    if (!(i & 1)) CanvasFill(canvas, left, plot.top + 1, left + 1, plot.bottom - 1, 0xFF383838);
    int bar_height = (i64)samples[i] * height / maximum;
    if (samples[i] > 0) bar_height = IntMax(1, bar_height);
    if (!bar_height) continue;
    int top = plot.bottom - 2 - bar_height;
    CanvasFill(canvas, left + 1, top, right - 1, plot.bottom - 2, kColors[mode]);
    CanvasFill(canvas, left + 1, top, right - 1, top + 1, 0xFFD8E8E8);
  }
  canvas->clip = clip;
  for (int i = 0; i < CITY_AGE_COHORT_COUNT; i += 2) {
    char age[8];
    snprintf(age, sizeof(age), "%d", i * 5);
    int center = plot.left + 2 + (2 * i + 1) * width / (2 * CITY_AGE_COHORT_COUNT);
    DrawCityInterfaceText(canvas->interface_font, canvas,
        center - CityInterfaceTextWidth(canvas->interface_font, age) / 2, plot.bottom + 3, age, canvas_text, false);
  }
  const char *oldest = "95";
  int oldest_x = plot.left + 2 + (2 * CITY_AGE_COHORT_COUNT - 1) * width / (2 * CITY_AGE_COHORT_COUNT);
  DrawCityInterfaceText(canvas->interface_font, canvas,
      oldest_x - CityInterfaceTextWidth(canvas->interface_font, oldest) / 2, plot.bottom + 3,
      oldest, canvas_text, false);
  DrawCityInterfaceTextCentered(canvas->interface_font, canvas,
      (CityRect){plot.left, plot.bottom + 15, plot.right, b.bottom - 2}, "Age (years)", canvas_text, false, 10);
}

static void FormatIndustryChartValue(char *text, size_t capacity, int mode, int value) {
  if (mode == CITY_INDUSTRY_ACCOUNTS) snprintf(text, capacity, "%.1f%%", value / 10.0);
  else if (mode == CITY_INDUSTRY_TAX) snprintf(text, capacity, "%d%%", value);
  else FormatGroupedInteger(text, capacity, value);
}

static void DrawIndustry(Canvas *canvas, const Window *window) {
  static const char *const kModes[CITY_INDUSTRY_MODE_COUNT] = {"Ratios", "Tax Rates", "Demand"};
  static const char *const kSummaries[CITY_INDUSTRY_MODE_COUNT] = {
    "Share of local industry", "Tax rate by industry", "Industry demand"
  };
  static const u32 kColors[CITY_INDUSTRY_MODE_COUNT] = {0xFF80C8FF, 0xFF60D898, 0xFFFFD080};
  const CityCanvasChartResources *resources = &window->city->game->ui->charts.resources;
  CityIndustryValues values;
  int mode = window->data_map_selection;
  if (!ReadCityIndustryValues(window->city, mode, &values)) return;
  CityRect b = window->bounds;
  for (int tab = 0; tab < CITY_INDUSTRY_MODE_COUNT; ++tab) {
    CityRect button = IndustryControlBounds(window, CITY_CONTROL_INDUSTRY_ACCOUNTS + tab);
    bool selected = tab == mode;
    DrawCanvasButtonFrame(canvas, button, canvas_title, 0xFFD3BB93, 0xFF674F2B, !selected);
    DrawCityInterfaceTextCentered(canvas->interface_font, canvas, button, kModes[tab],
        selected ? 0xFFFCFCFC : canvas_text, selected, 10);
  }
  DrawCityInterfaceText(canvas->interface_font, canvas, b.left + 6, b.top + 40,
      kSummaries[mode], canvas_text, false);
  u64 total = 0;
  if (mode == CITY_INDUSTRY_ACCOUNTS)
    for (int i = 0; i < CITY_INDUSTRY_COUNT; ++i) total += window->city->industry_accounts.value[i];
  int minimum = 0, maximum = values.maximum;
  for (int i = 0; i < CITY_INDUSTRY_COUNT; ++i) {
    if (mode == CITY_INDUSTRY_ACCOUNTS)
      values.value[i] = total ? (u64)window->city->industry_accounts.value[i] * 1000 / total : 0;
    minimum = IntMin(minimum, values.value[i]);
  }
  if (mode == CITY_INDUSTRY_ACCOUNTS) maximum = 1000;
  else {
    int step = mode == CITY_INDUSTRY_TAX ? 40 : 100;
    maximum = IntMax(step, (maximum + step - 1) / step * step);
    minimum = -((-minimum + step - 1) / step * step);
  }
  CityRect plot = IndustryControlBounds(window, CITY_CONTROL_INDUSTRY_CHART);
  DrawCanvasFrame(canvas, plot, 0xFF202020, 0xFF101010, 0xFF101010, false);
  int width = plot.right - plot.left - 4, range = maximum - minimum;
  int zero = plot.left + 2 + (i64)-minimum * width / range;
  for (int i = 0; i <= 4; ++i) {
    int x = plot.left + 2 + i * width / 4;
    CanvasFill(canvas, x, plot.top + 1, x + 1, plot.bottom - 1, 0xFF383838);
    char label[32];
    FormatIndustryChartValue(label, sizeof(label), mode, minimum + i * range / 4);
    int label_width = CityInterfaceTextWidth(canvas->interface_font, label);
    int left = IntClamp(x - label_width / 2, plot.left, plot.right - label_width);
    DrawCityInterfaceText(canvas->interface_font, canvas, left, plot.bottom + 3, label, canvas_text, false);
  }
  CanvasFill(canvas, zero, plot.top + 1, zero + 1, plot.bottom - 1, 0xFF888888);
  for (int i = 0; i < CITY_INDUSTRY_COUNT; ++i) {
    int top = plot.top + i * INDUSTRY_ROW_HEIGHT;
    CityRect clip = canvas->clip;
    canvas->clip = IntersectCityRects(clip, (CityRect){b.left + 6, top, plot.left - 4, top + INDUSTRY_ROW_HEIGHT});
    DrawCityInterfaceText(canvas->interface_font, canvas, b.left + 6, top + 2,
        resources->industry[i] ? resources->industry[i] : "", canvas_text, false);
    canvas->clip = IntersectCityRects(clip, plot);
    int end = plot.left + 2 + (i64)(values.value[i] - minimum) * width / range;
    if (end != zero) {
      int left = IntMin(zero, end), right = IntMax(zero, end);
      u32 color = values.value[i] < 0 ? 0xFFFF6060 : kColors[mode];
      CanvasFill(canvas, left, top + 2, right, top + INDUSTRY_ROW_HEIGHT - 2, color);
      CanvasFill(canvas, left, top + 2, right, top + 3, 0xFFD8E8E8);
    }
    canvas->clip = clip;
    char value[32];
    FormatIndustryChartValue(value, sizeof(value), mode, values.value[i]);
    int value_width = CityInterfaceTextWidth(canvas->interface_font, value);
    DrawCityInterfaceText(canvas->interface_font, canvas, b.right - value_width - 6, top + 2,
        value, canvas_text, false);
  }
}

static void DrawNeighborLabel(Canvas *canvas, CityRect bounds, const char *text) {
  for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x) {
      if (!x && !y) continue;
      DrawCityInterfaceTextCentered(canvas->interface_font, canvas,
          (CityRect){bounds.left + x, bounds.top + y, bounds.right + x, bounds.bottom + y},
          text, 0xFF101010, false, 10);
    }
  DrawCityInterfaceTextCentered(canvas->interface_font, canvas, bounds, text, 0xFFFCFCFC, false, 10);
}

// Native artwork has eight population stages, with empty local and neighboring cities sharing stage zero.
static int NeighborArtworkStage(u32 population) {
  if (!population) return CITY_NEIGHBOR_ART_EMPTY;
  if (population < 2000) return CITY_NEIGHBOR_ART_COTTAGES;
  if (population < 10000) return CITY_NEIGHBOR_ART_HAMLET;
  if (population < 50000) return CITY_NEIGHBOR_ART_SMALL_CITY;
  if (population < 100000) return CITY_NEIGHBOR_ART_LARGE_CITY;
  if (population < 500000) return CITY_NEIGHBOR_ART_METROPOLIS;
  return population < 1000000 ? CITY_NEIGHBOR_ART_HUGE_CITY : CITY_NEIGHBOR_ART_MEGACITY;
}

static void DrawNeighbors(Canvas *canvas, const Window *window) {
  // Draw the existing diamonds as-is. Neighbor origins differ by half an image
  // width and height; artwork receives only ordinary axis-aligned UI scaling.
  static const u8 kGrid[CITY_CANVAS_NEIGHBOR_ICON_COUNT][2] = {{1, 1}, {0, 0}, {2, 0}, {2, 2}, {0, 2}};
  static const u8 kPaintOrder[CITY_CANVAS_NEIGHBOR_ICON_COUNT] = {1, 2, 0, 4, 3};
  // Apply the screen-to-stored-side mapping once for both neighbor names and population.
  static const u8 kNeighborSides[4] = {2, 3, 0, 1};
  const CityResourceState *assets = window->city->game->resources;
  CityRect b = window->bounds;
  int tile_width = IntMin(NEIGHBOR_TILE_WIDTH, (b.right - b.left - 2 * NEIGHBOR_MAP_MARGIN) / 2) & ~3;
  int tile_height = tile_width / 2;
  int left = (b.left + b.right - 2 * tile_width) / 2, top = b.top + NEIGHBOR_MAP_TOP;
  CityRect regions[CITY_CANVAS_NEIGHBOR_ICON_COUNT];
  u32 populations[CITY_CANVAS_NEIGHBOR_ICON_COUNT];
  const char *names[CITY_CANVAS_NEIGHBOR_ICON_COUNT];
  CanvasFill(canvas, b.left + 2, b.top + 14, b.right - 2, b.bottom - 2, 0xFF222A30);
  for (int i = 0; i < CITY_CANVAS_NEIGHBOR_ICON_COUNT; ++i) {
    bool local = i == CITY_CANVAS_NEIGHBOR_ICON_CENTER;
    int side = local ? 0 : (kNeighborSides[i - 1] + window->city->geography.rotation) & 3;
    populations[i] = local ? ReadCityNeighborCentralPopulation(window->city) :
        ReadCityNeighborPopulation(window->city, side);
    int x = left + kGrid[i][0] * tile_width / 2, y = top + kGrid[i][1] * tile_height / 2;
    regions[i] = (CityRect){x, y, x + tile_width, y + tile_height};
    names[i] = local ? CityNameText(&window->city->name) : window->city->geography.neighbor_names.name[side];
  }
  for (int index = 0; index < CITY_CANVAS_NEIGHBOR_ICON_COUNT; ++index) {
    int i = kPaintOrder[index];
    CityRect region = regions[i];
    int stage = NeighborArtworkStage(populations[i]);
    if (!DrawCanvasSprite(canvas, window->picture_drawing, assets->neighbors[stage], region))
      window->city->game->execution_failed = true;
  }
  // Labels are the final layer, so the adjoining foreground regions cannot erase
  // a neighboring population or name. Match the reference's population/name order.
  for (int i = 0; i < CITY_CANVAS_NEIGHBOR_ICON_COUNT; ++i) {
    CityRect region = regions[i];
    int label_top = region.top + tile_height * 5 / 8;
    CityRect clip = canvas->clip;
    canvas->clip = IntersectCityRects(clip, region);
    char value[32];
    FormatGroupedInteger(value, sizeof(value), populations[i]);
    DrawNeighborLabel(canvas, (CityRect){region.left + 4, label_top, region.right - 4, label_top + 11}, value);
    DrawNeighborLabel(canvas, (CityRect){region.left + 4, label_top + 12, region.right - 4, label_top + 23}, names[i]);
    canvas->clip = clip;
  }
  char value[32], summary[96];
  u64 national = (u64)window->city->history[CITY_HISTORY_NATIONAL_POPULATION].sample[0] * 1000;
  FormatGroupedInteger(value, sizeof(value), national);
  snprintf(summary, sizeof(summary), "National population: %s", value);
  DrawCityInterfaceTextCentered(canvas->interface_font, canvas,
      (CityRect){b.left + 6, b.top + 18, b.right - 6, b.top + 32}, summary, 0xFFFCFCFC, true, 10);
}

static void Sc2kCanvasDrawCharts(Canvas *canvas, const Window *window) {
  if (canvas == NULL || window == NULL || window->city == NULL) return;
  CityRect b = window->bounds;
  DrawCanvasFrame(canvas, (CityRect){b.left, b.top + 14, b.right, b.bottom},
      canvas_title, 0xFFD3BB93, 0xFF674F2B, true);
  DrawCityWindowCaption(canvas, b, window->title, canvas_title, 0xFFD3BB93, 0xFF674F2B, 0);
  CityRect previous_clip = canvas->clip;
  CityRect interior = {window->bounds.left + 2, window->bounds.top + 16,
      window->bounds.right - 2, window->bounds.bottom - 2};
  if (window->kind != CITY_CANVAS_HISTORY)
    interior = (CityRect){window->bounds.left, window->bounds.top + 14,
        window->bounds.right, window->bounds.bottom - 2};
  canvas->clip = IntersectCityRects(previous_clip, interior);
  i32 code = Sc2kCanvasChartCode(window->kind);
  if (code == CITY_PANEL_HISTORY) DrawHistory(canvas, window);
  else if (code == CITY_PANEL_DEMOGRAPHICS && window->data_map_selection >= 0 &&
      window->data_map_selection < CITY_DEMOGRAPHIC_MODE_COUNT)
    DrawDemographics(canvas, window);
  else if (code == CITY_PANEL_NEIGHBORS) DrawNeighbors(canvas, window);
  else if (code == CITY_PANEL_INDUSTRY) DrawIndustry(canvas, window);
  canvas->clip = previous_clip;
}

bool Sc2kCanvasChartActionAt(const Window *window, i32 x, i32 y,
    i32 *code, i32 *control, i32 *value) {
  if (window == NULL || !window->visible || code == NULL || control == NULL || value == NULL) return false;
  *code = Sc2kCanvasChartCode(window->kind);
  i32 slot = ChartSlot(*code);
  if (slot < 0) return false;
  if (*code == CITY_PANEL_INDUSTRY) {
    for (int id = CITY_CONTROL_INDUSTRY_ACCOUNTS; id <= CITY_CONTROL_INDUSTRY_DEMAND; ++id) {
      if (!CanvasPointInRect(IndustryControlBounds(window, id), x, y)) continue;
      *control = id;
      *value = id - CITY_CONTROL_INDUSTRY_ACCOUNTS;
      return true;
    }
    return false;
  }
  if (*code == CITY_PANEL_DEMOGRAPHICS) {
    for (int id = CITY_CONTROL_POPULATION_GRAPH; id <= CITY_CONTROL_EDUCATION_GRAPH; ++id) {
      if (!CanvasPointInRect(DemographicControlBounds(window, id), x, y)) continue;
      *control = id;
      *value = id - CITY_CONTROL_POPULATION_GRAPH;
      return true;
    }
    return false;
  }
  if (*code == CITY_PANEL_HISTORY) {
    for (int id = CITY_CONTROL_HISTORY_SERIES_FIRST; id <= CITY_CONTROL_HISTORY_PERIOD_LAST; ++id) {
      if (!CanvasPointInRect(HistoryControlBounds(window, id), x, y)) continue;
      *control = id;
      *value = id < CITY_CONTROL_HISTORY_PERIOD_FIRST ?
          !(window->policy_flags & (1 << (id - CITY_CONTROL_HISTORY_SERIES_FIRST))) :
          id - CITY_CONTROL_HISTORY_PERIOD_FIRST;
      return true;
    }
    return false;
  }

  return false;
}

// Chart Host

static void InitializeChartLayout(CityChartState *charts, int slot) {
  static const char *const kTitles[] = {"City Graphs", "Population", "Neighbors", "Industry"};
  static const char *const kSeries[] = {"City size", "Residents", "Commerce", "Industry", "Traffic", "Pollution",
      "Land value", "Crime", "Electricity", "Water service", "Health", "Education", "Unemployment",
      "National economy", "National population", "National interest"};
  static const int kWidths[] = {HISTORY_WINDOW_WIDTH, POPULATION_WINDOW_WIDTH,
      NEIGHBOR_WINDOW_WIDTH, INDUSTRY_WINDOW_WIDTH};
  static const int kHeights[] = {HISTORY_WINDOW_HEIGHT, POPULATION_WINDOW_HEIGHT,
      NEIGHBOR_WINDOW_HEIGHT, INDUSTRY_WINDOW_HEIGHT};
  CityCanvasChartDialog *dialog = &charts->resources.dialog[slot];
  *dialog = (CityCanvasChartDialog){.bounds = {0, 0, kWidths[slot] - 4, kHeights[slot] - 18}};
  CopyText(charts->titles[slot], sizeof(charts->titles[slot]), kTitles[slot]);
  dialog->title = charts->titles[slot];
  if (slot == CITY_CANVAS_CHART_HISTORY) {
    dialog->control_count = CITY_HISTORY_SERIES_COUNT;
    for (int series = 0; series < CITY_HISTORY_SERIES_COUNT; ++series) {
      dialog->control[series].id = CITY_CONTROL_HISTORY_SERIES_FIRST + series;
      CopyText(dialog->control[series].text, sizeof(dialog->control[series].text), kSeries[series]);
    }
  }
}

bool PrepareCityChartPaintState(City *city, int code) {
  CityChartState *charts = &city->game->ui->charts;
  if (code == CITY_PANEL_INDUSTRY && charts->visible[CITY_CANVAS_CHART_INDUSTRY]) {
    CityIndustryValues values;
    if (!ReadCityIndustryValues(city, charts->industry_mode, &values)) return false;
    charts->industry_maximum = values.maximum;
  }
  return code == CITY_PANEL_HISTORY || code == CITY_PANEL_DEMOGRAPHICS || code == CITY_PANEL_INDUSTRY;
}

static Window *ChartWindow(CityChartState *charts, int slot) {
  if (charts->desktop == NULL) return NULL;
  return FindCityCanvasWindow(charts->desktop, charts->window_id[slot]);
}
bool IsCityPanelReady(const CityInterfaceState *ui, int code) {
  if (code == CITY_PANEL_MAP_DATA) return ui->map_data.desktop != NULL;
  return ChartSlot(code) >= 0 && ui->charts.ready;
}
static void UpdateChartWindow(City *city, int slot) {
  CityChartState *charts = &city->game->ui->charts;
  Window *window = ChartWindow(charts, slot);
  if (window == NULL) return;
  if (slot == CITY_CANVAS_CHART_HISTORY) {
    window->policy_flags = charts->history_series;
    window->data_map_selection = charts->history_period;
  } else if (slot == CITY_CANVAS_CHART_DEMOGRAPHICS) {
    window->data_map_selection = charts->demographic_mode;
    window->title = charts->demographic_captions[charts->demographic_mode];
  } else if (slot == CITY_CANVAS_CHART_INDUSTRY) {
    window->data_map_selection = charts->industry_mode;
  }
  if (slot == CITY_CANVAS_CHART_INDUSTRY)
    PrepareCityChartPaintState(city, CITY_PANEL_INDUSTRY);
  Sc2kDirtyMarkRect(&charts->desktop->dirty, window->bounds);
}
void HideCityChartPanel(City *city, int code) {
  int slot = ChartSlot(code);
  if (slot < 0) return;
  CityChartState *charts = &city->game->ui->charts;
  if (charts->window_id[slot] != 0 && charts->desktop != NULL)
    Sc2kCanvasDesktopDismissWindow(charts->desktop, charts->window_id[slot]);
  charts->window_id[slot] = 0;
  charts->visible[slot] = false;
}
bool SetCityPanelVisibilityByCode(City *city, int code) {
  CityChartState *charts = &city->game->ui->charts;
  int slot = ChartSlot(code);
  int button;
  bool visible;
  if (code == CITY_PANEL_MAP_DATA) {
    visible = ToggleCityMapDataDialog(city);
    button = CITY_TOOLBAR_BUTTON_MAP;
  } else {
    if (!IsCityPanelReady(city->game->ui, code)) return false;
    if (charts->visible[slot]) {
      HideCityChartPanel(city, code);
    } else {
      CityRect size = charts->resources.dialog[slot].bounds;
      int screen_width = charts->desktop->canvas_width, screen_height = charts->desktop->canvas_height;
      if (slot == CITY_CANVAS_CHART_NEIGHBORS) {
        const CityCanvasWindowPixelState *pixels = &charts->desktop->pixel_state;
        if (pixels->scale) {
          screen_width = (i64)pixels->width * 100 / pixels->scale;
          screen_height = (i64)pixels->height * 100 / pixels->scale;
        }
        size.right = IntMin(size.right, IntMax(80, screen_width - 8) - 4);
        int tile_width = IntMin((size.right + 4 - 2 * NEIGHBOR_MAP_MARGIN) / 2,
            IntMax(16, screen_height - NEIGHBOR_MAP_TOP - 16)) & ~3;
        size.right = 2 * tile_width + 2 * NEIGHBOR_MAP_MARGIN - 4;
        size.bottom = NEIGHBOR_MAP_TOP + tile_width + 8 - 18;
      }
      int left = IntMax((screen_width - size.right - 4) / 2, 0);
      int top = IntMax((screen_height - size.bottom - 18) / 2, 0);
      static const WindowKind kKinds[] = {
        CITY_CANVAS_HISTORY, CITY_CANVAS_DEMOGRAPHICS, CITY_CANVAS_NEIGHBORS, CITY_CANVAS_INDUSTRY
      };
      Window window = {0};
      window.kind = kKinds[slot];
      window.bounds = (CityRect){left, top, left + size.right + 4, top + size.bottom + 18};
      window.title = charts->resources.dialog[slot].title;
      window.city = city;
      int index = Sc2kCanvasDesktopOpen(charts->desktop, window);
      if (index < 0) { SetGameError(city->game, -3); return false; }
      charts->window_id[slot] = charts->desktop->windows[index].window_id;
      charts->visible[slot] = true;
      UpdateChartWindow(city, slot);
    }
    visible = charts->visible[slot];
    static const int kButtons[] = {CITY_TOOLBAR_BUTTON_GRAPHS, CITY_TOOLBAR_BUTTON_POPULATION,
      CITY_TOOLBAR_BUTTON_NEIGHBORS, CITY_TOOLBAR_BUTTON_INDUSTRY};
    button = kButtons[slot];
  }
  if ((GetCityToolbarStyle(city->game->ui, button) & CITY_TOOLBAR_STYLE_PRESSED) == 0)
    SetCityToolbarStyle(city->game->ui, button,
        visible ? CITY_TOOLBAR_STYLE_CHECKED_CHECKBOX : CITY_TOOLBAR_STYLE_CHECKBOX);
  return visible;
}

// Reconcile remembered panel visibility with the current stack before the shared pause is computed.
// Keep the chart order and map-data pass separate: closing each panel retains its own toolbar and overlay effects.
void ReconcileCityPanelWindows(City *city) {
  CityInterfaceState *ui = city->game->ui;
  if (city->game->command_depth == 0) {
    CityChartState *charts = &ui->charts;
    for (i32 slot = 0; slot < CITY_CANVAS_CHART_COUNT; ++slot) {
      if (!charts->visible[slot])
        continue;
      i32 open = 0;
      for (i32 index = 0; index < ui->desktop.count; ++index)
        open |= Sc2kCanvasChartCode(ui->desktop.windows[index].kind) == kChartCodes[slot];
      if (!open)
        SetCityPanelVisibilityByCode(city, kChartCodes[slot]);
    }
  }
  if (city->game->command_depth == 0 && ui->map_data.visible) {
    i32 open = 0;
    for (i32 index = 0; index < ui->desktop.count; ++index)
      open |= ui->desktop.windows[index].kind == CITY_CANVAS_MAP_DATA;
    if (!open)
      SetCityPanelVisibilityByCode(city, CITY_PANEL_MAP_DATA);
  }
}

bool SelectCityChartControl(City *city, int code, int control, int value) {
  if (!IsCityPanelReady(city->game->ui, code)) return false;
  CityChartState *charts = &city->game->ui->charts;
  if (code == CITY_PANEL_HISTORY) {
    if (control >= CITY_CONTROL_HISTORY_SERIES_FIRST && control <= CITY_CONTROL_HISTORY_SERIES_LAST &&
        (value == 0 || value == 1)) {
      u16 bit = 1 << (control - CITY_CONTROL_HISTORY_SERIES_FIRST);
      charts->history_series = value ? charts->history_series | bit : charts->history_series & ~bit;
    } else if (control >= CITY_CONTROL_HISTORY_PERIOD_FIRST && control <= CITY_CONTROL_HISTORY_PERIOD_LAST &&
        value == control - CITY_CONTROL_HISTORY_PERIOD_FIRST) {
      charts->history_period = value;
    } else return false;
  } else if (code == CITY_PANEL_DEMOGRAPHICS &&
      control >= CITY_CONTROL_POPULATION_GRAPH && control <= CITY_CONTROL_EDUCATION_GRAPH &&
      value == control - CITY_CONTROL_POPULATION_GRAPH) {
    charts->demographic_mode = value;
  } else if (code == CITY_PANEL_INDUSTRY &&
      control >= CITY_CONTROL_INDUSTRY_ACCOUNTS && control <= CITY_CONTROL_INDUSTRY_DEMAND &&
      value == control - CITY_CONTROL_INDUSTRY_ACCOUNTS) {
    charts->industry_mode = value;
  } else return false;
  UpdateChartWindow(city, ChartSlot(code));
  return true;
}
bool BindCityChartPanels(Game *game, CityCanvasDesktop *desktop) {
  CityChartState *charts = &game->ui->charts;
  if (charts->ready) return charts->desktop == desktop;
  charts->desktop = desktop;
  for (int slot = 0; slot < CITY_CANVAS_CHART_COUNT; ++slot)
    InitializeChartLayout(charts, slot);
  for (int i = 0; i < CITY_INDUSTRY_COUNT; ++i)
    charts->resources.industry[i] = kIndustryNames[i];
  charts->resources.workforce = "Workforce %s %d";
  charts->resources.resident_age = "Age distribution";
  charts->resources.nation = "National pop.: %lu000";
  for (int i = 0; i < CITY_DEMOGRAPHIC_MODE_COUNT; ++i)
    charts->demographic_captions[i] = kDemographicCaptions[i];
  charts->history_series = (1 << CITY_HISTORY_CITY_SIZE) | (1 << CITY_HISTORY_RESIDENTS) |
      (1 << CITY_HISTORY_CRIME) | (1 << CITY_HISTORY_EDUCATION);
  const CityResourceState *assets = game->resources;
  for (int stage = 0; stage < CITY_NEIGHBOR_ART_COUNT; ++stage)
    if (!assets->neighbors[stage]) return false;
  charts->ready = true;
  return true;
}
void RefreshCityChartPanels(City *city) {
  InvalidateCityInformation(city->game, CITY_UPDATE_HISTORY | CITY_UPDATE_POPULATION |
      CITY_UPDATE_NEIGHBORS | CITY_UPDATE_INDUSTRY);
}
void UnbindCityChartPanels(City *city) {
  for (int slot = 0; slot < CITY_CANVAS_CHART_COUNT; ++slot)
    HideCityChartPanel(city, kChartCodes[slot]);
  city->game->ui->charts.ready = false;
  city->game->ui->charts.desktop = NULL;
}

// Chart Windows

// Handles this window's painting and input; paint coordinates are local to the window.
bool ChartsWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    Canvas *canvas = event->canvas;
    Sc2kCanvasDrawCharts(canvas, window);
    return true;
  }
  if (event->kind != WINDOW_INPUT) return false;
  const CanvasSDLEvent *input = event->input;
  if (input->kind == CANVAS_SDL_POINTER_DOWN && input->button == CANVAS_SDL_BUTTON_LEFT) {
    int code, control, value;
    if (Sc2kCanvasChartActionAt(window, event->x, event->y, &code, &control, &value)) {
      SelectCityChartControl(game->city, code, control, value);
      return true;
    }
  }
  return false;
}

const char *CityChartTooltipAt(Game *game, const Window *window, int x, int y) {
  int code, control, value;
  if (Sc2kCanvasChartActionAt(window, x, y, &code, &control, &value)) {
    const CityCanvasChartControl *item = Sc2kCanvasChartControl(&game->ui->charts, code, control);
    return item ? item->text : "Choose the data shown in this graph.";
  }
  return "City statistics. Use the controls to choose the data and time period.";
}
