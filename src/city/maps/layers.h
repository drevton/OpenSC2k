#ifndef SC2K_SIMULATION_MAP_LAYERS_H
#define SC2K_SIMULATION_MAP_LAYERS_H

#include "city/maps/kinds.h"
#include "city/maps/terrain.h"
#include "city/maps/tile_storage.h"
#include "city/state.h"
#include <assert.h>

// Replace a valid tile's occupant and update ordinary/military counters plus category-specific metadata.
// Entering trees consumes one landscape RNG draw. Same-kind calls still perform category resets.
// Invalidates the old sprite; the caller finishes placement/flags/overlays and invalidates the final extents.
// Earlier metadata/elevation writes need their own pre-write invalidation.
void ReplaceCityTileAndStatistics(City *city, TileIndex tile, u8 kind);
// As above, followed by installation of an explicit two-bit variant.
void ReplaceCityTileVariant(City *city, TileIndex tile, int kind, int variant);

// Writes are plain assignments. Mutating operations invalidate tiles explicitly.
// All cell accessors use full-resolution city tiles. Coarse layers select the
// cell containing that tile; callers do not pass layer-relative indices.

static SC2K_FORCE_INLINE u8 ReadTile(const City *city, TileIndex tile) {
  return city->tile[tile].kind;
}

static SC2K_FORCE_INLINE void WriteTile(City *city, TileIndex tile, int value) {
  city->tile[tile].kind = value;
}

static SC2K_FORCE_INLINE int ReadCityZoneKind(const TileInfo *tp) {
  return tp->zone & CITY_TILE_SLOPED ? ZONE_NONE : tp->zone & CITY_ZONE_KIND_MASK;
}

static SC2K_FORCE_INLINE u8 ReadTerrainCode(const TileInfo *tp) {
  return tp->zone & CITY_TILE_SLOPED ? tp->zone & CITY_TERRAIN_SHAPE_MASK : CITY_TERRAIN_FLAT;
}

static SC2K_FORCE_INLINE void WriteTerrainCode(TileInfo *tp, int shape) {
  assert(shape >= 0 && shape <= CITY_TERRAIN_SHAPE_13);
  tp->zone = (tp->zone & CITY_TILE_GROWTH_MASK) |
      (shape ? CITY_TILE_SLOPED | shape : ReadCityZoneKind(tp));
}

static SC2K_FORCE_INLINE void WriteCityZoneKind(TileInfo *tp, int zone) {
  assert(!zone || !ReadTerrainCode(tp));
  tp->zone = (tp->zone & CITY_TILE_GROWTH_MASK) |
      (ReadTerrainCode(tp) ? tp->zone & 31 : zone & CITY_ZONE_KIND_MASK);
}

// Resolved zone and occupant metadata; the stored zone/slope discriminator is never exposed as a zone kind.
static SC2K_FORCE_INLINE u8 ReadTileZoneByte(const TileInfo *tp) {
  return (tp->terrain & CITY_ZONE_METADATA_MASK) | ReadCityZoneKind(tp);
}

static SC2K_FORCE_INLINE void WriteTileZoneByte(TileInfo *tp, int value) {
  WriteCityZoneKind(tp, value & CITY_ZONE_KIND_MASK);
  tp->terrain = (tp->terrain & 15) | (value & CITY_ZONE_METADATA_MASK);
}

static SC2K_FORCE_INLINE u8 ReadZoneByte(const City *city, TileIndex tile) {
  return city->native_map_storage ? ReadTileZoneByte(&city->tile[tile]) : city->tile[tile].zone;
}

// Permanent water is a property of the stored kind, including occupied water.
static SC2K_FORCE_INLINE bool HasTileWater(const TileInfo *tp) {
  return (ReadCityTileClasses(tp->kind) & TCLASS_WATER) != 0;
}

static SC2K_FORCE_INLINE CityWaterKind ReadTileWaterKind(const TileInfo *tp) {
  return !HasTileWater(tp) ? CITY_WATER_NONE : tp->flags & TF_SALTWATER ? CITY_WATER_SALT : CITY_WATER_FRESH;
}

// Retained algorithms write the resolved zone/metadata byte; unfinished stock imports still write raw XZON.
static SC2K_FORCE_INLINE void WriteZoneByte(City *city, TileIndex tile, int value) {
  if (city->native_map_storage) WriteTileZoneByte(&city->tile[tile], value);
  else city->tile[tile].zone = value;
}

// Primary axis: road for road/power and road/rail, rail for rail/power.
// Zero is east/west, one is north/south. The second network is perpendicular.
static SC2K_FORCE_INLINE int ReadCityCrossingAxis(const TileInfo *tp) {
  return (tp->terrain & CITY_CROSSING_AXIS_MASK) != 0;
}

static SC2K_FORCE_INLINE void WriteCityCrossingAxis(TileInfo *tp, int axis) {
  tp->terrain = (tp->terrain & ~CITY_CROSSING_AXIS_MASK) | (axis ? CITY_CROSSING_AXIS_MASK : 0);
}

