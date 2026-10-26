// TileInfo physical geometry (zero-based bits):
// altitude[0..4] is minimum land/bed height; zone[4] selects slope shape 1..13 in zone[0..3], otherwise land is flat
// and the low nibble is zoning. These are the only stored land-shape bits; terrain[0..3] is ground, not slope.
// A water-class kind gives altitude[5..9] its water-plane meaning. Dry buildings use that field for construction,
// bridge heads for style/phase; neither is a water height. Dry surface height always comes from altitude[0..4].
// Exposed kind or wet-occupant terrain[0..3] chooses ordinary water/river/bank; water height, bed height and slope
// then derive level water, shoreline or rapids. flags[0] selects salinity, not water presence.
// Terrain edits preserve altitude[10..14] tunnel depth, altitude[15], occupant metadata and the other shared fields
// according to tile_storage.h; callers explicitly invalidate geometry before and after a change.

#include "city/maps/terrain.h"
#include "city/infrastructure/lines.h"
#include "city/infrastructure/networks.h"
#include "city/saveload/map_format.h"
#include "city/maps/map.h"
#include "city/infrastructure/power_lines.h"
#include "city/maps/water.h"
#include "city/maps/ground.h"
#include "city/maps/tile_storage.h"
#include "city/maps/layers.h"
#include "city/maps/tables.h"
#include "city/presentation.h"

// Derive a shared vertex's height from its valid neighboring native tile corners. Vertex coordinates may lie on the
// outer map edge; absent neighbor cells are skipped and the highest contributing corner wins.
int ReadCityTerrainVertexHeight(const City *city, int x, int y) {
  int height = 0;
  for (int corner = 0; corner < 4; ++corner) {
    int tx = x - kTerrainCornerX[corner], ty = y - kTerrainCornerY[corner];
    TileIndex tile = MapTileXYOrInvalid(city, tx, ty);
    if (tile == INVALID_TILE) continue;
    const TileInfo *tp = &city->tile[tile];
    int shape = ReadTerrainCode(tp), level = ReadCityLandHeight(tp);
    height = IntMax(height, level + ((kTerrainRaisedCorners[shape] >> corner) & 1));
  }
  return height;
}

// Find the lowest shared edge height between adjacent tile records. Direction runs north, east, south, west;
// both tiles must be valid neighbors. Taking both records prevents water crossing an exposed raised edge.
int CityTerrainEdgeHeight(const TileInfo *tp, const TileInfo *neighbor, int direction) {
  assert(direction >= 0 && direction < 4);
  static const u8 kEdgeCorners[4][2] = {{0, 3}, {3, 2}, {2, 1}, {1, 0}};
  int corners = kTerrainRaisedCorners[ReadTerrainCode(tp)];
  int other = kTerrainRaisedCorners[ReadTerrainCode(neighbor)];
  int land = ReadCityLandHeight(tp), neighbor_land = ReadCityLandHeight(neighbor);
  int height = 32;
  for (int end = 0; end < 2; ++end) {
    int first = land + ((corners >> kEdgeCorners[direction][end]) & 1);
    int second = neighbor_land + ((other >> kEdgeCorners[(direction + 2) & 3][1 - end]) & 1);
    height = IntMin(height, IntMax(first, second));
  }
  return height;
}

// Derive a land slope from the valid tile's elevation and its neighbors. Reads native elevation only; callers use
// ReadCityTileGeometry for complete land/water geometry.
int CityTerrainSlopeAt(const City *city, TileIndex tile) {
  int elevation = ReadCityLandHeight(&city->tile[tile]), higher = 0;
  for (int direction = 0; direction < 8; ++direction) {
    TileIndex neighbor = GetRelativeTileOrInvalid(city, tile,
        kTerrainNeighborXOffsets[direction], kTerrainNeighborYOffsets[direction]);
    if (neighbor == INVALID_TILE) continue;
    int height = ReadCityLandHeight(&city->tile[neighbor]);
    if (height > elevation) higher |= kTerrainHigherNeighborBits[direction];
  }
  return kTerrainShapeByHigherNeighbors[higher];
}

