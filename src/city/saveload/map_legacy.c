#include "city/infrastructure/lines.h"
#include "city/maps/terrain.h"
#include "city/economy/facilities.h"
#include "city/maps/buildings.h"
#include "city/infrastructure/bridges.h"
#include "city/infrastructure/highways.h"
#include "city/infrastructure/power_lines.h"
#include "city/maps/water.h"
#include "city/maps/trees.h"
#include "city/infrastructure/types.h"
#include "city/maps/map_types.h"
#include "city/maps/tile_storage.h"
#include "city/terrain/terrain.h"
#include "city/tools/types.h"
#include "game/state.h"
#include "city/state.h"
#include "assets/state.h"
#include "game/errors.h"
#include "city/growth/growth.h"
#include "city/engine/simulation.h"
#include "city/maps/layers.h"
#include "city/maps/tables.h"
#include "city/tools/tools.h"
#include "city/saveload/city_file_internal.h"
#include "city/saveload/map_format.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "base/byte_order.h"

// Compact native highway kinds and paired stock artwork IDs are separate namespaces. Input is a stock kind; returns
// the legacy family base used by projection and does not decode a native footprint.
static int LegacyHighwayBase(int kind) {
  static const u8 kKinds[TK_HIGHWAY_POWER - TK_HIGHWAY + 1] = {
    [TK_HIGHWAY - TK_HIGHWAY] = TKL_HIGHWAY_FIRST,
    [TK_HIGHWAY_RAIL - TK_HIGHWAY] = TKL_HIGHWAY_RAIL_CROSSING_FIRST,
    [TK_HIGHWAY_ROAD - TK_HIGHWAY] = TKL_HIGHWAY_ROAD_CROSSING_FIRST,
    [TK_HIGHWAY_POWER - TK_HIGHWAY] = TKL_HIGHWAY_POWER_CROSSING_FIRST,
  };
  return kKinds[kind - TK_HIGHWAY];
}

// Convert a stock highway artwork kind to the corresponding native highway family kind. Input is a legacy kind, not a
// native stored record; no City state is read or modified.
static int NativeHighwayKind(int kind) {
  static const u8 kKinds[] = {TK_HIGHWAY, TK_HIGHWAY_ROAD, TK_HIGHWAY_RAIL,
      TK_HIGHWAY_POWER};
  return kKinds[(kind - TKL_HIGHWAY_FIRST) >> 1];
}

// Map a native transport/crossing kind to its fixed stock artwork family base. Input uses native kinds; unlisted kinds
// are returned unchanged and direction/axis are added only when projecting the complete tile.
static int LegacyBaseKind(int kind) {
  if (IsTileKindShoreTree(kind)) return CityTreeTile(CityTreeCount(kind));
  switch (kind) {
  case TK_WATER: case TK_RIVER: case TK_WATER_BANK: return TK_CLEAR;
  case TK_PARK: return TKL_PARK;
  case TK_POWER_LINE: case TK_POWER_LINE_TOWER: return TKL_POWER_LINE_EAST_WEST;
  case TK_POWER_LINE_WATER: case TK_POWER_LINE_TOWER_WATER: return TKL_POWER_LINE_BRIDGE;
  case TK_ROAD: return TKL_ROAD_EAST_WEST;
  case TK_RAIL: return TKL_RAIL_FIRST;
  case TK_TUNNEL_ENTRANCE: return TKL_TUNNEL_ENTRANCE_FIRST;
  case TK_ROAD_POWER_CROSSING: return TKL_ROAD_POWER_CROSSING_FIRST;
  case TK_ROAD_RAIL_CROSSING: return TKL_ROAD_RAIL_CROSSING_FIRST;
  case TK_RAIL_POWER_CROSSING: return TKL_RAIL_POWER_CROSSING_FIRST;
  case TK_HIGHWAY_ENTRANCE: return TKL_HIGHWAY_ENTRANCE_93;
  case TK_HIGHWAY_INCLINE: return TKL_HIGHWAY_INCLINE_FIRST;
  case TK_HIGHWAY_BEND: return TKL_HIGHWAY_BEND_FIRST;
  case TK_HIGHWAY_JUNCTION: return TKL_HIGHWAY_JUNCTION;
  case TK_SUBWAY_TO_RAIL: return TKL_SUBWAY_TO_RAIL_FIRST;
  default: return kind;
  }
}

// Decode a stock surface family before consulting native kind classifications. Detailed bridge/footprint
// conversion still reads unchanged source neighbors. Existing building IDs remain unchanged, except the retired
// second hydro artwork decodes to the native hydro plant; stock small park uses its reclaimed native slot.
static int NativeBaseKind(int kind) {
  if (kind >= TK_RESIDENTIAL_FIRST) return kind == TKL_HYDRO_POWER_LAST ? TK_HYDRO_POWER : kind;
  if (kind == TKL_PARK) return TK_PARK;
  if (kind <= TK_TREE_LAST) return IsLegacyRubbleKind(kind) ? TK_RUBBLE : kind;
  if (kind <= TKL_POWER_LINE_LAST) return TK_POWER_LINE;
  if (kind <= TKL_ROAD_LAST) return TK_ROAD;
  if (kind <= TKL_RAIL_TRANSITION_62) return TK_RAIL;
  if (kind <= TKL_TUNNEL_ENTRANCE_LAST) return TK_TUNNEL_ENTRANCE;
  if (kind <= TKL_RAIL_POWER_CROSSING_LAST) {
    static const u8 kCrossings[] = {TK_ROAD_POWER_CROSSING, TK_ROAD_RAIL_CROSSING, TK_RAIL_POWER_CROSSING};
    return kCrossings[(kind - TKL_ROAD_POWER_CROSSING_FIRST) >> 1];
  }
  if (kind <= TKL_HIGHWAY_LAST) return NativeHighwayKind(kind);
  if (kind <= TKL_BRIDGE_LAST) return kind < TKL_RAIL_BRIDGE_FIRST ? TK_ROAD_BRIDGE_SPAN : TK_RAIL_BRIDGE_SPAN;
  if (kind == TKL_POWER_LINE_BRIDGE) return TK_POWER_LINE_WATER;
  if (kind <= TKL_HIGHWAY_ENTRANCE_LAST) return TK_HIGHWAY_ENTRANCE;
  if (kind <= TKL_HIGHWAY_INCLINE_LAST) return TK_HIGHWAY_INCLINE;
  if (kind <= TKL_HIGHWAY_BEND_LAST) return TK_HIGHWAY_BEND;
  if (kind == TKL_HIGHWAY_JUNCTION) return TK_HIGHWAY_JUNCTION;
  if (kind <= TKL_HIGHWAY_BRIDGE_LAST) return TK_HIGHWAY_BRIDGE_SPAN;
  return TK_SUBWAY_TO_RAIL;
}

// Classify stock highway artwork ranges used during import. Input is a legacy kind; the read-only predicate must not
// replace native metadata-aware highway queries.
static bool IsLegacyHighway(int kind) {
  return (kind >= TKL_HIGHWAY_FIRST && kind <= TKL_HIGHWAY_LAST) ||
      (kind >= TKL_HIGHWAY_INCLINE_FIRST && kind <= TKL_HIGHWAY_BRIDGE_LAST);
}

// The stock fifteen ordinary layouts share the line connection table. Slopes store their axis; flat rail transition
// artwork comes from its adjoining slope. Input is stock artwork; returns its fixed layout mask without reading native
// geometry or discovering neighbors.
int LegacyTransportConnectionMask(int kind) {
  static const u8 kMasks[] = {5, 10, 10, 5, 10, 5, 3, 6, 12, 9, 11, 7, 14, 13, 15};
  if (kind >= TKL_POWER_LINE_EAST_WEST && kind <= TKL_POWER_LINE_LAST)
    return kMasks[kind - TKL_POWER_LINE_EAST_WEST];
  if (kind >= TKL_ROAD_EAST_WEST && kind <= TKL_ROAD_LAST)
    return kMasks[kind - TKL_ROAD_EAST_WEST];
  if (kind >= TKL_RAIL_FIRST && kind < TKL_RAIL_TRANSITION_59)
    return kMasks[kind - TKL_RAIL_FIRST];
  return kind & 1 ? CONN_NS : CONN_EW;
}

// Project a valid native transport tile and its decoded connection mask into stock artwork. Base and mask must select
// the admitted family/layout; returns an artwork kind without changing native storage.
int LegacyTransportTileKind(const City *city, TileIndex tile, int base, int mask) {
  base = LegacyBaseKind(base);
  int shape = ReadTerrainCode(&city->tile[tile]);
  if (kLineTerrainOverrideBySlope[shape]) return base + (u8)kLineTileOffsetByTerrain[shape];
  if (base == TKL_RAIL_FIRST) {
    int transition = FindCityRailTransition(city, tile);
    if (transition >= 0) return TKL_RAIL_TRANSITION_59 + transition;
  }
  return base + kLineTileOffsetByConnectionMask[mask];
}

// Decode highway orientation from stock artwork and flag bits. Inputs must use the legacy representation; the returned
// axis is a value for boundary conversion only.
int LegacyHighwayAxis(int kind, int flags) {
  int flipped = (flags & CITY_LEGACY_MAP_FLAG_ROTATED) != 0;
  if (kind >= TKL_HIGHWAY_BRIDGE_FIRST) return flipped;
  return (kind <= TKL_HIGHWAY_LAST ? (kind - TKL_HIGHWAY_FIRST) & 1 : kind & 1) ^ flipped;
}

// Convert current-map facing back to the stock sprite pair and mirror convention. Zone must contain the admitted
// native facing metadata; returns stock artwork without changing the hydro tile.
static int CityHydroSpriteKind(const City *city, int zone) {
  int orientation = (DecodeBuildingOrientation(zone) - city->geography.rotation) & 3;
  return TK_HYDRO_POWER + (orientation >> 1);
}

// Stock projection for codecs and retained orientation-based algorithms. Native power lines store cardinal connections
// independently of tower presence. Mask and slope must be supported decoded values; returns only a stock artwork
// projection, never a native storage edit.
u8 LegacyPowerTileKind(int mask, int shape) {
  static const u8 kPowerOffsets[] = {0, 0, 1, 6, 0, 0, 7, 11, 1, 9, 1, 10, 8, 13, 12, 14};
  if (shape >= CITY_TERRAIN_SHAPE_1 && shape <= CITY_TERRAIN_SHAPE_4) return 15 + shape;
  if (shape >= CITY_TERRAIN_SHAPE_9 && shape <= CITY_TERRAIN_SHAPE_12) return TKL_POWER_LINE_NORTH_SOUTH;
  return TKL_POWER_LINE_EAST_WEST + kPowerOffsets[mask];
}

// Classify stock ordinary-line artwork for the requested family. Artwork and family use the codec/artwork namespace;
// no native map geometry is decoded here.
bool IsCityOrdinaryLineArtwork(int artwork, int family) {
  int last = family == TK_POWER_LINE ? TKL_POWER_LINE_LAST :
      family == TK_ROAD ? TKL_ROAD_LAST : TKL_RAIL_LAST;
  family = LegacyBaseKind(family);
  return artwork >= family && artwork <= last;
}

