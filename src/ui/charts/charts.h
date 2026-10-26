#ifndef SC2K_DOMAIN_SRC_UI_CHARTS_CHARTS_H
#define SC2K_DOMAIN_SRC_UI_CHARTS_CHARTS_H

#include "city/economy/types.h"
#include "city/types.h"
#include "ui/windows/types.h"

typedef struct Game Game;
typedef struct CityInterfaceState CityInterfaceState;

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

// Chart Canvas
const CityCanvasChartControl *Sc2kCanvasChartControl(const CityChartState *charts, i32 code, i32 id);
i32 Sc2kCanvasChartCode(WindowKind kind);
bool Sc2kCanvasChartActionAt(const Window *window, i32 x, i32 y,
    i32 *code, i32 *control, i32 *value);

// Chart Host
// Refresh the cached values needed by a visible chart before painting it. Returns whether the requested panel was
// handled; calculation may update the UI chart cache but does not advance simulation.
bool PrepareCityChartPaintState(City *city, int code);
// Prepare UI chart resources and borrow the desktop; Game supplies UI and immutable neighbor artwork.
// Rebinding a ready set succeeds only for the same desktop. Failed preparation retains its partial UI writes.
bool BindCityChartPanels(Game *game, CityCanvasDesktop *desktop);
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

// Read whether a chart or map-data panel has its required UI binding. Does not open, refresh or mutate it.
bool IsCityPanelReady(const CityInterfaceState *ui, int code);
bool SetCityPanelVisibilityByCode(City *city, int code);
// Outside commands, clear remembered chart/map-data visibility when no corresponding window remains.
// Retains the normal panel close effects on toolbar state, map overlays and damage.
void ReconcileCityPanelWindows(City *city);

bool SelectCityChartControl(City *city, int code, int control, int value);

// Returns borrowed hover help for these controls, or null when no help applies.
const char *CityChartTooltipAt(Game *game, const Window *window, int x, int y);

void HideCityChartPanel(City *city, int code);

#endif
