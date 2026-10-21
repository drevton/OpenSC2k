// TileInfo physical geometry (zero-based bits):
// altitude[0..4] is minimum land/bed height; zone[4] selects slope shape 1..13 in zone[0..3], otherwise land is flat
// and the low nibble is zoning. These are the only stored land-shape bits; terrain[0..3] is ground, not slope.
// A water-class kind gives altitude[5..9] its water-plane meaning. Dry buildings use that field for construction,
// bridge heads for style/phase; neither is a water height. Dry surface height always comes from altitude[0..4].
// Exposed kind or wet-occupant terrain[0..3] chooses ordinary water/river/bank; water height, bed height and slope
// then derive level water, shoreline or rapids. flags[0] selects salinity, not water presence.
// Terrain edits preserve altitude[10..14] tunnel depth, altitude[15], occupant metadata and the other shared fields
// according to tile_storage.h; callers explicitly invalidate geometry before and after a change.

#include "city/maps/water.h"
#include "city/maps/ground.h"
#include "city/maps/tile_storage.h"
#include "city/maps/layers.h"
#include "city/maps/map.h"
#include "city/maps/tables.h"
#include "city/presentation.h"

static const u8 kRaisedCorners[16] = {0, 9, 3, 6, 12, 11, 7, 14, 13, 1, 2, 4, 8, 15, 15, 0};

// A shoreline's low edge or corner becomes ordinary slope when newly created
// dry land meets it at water level. Its raised, landward edge remains a coast.
void ReclaimCityShorelineBanks(City *city, TileIndex land) {
  const TileInfo *ground = &city->tile[land];
  if (HasTileWater(ground)) return;
  CityTileGeometry geometry = ReadCityTileGeometry(city, land);
  int raised = kRaisedCorners[geometry.land_shape];
  static const i8 kCornerX[4] = {0, 0, 1, 1}, kCornerY[4] = {0, 1, 1, 0};
  bool changed = false;
  for (int direction = 0; direction < 8; ++direction) {
    int dx = kTerrainNeighborXOffsets[direction], dy = kTerrainNeighborYOffsets[direction];
    TileIndex neighbor = GetRelativeTileOrInvalid(city, land, dx, dy);
    if (neighbor == INVALID_TILE || !IsTileKindShoreBank(city->tile[neighbor].kind)) continue;
    CityTileGeometry bank = ReadCityTileGeometry(city, neighbor);
    if (bank.land_height != bank.water_height || !bank.land_shape) continue;
    int bank_raised = kRaisedCorners[bank.land_shape];
    bool touches = false;
    for (int corner = 0; corner < 4; ++corner) {
      if (geometry.land_height + ((raised >> corner) & 1) > bank.water_height) continue;
      for (int other = 0; other < 4; ++other)
        if (kCornerX[corner] == dx + kCornerX[other] && kCornerY[corner] == dy + kCornerY[other] &&
            !(bank_raised & (1 << other))) touches = true;
    }
    if (!touches) continue;
    InvalidateTile(city, neighbor);
    int kind = city->tile[neighbor].kind;
    ReplaceCityTileAndStatistics(city, neighbor,
        IsTileKindShoreTree(kind) ? CityTreeTile(CityTreeCount(kind)) : TK_CLEAR);
    TileInfo *tp = &city->tile[neighbor];
    tp->altitude &= CITY_ALTITUDE_CLEAR_WATER;
    tp->flags &= ~TF_SALTWATER;
    RefreshCityTileClimates(city, neighbor);
    BeginCityBareGroundRecovery(city, tp);
    changed = true;
  }
  if (changed) {
    CityPoint point = MapTilePt(city, land);
    int left = IntMax(0, point.x - 2), top = IntMax(0, point.y - 2);
    InvalidateTileRange(city, MapTileXY(city, left, top),
        IntMin(city->map_width, point.x + 3) - left, IntMin(city->map_height, point.y + 3) - top);
  }
}