// Derive the visible shoreline slope from a valid tile and its already decoded geometry. Geometry must describe that
// same tile; neighboring water levels can override the bank's apparent edge.
static int ShoreSlope(const City *city, TileIndex tile, CityTileGeometry geometry) {
  int shape = geometry.land_shape, corners = kTerrainRaisedCorners[shape];
  // A shared edge at a flat river mouth belongs to the water plane, even
  // when diagonal land neighbors would otherwise raise its corners.
  for (int direction = 0; direction < 8; direction += 2) {
    TileIndex next =
        GetRelativeTileOrInvalid(city, tile, kTerrainNeighborXOffsets[direction], kTerrainNeighborYOffsets[direction]);
    if (next == INVALID_TILE) continue;
    const TileInfo *tp = &city->tile[next];
    if (ReadCityWaterGroundKind(tp) != TK_RIVER) continue;
    int terrain = ReadTerrainCode(tp), water_height = ReadCityWaterHeight(tp);
    bool level = !terrain || ReadCityLandHeight(tp) != water_height;
    if (level && water_height == geometry.water_height)
      corners &= ~kTerrainHigherNeighborBits[direction];
  }
  return corners == kTerrainRaisedCorners[shape] ? shape : kTerrainShapeByHigherNeighbors[corners];
}

// Read stored bed geometry and resolve the visible surface from its water kind.
// Straight hydro spillways retain the river slope; compound river slopes remain rapids for placement.
CityTileGeometry ReadCityTileGeometry(const City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile));
  const TileInfo *tp = &city->tile[tile];
  CityTileGeometry geometry = {.land_height = ReadCityLandHeight(tp),
      .land_shape = ReadTerrainCode(tp), .surface_height = ReadCitySurfaceHeight(tp), .water = ReadTileWaterKind(tp)};
  geometry.surface_shape = geometry.land_shape;
  if (geometry.water == CITY_WATER_NONE) return geometry;
  geometry.water_height = geometry.surface_height;
  geometry.surface_shape = CITY_TERRAIN_FLAT;
  geometry.water_surface = CITY_WATER_SURFACE_LEVEL;
  if (tp->kind == TK_HYDRO_POWER) {
    geometry.water_surface = CITY_WATER_SURFACE_RAPIDS;
    if (geometry.land_shape <= CITY_TERRAIN_SHAPE_4) geometry.surface_shape = geometry.land_shape;
    return geometry;
  }
  // Submerged beds keep a level surface, regardless of their slope.
  if (geometry.land_height != geometry.water_height || !geometry.land_shape) return geometry;
  switch (ReadCityWaterGroundKind(tp)) {
  case TK_RIVER:
    geometry.water_surface = CITY_WATER_SURFACE_RAPIDS;
    if (geometry.land_shape <= CITY_TERRAIN_SHAPE_4) geometry.surface_shape = geometry.land_shape;
    break;
  case TK_WATER_BANK:
    geometry.surface_shape = ShoreSlope(city, tile, geometry);
    if (geometry.surface_shape) geometry.water_surface = CITY_WATER_SURFACE_SHORE;
    break;
  }
  return geometry;
}

static void FinishCityTerrainRecalculation(City *city, TileIndex tile_index, int prior_kind);

// Recalculate terrain shape from the eight neighboring elevations. Requires a valid admitted tile; removes
// incompatible construction and updates terrain through its native geometry path.
void RecalculateTerrainCell(City *city, TileIndex tile) {
  if (!IsMapTileValid(city, tile)) return;
  InvalidateTile(city, tile);
  int kind = city->tile[tile].kind;
  if (!IsTileKindUnbuilt(kind)) BulldozeCityTile(city, tile, true);
  FinishCityTerrainRecalculation(city, tile, kind);
}

// Restore terrain elevation gradients around a changed cell. Invalid input is ignored; changes only admitted nearby
// cells and explicitly damages their old/new geometry.
void RebalanceAdjacentTerrainElevations(City *city, TileIndex tile) {
  if (!IsMapTileValid(city, tile)) return;
  int level = ReadCityLandHeight(&city->tile[tile]);
  for (int neighbor = 0; neighbor < 4; ++neighbor) {
    TileIndex nearby = GetRelativeTileOrInvalid(city, tile, kLineXStep[neighbor], kLineYStep[neighbor]);
    if (nearby == INVALID_TILE) continue;
    TileInfo *tp = &city->tile[nearby];
    if (!IsTileKindUnbuilt(tp->kind)) continue;
    int altitude = tp->altitude;
    int difference = level - ReadCityLandHeight(tp);
    if (difference >= -1 && difference <= 1) continue;
    InvalidateTile(city, nearby);
    tp->altitude = (difference > 1 ? level - 1 : level + 1) | (altitude & CITY_ALTITUDE_CLEAR_LAND);
    InvalidateTile(city, nearby);
    RebalanceAdjacentTerrainElevations(city, nearby);
    RefreshTerrainNeighborhood(city, nearby);
  }
}

