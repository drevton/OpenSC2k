#ifndef SC2K_CITY_MAPS_TERRAIN_H
#define SC2K_CITY_MAPS_TERRAIN_H

#include "city/types.h"
#include "city/maps/kinds.h"

// Land shape comes from zone's slope encoding, including submerged beds. Kind determines water presence;
// flags retain salinity. Wet occupants retain their underlying water kind in terrain's low nibble.
// Heights are map levels. A dry tile has no water height, even if altitude contains stale water bits
// or a building stage. Callers must supply a valid tile.

// Water presence comes from TileKind; dry tiles may retain TF_SALTWATER.
typedef enum CityWaterKind {
  CITY_WATER_NONE = 0,
  CITY_WATER_FRESH = 1,
  CITY_WATER_SALT = 2
} CityWaterKind;

typedef enum CityWaterSurface {
  CITY_WATER_SURFACE_NONE,
  CITY_WATER_SURFACE_LEVEL,
  CITY_WATER_SURFACE_SHORE,
  CITY_WATER_SURFACE_RAPIDS,
  CITY_WATER_SURFACE_UNSUPPORTED
} CityWaterSurface;

// Record queries read only the supplied tile and immutable kind classes; they do not require City state.
// Permanent water is a property of the stored kind, including occupied water.
static SC2K_FORCE_INLINE bool HasTileWater(const TileInfo *tp) {
  return (ReadCityTileClasses(tp->kind) & TCLASS_WATER) != 0;
}

static SC2K_FORCE_INLINE CityWaterKind ReadTileWaterKind(const TileInfo *tp) {
  return !HasTileWater(tp) ? CITY_WATER_NONE : tp->flags & TF_SALTWATER ? CITY_WATER_SALT : CITY_WATER_FRESH;
}

// Land shape is independent of the occupant; water presence comes from its kind.
static SC2K_FORCE_INLINE bool IsFlatDryTile(const TileInfo *tp) {
  return ReadTerrainCode(tp) == CITY_TERRAIN_FLAT && !HasTileWater(tp);
}

// Unbuilt includes rubble, radioactive waste and trees. Callers add any
// tool-specific restrictions on those kinds, zoning or underground contents.
static SC2K_FORCE_INLINE bool IsFlatDryUnbuiltTile(const TileInfo *tp) {
  return IsTileKindUnbuilt(tp->kind) && IsFlatDryTile(tp);
}

// Read the bed's minimum height, excluding water level and construction progress.
static SC2K_FORCE_INLINE int ReadCityLandHeight(const TileInfo *tp) {
  return tp->altitude & CITY_ALTITUDE_LAND_MASK;
}

// Use this for elevation calculations unless the caller already knows that the
// tile contains real water. A construction stage must never become a height.
static SC2K_FORCE_INLINE int ReadCityWaterHeight(const TileInfo *tp) {
  int altitude = tp->altitude;
  return IsTileKindDryBuilding(tp->kind) || IsTileKindBridgeHead(tp->kind) ? altitude & CITY_ALTITUDE_LAND_MASK :
      (altitude & CITY_ALTITUDE_WATER_MASK) >> CITY_ALTITUDE_WATER_SHIFT;
}

// Read the water plane on wet tiles and the land height on dry tiles.
// Building stages and stale water bits never contribute to the surface height.
static SC2K_FORCE_INLINE int ReadCitySurfaceHeight(const TileInfo *tp) {
  return HasTileWater(tp) ? ReadCityWaterHeight(tp) : ReadCityLandHeight(tp);
}

// Decoded world geometry. Rendering rotates its shapes into the camera before invoking family draw callbacks.
typedef struct CityTileGeometry {
  int land_height, land_shape;
  int water_height;
  int surface_height, surface_shape;
  CityWaterKind water;
  CityWaterSurface water_surface;
} CityTileGeometry;

// Resolve surface shapes and water classification. For individual stored fields, use the record queries above;
// ReadCitySurfaceHeight reads just the surface elevation without decoding shoreline geometry.
CityTileGeometry ReadCityTileGeometry(const City *city, TileIndex tile);
int CityTerrainSlopeAt(const City *city, TileIndex tile);
// Sample a shared vertex from its valid neighboring tile corners, taking the highest contributing height.
// Coordinates may lie on the outer map edge; a point with no neighboring tile returns zero. This is read-only.
int ReadCityTerrainVertexHeight(const City *city, int x, int y);
// Lowest height shared by the two adjacent records along a north/east/south/west edge.
int CityTerrainEdgeHeight(const TileInfo *tp, const TileInfo *neighbor, int direction);

// Recalculate after removing/editing an occupant. Removes incompatible construction while preserving native
// point-edit slopes and existing water levels; it does not charge or apply the terrain editor's sea-level policy.
// Invalid tiles are ignored. The caller also invalidates any old geometry changed before this call.
void RecalculateTerrainCell(City *city, TileIndex tile);
// Restore admitted neighboring elevation gradients and refresh their terrain. Invalid input is ignored;
// recursive edits publish their own old/new damage and do not charge.
void RebalanceAdjacentTerrainElevations(City *city, TileIndex tile);
// Reconstruct and recalculate each valid tile in the three-by-three neighborhood, with old/new damage.
// Requires a generation/disaster context that permits rebuilding corners; invalid input is ignored.
void RefreshTerrainNeighborhood(City *city, TileIndex tile);

#endif