// Test whether a stock artwork kind belongs to rubble. Input is a legacy kind; this predicate neither reads City
// metadata nor changes recovery state.
bool IsLegacyRubbleKind(int kind) {
  return kind >= TK_RUBBLE && kind <= TKL_RUBBLE_LAST;
}

// Test whether a stock artwork kind is a tunnel entrance. Input uses stock encoding and the result has no map or
// tunnel-search side effects.
bool IsLegacyTunnelEntranceKind(int kind) {
  return kind >= TKL_TUNNEL_ENTRANCE_FIRST && kind <= TKL_TUNNEL_ENTRANCE_LAST;
}

// Test the stock highway-ramp artwork range. Input is a legacy kind; native gameplay uses its own kind and orientation
// metadata.
bool IsLegacyHighwayRampKind(int kind) {
  return kind >= TKL_HIGHWAY_ENTRANCE_93 && kind <= TKL_HIGHWAY_ENTRANCE_LAST;
}

// Test whether a stock artwork selector represents supported surface transport. Performs classification only and must
// not be used to decode native tile storage.
bool IsCitySurfaceTransportArtwork(int artwork) {
  return artwork >= TKL_ROAD_EAST_WEST && artwork <= TKL_RAIL_TRANSITION_62;
}

// Test whether a stock artwork selector represents a rail incline. Returns a predicate without consulting neighboring
// native geometry.
bool IsCityRailSlopeArtwork(int artwork) {
  return artwork >= TKL_RAIL_SLOPE_46 && artwork <= TKL_RAIL_SLOPE_49;
}

// Test whether a stock artwork selector represents a flat rail transition. This is a boundary/artwork classification
// and does not establish a native connection.
bool IsCityRailTransitionArtwork(int artwork) {
  return artwork >= TKL_RAIL_TRANSITION_59 && artwork <= TKL_RAIL_TRANSITION_62;
}

// Map a supported transport family and native slope shape to stock incline artwork. Inputs must be decoded valid
// values; no map or connection metadata is modified.
int CityLineSlopeArtwork(int family, int shape) {
  family = LegacyBaseKind(family);
  return family + shape + 1;
}

// Convert rail-transition artwork to its corresponding flat rail layout. Input must be in the supported artwork
// namespace; result is a value for projection, not a tile edit.
int CityRailFlatArtwork(int artwork) {
  if (IsCityRailSlopeArtwork(artwork)) return TKL_RAIL_FIRST + !(artwork & 1);
  if (IsCityRailTransitionArtwork(artwork)) return TKL_RAIL_FIRST + (artwork & 1);
  return artwork;
}

// Read the positive-axis facing convention from admitted stock ramp artwork. Does not inspect City rotation or write
// native orientation metadata.
bool CityRampArtworkFacesPositive(int artwork) { return artwork > TKL_HIGHWAY_ENTRANCE_94; }

// These ranges belong to SC2 files and the stock artwork catalog. Native maps store only the first kind, with its
// variant in zone bits 4–5. Returns the native base-kind projection for a stock range; input remains a legacy kind
// throughout classification.
static int NativeVariantKindFromLegacy(int kind) {
  if (kind >= TK_RUBBLE && kind <= TKL_RUBBLE_LAST) return TK_RUBBLE;
  if (kind >= TKL_SUBWAY_TO_RAIL_FIRST && kind <= TKL_SUBWAY_TO_RAIL_LAST)
    return TK_SUBWAY_TO_RAIL;
  if (IsLegacyTunnelEntranceKind(kind))
    return TK_TUNNEL_ENTRANCE;
  if (IsLegacyHighwayRampKind(kind))
    return TK_HIGHWAY_ENTRANCE;
  return -1;
}

// Move a stock variant-kind choice into the native zone metadata bits. Kind and zone must describe the same imported
// cell; returns the translated zone byte without writing map storage.
static int LegacyVariantZone(int kind, int zone) {
  return (zone & CITY_ZONE_KIND_MASK) | (kind == TK_SUBWAY_TO_RAIL ? CITY_ZONE_CORNER_MASK : 0);
}

// Stock decoding reads its source records directly; native geometry helpers must never reinterpret a partially decoded
// map. The stock terrain writer also uses this decoder with a projected code over a complete native map.
static CityWaterKind LegacySourceWaterKind(const City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile));
  if (city->native_map_storage) return ReadTileWaterKind(&city->tile[tile]);
  const TileInfo *tp = &city->tile[tile];
  if ((tp->terrain & CITY_TERRAIN_KIND_MASK) < CITY_TERRAIN_WATER_FIRST) return CITY_WATER_NONE;
  return tp->flags & TF_SALTWATER ? CITY_WATER_SALT : CITY_WATER_FRESH;
}

// A stock source still stores water height on dry building records. Only complete native records interpret that field
// as construction progress.
static int LegacySourceWaterHeight(const City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile));
  const TileInfo *tp = &city->tile[tile];
  return city->native_map_storage ? ReadCityWaterHeight(tp) :
      (tp->altitude & CITY_ALTITUDE_WATER_MASK) >> CITY_ALTITUDE_WATER_SHIFT;
}

// Derive a stock river projection from a valid tile and its decoded geometry. Geometry must describe that tile; the
// result is used at the file/artwork boundary rather than as native land shape.
static int LegacyRiverSlope(const City *city, TileIndex tile, CityTileGeometry geometry) {
  if (!(city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE)) || geometry.water_height <= geometry.land_height)
    return geometry.land_shape <= CITY_TERRAIN_SHAPE_4 ? geometry.land_shape : CITY_TERRAIN_FLAT;
  // Code 62 has no direction bits. Generated rapids store the upper water
  // level and the lower bed; face the lowest cardinal water neighbor.
  int lowest = geometry.water_height, downhill = -1;
  for (int direction = 0; direction < 8; direction += 2) {
    TileIndex next =
        GetRelativeTileOrInvalid(city, tile, kTerrainNeighborXOffsets[direction], kTerrainNeighborYOffsets[direction]);
    if (next == INVALID_TILE) continue;
    if (LegacySourceWaterKind(city, next) == CITY_WATER_NONE) continue;
    int water = LegacySourceWaterHeight(city, next);
    if (water < lowest) { lowest = water; downhill = direction; }
  }
  return downhill < 0 ? CITY_TERRAIN_FLAT :
      kTerrainShapeByHigherNeighbors[kTerrainHigherNeighborBits[(downhill + 4) & 7]];
}

// Derive stock bank artwork from a valid tile and its decoded native shoreline geometry. Uses neighbors without
// changing water/land records; callers must not feed the result back as a native slope.
static int LegacyShoreSlope(const City *city, TileIndex tile, CityTileGeometry geometry) {
  static const u8 kRaisedCorners[] = {0, 9, 3, 6, 12, 11, 7, 14, 13, 1, 2, 4, 8, 15};
  int shape = geometry.land_shape, corners = kRaisedCorners[shape];
  // A shared edge at a flat river mouth belongs to the water plane, even
  // when diagonal land neighbors would otherwise raise its corners.
  for (int direction = 0; direction < 8; direction += 2) {
    TileIndex next =
        GetRelativeTileOrInvalid(city, tile, kTerrainNeighborXOffsets[direction], kTerrainNeighborYOffsets[direction]);
    if (next == INVALID_TILE) continue;
    const TileInfo *tp = &city->tile[next];
    int terrain = city->native_map_storage ?
        ProjectCityTerrainToLegacy(city, next, ReadTerrainCode(tp)) :
        tp->terrain & CITY_TERRAIN_KIND_MASK;
    bool level = terrain >= CITY_TERRAIN_BANK_48 && terrain != CITY_TERRAIN_WATERFALL;
    if (LegacySourceWaterKind(city, next) == CITY_WATER_FRESH && level &&
        LegacySourceWaterHeight(city, next) == geometry.water_height)
      corners &= ~kTerrainHigherNeighborBits[direction];
  }
  return corners == kRaisedCorners[shape] ? shape : kTerrainShapeByHigherNeighbors[corners];
}

// Decode stock land/water geometry using the supplied stock terrain code and unchanged source records. Tile must be
// valid; native geometry helpers cannot safely interpret this partially imported representation.
static CityTileGeometry DecodeLegacyCityTileGeometry(const City *city, TileIndex tile, int terrain) {
  CityTileGeometry geometry = {
      .land_height = ReadCityLandHeight(&city->tile[tile]),
      .land_shape = terrain < CITY_TERRAIN_BANK_48 ? terrain & CITY_TERRAIN_SHAPE_MASK :
          CityTerrainSlopeAt(city, tile),
      .water = terrain >= CITY_TERRAIN_WATER_FIRST ?
          (city->tile[tile].flags & TF_SALTWATER ? CITY_WATER_SALT : CITY_WATER_FRESH) :
          CITY_WATER_NONE};
  if (geometry.land_shape == CITY_TERRAIN_RAISE_LEVEL || geometry.land_shape == CITY_TERRAIN_SHAPE_14)
    geometry.land_shape = CITY_TERRAIN_SHAPE_13;
  if (geometry.land_shape == CITY_TERRAIN_SHAPE_15)
    geometry.land_shape = CITY_TERRAIN_FLAT;
  geometry.surface_height = geometry.land_height;
  geometry.surface_shape = geometry.land_shape;
  if (geometry.water == CITY_WATER_NONE) return geometry;
  geometry.water_height = LegacySourceWaterHeight(city, tile);
  geometry.surface_height = geometry.water_height;
  geometry.surface_shape = CITY_TERRAIN_FLAT;
  geometry.water_surface = CITY_WATER_SURFACE_LEVEL;
  if (terrain == CITY_TERRAIN_WATERFALL) {
    // Unrepresentable compound rapids use flat artwork, but remain rapids
    // for placement: a bridge must never mistake the fallback for level water.
    geometry.water_surface = CITY_WATER_SURFACE_RAPIDS;
    geometry.surface_shape = LegacyRiverSlope(city, tile, geometry);
    if ((city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE)) && geometry.surface_shape)
      geometry.surface_height = geometry.land_height;
  } else if (terrain == CITY_TERRAIN_HYDRO_SITE_46) {
    geometry.water_surface = CITY_WATER_SURFACE_RAPIDS;
  } else if ((city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE)) &&
      (terrain < CITY_TERRAIN_WATER_FIRST || terrain >= CITY_TERRAIN_BANK_END)) {
    geometry.water_surface = CITY_WATER_SURFACE_UNSUPPORTED;
  } else if ((city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE)) && terrain > CITY_TERRAIN_BANK_32 &&
      terrain <= CITY_TERRAIN_BANK_32_SHAPE_13 && geometry.land_height == geometry.water_height) {
    geometry.surface_shape = LegacyShoreSlope(city, tile, geometry);
    if (geometry.surface_shape) geometry.water_surface = CITY_WATER_SURFACE_SHORE;
  }
  return geometry;
}