// Generation and original disaster algorithms write tile minima directly. Only those callers reconstruct corners;
// ordinary demolition preserves point edits. Tile must be valid or INVALID_TILE; reconstructs only admitted
// native-generation/disaster corners and does not replace ordinary point-edit geometry.
static void PrepareCityTerrainRebuild(City *city, TileIndex tile) {
  if (!(city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) || tile == INVALID_TILE) return;
  InvalidateTile(city, tile);
  int shape = CityTerrainSlopeAt(city, tile);
  TileInfo *tp = &city->tile[tile];
  if (shape == CITY_TERRAIN_RAISE_LEVEL) {
    int altitude = tp->altitude;
    tp->altitude = (altitude & CITY_ALTITUDE_CLEAR_LAND) |
        ((altitude & CITY_ALTITUDE_LAND_MASK) + 1);
    shape = CITY_TERRAIN_FLAT;
  }
  WriteTerrainCode(tp, shape);
}

// Refresh terrain state and drawing bounds for the 3-by-3 neighborhood. Invalid input is ignored; reconstructs each
// valid neighbor only for callers that own that terrain-rebuild semantics.
void RefreshTerrainNeighborhood(City *city, TileIndex tile) {
  if (!IsMapTileValid(city, tile)) return;
  for (int neighbor = 0; neighbor < 9; ++neighbor) {
    TileIndex nearby = GetRelativeTileOrInvalid(city, tile,
        kTerrainNeighborXOffsets[neighbor], kTerrainNeighborYOffsets[neighbor]);
    if (nearby == INVALID_TILE) continue;
    InvalidateTile(city, nearby);
    PrepareCityTerrainRebuild(city, nearby);
    RecalculateTerrainCell(city, nearby);
    InvalidateTile(city, nearby);
  }
}

// Complete a valid cell's terrain category change, removing incompatible surface/underground construction and
// normalizing water. prior_kind is captured before demolition; callers preserve statistics and invalidate
// old/new extents around the recalculation.
static void FinishCityTerrainRecalculation(City *city, TileIndex tile_index, int prior_kind) {
  if (city->settings.timer_mode != CITY_TIMER_STOPPED && prior_kind != TK_RADIOACTIVE_WASTE)
    ReplaceCityTileAndStatistics(city, tile_index,
        ReadCityWaterGroundKind(&city->tile[tile_index]));
  if (ReadTileUndergroundKind(&city->tile[tile_index]) != TUK_CLEAR)
    SetUndergroundTile(city, tile_index, TUK_CLEAR);
  TileInfo *tp = &city->tile[tile_index];
  int elevation = ReadCityLandHeight(tp);
  bool wet = HasTileWater(tp);
  int water_height = wet ? ReadCityWaterHeight(tp) : 0;
  int shape = city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE) ?
      ReadTerrainCode(tp) : CityTerrainSlopeAt(city, tile_index);
  if (shape != 0)
    WriteZoneByte(city, tile_index, ReadZoneByte(city, tile_index) & CITY_ZONE_METADATA_MASK);
  if (shape == CITY_TERRAIN_RAISE_LEVEL) {
    elevation += 1;
    tp->altitude = (tp->altitude & CITY_ALTITUDE_CLEAR_LAND) | elevation;
  }
  if ((city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) || ReadCityWaterGroundKind(tp) == TK_RIVER) {
    // Recalculation removes structures without changing the water plane. Actual terrain edits apply sea level
    // when they change the vertices; doing that here would flood dry ground after highway demolition.
    if (wet) SetCityTerrainWater(city, tile_index, water_height, shape, true);
    else WriteTerrainCode(tp, shape);
    tp = &city->tile[tile_index];
    if (HasTileWater(tp))
      ReplaceCityTileAndStatistics(city, tile_index, ReadCityWaterGroundKind(tp));
  } else if (city->geography.edit_height > elevation) {
    tp->kind = TK_WATER;
    tp->altitude = (tp->altitude & CITY_ALTITUDE_CLEAR_WATER) |
        (CITY_ALTITUDE_WATER_STEP * city->geography.edit_height);
    ReplaceCityTileAndStatistics(city, tile_index, ReadCityWaterGroundKind(tp));
    WriteLegacyCityTerrainCode(city, tile_index, shape == CITY_TERRAIN_RAISE_LEVEL
            ? CITY_TERRAIN_WATER_FIRST
            : (u8)(shape +
                  (city->geography.edit_height - elevation == 1 ? CITY_TERRAIN_BANK_32 : CITY_TERRAIN_WATER_FIRST)));
  } else {
    WriteLegacyCityTerrainCode(city, tile_index, shape == CITY_TERRAIN_RAISE_LEVEL ? CITY_TERRAIN_FLAT : shape);
  }
  RefreshCityPowerConnections(city, tile_index);
  RefreshCityTileClimates(city, tile_index);
  ReclaimCityShorelineBanks(city, tile_index);
}
