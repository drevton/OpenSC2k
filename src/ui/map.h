#ifndef SC2K_DOMAIN_SRC_UI_MAP_H
#define SC2K_DOMAIN_SRC_UI_MAP_H

#include "city/types.h"
#include "drawing/maps/map_view.h"

typedef struct Drawing Drawing;
typedef struct CityMapViewConfig CityMapViewConfig;
typedef struct CityCanvasDesktop CityCanvasDesktop;

// UI owns the committed selection and reusable scratch. Cells use City TileIndex values; UI resolves the
// sorted selection during tile painting and Drawing supplies marker bounds and pixels.
typedef struct {
  TileIndex tile;
} CityCanvasPlacementCell;

typedef struct {
  CityCanvasPlacementCell *cells, *scratch; // Committed cells are sorted by TileIndex.
  int count, capacity;
  CityRect bounds;
  int origin_x, origin_y;
  u32 zoom, magnified, underground;
  bool point;
} CityCanvasPlacementSelection;

// Map Cursor

typedef struct City City;
typedef struct Game Game;
typedef struct CityInterfaceState CityInterfaceState;

// Change a local display layer. Disabling vehicles clears their visualization; a change refreshes every map view.
void ToggleCityMapLayer(Game *game, CityMapLayer layer);

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
void Sc2kRefreshCityStatusPanel(City *city);
void RefreshCityCursorStatus(City *city, i32 mode);
void InvalidateCityMapSelection(City *city, const CityRect *rect);

// Native presentation input, in the map's canvas coordinates.
void SetCityMapCursorPosition(CityInterfaceState *ui, int x, int y);

// Map Navigation

void SetCityMapNavigationPresentation(Game *game, bool enabled, bool interactive);
void ReadCityMapClientRect(Drawing *drawing, CityRect *rect);
// Read the host client area without scrollbar strips; return the resulting bottom coordinate.
i32 GetCityMapClientRect(Drawing *drawing, CityRect *rect);
// Map renderer rectangles are scaled into native window pixels before queuing UI damage.
bool InvalidateCityMap(Game *game, const CityRect *rect);
bool CaptureCityMapPointer(Game *game);
int ReleaseCityMapWindowPointer(Game *game);

// Main-camera controls preserve the existing zoom, scroll and preview refresh ordering.
// NULL selects the default view only for centering; the other operations require a supplied view.
i32 CenterCityMapViewAtTile(City *city, CityMapViewConfig *view, int x, int y);
void ZoomCityMapViewOut(City *city, CityMapViewConfig *view);
void ZoomCityMapViewIn(City *city, CityMapViewConfig *view);
void OffsetCityMapOrigin(City *city, CityMapViewConfig *view, int delta_x, int delta_y);
// These controls use the bound main-camera origin and supplied view configuration. Rotation discards its retained
// pixels, sprites, animation and label bounds; scroll updates return the vertical scrollbar percentage.
void RotateCityMapView(City *city, CityMapViewConfig *view, bool clockwise);
i32 UpdateCityMapViewScrollPosition(City *city, CityMapViewConfig *view);
void MaskCityMapOriginForZoom(Drawing *drawing, CityMapViewConfig *view);
// An unknown/zero viewport ID falls back to the main camera. Shaded viewports retain their rotation.
void RotateCityMapViewport(City *city, u32 window_id, bool clockwise);
void RotateFocusedCityMapView(City *city, bool clockwise);

// Presentation

// Ensure capacity for the UI-owned selection and scratch without changing retained cells. False means allocation failed.
bool ReserveCityPlacementCells(CityCanvasDesktop *desktop, int count);
// Replace the sorted selection using Drawing's bounds. True means the visible selection changed;
// City and its RNG remain read-only.
// Input may be unordered and contain duplicates. External input is copied;
// the desktop's scratch may be sorted in place.
bool SetCityCanvasPlacementCells(const City *city, CityCanvasDesktop *desktop, const CityCanvasPlacementCell *cells,
    int count, bool point);
// Retained command drag between valid tile endpoints: prepare its drawing clip, delegate counts/damage to City,
// then reset the clip. Admission and City updates keep their original order; this does not place any infrastructure.
void UpdateInfrastructureLinePlacementPreview(City *city, CityMapViewConfig *view, TileIndex start, TileIndex end);

void ShowCityConstructionCost(City *city, TileIndex tile, i64 cost);

#endif