static SC2K_FORCE_INLINE int ReadCityCrossingConnections(const TileInfo *tp) {
  return ReadCityCrossingAxis(tp) ? CONN_NS : CONN_EW;
}

static SC2K_FORCE_INLINE bool IsCityWaterPowerLine(const TileInfo *tp) {
  return IsTileKindPlainPowerLine(tp->kind) && HasTileWater(tp);
}

static SC2K_FORCE_INLINE bool CityPowerLineHasTower(const TileInfo *tp) {
  return tp->kind == TK_POWER_LINE_TOWER || tp->kind == TK_POWER_LINE_TOWER_WATER;
}

// Ordinary road/rail/power payloads use the four cardinal connection bits.
static SC2K_FORCE_INLINE int ReadTransportConnections(const TileInfo *tp) {
  return tp->terrain >> CITY_ZONE_METADATA_SHIFT;
}

static SC2K_FORCE_INLINE void WriteTransportConnections(TileInfo *tp, int mask) {
  tp->terrain = (tp->terrain & CITY_ZONE_KIND_MASK) |
      mask << CITY_ZONE_METADATA_SHIFT;
}

static SC2K_FORCE_INLINE int ReadTileVariant(const TileInfo *tp) {
  return (tp->terrain & CITY_TILE_VARIANT_MASK) >> CITY_TILE_VARIANT_SHIFT;
}

// Physical corner indices in the stored map, independent of camera rotation.
enum {
  CITY_BUILDING_CORNER_TOP_LEFT, CITY_BUILDING_CORNER_BOTTOM_LEFT,
  CITY_BUILDING_CORNER_BOTTOM_RIGHT, CITY_BUILDING_CORNER_TOP_RIGHT,
};

bool IsCityFootprintCorner(const City *city, TileIndex tile, int corner);

static SC2K_FORCE_INLINE int EncodeBuildingOrientation(int orientation) {
  return (orientation << CITY_BUILDING_ORIENTATION_SHIFT) & CITY_BUILDING_ORIENTATION_MASK;
}

static SC2K_FORCE_INLINE int DecodeBuildingOrientation(int zone) {
  return (zone & CITY_BUILDING_ORIENTATION_MASK) >> CITY_BUILDING_ORIENTATION_SHIFT;
}

// Native facing is relative to the current map. New buildings on non-building
// tiles start with the stock default facing in that map rotation.
static SC2K_FORCE_INLINE int ReadBuildingOrientation(const City *city, TileIndex tile) {
  return IsTileKindBuilding(ReadTile(city, tile)) ?
      DecodeBuildingOrientation(city->tile[tile].terrain) :
      city->geography.rotation;
}

// Decode native footprint markers in their tile-specific role.
int ReadFootprintCorners(const City *city, TileIndex tile);

// Unbuilt includes rubble, radioactive waste and trees. Callers add any
// tool-specific restrictions on those kinds, zoning or underground contents.
static SC2K_FORCE_INLINE bool IsFlatDryUnbuiltTile(const TileInfo *tp) {
  return IsTileKindUnbuilt(tp->kind) && ReadTerrainCode(tp) == CITY_TERRAIN_FLAT &&
      !HasTileWater(tp);
}

// Exposed water stores its behavior in kind. Occupants retain that ground kind
// in terrain's low nibble so demolition restores the same spreading rule.
static SC2K_FORCE_INLINE int ReadCityWaterGroundKind(const TileInfo *tp) {
  return IsTileKindShoreTree(tp->kind) ? TK_WATER_BANK : IsTileKindExposedWater(tp->kind) ? tp->kind :
      HasTileWater(tp) ? (tp->terrain & 15) + TK_WATER : TK_CLEAR;
}

static SC2K_FORCE_INLINE int ReadTileSnowCoverage(const TileInfo *tp) {
  int state = tp->terrain & 15;
  return (!HasTileWater(tp) || IsTileKindShoreBank(tp->kind)) && state >= 1 && state <= 4 ? state : 0;
}

static SC2K_FORCE_INLINE u8 ReadPollution(const City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  return city->half[(point.y >> 1) * (city->map_width >> 1) + (point.x >> 1)].pollution;
}

static SC2K_FORCE_INLINE void WritePollution(City *city, TileIndex tile, int value) {
  CityPoint point = MapTilePt(city, tile);
  city->half[(point.y >> 1) * (city->map_width >> 1) + (point.x >> 1)].pollution = value;
}

static SC2K_FORCE_INLINE u8 ReadTrafficDensity(const City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  return city->half[(point.y >> 1) * (city->map_width >> 1) + (point.x >> 1)].traffic_density;
}

static SC2K_FORCE_INLINE void WriteTrafficDensity(City *city, TileIndex tile, int value) {
  CityPoint point = MapTilePt(city, tile);
  city->half[(point.y >> 1) * (city->map_width >> 1) + (point.x >> 1)].traffic_density = value;
}

static SC2K_FORCE_INLINE u8 ReadCrime(const City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  return city->half[(point.y >> 1) * (city->map_width >> 1) + (point.x >> 1)].crime;
}

