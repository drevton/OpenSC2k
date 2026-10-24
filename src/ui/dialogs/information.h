#ifndef SC2K_UI_INFORMATION_H
#define SC2K_UI_INFORMATION_H

#include "city/maps/information.h"
#include "city/types.h"
#include "drawing/maps/map_sprites.h"

typedef struct CityInterfaceState CityInterfaceState;

// Tile inspection retains facts and artwork choices. Facility windows instead read their live facility record.
typedef struct CityTileInformationSnapshot {
  u32 window_id;
  CityTileInformation data;
  MapTilePreview preview;
  bool us_currency;
  char name[256];
} CityTileInformationSnapshot;

enum { CITY_TILE_INFORMATION_WIDTH = 280, CITY_TILE_INFORMATION_HEIGHT = 128 };

// Forget a closed inspector snapshot and return whether its window remains present, including hidden windows.
// Does not repaint, invoke callbacks or change City state.
bool ReconcileCityTileInformationWindow(CityInterfaceState *ui);
// Open or reuse the appropriate inspector, including its UI selection and pause policy.
void PresentCityTileInformationDialog(City *city, TileIndex tile);

#endif
