#include "city/infrastructure/power_lines.h"
#include "city/maps/trees.h"
#include "city/maps/tile_storage.h"
#include "city/maps/layers.h"
#include "city/saveload/city_file_internal.h"
#include "city/saveload/map_format.h"
#include "base/byte_order.h"
#include <string.h>

// Initialize imported native ground, climate, tree and power metadata when no landscape state exists. Reuses an
// existing nonzero landscape seed without rerandomizing trees; initialization may consume the independent landscape
// RNG and invalidates the map.
void InitializeCityLandscape(City *city) {
  if (city->landscape_random) {
    InitializeCityPowerConnections(city);
    return;
  }
  city->landscape_cursor = 1;
  city->landscape_tick = 0;
  city->landscape_random = city->random.crt ^ city->random.event ^ 0x6D2B79F5;
  if (!city->landscape_random) city->landscape_random = 1;
  TileIndex cells = city->map_cell_count;
  for (TileIndex tile = 0; tile < cells; ++tile) {
    TileInfo *tp = &city->tile[tile];
    int kind = tp->kind;
    if (kind == TK_CLEAR || IsTileKindExposedWater(kind) || IsTileKindTree(kind)) {
      tp->terrain &= 15;
      if (IsTileKindTree(kind)) {
        WriteTreeSpecies(tp, city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE) ?
            RandomCityTreeSpecies(city, ReadCityTileBaseClimate(city, tp)) : 0);
        WriteTreeGrowth(tp, CITY_TREE_GROWN);
      }
    }
  }
  InitializeCityPowerConnections(city);
  city->protect_terrain_structures = (city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE)) != 0;
  InvalidateMap(city);
}

// Native map packet: six byte planes and the existing weather lattice. Stock base chunks remain projections.
u32 CityGroundExtensionSize(const City *city) {
  if (!city->native_map_storage) return 0;
  return 6 * city->map_cell_count + CITY_SAVE_CLIMATE_HEADER_SIZE +
      TerrainSnowNoiseSize(city->map_width, city->map_height);
}

bool EncodeCityGroundExtension(const City *city, u8 *bytes, u32 size) {
  int cells = city->map_cell_count;
  if (!city->native_map_storage || size < CityGroundExtensionSize(city) || size > CITY_SAVE_EXTENSION_LIMIT)
    return false;
  for (int tile = 0; tile < cells; ++tile) {
    const TileInfo *tp = &city->tile[tile];
    bytes[tile] = tp->terrain;
    bytes[cells + tile] = tp->zone;
    bytes[2 * cells + tile] = tp->flags;
    bytes[3 * cells + tile] = tp->kind;
    WriteLE16(bytes + 4 * cells + 2 * tile, tp->altitude);
  }
  u32 controls = TerrainSnowNoiseSize(city->map_width, city->map_height);
  u8 *header = bytes + 6 * cells;
  memcpy(header, "CLIM", 4);
  WriteLE32(header + 4, controls);
  memcpy(header + CITY_SAVE_CLIMATE_HEADER_SIZE, city->snow.points, controls);
  return true;
}