// Decode one valid tile while the source map is still in stock representation. Returns a value without publishing
// native records or mutating source data used by later conversions.
CityTileGeometry ReadLegacyCityTileGeometry(const City *city, TileIndex tile) {
  return city->native_map_storage ? ReadCityTileGeometry(city, tile) :
      DecodeLegacyCityTileGeometry(city, tile, city->tile[tile].terrain & CITY_TERRAIN_KIND_MASK);
}

// Map stock water terrain to the native ground kind. Rivers include their banks and rapids;
// ordinary shores retain the spreading-water kind even when the bed is sloped.
static int LegacyWaterGroundKind(int terrain) {
  if (terrain == CITY_TERRAIN_HYDRO_SITE_46 || terrain >= CITY_TERRAIN_BANK_48) return TK_RIVER;
  return terrain >= CITY_TERRAIN_BANK_32 ? TK_WATER_BANK : TK_WATER;
}

// Apply a stock terrain-code edit through the boundary's native record conversion. Tile must be valid and value a
// supported stock code; retains the existing caller's metadata and invalidation responsibilities.
void WriteLegacyCityTerrainCode(City *city, TileIndex tile, int value) {
  TileInfo *tp = &city->tile[tile];
  CityTileGeometry geometry = DecodeLegacyCityTileGeometry(city, tile, value & CITY_TERRAIN_KIND_MASK);
  bool wet = value >= CITY_TERRAIN_WATER_FIRST, was_wet = HasTileWater(tp);
  int ground = wet ? LegacyWaterGroundKind(value) : TK_CLEAR;
  if (wet && geometry.water_surface == CITY_WATER_SURFACE_RAPIDS && geometry.surface_shape) {
    geometry.land_shape = geometry.surface_shape;
    tp->altitude = (tp->altitude & CITY_ALTITUDE_CLEAR_WATER) |
        geometry.land_height << CITY_ALTITUDE_WATER_SHIFT;
  }
  int kind = CityTileKindForWater(tp->kind, wet);
  if (wet && IsTileKindExposedWater(kind)) kind = ground;
  if (kind != tp->kind) ReplaceCityTileAndStatistics(city, tile, kind);
  WriteTerrainCode(tp, geometry.land_shape);
  if (wet) tp->terrain = (tp->terrain & 240) | (IsTileKindExposedWater(kind) ? 0 : ground - TK_WATER);
  else if (was_wet) WriteGroundKind(tp, CITY_GROUND_GRASS);
  if (was_wet && !wet) {
    tp->flags &= ~TF_SALTWATER;
    if (CityTileCanStoreBuildingStage(tp)) tp->altitude &= CITY_ALTITUDE_CLEAR_WATER;
  }
}

// Legacy channels omit their bed shape. Resolve it once during import, before publishing any converted record. No
// native redraw reconstructs this slope.
void TranslateLegacyCityTerrain(const City *city, TileIndex tile, TileInfo *result) {
  assert(IsMapTileValid(city, tile));
  *result = city->tile[tile];
  CityTileGeometry geometry = ReadLegacyCityTileGeometry(city, tile);
  if (geometry.land_shape == CITY_TERRAIN_SHAPE_14) geometry.land_shape = CITY_TERRAIN_SHAPE_13;
  if (geometry.land_shape == CITY_TERRAIN_SHAPE_15) geometry.land_shape = CITY_TERRAIN_FLAT;
  assert(!city->native_map_storage);
  int category = CITY_GROUND_GRASS;
  if (geometry.water != CITY_WATER_NONE) {
    category = LegacyWaterGroundKind(city->tile[tile].terrain & CITY_TERRAIN_KIND_MASK);
    // Legacy channels never stored their bed corners. Sloped river beds follow
    // the decoded river surface instead of reconstructing it again on redraw.
    if (geometry.water_surface == CITY_WATER_SURFACE_RAPIDS && geometry.surface_shape)
      geometry.land_shape = geometry.surface_shape;
    // Stock channel corners inferred from neighboring banks are not a flowing
    // slope. Only waterfall records may install a sloped river at water level.
    if (category == TK_RIVER && geometry.water_surface == CITY_WATER_SURFACE_LEVEL &&
        geometry.land_height == geometry.water_height) geometry.land_shape = CITY_TERRAIN_FLAT;
  }
  int payload = category << 4;
  result->terrain = payload | (geometry.land_shape & CITY_TERRAIN_SHAPE_MASK);
  if (geometry.water != CITY_WATER_NONE) result->flags |= CITY_LEGACY_MAP_FLAG_WATER;
  else result->flags &= ~CITY_LEGACY_MAP_FLAG_WATER;
  if (geometry.water_surface == CITY_WATER_SURFACE_RAPIDS)
    result->altitude = (result->altitude & CITY_ALTITUDE_CLEAR_WATER) |
        geometry.land_height << CITY_ALTITUDE_WATER_SHIFT;
}

// Derive a stock bank pattern from a valid tile's unchanged neighbor water flags. This is read-only and
// traversal-order independent; it must not infer water presence from already converted bank artwork.
static int LegacyRiverBankFromNeighbors(const City *city, TileIndex tile) {
  static const u8 kCardinalShapes[16] = {13, 21, 18, 8, 19, 16, 5, 1, 20, 7, 17, 4, 6, 3, 2, 0};
  static const u8 kDiagonalShapes[16] = {0, 9, 10, 0, 11, 0, 0, 0, 12, 0, 0, 0, 0, 0, 0, 0};
  static const i8 kDx[8] = {-1, 0, 1, 0, -1, -1, 1, 1};
  static const i8 kDy[8] = {0, 1, 0, -1, -1, 1, 1, -1};
  int adjacent = 0, missing = 0;
  for (int direction = 0; direction < 8; ++direction) {
    TileIndex neighbor = GetRelativeTileOrInvalid(city, tile, kDx[direction], kDy[direction]);
    if (neighbor == INVALID_TILE) continue;
    bool water = city->native_map_storage ? HasTileWater(&city->tile[neighbor]) :
        (city->tile[neighbor].flags & CITY_LEGACY_MAP_FLAG_WATER) != 0;
    if (direction < 4) { if (water) adjacent |= 1 << direction; }
    else if (!water) missing |= 1 << (direction - 4);
  }
  return CITY_TERRAIN_BANK_48 + (adjacent == CONN_ALL ?
      kDiagonalShapes[missing] : kCardinalShapes[adjacent]);
}

// Mandatory normalization at the native import boundary. Only derived river banks change; waterfalls, heights, water
// flags and other tile bytes stay put. The pass reads only flags, so its result is independent of traversal order. Run
// only at import normalization; returns the number of changed derived bank records and does not alter native maps
// already admitted.
int RepairLegacyCityRiverBanks(City *city) {
  if (city->native_map_storage || !(city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE))) return 0;
  int changed = 0;
  for (TileIndex tile = 0; tile < city->map_cell_count; ++tile) {
    TileInfo *tp = &city->tile[tile];
    int terrain = tp->terrain & CITY_TERRAIN_KIND_MASK;
    if (!(tp->flags & CITY_LEGACY_MAP_FLAG_WATER) || terrain < CITY_TERRAIN_BANK_48 ||
        terrain > CITY_TERRAIN_BANK_48 + CITY_REGRADE_SHAPE_21 || terrain == CITY_TERRAIN_WATERFALL ||
        terrain == CITY_TERRAIN_BANK_48_SHAPE_15) continue;
    int repaired = LegacyRiverBankFromNeighbors(city, tile);
    if (terrain == repaired) continue;
    tp->terrain = (tp->terrain & CITY_TERRAIN_METADATA_MASK) | repaired;
    ++changed;
  }
  return changed;
}

// Stock channel artwork distinguishes twenty bank patterns. Retained algorithms and codecs request this combined
// slope/bank/water code explicitly; native surface geometry stays in ReadCityTileGeometry.
static SC2K_FORCE_INLINE int ProjectLegacyTerrainCode(const City *city, TileIndex tile, int terrain) {
  assert(IsMapTileValid(city, tile));
  if (!city->native_map_storage)
    return terrain;
  int shape = terrain & CITY_TERRAIN_SHAPE_MASK;
  const TileInfo *tp = &city->tile[tile];
  if (!HasTileWater(tp)) return shape;
  int ground = ReadCityWaterGroundKind(tp);
  if (tp->kind == TK_HYDRO_POWER) return CITY_TERRAIN_HYDRO_SITE_46;
  if (ground == TK_RIVER) {
    if (shape && ReadCityLandHeight(tp) == ReadCityWaterHeight(tp))
      return CITY_TERRAIN_WATERFALL;
    return LegacyRiverBankFromNeighbors(city, tile);
  }
  return (ground == TK_WATER_BANK ? CITY_TERRAIN_BANK_32 : CITY_TERRAIN_WATER_FIRST) + shape;
}

int ProjectCityTerrainToLegacy(const City *city, TileIndex tile, int terrain) {
  return ProjectLegacyTerrainCode(city, tile, terrain);
}

int ReadLegacyTerrainCode(const City *city, TileIndex tile) {
  const TileInfo *tp = &city->tile[tile];
  int terrain = city->native_map_storage ? ReadTerrainCode(tp) : tp->terrain & CITY_TERRAIN_KIND_MASK;
  return ProjectLegacyTerrainCode(city, tile, terrain);
}

// Ordinary native kinds select structural stock pieces. Drawbridges additionally project their moving centre leaf.
static int ProjectStyledCityBridgeKind(const City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile));
  int kind = ReadTile(city, tile);
  if (IsTileKindHighwayBridge(kind))
    return TKL_HIGHWAY_BRIDGE_FIRST + ReadCityHighwayBridgePiece(city, tile);
  bool head = IsCityBridgeHead(&city->tile[tile]);
  if (ReadCityBridgeStyle(city, tile) == BRIDGE_STYLE_DRAWBRIDGE) {
    if (head) return TKL_ROAD_BRIDGE_DECK;
    if (CityDrawbridgeCenter(city, tile) == tile)
      return IsCityDrawbridgeOpen(city, tile) ? TKL_RAISING_BRIDGE_OPEN : TKL_RAISING_BRIDGE_CLOSED;
    return TKL_BRIDGE_FIRST;
  }
  return IsTileKindRailBridge(kind) ?
      (head ? TKL_RAIL_BRIDGE_LAST : TKL_RAIL_BRIDGE_FIRST) :
      head ? TKL_ROAD_BRIDGE_DECK : TKL_SUSPENSION_BRIDGE_FIRST;
}

