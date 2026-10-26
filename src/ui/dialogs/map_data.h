#ifndef SC2K_UI_MAP_DATA_H
#define SC2K_UI_MAP_DATA_H

#include "drawing/maps/map_data.h"

typedef struct Canvas Canvas;
typedef struct Drawing Drawing;
typedef struct CityInterfaceState CityInterfaceState;
typedef struct Window Window;
typedef struct CityCanvasDesktop CityCanvasDesktop;
typedef union CityStatistics CityStatistics;

// UI retains controls and window lifetime; Drawing owns the referenced bitmap caches.
typedef struct CityMapDataState {
  struct CityCanvasDesktop *desktop;
  struct CityMapDataWindowDrawing *drawing;
  u32 window_id;
  u8 group, option;
  bool visible;
  bool refresh_enabled;
  bool isometric;
  CityRect refresh_rect;
} CityMapDataState;

enum { CITY_MAP_DATA_MIN_WIDTH = 250, CITY_MAP_DATA_MIN_HEIGHT = 160,
    CITY_MAP_DATA_DEFAULT_WIDTH = 356, CITY_MAP_DATA_DEFAULT_HEIGHT = 270 };

// Borrow the desktop (or detach with NULL) and select isometric projection. Caller finishes window lifetimes before
// detaching; this binding does not close windows, release Drawing caches or change City.
void SetCityMapDataPresentation(CityInterfaceState *ui, CityCanvasDesktop *desktop);
// Combined group/option index; reading the selection does not refresh its preview.
int ReadCityMapDataSelection(const CityMapDataState *state);
void HideCityMapDataDialog(City *city);
bool ToggleCityMapDataDialog(City *city);
void ToggleCityMapDataOverlay(City *city);
void CityMapDataDialogOnOverlaySelectionChanged(City *city, int selection);
void CityMapDataDialogOnListSelectionChanged(City *city, int selection);
// Translate the combined selection into the main map's renderer mode, preserving unsupported modes.
void ApplyCityMapDataModeSelection(Drawing *drawing, int selection);

// Rebuild the shared preview when a map-data window exists, then attach it and request window damage.
void UpdateCityMapDataWindows(City *city);
// Queue preview damage. Refresh respects the dialog's gate; Invalidate bypasses it. Neither rebuilds the pixels.
bool RefreshCityMapDataPreview(City *city);
bool InvalidateCityMapDataPreview(City *city);
// Window openers return the desktop array index or -1.
i32 Sc2kCanvasDesktopOpenMapData(CityCanvasDesktop *desktop, CityRect bounds, i32 selection);
i32 Sc2kCanvasDesktopOpenLandUse(CityCanvasDesktop *desktop, CityRect bounds, const CityStatistics *statistics);
void Sc2kCanvasDesktopUpdateLandUse(CityCanvasDesktop *desktop, const CityStatistics *statistics);
// Paint an attached map-data window with its Drawing-owned caches already prepared.
void DrawCityMapDataWindow(Drawing *drawing, Canvas *canvas, const Window *window);
const char *CityMapTooltipAt(const Window *window, int x, int y);

#endif
