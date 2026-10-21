#ifndef SC2K_DOMAIN_SRC_UI_CHARTS_CHARTS_H
#define SC2K_DOMAIN_SRC_UI_CHARTS_CHARTS_H

#include "city/economy/types.h"
#include "city/presentation.h"

#include "city/saveload/file.h"
#include "drawing/canvas.h"
#include "city/types.h"
#include "ui/windows/state.h"

// Canvas Ui Ids

// Host chart resource array slots, distinct from original dialog IDs.
typedef enum CityCanvasChartSlot {
  CITY_CANVAS_CHART_HISTORY = 0, CITY_CANVAS_CHART_DEMOGRAPHICS = 1,
  CITY_CANVAS_CHART_NEIGHBORS = 2, CITY_CANVAS_CHART_INDUSTRY = 3,
  CITY_CANVAS_CHART_COUNT = 4,
} CityCanvasChartSlot;

// Neighbor painter positions, after rotating original neighbor directions.
typedef enum CityCanvasNeighborIcon {
  CITY_CANVAS_NEIGHBOR_ICON_CENTER = 0, CITY_CANVAS_NEIGHBOR_ICON_NORTH_WEST = 1,
  CITY_CANVAS_NEIGHBOR_ICON_NORTH_EAST = 2, CITY_CANVAS_NEIGHBOR_ICON_SOUTH_EAST = 3,
  CITY_CANVAS_NEIGHBOR_ICON_SOUTH_WEST = 4, CITY_CANVAS_NEIGHBOR_ICON_COUNT = 5,
} CityCanvasNeighborIcon;

// Chart State

typedef struct {
  i32 id;
  CityRect bounds; // Client coordinates, using the native dialog-unit mapping.
  char text[96];
  const void *font_glyphs; // Borrowed owned canvas glyph table,256 entries.
} CityCanvasChartControl;
typedef struct {
  const char *title;
  CityRect bounds;
  CityCanvasChartControl control[24];
  i32 control_count;
} CityCanvasChartDialog;
typedef struct {
  CityCanvasChartDialog dialog[CITY_CANVAS_CHART_COUNT]; // History, demographics, neighbors, industry.
  const char *industry[CITY_INDUSTRY_COUNT];
  const char *workforce;
  const char *resident_age;
  const char *nation;
  const void *neighbor_font_glyphs;
} CityCanvasChartResources;

typedef struct {
  struct CityCanvasDesktop *desktop;
  CityCanvasChartResources resources;
  char titles[CITY_CANVAS_CHART_COUNT][96];
  const char *demographic_captions[CITY_DEMOGRAPHIC_MODE_COUNT];
  bool ready;
  bool visible[CITY_CANVAS_CHART_COUNT];
  u32 window_id[CITY_CANVAS_CHART_COUNT];
  u16 history_series;
  u8 history_period, demographic_mode, industry_mode;
  int industry_maximum;
} CityChartState;

// Chart Primitives
// Shared raster services preserve desktop clipping and independent UI scaling.
void Sc2kCanvasChartFill(Canvas *canvas, CityRect rect, u32 color);
void Sc2kCanvasChartText(Canvas *canvas, i32 x, i32 y, const char *text, u32 color);
i32 Sc2kCanvasChartTextWidth(const char *text);
void Sc2kCanvasChartFrame(Canvas *canvas, CityRect rect, const char *title);
void Sc2kCanvasChartBox(Canvas *canvas, CityRect rect, u32 color, i32 raised);

// Chart Canvas
const CityCanvasChartControl *Sc2kCanvasChartControl(const CityChartState *charts, i32 code, i32 id);
i32 Sc2kCanvasChartCode(WindowKind kind);
void Sc2kCanvasDrawCharts(Canvas *canvas, const Window *window);
bool Sc2kCanvasChartActionAt(const Window *window, i32 x, i32 y,
    i32 *code, i32 *control, i32 *value);

// Chart Host
bool PrepareCityChartPaintState(City *city, int code);
bool BindCityChartPanels(City *city, CityCanvasDesktop *desktop);
void UnbindCityChartPanels(City *city);
void RefreshCityChartPanels(City *city);

// Chart Windows

// Panel IDs and child-control IDs are distinct command parameter domains.
typedef enum CityPanelId {
  CITY_PANEL_DEMOGRAPHICS = 128,
  CITY_PANEL_HISTORY = 152,
  CITY_PANEL_NEIGHBORS = 157,
  CITY_PANEL_INDUSTRY = 160,
  CITY_PANEL_MAP_DATA = 246,
} CityPanelId;

typedef enum CityChartControlId {
  CITY_CONTROL_POPULATION_GRAPH = 252,
  CITY_CONTROL_HEALTH_GRAPH = 253,
  CITY_CONTROL_EDUCATION_GRAPH = 254,
  CITY_CONTROL_HISTORY_SERIES_FIRST = 264,
  CITY_CONTROL_HISTORY_SERIES_LAST = 279,
  CITY_CONTROL_HISTORY_PLOT = 283, CITY_CONTROL_HISTORY_LEGEND = 284,
  CITY_CONTROL_HISTORY_PERIOD_FIRST = 280,
  CITY_CONTROL_HISTORY_PERIOD_LAST = 282,
  CITY_CONTROL_DEMOGRAPHIC_CHART = 289,
  CITY_CONTROL_INDUSTRY_ACCOUNTS = 291,
  CITY_CONTROL_INDUSTRY_TAX = 292,
  CITY_CONTROL_INDUSTRY_DEMAND = 293,
  CITY_CONTROL_INDUSTRY_CHART = 295
} CityChartControlId;

bool CityPanelHostReady(City *city, int code);
bool CityChartPanelReady(City *city, int code);
bool SetCityPanelVisibilityByCode(City *city, int code);
int RefreshCityPanel(City *city, int code);

bool SelectCityChartControl(City *city, int code, int control, int value);

#endif