// Codecs handle byte order, RLE and containers; stock map projections live here.
static u8 ProjectLegacyCityTileKind(const City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile));
  int kind = ReadTile(city, tile);
  if (!city->native_map_storage) return kind;
  if (IsTileKindShoreTree(kind)) return CityTreeTile(CityTreeCount(kind));
  if (IsTileKindBridge(kind)) return ProjectStyledCityBridgeKind(city, tile);
  int zone = ReadTileZoneByte(&city->tile[tile]);
  int construction = CityBuildingConstructionKind(&city->tile[tile]);
  if (construction) return construction;
  if (kind == TK_HYDRO_POWER) return CityHydroSpriteKind(city, zone);
  if (IsTileKindStraightHighwayFamily(kind))
    return LegacyHighwayBase(kind) + ReadCityHighwayAxis(&city->tile[tile]);
  if (IsTileKindMultiHighway(kind)) {
    int shape = ReadCityHighwayFootprintShape(&city->tile[tile]);
    return TKL_HIGHWAY_ENTRANCE_93 + shape;
  }
  if (IsTileKindVariant(kind))
    return LegacyBaseKind(kind) + ReadTileVariant(&city->tile[tile]);
  if (IsTileKindGroundCrossing(kind)) return LegacyBaseKind(kind) + ReadCityCrossingAxis(&city->tile[tile]);
  if (IsTileKindPlainTransport(kind))
    return LegacyTransportTileKind(city, tile, kind, zone >> CITY_ZONE_METADATA_SHIFT);
  if (IsCityWaterPowerLine(&city->tile[tile])) return TKL_POWER_LINE_BRIDGE;
  return IsTileKindPlainPowerLine(kind) ? LegacyPowerTileKind(ReadTransportConnections(&city->tile[tile]),
      ReadTerrainCode(&city->tile[tile])) : LegacyBaseKind(kind);
}

// Project a valid native tile's zone/footprint metadata to the stock byte representation. Bridge style must match its
// span when relevant; City storage remains native and unchanged.
static u8 ProjectLegacyCityTileZone(const City *city, TileIndex tile, int bridge_style) {
  int kind = ReadTile(city, tile), zone = ReadTileZoneByte(&city->tile[tile]);
  if (!city->native_map_storage) return city->tile[tile].zone;
  if (IsTileKindBridge(kind)) {
    // Retain the stock projection's one-based extension style payload.
    if (!IsTileKindHighwayBridge(kind)) return (bridge_style + 1) << CITY_ZONE_METADATA_SHIFT;
    return CityHighwayCornerMask(city, CityBridgeHighwayZone(city, tile));
  }
  if (IsTileKindVariant(kind)) return LegacyVariantZone(kind, zone);
  if (IsTileKindSingleTileFootprint(kind) || IsTileKindStraightHighwayFamily(kind))
    return (zone & CITY_ZONE_KIND_MASK) | CITY_ZONE_CORNER_MASK;
  if (IsTileKindHighwayFootprint(kind))
    return (zone & CITY_ZONE_KIND_MASK) | CityHighwayCornerMask(city, zone);
  if (kind == TK_CLEAR || IsTileKindExposedWater(kind) || IsTileKindTree(kind) || IsTileKindPowerLine(kind) ||
      IsTileKindGroundCrossing(kind) || IsTileKindPlainTransport(kind)) return zone & CITY_ZONE_KIND_MASK;
  if (IsTileKindBuilding(kind))
    return (zone & CITY_ZONE_KIND_MASK) |
        CityBuildingCornerMask(city, tile, &city->tile[0].terrain, sizeof(TileInfo));
  return zone;
}

// Project stored building facing and bridge axes into the stock mirror bit. Camera rotation is applied separately by
// the renderer.
bool LegacyCityTileFlip(const City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile));
  int kind = ReadTile(city, tile), zone = ReadTileZoneByte(&city->tile[tile]);
  if (IsCityWaterPowerLine(&city->tile[tile]))
    return (ReadTransportConnections(&city->tile[tile]) & CONN_NS) != 0;
  if (IsTileKindBridge(kind))
    return (zone & CITY_BRIDGE_AXIS_MASK) != 0;
  if (kind == TK_HIGHWAY_ENTRANCE) return ReadCityHighwayRampMirror(&city->tile[tile]);
  if (IsTileKindHighwayAxis(kind)) {
    int axis = ReadCityHighwayAxis(&city->tile[tile]);
    return IsTileKindStraightHighwayFamily(kind) ? false :
        axis ^ LegacyHighwayAxis(ProjectLegacyCityTileKind(city, tile), 0);
  }
  if (IsTileKindBuilding(kind))
    return (DecodeBuildingOrientation(zone) - city->geography.rotation) & 1;
  return false;
}

// Project a valid native tile to a stock kind for retained codecs/artwork algorithms. Returns a byte-compatible value
// without rewriting the City's stored kind or variant metadata.
static u8 LegacyCityTileKind(const City *city, TileIndex tile) {
  return ProjectLegacyCityTileKind(city, tile);
}

// Project one complete valid native tile into a stock-format record. Returns a value and leaves source metadata
// unchanged; byte order and RLE are separate codec steps.
TileInfo TranslateCityTileToLegacy(const City *city, TileIndex tile) {
  TileInfo result = city->tile[tile];
  if (city->native_map_storage) {
    int bridge_style = IsTileKindBridge(result.kind) ? ReadCityBridgeStyle(city, tile) : 0;
    if (IsCityBridgeHead(&city->tile[tile])) result.altitude &= CITY_ALTITUDE_CLEAR_WATER;
    result.kind = ProjectLegacyCityTileKind(city, tile);
    result.zone = ProjectLegacyCityTileZone(city, tile, bridge_style);
    result.terrain = ProjectCityTerrainToLegacy(city, tile, ReadTerrainCode(&city->tile[tile]));
    result.flags = (result.flags & ~(CITY_LEGACY_MAP_FLAG_ROTATED | CITY_LEGACY_MAP_FLAG_WATER)) |
        (HasTileWater(&city->tile[tile]) ? CITY_LEGACY_MAP_FLAG_WATER : 0) |
        (LegacyCityTileFlip(city, tile) ? CITY_LEGACY_MAP_FLAG_ROTATED : 0);
  }
  result.overlay = ReadOverlay(city, tile);
  return result;
}

// Project one valid map cell's schema-selected byte into the stock representation. Spec must describe the admitted map
// field; no native record is overwritten during export.
u8 ExportCityMapByte(const City *city, const CityFileChunkSpec *spec, TileIndex tile) {
  TileInfo record = TranslateCityTileToLegacy(city, tile);
  return ((const u8 *)&record)[spec->native_offset];
}

// Export a bounded schema-selected map chunk using complete-record stock projection. Bytes must hold size output bytes
// and spec/size must fit the source; native tiles remain unchanged throughout traversal.
void ExportCityMapChunk(City *city, const CityFileChunkSpec *spec, u8 *bytes, u32 size) {
  if (spec->storage == CITY_FILE_TILE && spec->conversion == CITY_FILE_BYTES)
    for (TileIndex tile = 0; tile < size; ++tile) bytes[tile] = ExportCityMapByte(city, spec, tile);
  else if (spec->storage == CITY_FILE_TILE && spec->conversion == CITY_FILE_WORDS16) {
    for (TileIndex tile = 0; tile < size / 2; ++tile) {
      TileInfo record = TranslateCityTileToLegacy(city, tile);
      memcpy(bytes + 2 * tile, &record.altitude, sizeof(record.altitude));
    }
  } else CopyCityFileChunkBytes(city, spec, bytes, size, false);
}

// Validate native tile discriminators and footprint topology directly, without a stock projection or staging map.
bool ValidateCityTileRecords(const City *city) {
  TileIndex cells = city->map_cell_count;
  bool valid = true;
  // Validate local discriminators before inspecting neighboring geometry.
  for (TileIndex tile = 0; tile < cells && valid; ++tile) {
    const TileInfo *tp = &city->tile[tile];
    valid = (tp->zone & CITY_TILE_SLOPED) ? ReadTerrainCode(tp) >= 1 &&
        ReadTerrainCode(tp) <= CITY_TERRAIN_SHAPE_13 : ReadCityZoneKind(tp) <= ZONE_SEAPORT;
    if (!(ReadCityTileClasses(tp->kind) & ~CITY_TILE_MODULE_MASK) && tp->kind != TK_CLEAR &&
        !IsTileKindExposedWater(tp->kind) && tp->kind != TK_RADIOACTIVE_WASTE) valid = false;
    if (IsCityBridgeHead(tp)) {
      int style = ReadTileBridgeStyle(tp);
      if (IsTileKindHighwayBridge(tp->kind)) {
        valid = valid && (style == HIGHWAY_BRIDGE_STYLE_STANDARD || style == HIGHWAY_BRIDGE_STYLE_REINFORCED);
      } else {
        valid = valid && style < BRIDGE_STYLE_COUNT &&
            (style != BRIDGE_STYLE_DRAWBRIDGE || tp->kind == TK_ROAD_BRIDGE_HEAD) &&
            !(tp->altitude & CITY_BRIDGE_HIGHWAY_PHASE_MASK);
      }
    }
  }
  for (TileIndex tile = 0; tile < cells && valid; ++tile) {
    const TileInfo *tp = &city->tile[tile];
    int kind = tp->kind, ground = ReadGroundKind(tp), metadata = tp->terrain & 240;
    bool bridge = IsTileKindBridge(kind), head = IsCityBridgeHead(tp), wet = HasTileWater(tp);
    if (IsTileKindShoreBank(kind)) {
      valid = valid && ground <= CITY_GROUND_SHORE;
    } else if (wet) {
      valid = valid && (tp->terrain & 15) <= TK_WATER_BANK - TK_WATER &&
          (!IsTileKindExposedWater(kind) || !(tp->terrain & 15));
    } else {
      valid = valid && ground <= CITY_GROUND_SHORE;
      if (ground >= CITY_GROUND_ROUGH && ground <= CITY_GROUND_RECOVERY_LAST &&
          kind != TK_CLEAR && !IsTileKindTree(kind) && !bridge && !IsTileKindGroundTransport(kind)) valid = false;
    }
    if (IsTileKindShoreTree(kind)) {
      CityTileGeometry geometry = ReadCityTileGeometry(city, tile);
      valid = valid && geometry.land_height == geometry.water_height && geometry.land_shape != CITY_TERRAIN_FLAT;
    }
    if (IsTileKindTree(kind)) {
      valid = valid && ReadTreeSpecies(tp) < CityTreeSpeciesCount(ReadCityTileBaseClimate(city, tp)) &&
          ReadTreeGrowth(tp) <= CITY_TREE_DEAD && ground != CITY_GROUND_ROCKS;
    } else if (kind != TK_CLEAR && kind != TK_WATER_BANK)
      valid = valid && !(tp->zone & (CITY_TILE_GROWTH_MASK & ~(kind == TK_WIND_POWER ? CITY_WIND_MODERN_MASK : 0)));
    if (bridge) {
      int allowed = CITY_BRIDGE_AXIS_MASK | (head ? CITY_BRIDGE_HEAD_FACING_MASK : 0) |
          (IsTileKindHighwayBridge(kind) ? CITY_BRIDGE_HIGHWAY_RIGHT : 0);
      valid = valid && !ReadCityZoneKind(tp) && !(metadata & ~allowed) && ValidateCityBridgeSpan(city, tile);
    } else if (kind == TK_CLEAR || IsTileKindExposedWater(kind)) valid = valid && !metadata;
    else if (IsTileKindGroundCrossing(kind)) valid = valid && !(metadata & ~CITY_CROSSING_AXIS_MASK);
    else if (IsTileKindVariant(kind)) valid = valid && !(metadata & ~(CITY_TILE_VARIANT_MASK |
        (kind == TK_HIGHWAY_ENTRANCE ? CITY_HIGHWAY_RAMP_MIRROR_MASK : 0)));
    else if (IsTileKindSingleTileFootprint(kind)) valid = valid && !metadata;
    if (IsTileKindPlainPowerLine(kind) && !wet)
      valid = valid && ReadTransportConnections(tp) == FindCityPowerConnectionMask(city, tile);
    if (!valid) break;
    if (IsTileKindHighwayFootprint(kind)) {
      CityRect bounds = CityHighwayFootprintBounds(city, tile, &city->tile[0].terrain, sizeof(TileInfo));
      int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
      bool rectangle = width == 2 && height == 2;
      if (IsTileKindStraightHighwayFamily(kind)) rectangle |= ReadCityHighwayAxis(tp) ?
          width == 2 && height == 1 : width == 1 && height == 2;
      valid = valid && rectangle && bounds.left >= 0 && bounds.top >= 0 &&
          bounds.right <= city->map_width && bounds.bottom <= city->map_height;
      if (valid) for (int dy = 0; dy < height; ++dy) for (int dx = 0; dx < width; ++dx) {
        const TileInfo *other = &city->tile[MapTileXY(city, bounds.left + dx, bounds.top + dy)];
        int expected = (dx ? CITY_BUILDING_CONTINUE_X : 0) | (dy ? CITY_BUILDING_CONTINUE_Y : 0);
        valid = valid && (IsTileKindStraightHighwayFamily(kind) ?
            IsTileKindStraightHighwayFamily(other->kind) : other->kind == kind) &&
            (other->terrain & CITY_BUILDING_CONTINUATION_MASK) == expected &&
            !((other->terrain ^ tp->terrain) & CITY_HIGHWAY_ORIENTATION_MASK);
      }
    }
    if (IsTileKindBuilding(kind)) {
      CityPoint point = MapTilePt(city, tile);
      if (metadata & CITY_BUILDING_CONTINUE_X) valid = valid && point.x > 0 &&
          ReadTile(city, tile - 1) == kind &&
          !((city->tile[tile - 1].terrain ^ tp->terrain) & CITY_BUILDING_ORIENTATION_MASK);
      if (metadata & CITY_BUILDING_CONTINUE_Y) valid = valid && point.y > 0 &&
          ReadTile(city, tile - city->map_width) == kind &&
          !((city->tile[tile - city->map_width].terrain ^ tp->terrain) & CITY_BUILDING_ORIENTATION_MASK);
    }
  }
  return valid;
}

