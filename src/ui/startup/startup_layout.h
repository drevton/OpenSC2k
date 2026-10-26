#ifndef SC2K_UI_STARTUP_LAYOUT_H
#define SC2K_UI_STARTUP_LAYOUT_H

#include "city/terrain/types.h"
#include "ui/startup/startup.h"

// Dropdown values encode UI choices only; the generator receives CityTerrainSettings.
enum { kTerrainSmoothnessFirst = CITY_TERRAIN_VARIETY_COUNT };

static inline bool IsTerrainFeatureDropdown(int id) {
  return id >= CITY_SETUP_COAST && id <= CITY_SETUP_LANDFORM;
}

static inline bool IsTerrainGroupedDropdown(int id) {
  return IsTerrainFeatureDropdown(id) || id == CITY_SETUP_VARIETY;
}

// Initialize caller-owned control storage. Item lists and help strings are borrowed immutable data;
// they remain valid for the process lifetime. Setup reads only UI's mayor name; the year query reads its dialog.
// The intro builder requires zeroed storage; unsupported resources return false without modifying it.
void BuildCitySetupLayout(const CityInterfaceState *ui, CityCanvasStartupWindow *layout);
void BuildCityYearQueryLayout(const CityNewCitySetupDialog *dialog, CityCanvasStartupWindow *layout);
bool BuildCityStartupLayout(u32 resource, CityCanvasStartupWindow *layout);

// Refresh captions, enabled states and checks from UI's terrain choices, including a matching open dropdown.
// Requires the active setup desktop and a complete setup layout. Does not generate terrain or change City.
void RefreshCityTerrainControls(CityInterfaceState *ui, CityCanvasStartupWindow *layout);

// Apply a coast/river/landform item to caller-owned settings. Requires one of those controls and its full item list.
// Invalid or disabled choices return false without changing settings. Generation and failure recovery belong
// to the host; this performs no allocation, I/O, RNG, notifications or window lifetime operations.
bool SelectCityTerrainFeature(CityTerrainSettings *settings, const CityCanvasStartupControl *control, int value);

#endif