// Find the lowest shared edge height between adjacent tile records. Direction runs north, east, south, west;
// both tiles must be valid neighbors. Taking both records prevents water crossing an exposed raised edge.
int CityTerrainEdgeHeight(const TileInfo *tp, const TileInfo *neighbor, int direction) {
  assert(direction >= 0 && direction < 4);
  static const u8 kEdgeCorners[4][2] = {{0, 3}, {3, 2}, {2, 1}, {1, 0}};
  int corners = kRaisedCorners[ReadTerrainCode(tp)];
  int other = kRaisedCorners[ReadTerrainCode(neighbor)];
  int height = 32;
  for (int end = 0; end < 2; ++end) {
    int first = ReadCityLandHeight(tp) + ((corners >> kEdgeCorners[direction][end]) & 1);
    int second = ReadCityLandHeight(neighbor) + ((other >> kEdgeCorners[(direction + 2) & 3][1 - end]) & 1);
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
  int shape = geometry.land_shape, corners = kRaisedCorners[shape];
  // A shared edge at a flat river mouth belongs to the water plane, even
  // when diagonal land neighbors would otherwise raise its corners.
  for (int direction = 0; direction < 8; direction += 2) {
    TileIndex next =
        GetRelativeTileOrInvalid(city, tile, kTerrainNeighborXOffsets[direction], kTerrainNeighborYOffsets[direction]);
    if (next == INVALID_TILE) continue;
    const TileInfo *tp = &city->tile[next];
    int terrain = ReadTerrainCode(tp);
    bool level = !terrain ||
        ReadCityLandHeight(tp) != ReadCityWaterHeight(tp);
    if (ReadCityWaterGroundKind(tp) == TK_RIVER && level &&
        ReadCityWaterHeight(tp) == geometry.water_height)
      corners &= ~kTerrainHigherNeighborBits[direction];
  }
  return corners == kRaisedCorners[shape] ? shape : kTerrainShapeByHigherNeighbors[corners];
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

// Test whether a valid tile belongs to saltwater coastline or touches one. Reads only the bounded neighborhood and
// does not change water flags or elevation.
static bool UsesRaisedCoast(const City *city, TileIndex tile) {
  if (ReadTileWaterKind(&city->tile[tile]) == CITY_WATER_SALT) return true;
  for (int direction = 0; direction < 8; ++direction) {
    TileIndex neighbor = GetRelativeTileOrInvalid(city, tile,
        kTerrainNeighborXOffsets[direction], kTerrainNeighborYOffsets[direction]);
    if (neighbor != INVALID_TILE && ReadTileWaterKind(&city->tile[neighbor]) == CITY_WATER_SALT)
      return true;
  }
  return false;
}

// Update water level and bed shape, preserving river behavior independently of salinity.
// A raised river bed becomes dry unless keep_rapids retains an existing sloped river surface.
// The caller invalidates old/new extents; kind changes also update tile statistics.
void SetCityTerrainWater(City *city, TileIndex tile, int water_height, int land_shape, bool keep_rapids) {
  assert(IsMapTileValid(city, tile));
  TileInfo *tp = &city->tile[tile];
  int altitude = tp->altitude, land = ReadCityLandHeight(tp);
  if (land_shape == CITY_TERRAIN_RAISE_LEVEL) land_shape = CITY_TERRAIN_FLAT;
  CityWaterKind water = ReadTileWaterKind(tp);
  int ground = ReadCityWaterGroundKind(tp);
  bool river = ground == TK_RIVER;
  bool coast = water != CITY_WATER_NONE ? water == CITY_WATER_SALT : UsesRaisedCoast(city, tile);
  bool was_wet = water != CITY_WATER_NONE;
  int bank_ground = IsTileKindShoreBank(tp->kind) ? ReadGroundKind(tp) : CITY_GROUND_GRASS;
  bool changed_bank = tp->kind != TK_WATER_BANK || ReadTerrainCode(tp) != land_shape ||
      ReadCityWaterHeight(tp) != water_height;
  bool was_rapids = keep_rapids && river && ReadTerrainCode(tp) && land == ReadCityWaterHeight(tp);
  bool wet = land < water_height || (land == water_height && (!land_shape || !river || was_rapids));
  if (!wet) {
    tp->flags &= ~TF_SALTWATER;
    WriteTerrainCode(tp, land_shape);
    ground = TK_CLEAR;
  } else {
    ground = river ? TK_RIVER : land == water_height && land_shape ? TK_WATER_BANK : TK_WATER;
    tp->flags = (tp->flags & ~TF_SALTWATER) | (coast ? TF_SALTWATER : 0);
  }
  int kind = CityTileKindForWater(tp->kind, wet);
  if (IsTileKindShoreTree(kind) && ground != TK_WATER_BANK) kind = ground;
  if (IsTileKindExposedWater(kind)) kind = ground;
  if (kind != tp->kind) ReplaceCityTileAndStatistics(city, tile, kind);
  if (wet) {
    // Replacing a dry building clears its old construction-stage bits, which
    // share this field. Install the water plane after that category change.
    tp->altitude = (altitude & CITY_ALTITUDE_CLEAR_WATER) | water_height << CITY_ALTITUDE_WATER_SHIFT;
    WriteTerrainCode(tp, land_shape);
    tp->terrain = (tp->terrain & 240) | (IsTileKindShoreBank(kind) ? bank_ground :
        IsTileKindExposedWater(kind) ? 0 : ground - TK_WATER);
    if (kind == TK_WATER_BANK && changed_bank) BeginCityBareGroundRecovery(city, tp);
  } else if (was_wet && (kind == TK_CLEAR || IsTileKindTree(kind))) {
    tp->altitude &= CITY_ALTITUDE_CLEAR_WATER;
    if (kind == TK_CLEAR) BeginCityBareGroundRecovery(city, tp);
  }
}