// Native planes are applied after stock records, retaining partial writes on an invalid extension.
bool ApplyCityGroundExtension(City *city, const u8 *bytes, u32 size) {
  TileIndex cells = city->map_cell_count;
  if (!city->native_map_storage || size < CityGroundExtensionSize(city) || size > CITY_SAVE_EXTENSION_LIMIT)
    return false;
  u32 controls = TerrainSnowNoiseSize(city->map_width, city->map_height);
  const u8 *header = bytes + 6 * cells;
  if (memcmp(header, "CLIM", 4) || ReadLE32(header + 4) != controls) return false;
  size_t map_size = (size_t)cells * sizeof(TileInfo);
  TileInfo *legacy = malloc(map_size);
  if (!legacy) return false;
  memcpy(legacy, city->tile, map_size);
  for (TileIndex tile = 0; tile < cells; ++tile) {
    TileInfo *tp = &city->tile[tile];
    tp->terrain = bytes[tile];
    tp->zone = bytes[cells + tile];
    tp->flags = bytes[2 * cells + tile];
    tp->kind = bytes[3 * cells + tile];
    tp->altitude = ReadLE16(bytes + 4 * cells + 2 * tile);
  }
  bool valid = ValidateCityTileRecords(city);
  for (TileIndex tile = 0; tile < cells && valid; ++tile) {
    TileInfo projected = TranslateCityTileToLegacy(city, tile);
    valid = projected.kind == legacy[tile].kind && projected.zone == legacy[tile].zone &&
        projected.terrain == legacy[tile].terrain && projected.flags == legacy[tile].flags &&
        projected.altitude == legacy[tile].altitude;
  }
  free(legacy);
  if (valid) memcpy(city->snow.points, header + CITY_SAVE_CLIMATE_HEADER_SIZE, controls);
  return valid;
}

// All neighbour reads use City's unchanged source records. The caller stages complete output records separately, so
// kind, flags and future payload changes cannot alter a later tile's interpretation. Coordinates/size must describe an
// admitted stock footprint; returns native corner metadata without publishing a converted source tile.
static int LegacyRectangleCorners(const City *city, int dx, int dy, int size) {
  int mask = 0, rotation = city->geography.rotation;
  if (!dx && !dy) mask |= CITY_ZONE_CORNER_0 << rotation;
  if (!dx && dy == size - 1) mask |= CITY_ZONE_CORNER_0 << ((rotation + 1) & 3);
  if (dx == size - 1 && dy == size - 1) mask |= CITY_ZONE_CORNER_0 << ((rotation + 2) & 3);
  if (dx == size - 1 && !dy) mask |= CITY_ZONE_CORNER_0 << ((rotation + 3) & 3);
  return mask;
}

// Decode a valid stock building cell and its continuation/variant metadata into caller-owned result. Source map must
// remain unchanged during the whole conversion pass; false declines unsupported or malformed building geometry.
static bool TranslateLegacyBuilding(const City *city, TileIndex tile, TileInfo *result) {
  int kind = ReadTile(city, tile), size = kCityBuildingFootprintSize[kind - TK_RESIDENTIAL_FIRST];
  CityPoint point = MapTilePt(city, tile);
  int corners = city->tile[tile].zone & CITY_ZONE_CORNER_MASK;
  for (int dy = 0; dy < size; ++dy) for (int dx = 0; dx < size; ++dx) {
    int left = point.x - dx, top = point.y - dy;
    if (left < 0 || top < 0 || left + size > city->map_width || top + size > city->map_height ||
        corners != LegacyRectangleCorners(city, dx, dy, size)) continue;
    TileIndex anchor = MapTileXY(city, left, top);
    if (ReadTile(city, anchor) != kind || (city->tile[anchor].zone & CITY_ZONE_CORNER_MASK) !=
        LegacyRectangleCorners(city, 0, 0, size)) continue;
    bool complete = true, submerged = false;
    int water_height = -1, water_flags = 0, water_ground = TK_WATER, minimum_height = 31;
    for (int cy = 0; cy < size && complete; ++cy) for (int cx = 0; cx < size; ++cx) {
      TileIndex cell = MapTileXY(city, left + cx, top + cy);
      const TileInfo *tp = &city->tile[cell];
      if (tp->kind != kind || (tp->zone & CITY_ZONE_CORNER_MASK) !=
          LegacyRectangleCorners(city, cx, cy, size)) {
        complete = false;
        break;
      }
      bool water = LegacySourceWaterKind(city, cell) != CITY_WATER_NONE;
      submerged |= water;
      if (kind == TK_MARINA) {
        minimum_height = IntMin(minimum_height, ReadCityLandHeight(tp));
        if (water_height < 0 && water) {
          water_height = LegacySourceWaterHeight(city, cell);
          water_flags = tp->flags & TF_SALTWATER;
          water_ground = LegacyWaterGroundKind(tp->terrain & CITY_TERRAIN_KIND_MASK);
        }
      }
    }
    if (!complete) continue;
    // Removing a submerged member must not leave dry cells linked to a missing building.
    if (submerged && !(ReadCityTileClasses(kind) & TCLASS_WATER) && kind != TKL_HYDRO_POWER_LAST)
      goto clear;
    if (kind == TK_MARINA) {
      if (water_height < 0) water_height = minimum_height;
      result->altitude = (result->altitude & ~(CITY_ALTITUDE_LAND_MASK | CITY_ALTITUDE_WATER_MASK)) |
          IntMin(ReadCityLandHeight(result), water_height) | water_height << CITY_ALTITUDE_WATER_SHIFT;
      int ground = result->flags & CITY_LEGACY_MAP_FLAG_WATER ? result->terrain >> 4 : water_ground;
      result->terrain = (result->terrain & CITY_TERRAIN_SHAPE_MASK) | ground << 4;
      result->flags = (result->flags & ~TF_SALTWATER) | water_flags | CITY_LEGACY_MAP_FLAG_WATER;
    }
    int orientation = (city->tile[anchor + size - 1].flags & CITY_LEGACY_MAP_FLAG_ROTATED) ? 1 : 0;
    result->zone = (result->zone & CITY_ZONE_KIND_MASK) | (dx ? CITY_BUILDING_CONTINUE_X : 0) |
        (dy ? CITY_BUILDING_CONTINUE_Y : 0) | EncodeBuildingOrientation(orientation + city->geography.rotation);
    return true;
  }
  // Shipped cities contain incomplete buildings: isolated interiors in SYDNEY
  // and loading bays partly overwritten by other seaport buildings in JUNETOWN.
  // Clear only this surviving tile, retaining its ground geometry and zoning.
  if (size == 1 || (result->flags & CITY_LEGACY_MAP_FLAG_WATER)) return false;
clear:
  result->kind = TK_CLEAR;
  result->zone &= CITY_ZONE_KIND_MASK;
  result->terrain &= CITY_TERRAIN_SHAPE_MASK;
  result->flags &= ~(TF_PIPED | TF_POWERED | TF_POWERABLE);
  result->altitude &= CITY_ALTITUDE_CLEAR_WATER;
  return true;
}

// Derive a valid stock highway cell's footprint offsets from unchanged stock artwork/flags. Returns coordinates used
// by the decoder; does not alter the source or search a native footprint.
static CityPoint LegacyHighwayOffsets(const City *city, TileIndex tile) {
  int kind = ReadTile(city, tile);
  if (kind <= TKL_HIGHWAY_LAST) {
    // Stock straight highways have an implicit 2x2 grid. Decode it once.
    CityPoint point = MapTilePt(city, tile);
    return (CityPoint){point.x & 1, point.y & 1};
  }
  int corner = 0;
  while (corner < 4 && (city->tile[tile].zone & CITY_ZONE_CORNER_MASK) !=
      (CITY_ZONE_CORNER_0 << ((city->geography.rotation + corner) & 3))) ++corner;
  return corner == 4 ? (CityPoint){-1, -1} : (CityPoint){corner >= 2, corner == 1 || corner == 2};
}

