#ifndef SC2K_DOMAIN_SRC_UI_MAP_H
#define SC2K_DOMAIN_SRC_UI_MAP_H

#include "drawing/canvas.h"
#include "drawing/maps/map_view.h"
#include "ui/windows/state.h"
#include "city/types.h"

// Map Data

// Map categories; fire protection and education have independent tabs.
typedef enum CityMapDataGroup {
  CITY_DATA_GROUP_SURFACE = 0, CITY_DATA_GROUP_TRANSPORT = 1,
  CITY_DATA_GROUP_POWER = 2, CITY_DATA_GROUP_WATER = 3,
  CITY_DATA_GROUP_POPULATION = 4, CITY_DATA_GROUP_POLICE = 5,
  CITY_DATA_GROUP_POLLUTION = 6, CITY_DATA_GROUP_LAND_VALUE = 7,
  CITY_DATA_GROUP_FIRE = 8, CITY_DATA_GROUP_EDUCATION = 9, CITY_DATA_GROUP_COUNT = 10,
  CITY_DATA_SELECTION_STRIDE = 10
} CityMapDataGroup;

// group*10 + option. Names follow exact original string resources327..344,
// not historical names of reconstruction helpers or map accessors.
typedef enum CityMapDataSelection {
  CITY_DATA_SELECTION_STRUCTURES = 0,
  CITY_DATA_SELECTION_ZONES = 1,
  CITY_DATA_SELECTION_ROADS = 10,
  CITY_DATA_SELECTION_RAIL = 11,
  CITY_DATA_SELECTION_TRAFFIC = 12,
  CITY_DATA_SELECTION_POWER = 20,
  CITY_DATA_SELECTION_WATER = 30,
  CITY_DATA_SELECTION_DENSITY = 40,
  CITY_DATA_SELECTION_GROWTH = 41,
  CITY_DATA_SELECTION_CRIME = 50,
  CITY_DATA_SELECTION_POLICE_COVERAGE = 51,
  CITY_DATA_SELECTION_POLICE_STATIONS = 52,
  CITY_DATA_SELECTION_POLLUTION = 60,
  CITY_DATA_SELECTION_LAND_VALUE = 70,
  CITY_DATA_SELECTION_FIRE_COVERAGE = 80,
  CITY_DATA_SELECTION_FIRE_STATIONS = 81,
  CITY_DATA_SELECTION_SCHOOLS = 90,
  CITY_DATA_SELECTION_COLLEGES = 91
} CityMapDataSelection;

// Options per image tab; command admission and the map window use the same bounds.
static inline int CityMapDataOptionCount(int group) {
  static const u8 kCounts[CITY_DATA_GROUP_COUNT] = {2, 3, 1, 1, 2, 3, 1, 1, 2, 2};
  return kCounts[group];
}

static inline bool IsCityMapDataSelection(int selection) {
  return selection >= 0 && selection < CITY_DATA_GROUP_COUNT * CITY_DATA_SELECTION_STRIDE &&
      selection % CITY_DATA_SELECTION_STRIDE < CityMapDataOptionCount(selection / CITY_DATA_SELECTION_STRIDE);
}

// Map Cursor

typedef struct City City;
typedef struct Game Game;
typedef struct CityInterfaceState CityInterfaceState;

// Cursor read/results observed by optional diagnostics; no native pointer identities.
typedef enum CityCursorInputKind {
  CITY_CURSOR_INPUT_WINDOW_RECT = 1,
  CITY_CURSOR_INPUT_POSITION,
  CITY_CURSOR_INPUT_LOAD_WAIT,
  CITY_CURSOR_INPUT_INSTALL,
  CITY_CURSOR_INPUT_SHOW,
  CITY_CURSOR_INPUT_CAPTURE,
  CITY_CURSOR_INPUT_RELEASE,
} CityCursorInputKind;

typedef struct CityCursorInputRecord {
  u32 kind;
  u32 handle;
  i32 value[4];
} CityCursorInputRecord;

enum { CITY_SYSTEM_CURSOR_WAIT = 0x7F02 };

void ReadCityMapCursorRect(Game *game, CityRect *rect);
void ReadCityMapCursorPosition(Game *game, int *x, int *y);
u32 LoadCityWaitCursor(Game *game);
u32 InstallCityMapCursor(Game *game, u32 handle);
bool ShowCityMapCursor(Game *game);
void RefreshCityCursorStatus(City *city, i32 mode);
void InvalidateCityMapSelection(City *city, const CityRect *rect);

// Native presentation input, in the map's canvas coordinates.
void SetCityMapCursorPosition(CityInterfaceState *ui, int x, int y);

// Map Data Dialog

bool IsCityMapDataVisible(City *city);
void SetCityMapDataPresentation(City *city, CityCanvasDesktop *desktop);
bool CityMapDataServiceAvailable(City *city);
int ReadCityMapDataSelection(City *city);
void SetCityMapDataSelection(City *city, int selection);
void HideCityMapDataDialog(City *city);

// Map Navigation

void SetCityMapNavigationPresentation(Game *game, bool enabled, bool interactive);
void ReadCityMapClientRect(Drawing *drawing, CityRect *rect);
// Map renderer rectangles are scaled into native window pixels before queuing UI damage.
bool InvalidateCityMap(Game *game, const CityRect *rect);
bool InvalidateCityMapDataPreview(City *city);
bool CaptureCityMapPointer(Game *game);
int ReleaseCityMapWindowPointer(Game *game);

// Presentation

// Ensure capacity for the UI-owned selection and scratch without changing retained cells. False means allocation failed.
bool ReserveCityPlacementCells(CityCanvasDesktop *desktop, int count);
// Replace the sorted selection using Drawing's bounds. True means the visible selection changed;
// City and its RNG remain read-only.
bool SetCityCanvasPlacementCells(const City *city, CityCanvasDesktop *desktop, const CityCanvasPlacementCell *cells,
    int count, bool point);
// Retained command drag presentation: prepare its drawing clip, delegate construction counts/damage to City,
// then reset the clip. Admission and City updates keep their original order; this does not place any infrastructure.
void UpdateInfrastructureLinePlacementPreview(City *city, CityMapViewConfig *view, int x,
    int y, int end_x, int end_y);

void SetCityPaletteDialogState(City *city, bool paused);
void PresentCityScreenShake(City *city, CityMapViewConfig *view, CityRect destination, CityRect source);

// Present the native canvas, including all visible map windows.
bool PresentCityMapView(Game *game);

void ApplicationUpdateDialogPause(City *city);

// Tile Dialog

typedef struct City City;

// UI owns the rendered inspection pages and window lifetime.
void PresentCityTileInformationDialog(City *city, TileIndex tile);

// Zone Cost

// Borrowed presentation data, restored by each nested caller.
typedef struct CityZoneCostPresentation {
  Canvas *canvas;
  const char *text;
  CityMapViewConfig *view;
} CityZoneCostPresentation;
typedef struct Drawing Drawing;
void PresentCityZoneDragCost(City *city, Drawing *drawing, CityMapViewConfig *view, u32 cost, int x, int y);
int MeasureCityZoneCostText(const CityZoneCostPresentation *presentation, int cost, int zoom);
void DrawCityZoneCostText(Game *game, const CityZoneCostPresentation *presentation, int cost, int zoom,
    int x, int baseline_y, u32 color, const CityRect *bounds);

#endif
