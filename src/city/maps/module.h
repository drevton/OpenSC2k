#ifndef SC2K_CITY_MAPS_MODULE_H
#define SC2K_CITY_MAPS_MODULE_H

#include "city/maps/map.h"

// Immutable kind metadata, never stored in a tile or save file. Four bits permit sixteen families.
typedef enum TileModuleId {
  TILE_MODULE_NONE, TILE_MODULE_GROUND, TILE_MODULE_TREES, TILE_MODULE_WATER,
  TILE_MODULE_ROAD, TILE_MODULE_RAIL, TILE_MODULE_POWER, TILE_MODULE_CROSSING,
  TILE_MODULE_HIGHWAY, TILE_MODULE_BRIDGE, TILE_MODULE_BUILDING, TILE_MODULE_REMAINS,
  TILE_MODULE_TUNNEL, TILE_MODULE_COUNT
} TileModuleId;

enum { CITY_TILE_MODULE_SHIFT = 19, CITY_TILE_MODULE_MASK = 15 << CITY_TILE_MODULE_SHIFT };
_Static_assert(TILE_MODULE_COUNT <= 16, "tile module index fits its class bits");
_Static_assert(CITY_TILE_MODULE_MASK == 0x780000, "module selector occupies class bits 19 through 22");

typedef struct TileModule {
  // One scheduled landscape visit. The traversal owns order/cadence; family code owns its writes and RNG draws.
  // A callback performs common ground-cover refresh at its original point, including the clear-water early return.
  // NULL uses only the shared ground-cover refresh. Economic growth keeps its separate scheduled scan.
  void (*tick)(City *city, TileIndex tile);
  // Drawing receives prepared camera geometry; false leaves drawing to the shared artwork path.
  // May change Drawing pixels/state, never City or simulation RNG.
  bool (*draw)(City *city, TileIndex tile, int zoom, int screen_x, int ground_y, const CityTileGeometry *geometry);
  // Sorted rendering calls this for each tile beneath a deferred object, after preparing that object's sprites.
  // Paint only ground/underpasses: no selection, overlays or object submission. NULL/false uses natural terrain.
  // Geometry is camera-relative; preserve the current clip and never change City or simulation RNG.
  bool (*draw_ground)(City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
      const CityTileGeometry *geometry);
} TileModule;

extern const TileModule kCityGroundModule, kCityTreeModule, kCityWaterModule, kCityPowerModule;
extern const TileModule kCitySurfaceLineModule, kCityHighwayModule, kCityBridgeModule, kCityBuildingModule;
extern const TileModule kCityRemainsModule;
extern const TileModule *const kCityTileModules[TILE_MODULE_COUNT];

#endif
