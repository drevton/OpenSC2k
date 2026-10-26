#ifndef SC2K_SIMULATION_MAP_LAYERS_H
#define SC2K_SIMULATION_MAP_LAYERS_H

#include "city/maps/kinds.h"
#include "city/maps/tile_storage.h"
#include "city/state.h"

// Replace a valid tile's occupant and update ordinary/military counters plus category-specific metadata.
// Entering trees consumes one landscape RNG draw. Same-kind calls still perform category resets.
// Invalidates the old sprite; the caller finishes placement/flags/overlays and invalidates the final extents.
// Earlier metadata/elevation writes need their own pre-write invalidation.
// Map backing stays in place: borrowed TileInfo pointers survive this call, but their fields may change.
void ReplaceCityTileAndStatistics(City *city, TileIndex tile, u8 kind);
// As above, followed by installation of an explicit two-bit variant.
void ReplaceCityTileVariant(City *city, TileIndex tile, int kind, int variant);

// Read the removal selection area, starting at its northwest tile, without mutation. Connected bridge/tunnel/runway
// removal can extend beyond this selection. Unresolved footprints have INVALID_TILE and zero dimensions.
CityTileArea ResolveBulldozeFootprint(const City *city, TileIndex tile);
// Mutate an admitted structure, including connected tunnels/bridges/runways/piers and footprint metadata.
// Requires a valid tile in a complete native map; performs damage/sound/effects but no admission, pricing or payment.
void BulldozeCityTile(City *city, TileIndex tile, bool animate);

// Writes are plain assignments. Mutating operations invalidate tiles explicitly.
// All cell accessors use full-resolution city tiles. Coarse layers select the
// cell containing that tile; callers do not pass layer-relative indices or round to a coarse-cell boundary.

static SC2K_FORCE_INLINE u8 ReadZoneByte(const City *city, TileIndex tile) {
  return city->native_map_storage ? ReadTileZoneByte(&city->tile[tile]) : city->tile[tile].zone;
}

// Retained algorithms write the resolved zone/metadata byte; unfinished stock imports still write raw XZON.
static SC2K_FORCE_INLINE void WriteZoneByte(City *city, TileIndex tile, int value) {
  if (city->native_map_storage) WriteTileZoneByte(&city->tile[tile], value);
  else city->tile[tile].zone = value;
}

// Decode native footprint markers into rotation-relative CityFootprintCornerMask bits in the low nibble.
int ReadFootprintCorners(const City *city, TileIndex tile);

static SC2K_FORCE_INLINE u8 ReadPollution(const City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  return city->half[(point.y >> 1) * (city->map_width >> 1) + (point.x >> 1)].pollution;
}

static SC2K_FORCE_INLINE u8 ReadTrafficDensity(const City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  return city->half[(point.y >> 1) * (city->map_width >> 1) + (point.x >> 1)].traffic_density;
}

static SC2K_FORCE_INLINE u8 ReadCrime(const City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  return city->half[(point.y >> 1) * (city->map_width >> 1) + (point.x >> 1)].crime;
}

static SC2K_FORCE_INLINE u8 ReadLandValue(const City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  return city->half[(point.y >> 1) * (city->map_width >> 1) + (point.x >> 1)].land_value;
}

static SC2K_FORCE_INLINE u8 ReadDensity(const City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  return city->quarter[(point.y >> 2) * (city->map_width >> 2) + (point.x >> 2)].density;
}

static SC2K_FORCE_INLINE u8 ReadPoliceCoverage(const City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  return city->quarter[(point.y >> 2) * (city->map_width >> 2) + (point.x >> 2)].police_coverage;
}

static SC2K_FORCE_INLINE u8 ReadFireCoverage(const City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  return city->quarter[(point.y >> 2) * (city->map_width >> 2) + (point.x >> 2)].fire_coverage;
}

static SC2K_FORCE_INLINE u8 ReadDensityHistory(const City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  return city->quarter[(point.y >> 2) * (city->map_width >> 2) + (point.x >> 2)].density_history;
}

#endif