static SC2K_FORCE_INLINE void WriteCrime(City *city, TileIndex tile, int value) {
  CityPoint point = MapTilePt(city, tile);
  city->half[(point.y >> 1) * (city->map_width >> 1) + (point.x >> 1)].crime = value;
}

static SC2K_FORCE_INLINE u8 ReadLandValue(const City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  return city->half[(point.y >> 1) * (city->map_width >> 1) + (point.x >> 1)].land_value;
}

static SC2K_FORCE_INLINE void WriteLandValue(City *city, TileIndex tile, int value) {
  CityPoint point = MapTilePt(city, tile);
  city->half[(point.y >> 1) * (city->map_width >> 1) + (point.x >> 1)].land_value = value;
}

static SC2K_FORCE_INLINE u8 ReadDensity(const City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  return city->quarter[(point.y >> 2) * (city->map_width >> 2) + (point.x >> 2)].density;
}

static SC2K_FORCE_INLINE void WriteDensity(City *city, TileIndex tile, int value) {
  CityPoint point = MapTilePt(city, tile);
  city->quarter[(point.y >> 2) * (city->map_width >> 2) + (point.x >> 2)].density = value;
}

static SC2K_FORCE_INLINE u8 ReadPoliceCoverage(const City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  return city->quarter[(point.y >> 2) * (city->map_width >> 2) + (point.x >> 2)].police_coverage;
}

static SC2K_FORCE_INLINE void WritePoliceCoverage(City *city, TileIndex tile, int value) {
  CityPoint point = MapTilePt(city, tile);
  city->quarter[(point.y >> 2) * (city->map_width >> 2) + (point.x >> 2)].police_coverage = value;
}

static SC2K_FORCE_INLINE u8 ReadFireCoverage(const City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  return city->quarter[(point.y >> 2) * (city->map_width >> 2) + (point.x >> 2)].fire_coverage;
}

static SC2K_FORCE_INLINE void WriteFireCoverage(City *city, TileIndex tile, int value) {
  CityPoint point = MapTilePt(city, tile);
  city->quarter[(point.y >> 2) * (city->map_width >> 2) + (point.x >> 2)].fire_coverage = value;
}

static SC2K_FORCE_INLINE u8 ReadDensityHistory(const City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  return city->quarter[(point.y >> 2) * (city->map_width >> 2) + (point.x >> 2)].density_history;
}

static SC2K_FORCE_INLINE void WriteDensityHistory(City *city, TileIndex tile, int value) {
  CityPoint point = MapTilePt(city, tile);
  city->quarter[(point.y >> 2) * (city->map_width >> 2) + (point.x >> 2)].density_history = value;
}

static SC2K_FORCE_INLINE u8 ReadUnderground(const City *city, TileIndex tile) {
  return city->tile[tile].underground & CITY_UNDERGROUND_KIND_MASK;
}

static SC2K_FORCE_INLINE void WriteUnderground(City *city, TileIndex tile, int value) {
  TileInfo *tp = &city->tile[tile];
  tp->underground = !(city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) ? value :
      (tp->underground & CITY_UNDERGROUND_METADATA_MASK) | (value & CITY_UNDERGROUND_KIND_MASK);
}

static SC2K_FORCE_INLINE u16 ReadAltitudeWord(const City *city, TileIndex tile) {
  return city->tile[tile].altitude;
}

static SC2K_FORCE_INLINE void WriteAltitudeWord(City *city, TileIndex tile, int value) {
  city->tile[tile].altitude = value;
}

// Read the bed's minimum height, excluding water level and construction progress.
static SC2K_FORCE_INLINE int ReadCityLandHeight(const TileInfo *tp) {
  return tp->altitude & CITY_ALTITUDE_LAND_MASK;
}

static SC2K_FORCE_INLINE bool CityTileCanStoreBuildingStage(const TileInfo *tp) {
  return IsTileKindBuilding(tp->kind) && !HasTileWater(tp);
}

// Use this for elevation calculations unless the caller already knows that the
// tile contains real water. A construction stage must never become a height.
static SC2K_FORCE_INLINE int ReadCityWaterHeight(const TileInfo *tp) {
  int altitude = tp->altitude;
  return CityTileCanStoreBuildingStage(tp) || IsTileKindBridgeHead(tp->kind) ? altitude & CITY_ALTITUDE_LAND_MASK :
      (altitude & CITY_ALTITUDE_WATER_MASK) >> CITY_ALTITUDE_WATER_SHIFT;
}

// Read the water plane on wet tiles and the land height on dry tiles.
// Building stages and stale water bits never contribute to the surface height.
static SC2K_FORCE_INLINE int ReadCitySurfaceHeight(const TileInfo *tp) {
  return HasTileWater(tp) ? ReadCityWaterHeight(tp) : ReadCityLandHeight(tp);
}

static inline int ReadScratch(const City *city, int x, int y) {
  const i16 *cells = (const i16 *)city->scratch;
  return cells[y * city->map_width + x];
}

static inline void WriteScratch(City *city, int x, int y, int value) {
  city->scratch[y * city->map_width + x] = value;
}

#endif