// Decode one valid stock highway cell into a staged native record. Source neighbors must remain in stock form until
// conversion completes; returns whether this family handled the tile.
static bool TranslateLegacyHighway(const City *city, TileIndex tile, TileInfo *result) {
  int kind = ReadTile(city, tile);
  CityPoint offset = LegacyHighwayOffsets(city, tile);
  if (offset.x < 0) return false;
  int dx = offset.x, dy = offset.y;
  CityPoint point = MapTilePt(city, tile);
  point.x -= dx;
  point.y -= dy;
  if (point.x < 0 || point.y < 0 || point.x + 1 >= city->map_width || point.y + 1 >= city->map_height) return false;
  for (int cy = 0; cy < 2; ++cy) for (int cx = 0; cx < 2; ++cx) {
    TileIndex cell = MapTileXY(city, point.x + cx, point.y + cy);
    const TileInfo *tp = &city->tile[cell];
    int other = tp->kind;
    if (kind <= TKL_HIGHWAY_LAST ? other < TKL_HIGHWAY_FIRST || other > TKL_HIGHWAY_LAST :
        other != kind) return false;
    if (kind >= TKL_HIGHWAY_INCLINE_FIRST && (tp->zone & CITY_ZONE_CORNER_MASK) !=
        LegacyRectangleCorners(city, cx, cy, 2)) return false;
  }
  result->zone = (result->zone & CITY_ZONE_KIND_MASK) | (dx ? CITY_BUILDING_CONTINUE_X : 0) |
      (dy ? CITY_BUILDING_CONTINUE_Y : 0);
  return true;
}

// Test whether a valid cell in the unchanged stock map belongs to a highway bridge. This classification is read-only
// and is not suitable for a partially converted native map.
static bool IsLegacyHighwayWaterCell(const City *city, TileIndex tile) {
  int kind = ReadTile(city, tile);
  if (kind >= TKL_HIGHWAY_BRIDGE_FIRST && kind <= TKL_HIGHWAY_BRIDGE_LAST) return true;
  if (kind < TKL_HIGHWAY_FIRST || kind > TKL_HIGHWAY_LAST) return false;
  CityPoint point = MapTilePt(city, tile);
  point.x &= ~1;
  point.y &= ~1;
  for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
    TileIndex other = MapTileXYOrInvalid(city, point.x + dx, point.y + dy);
    if (other != INVALID_TILE && (city->tile[other].flags & CITY_LEGACY_MAP_FLAG_WATER)) return true;
  }
  return false;
}

static bool IsLegacyHighwayBridgeCell(const City *city, TileIndex tile) {
  if (IsLegacyHighwayWaterCell(city, tile)) return true;
  const TileInfo *tp = &city->tile[tile];
  if (tp->kind < TKL_HIGHWAY_FIRST || tp->kind > TKL_HIGHWAY_LAST) return false;
  int axis = LegacyHighwayAxis(tp->kind, tp->flags);
  for (int side = -1; side <= 1; side += 2) {
    TileIndex next = GetRelativeTileOrInvalid(city, tile, side * !axis, side * axis);
    if (next != INVALID_TILE && IsLegacyHighwayWaterCell(city, next) &&
        LegacyHighwayAxis(ReadTile(city, next), city->tile[next].flags) == axis) return true;
  }
  return false;
}

// Locate spans in the immutable stock map. Lane/corner metadata prevents parallel bridges and perpendicular crossings
// from joining during decoding. Tile, axis, lane and side must describe the unchanged source span; unmatched or
// off-map neighbors return INVALID_TILE.
static TileIndex LegacyHighwayBridgeNeighbor(const City *city, TileIndex tile, int axis, int lane, int side) {
  TileIndex next = GetRelativeTileOrInvalid(city, tile, side * !axis, side * axis);
  if (next == INVALID_TILE) return INVALID_TILE;
  const TileInfo *tp = &city->tile[next];
  if (!IsLegacyHighwayBridgeCell(city, next) || LegacyHighwayAxis(tp->kind, tp->flags) != axis) return INVALID_TILE;
  CityPoint offset = LegacyHighwayOffsets(city, next);
  return (axis ? offset.x : offset.y) == lane ? next : INVALID_TILE;
}

// Decode the stock bridge lane, span identity and native metadata into a staged output record. Requires valid
// unchanged source geometry; false rejects/declines unsupported matching without publishing partial converted records.
static bool TranslateLegacyHighwayBridge(const City *city, TileIndex tile, TileInfo *result) {
  if (!TranslateLegacyHighway(city, tile, result)) return false;
  const TileInfo *source = &city->tile[tile];
  int axis = LegacyHighwayAxis(source->kind, source->flags);
  int lane = (result->zone & (axis ? CITY_BUILDING_CONTINUE_X : CITY_BUILDING_CONTINUE_Y)) != 0;
  TileIndex first = tile, last = tile, next;
  while ((next = LegacyHighwayBridgeNeighbor(city, first, axis, lane, -1)) != INVALID_TILE) first = next;
  while ((next = LegacyHighwayBridgeNeighbor(city, last, axis, lane, 1)) != INVALID_TILE) last = next;
  int stride = axis ? city->map_width : 1;
  int length = (last - first) / stride + 1;
  if (length < 2 || (length & 1)) return false;
  if (LegacySourceWaterKind(city, first) != CITY_WATER_NONE ||
      LegacySourceWaterKind(city, last) != CITY_WATER_NONE) {
    result->kind = TK_WATER;
    result->zone &= CITY_ZONE_KIND_MASK;
    return true;
  }
  bool head = tile == first || tile == last;
  int phase = source->kind >= TKL_HIGHWAY_BRIDGE_FIRST ?
      (source->kind - TKL_HIGHWAY_BRIDGE_FIRST) ^ (((tile - first) / stride / 2) & 1) : 0;
  result->kind = head ? TK_HIGHWAY_BRIDGE_HEAD : TK_HIGHWAY_BRIDGE_SPAN;
  result->zone = (axis ? CITY_BRIDGE_AXIS_MASK : 0) | (lane ? CITY_BRIDGE_HIGHWAY_RIGHT : 0) |
      (head && tile == last ? CITY_BRIDGE_HEAD_FACING_MASK : 0);
  if (head) result->altitude = (result->altitude & CITY_ALTITUDE_CLEAR_WATER) |
      (phase ? CITY_BRIDGE_HIGHWAY_PHASE_MASK : 0);
  if (head) result->terrain = (result->terrain & 15) | HIGHWAY_BRIDGE_STYLE_STANDARD << 4;
  result->flags &= ~CITY_LEGACY_MAP_FLAG_ROTATED;
  return true;
}

// Classify stock road/rail bridge artwork kinds used by import. Does not inspect native head/span metadata or mutate a
// City.
static bool IsLegacySurfaceBridge(int kind) {
  return kind >= TKL_SUSPENSION_BRIDGE_FIRST && kind <= TKL_RAIL_BRIDGE_LAST;
}

// Check whether a valid stock cell belongs to the requested bridge transport and axis. Reads unchanged source records
// only; returns false for a different family or incompatible piece.
static bool LegacyBridgeMatches(const City *city, TileIndex tile, bool rail, int axis) {
  const TileInfo *tp = &city->tile[tile];
  return IsLegacySurfaceBridge(tp->kind) && (tp->kind >= TKL_RAIL_BRIDGE_FIRST) == rail &&
      ((tp->flags & CITY_LEGACY_MAP_FLAG_ROTATED) != 0) == axis;
}

