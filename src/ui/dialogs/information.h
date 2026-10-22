#ifndef SC2K_UI_INFORMATION_H
#define SC2K_UI_INFORMATION_H

#include "city/maps/map_types.h"
#include "city/maps/water.h"
#include "city/types.h"

typedef struct CityCanvasDesktop CityCanvasDesktop;
typedef struct CityInterfaceState CityInterfaceState;

// Tile inspection retains facts and artwork choices. Facility windows instead read their live facility record.
typedef struct CityTileInformationSnapshot {
  u32 window_id;
  CityTileInformation data;
  u16 sprite;
  CityRiverSprites river;
  bool us_currency, sprite_flipped;
  char name[256];
} CityTileInformationSnapshot;

enum { CITY_TILE_INFORMATION_WIDTH = 280, CITY_TILE_INFORMATION_HEIGHT = 128 };

// Uses snapshot->data.tile as input; preserves its window ID and replaces the captured facts and presentation data.
void CaptureCityTileInformation(City *city, CityTileInformationSnapshot *snapshot);
// Forget a closed inspector snapshot and return whether its window remains present, including hidden windows.
// Does not repaint, invoke callbacks or change City state.
bool ReconcileCityTileInformationWindow(CityInterfaceState *ui);
// Open or reuse the appropriate inspector, including its UI selection and pause policy.
void PresentCityTileInformationDialog(City *city, TileIndex tile);
// Low-level openers return desktop array indices or -1 on failure. A negative facility argument returns zero unopened.
i32 Sc2kCanvasDesktopOpenFacilityInformation(CityCanvasDesktop *desktop, CityRect bounds, i32 facility);
i32 Sc2kCanvasDesktopOpenTileInformation(City *city, CityRect bounds, TileIndex tile);

#endif
