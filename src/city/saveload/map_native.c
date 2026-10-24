#include "city/maps/tile_storage.h"
#include "city/saveload/types.h"
#include "city/saveload/map_format.h"
#include "city/state.h"
#include "city/terrain/terrain.h"
#include "base/byte_order.h"
#include <string.h>

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