// Decode a whole span's identity from the unchanged stock records. Its two banks become native heads; all structural
// artwork is selected from the head's style.
static bool TranslateLegacySurfaceBridge(const City *city, TileIndex tile, TileInfo *result) {
  assert(IsMapTileValid(city, tile));
  static const i8 kSteps[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
  TileIndex member = tile;
  int kind = ReadTile(city, tile), axis;
  bool rail;
  if (IsLegacySurfaceBridge(kind)) {
    rail = kind >= TKL_RAIL_BRIDGE_FIRST;
    axis = (city->tile[tile].flags & CITY_LEGACY_MAP_FLAG_ROTATED) != 0;
  } else {
    bool found = false;
    for (int direction = 0; direction < 4; ++direction) {
      TileIndex neighbor = GetRelativeTileOrInvalid(city, tile, kSteps[direction][0], kSteps[direction][1]);
      if (neighbor == INVALID_TILE || !IsLegacySurfaceBridge(ReadTile(city, neighbor))) continue;
      rail = ReadTile(city, neighbor) >= TKL_RAIL_BRIDGE_FIRST;
      axis = direction >= 2;
      int base = rail ? TKL_RAIL_FIRST : TKL_ROAD_EAST_WEST;
      if ((!IsTileKindUnbuilt(kind) && (kind < base || kind >= base + 15)) ||
          LegacySourceWaterKind(city, tile) != CITY_WATER_NONE ||
          !LegacyBridgeMatches(city, neighbor, rail, axis)) continue;
      member = neighbor;
      found = true;
      break;
    }
    if (!found) return false;
  }
  TileIndex first = member, last = member, next;
  BridgeStyle style = rail ? BRIDGE_STYLE_CANTILEVER_STEEL : BRIDGE_STYLE_CONCRETE;
  for (int side = -1; side <= 1; side += 2) {
    TileIndex current = member;
    for (;;) {
      int piece = ReadTile(city, current);
      if (!rail && (piece == TKL_RAISING_BRIDGE_CLOSED || piece == TKL_RAISING_BRIDGE_OPEN))
        style = BRIDGE_STYLE_DRAWBRIDGE;
      else if (!rail && style != BRIDGE_STYLE_DRAWBRIDGE && piece <= TKL_SUSPENSION_BRIDGE_LAST)
        style = BRIDGE_STYLE_SUSPENSION_STEEL;
      next = GetRelativeTileOrInvalid(city, current, side * !axis, side * axis);
      if (next == INVALID_TILE || !LegacyBridgeMatches(city, next, rail, axis)) break;
      current = next;
    }
    int base = rail ? TKL_RAIL_FIRST : TKL_ROAD_EAST_WEST;
    if (next != INVALID_TILE &&
        (IsTileKindUnbuilt(ReadTile(city, next)) ||
            (ReadTile(city, next) >= base && ReadTile(city, next) < base + 15)) &&
        LegacySourceWaterKind(city, next) == CITY_WATER_NONE) current = next;
    if (side < 0) first = current; else last = current;
  }
  if (first == last || (member != tile && tile != first && tile != last)) return false;
  if (LegacySourceWaterKind(city, first) != CITY_WATER_NONE ||
      LegacySourceWaterKind(city, last) != CITY_WATER_NONE) {
    result->kind = TK_WATER;
    result->zone &= CITY_ZONE_KIND_MASK;
    return true;
  }
  bool head = tile == first || tile == last;
  result->kind = rail ? (head ? TK_RAIL_BRIDGE_HEAD : TK_RAIL_BRIDGE_SPAN) :
      head ? TK_ROAD_BRIDGE_HEAD : TK_ROAD_BRIDGE_SPAN;
  result->zone = (axis ? CITY_BRIDGE_AXIS_MASK : 0) | (head && tile == last ? CITY_BRIDGE_HEAD_FACING_MASK : 0);
  if (head) {
    result->altitude &= CITY_ALTITUDE_CLEAR_WATER;
    result->terrain = (result->terrain & 15) | style << 4;
  }
  result->flags &= ~CITY_LEGACY_MAP_FLAG_ROTATED;
  return true;
}

// Decode one complete stock tile to caller-owned native output, using unchanged source neighbors. Tile must be valid
// and output writable; false reports unsupported/malformed geometry without installing that output in the source map.
bool TranslateCityTileFromLegacy(const City *city, TileIndex tile, TileInfo *result) {
  *result = city->tile[tile];
  int kind = result->kind;
  if (city->native_map_storage) return true;
  result->kind = NativeBaseKind(kind);
  TranslateLegacyCityTerrain(city, tile, result);
  if ((result->flags & CITY_LEGACY_MAP_FLAG_WATER) && kind >= TK_RESIDENTIAL_FIRST &&
      kind != TK_HYDRO_POWER && kind != TKL_HYDRO_POWER_LAST && kind != TK_PIER && kind != TK_MARINA) {
    result->kind = TK_WATER;
    goto water_kind;
  }
  if (kind == TK_CLEAR && (result->flags & CITY_LEGACY_MAP_FLAG_WATER)) result->kind = TK_WATER;
  if (IsLegacyHighwayBridgeCell(city, tile)) {
    if (!TranslateLegacyHighwayBridge(city, tile, result)) return false;
    goto water_kind;
  }
  if (IsLegacySurfaceBridge(kind)) {
    if (!TranslateLegacySurfaceBridge(city, tile, result)) return false;
    goto water_kind;
  }
  if ((IsCitySurfaceTransportArtwork(kind) || IsTileKindUnbuilt(kind)) &&
      TranslateLegacySurfaceBridge(city, tile, result)) goto water_kind;
  if (kind >= TK_RESIDENTIAL_FIRST) {
    if (!TranslateLegacyBuilding(city, tile, result)) return false;
    if (result->kind == TK_CLEAR) goto water_kind;
  }
  if (kind == TK_HYDRO_POWER || kind == TKL_HYDRO_POWER_LAST) {
    int mirror = (DecodeBuildingOrientation(result->zone) - city->geography.rotation) & 1;
    int orientation = ((kind - TK_HYDRO_POWER) << 1) | mirror;
    result->kind = TK_HYDRO_POWER;
    result->zone = (result->zone & CITY_ZONE_KIND_MASK) |
        EncodeBuildingOrientation(orientation + city->geography.rotation);
  }
  if (IsLegacyHighway(kind)) {
    if (!TranslateLegacyHighway(city, tile, result)) return false;
  } else if (IsTileKindSingleTileFootprint(NativeBaseKind(kind))) {
    result->zone &= CITY_ZONE_KIND_MASK;
  }
  if (IsLegacyHighway(kind)) {
    if (kind <= TKL_HIGHWAY_LAST) {
      result->kind = NativeHighwayKind(kind);
      result->zone |= LegacyHighwayAxis(kind, result->flags) << CITY_HIGHWAY_ORIENTATION_SHIFT;
    } else {
      int shape = kind - TKL_HIGHWAY_ENTRANCE_93;
      result->kind = CityHighwayFootprintKind(shape);
      int orientation = CityHighwayFootprintOrientation(shape);
      // Fold stock horizontal mirroring into the physical incline direction.
      if (result->kind == TK_HIGHWAY_INCLINE && (result->flags & CITY_LEGACY_MAP_FLAG_ROTATED)) orientation ^= 1;
      result->zone |= orientation << CITY_HIGHWAY_ORIENTATION_SHIFT;
    }
  }
  bool land_power = kind >= TKL_POWER_LINE_EAST_WEST && kind <= TKL_POWER_LINE_LAST;
  if (IsCitySurfaceTransportArtwork(kind)) {
    result->kind = kind < TKL_RAIL_FIRST ? TK_ROAD : TK_RAIL;
    result->zone = (result->zone & CITY_ZONE_KIND_MASK) |
        LegacyTransportConnectionMask(kind) << CITY_ZONE_METADATA_SHIFT;
  }
  if (land_power || kind == TKL_POWER_LINE_BRIDGE) {
    result->kind = TK_POWER_LINE;
    result->zone &= CITY_ZONE_KIND_MASK;
    int connections = land_power ? LegacyTransportConnectionMask(kind) :
        result->flags & CITY_LEGACY_MAP_FLAG_ROTATED ? CONN_NS : CONN_EW;
    result->zone |= connections << CITY_ZONE_METADATA_SHIFT;
  } else if (kind >= TKL_ROAD_POWER_CROSSING_FIRST && kind <= TKL_RAIL_POWER_CROSSING_LAST) {
    static const u8 kCrossingKinds[] = {TK_ROAD_POWER_CROSSING, TK_ROAD_RAIL_CROSSING, TK_RAIL_POWER_CROSSING};
    int axis = (kind - TKL_ROAD_POWER_CROSSING_FIRST) & 1;
    result->kind = kCrossingKinds[(kind - TKL_ROAD_POWER_CROSSING_FIRST) >> 1];
    result->zone &= CITY_ZONE_KIND_MASK;
    result->zone |= axis ? CITY_CROSSING_AXIS_MASK : 0;
  }
  int base = NativeVariantKindFromLegacy(kind);
  if (base >= 0) {
    result->kind = base;
    result->zone = (result->zone & CITY_ZONE_KIND_MASK) |
        ((kind - LegacyBaseKind(base)) << CITY_TILE_VARIANT_SHIFT);
    if (base == TK_HIGHWAY_ENTRANCE) {
      if (result->flags & CITY_LEGACY_MAP_FLAG_ROTATED) result->zone |= CITY_HIGHWAY_RAMP_MIRROR_MASK;
    }
  }
water_kind:
  result->kind = CityTileKindForWater(result->kind, (result->flags & CITY_LEGACY_MAP_FLAG_WATER) != 0);
  if ((result->flags & CITY_LEGACY_MAP_FLAG_WATER) && IsTileKindExposedWater(result->kind)) {
    result->kind = result->terrain >> 4;
    result->terrain &= CITY_TERRAIN_SHAPE_MASK;
  }
  if (IsTileKindExposedWater(result->kind) && kind != TK_CLEAR) {
    result->zone &= CITY_ZONE_KIND_MASK;
    result->flags &= ~(TF_PIPED | TF_POWERED | TF_POWERABLE);
    result->overlay = CITY_OVERLAY_NONE;
  }
  result->flags &= ~(CITY_LEGACY_MAP_FLAG_ROTATED | CITY_LEGACY_MAP_FLAG_WATER);
  // Dry stock buildings have no construction-stage payload. Normalize that
  // unused water-height field at import, before gameplay reads native records.
  if (CityTileCanStoreBuildingStage(result)) result->altitude &= CITY_ALTITUDE_CLEAR_WATER;
  int shape = result->terrain & CITY_TERRAIN_SHAPE_MASK;
  int metadata = result->zone & CITY_ZONE_METADATA_MASK;
  int ground = HasTileWater(result) ? (IsTileKindExposedWater(result->kind) ? 0 :
      (result->terrain >> 4) - TK_WATER) : CITY_GROUND_GRASS;
  if (IsCityBridgeHead(result)) WriteTileBridgeStyle(result, result->terrain >> 4);
  result->terrain = metadata | ground;
  result->zone = shape ? CITY_TILE_SLOPED | shape : result->zone & CITY_ZONE_KIND_MASK;
  return true;
}

// Reset native map-format metadata to its initialization defaults. Requires a City owner; this changes format state
// rather than converting the contents of map arrays.
void InitializeCityMapFormat(City *city) {
  city->native_map_storage = true;
}

// Mark map storage as stock source before loading its records. Call before boundary conversion; native
// simulation/geometry consumers must not inspect an unfinished stock map.
void BeginLegacyCityMapImport(City *city) {
  city->native_map_storage = false;
}

// File decoding retains its original writes and unwind behaviour. Conversion reads unchanged stock records and
// publishes native tiles only after success. Simulation rebuilding must never inspect an unfinished stock map. Stages
// complete records while source neighbors stay stock; false preserves load failure state and success publishes native
// storage before rebuild.
bool ConvertCityMapToNative(City *city) {
  if (city->native_map_storage) return true;
  RepairLegacyCityRiverBanks(city);
  size_t size = (size_t)city->map_cell_count * sizeof(TileInfo);
  TileInfo *converted = malloc(size);
  if (!converted) return false;
  u32 cleared = 0, submerged = 0, sloped_zones = 0;
  for (TileIndex tile = 0; tile < city->map_cell_count; ++tile) {
    if (!TranslateCityTileFromLegacy(city, tile, &converted[tile])) {
      free(converted);
      return false;
    }
    sloped_zones += (city->tile[tile].zone & CITY_ZONE_KIND_MASK) && ReadTerrainCode(&converted[tile]);
    cleared += ReadTile(city, tile) >= TK_RESIDENTIAL_FIRST && converted[tile].kind == TK_CLEAR;
    submerged += ReadTile(city, tile) != TK_CLEAR && IsTileKindExposedWater(converted[tile].kind);
  }
  // Stock coastlines can have dry, flat land at the waterline. Preserve that layout
  // while still allowing water to reclaim neighboring shore slopes.
  city->fixes &= ~(1ull << CITY_FIX_WATER_SPREADING);
  city->load_sloped_zone_tiles = sloped_zones;
  city->load_cleared_building_tiles = cleared;
  city->load_submerged_occupant_tiles = submerged;
  memcpy(city->tile, converted, size);
  free(converted);
  city->native_map_storage = true;
  RecountCityTileStatistics(city);
  InvalidateMap(city);
  return true;
}

// Complete imported-map initialization using the decoded native format and runtime metadata. Requires a successful
// load/conversion; false is a load failure and must not trigger later success rebuilding.
bool ImportCityMapFormat(City *city) {
  if (!ConvertCityMapToNative(city)) return false;
  InitializeCityLandscape(city);
  // MISC contains projected stock counters; native tile records are canonical.
  RecountCityTileStatistics(city);
  for (int record = 0; record < CITY_FACILITY_RECORD_COUNT; ++record)
    if (city->annual_facilities.record[record].type == TKL_HYDRO_POWER_LAST)
      city->annual_facilities.record[record].type = TK_HYDRO_POWER;
  return true;
}

// Project a valid native bridge cell into its stock artwork kind using stored span metadata. Span must be consistent;
// this conversion does not change bridge topology or the stored native kind.
int LegacyCityBridgeKind(const City *city, TileIndex tile) {
  return IsTileKindBridge(ReadTile(city, tile)) ? ProjectStyledCityBridgeKind(city, tile) : ReadTile(city, tile);
}

// Map a stock artwork selector to the category used by tile information presentation. Input uses the projection
// namespace; result is a category value without City edits.
int CityTileInformationCategory(int artwork) {
  static const u8 kTileInformationCategoryLimits[CITY_TILE_CATEGORY_COUNT] = {
      TK_CLEAR + 1,
      TKL_RUBBLE_LAST + 1,
      TK_RADIOACTIVE_WASTE + 1,
      TK_TREE_LAST + 1,
      TKL_PARK + 1,
      TKL_POWER_LINE_LAST + 1,
      TKL_ROAD_LAST + 1,
      TKL_TUNNEL_ENTRANCE_FIRST,
      TKL_TUNNEL_ENTRANCE_LAST + 1,
      TKL_ROAD_RAIL_CROSSING_LAST + 1,
      TKL_RAIL_POWER_CROSSING_LAST + 1,
      TKL_HIGHWAY_LAST + 1,
      TKL_SUSPENSION_BRIDGE_LAST + 1,
      TKL_BRIDGE_LAST + 1,
      TKL_POWER_LINE_BRIDGE + 1,
      TKL_HIGHWAY_ENTRANCE_LAST + 1,
      TKL_HIGHWAY_JUNCTION + 1,
      TKL_HIGHWAY_BRIDGE_LAST + 1,
      TKL_SUBWAY_TO_RAIL_LAST + 1,
      TK_LOWER_CLASS_HOMES_LAST + 1,
      TK_MIDDLE_CLASS_HOMES_LAST + 1,
      TK_LUXURY_HOMES_LAST + 1
  };
  if (artwork >= TK_GAS_STATION_1X1_1) return artwork - 102;
  for (int category = 0; category < CITY_TILE_CATEGORY_COUNT; ++category)
    if (kTileInformationCategoryLimits[category] > artwork) return category;
  return artwork;
}

// Decode the fixed stock tile-count block into caller-owned count storage. Bytes must contain the whole block in its
// expected byte order; output is replaced without modifying a live map.
void DecodeCityLegacyTileCounts(const u8 *bytes, CityTileCounts *counts) {
  for (int slot = 0; slot < TK_COUNT; ++slot) {
    i16 value;
    memcpy(&value, bytes + slot * 2, 2);
    counts->value[slot] = value;
  }
  static const u8 kFamilies[][2] = {
      {TK_RUBBLE, TKL_RUBBLE_LAST},
      {TKL_POWER_LINE_EAST_WEST, TKL_POWER_LINE_LAST},
      {TKL_ROAD_EAST_WEST, TKL_ROAD_LAST},
      {TKL_RAIL_FIRST, TKL_RAIL_TRANSITION_62},
      {TKL_ROAD_POWER_CROSSING_FIRST, TKL_ROAD_POWER_CROSSING_LAST},
      {TKL_ROAD_RAIL_CROSSING_FIRST, TKL_ROAD_RAIL_CROSSING_LAST},
      {TKL_RAIL_POWER_CROSSING_FIRST, TKL_RAIL_POWER_CROSSING_LAST},
      {TKL_TUNNEL_ENTRANCE_FIRST, TKL_TUNNEL_ENTRANCE_LAST},
      {TKL_HIGHWAY_FIRST, TKL_HIGHWAY_NS},
      {TKL_HIGHWAY_ROAD_CROSSING_FIRST, TKL_HIGHWAY_ROAD_CROSSING_LAST},
      {TKL_HIGHWAY_RAIL_CROSSING_FIRST, TKL_HIGHWAY_RAIL_CROSSING_LAST},
      {TKL_HIGHWAY_POWER_CROSSING_FIRST, TKL_HIGHWAY_POWER_CROSSING_LAST},
      {TKL_HIGHWAY_BRIDGE_FIRST, TKL_HIGHWAY_BRIDGE_LAST},
      {TKL_HIGHWAY_ENTRANCE_93, TKL_HIGHWAY_ENTRANCE_LAST},
      {TKL_HIGHWAY_INCLINE_FIRST, TKL_HIGHWAY_INCLINE_LAST},
      {TKL_HIGHWAY_BEND_FIRST, TKL_HIGHWAY_BEND_LAST},
      {TKL_SUBWAY_TO_RAIL_FIRST, TKL_SUBWAY_TO_RAIL_LAST},
      {TK_HYDRO_POWER, TKL_HYDRO_POWER_LAST}};
  for (int family = 0; family < (int)(sizeof(kFamilies) / sizeof(kFamilies[0])); ++family)
    for (int kind = kFamilies[family][0] + 1; kind <= kFamilies[family][1]; ++kind) {
      counts->count[kFamilies[family][0]] += counts->count[kind];
      counts->count[kind] = 0;
    }
}

// Direction belongs to native metadata. Split counts only at the stock boundary, preserving each family's live total
// rather than rebuilding unrelated counters. Output must hold TK_COUNT counters; projects existing live totals without
// rebuilding unrelated categories from the map.
void ReadCityLegacyTileCounts(const City *city, u32 counts[TK_COUNT]) {
  memcpy(counts, city->statistics.count, sizeof(city->statistics.count));
  if (!city->native_map_storage) return;
  bool hydro = counts[TK_HYDRO_POWER] != 0;
  if (hydro) counts[TKL_HYDRO_POWER_LAST] = 0;
  bool highway_bridges = counts[TKL_HIGHWAY_BRIDGE_FIRST] != 0;
  counts[TKL_HIGHWAY_BRIDGE_LAST] = 0;
  bool transport = counts[TKL_POWER_LINE_EAST_WEST] || counts[TKL_ROAD_EAST_WEST] || counts[TKL_RAIL_FIRST];
  bool variants = counts[TK_RUBBLE] || counts[TKL_TUNNEL_ENTRANCE_FIRST] ||
      counts[TKL_HIGHWAY_ENTRANCE_93] || counts[TKL_SUBWAY_TO_RAIL_FIRST];
  bool crossings = counts[TKL_ROAD_POWER_CROSSING_FIRST] || counts[TKL_ROAD_RAIL_CROSSING_FIRST] ||
      counts[TKL_RAIL_POWER_CROSSING_FIRST];
  counts[TKL_ROAD_POWER_CROSSING_LAST] = 0;
  counts[TKL_ROAD_RAIL_CROSSING_LAST] = 0;
  counts[TKL_RAIL_POWER_CROSSING_LAST] = 0;
  bool highway_shapes = counts[TKL_HIGHWAY_INCLINE_FIRST] || counts[TKL_HIGHWAY_BEND_FIRST];
  for (int kind = TKL_HIGHWAY_INCLINE_FIRST + 1; kind <= TKL_HIGHWAY_INCLINE_LAST; ++kind) counts[kind] = 0;
  for (int kind = TKL_HIGHWAY_BEND_FIRST + 1; kind <= TKL_HIGHWAY_BEND_LAST; ++kind) counts[kind] = 0;
  bool split = hydro || transport || variants || highway_bridges || crossings || highway_shapes;
  for (int kind = 0; kind < TK_COUNT; ++kind)
    if (IsTileKindVariant(kind))
      for (int variant = 1; variant < 4; ++variant) counts[LegacyBaseKind(kind) + variant] = 0;
  for (int kind = TKL_HIGHWAY_FIRST; kind <= TKL_HIGHWAY_LAST; kind += 2) {
    counts[kind + 1] = 0;
    split |= counts[kind] != 0;
  }
  if (!split) return;
  for (TileIndex tile = 0; tile < city->map_cell_count; ++tile) {
    const TileInfo *tp = &city->tile[tile];
    if (ReadCityZoneKind(tp) == ZONE_MILITARY) continue;
    if (highway_bridges && IsTileKindHighwayBridge(tp->kind)) {
      if (LegacyCityBridgeKind(city, tile) == TKL_HIGHWAY_BRIDGE_LAST) {
        --counts[TKL_HIGHWAY_BRIDGE_FIRST];
        ++counts[TKL_HIGHWAY_BRIDGE_LAST];
      }
    } else if ((variants && IsTileKindVariant(tp->kind)) ||
        (transport && (IsTileKindPlainTransport(tp->kind) || IsTileKindPlainPowerLine(tp->kind))) ||
        (crossings && IsTileKindGroundCrossing(tp->kind)) ||
        (highway_shapes && IsTileKindMultiHighway(tp->kind))) {
      int legacy = LegacyCityTileKind(city, tile), statistic = CityTileStatisticAt(city, tile);
      if (legacy != statistic) {
        --counts[statistic];
        ++counts[legacy];
      }
    } else if (hydro && tp->kind == TK_HYDRO_POWER &&
        CityHydroSpriteKind(city, ReadTileZoneByte(tp)) == TKL_HYDRO_POWER_LAST) {
      --counts[TK_HYDRO_POWER];
      ++counts[TKL_HYDRO_POWER_LAST];
    } else if (IsTileKindStraightHighway(tp->kind) && ReadCityHighwayAxis(tp)) {
      int base = LegacyHighwayBase(tp->kind);
      --counts[base];
      ++counts[base + 1];
    }
  }
}

// Keep established simulation counters independent of native-only storage IDs. Kind must be a supported native
// statistics input; returns the compatibility counter key without changing any count.
int CityTileStatisticsKind(const City *city, int kind) {
  if (!city->native_map_storage) return kind;
  if (kind == TK_POWER_LINE_WATER || kind == TK_POWER_LINE_TOWER_WATER) return TKL_POWER_LINE_BRIDGE;
  if (IsTileKindPlainPowerLine(kind)) return TKL_POWER_LINE_EAST_WEST;
  if (kind == TK_RAIL) return TKL_RAIL_FIRST;
  if (IsTileKindStraightHighwayFamily(kind)) return LegacyHighwayBase(kind);
  if (!IsTileKindBridge(kind)) return LegacyBaseKind(kind);
  switch (CityBridgeHeadKind(kind)) {
  case TK_ROAD_BRIDGE_HEAD:
    return IsTileKindBridgeHead(kind) ? TKL_ROAD_BRIDGE_DECK : TKL_SUSPENSION_BRIDGE_FIRST;
  case TK_RAIL_BRIDGE_HEAD:
    return IsTileKindBridgeHead(kind) ? TKL_RAIL_BRIDGE_LAST : TKL_RAIL_BRIDGE_FIRST;
  default:
    return TKL_HIGHWAY_BRIDGE_FIRST;
  }
}

// Read the statistics kind associated with a valid native tile and its stored variant/progress. Returns the counter
// key used by simulation bookkeeping; no counter or tile is changed.
int CityTileStatisticAt(const City *city, TileIndex tile) {
  const TileInfo *tp = &city->tile[tile];
  if (city->native_map_storage && IsCityWaterPowerLine(tp)) return TKL_POWER_LINE_BRIDGE;
  int construction = CityBuildingConstructionKind(tp);
  return construction ? construction : CityTileStatisticsKind(city, tp->kind);
}
