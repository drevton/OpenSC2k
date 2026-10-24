#ifndef SC2K_CITY_MAPS_TERRAIN_H
#define SC2K_CITY_MAPS_TERRAIN_H

#include "city/types.h"

typedef struct TileInfo TileInfo;

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

// Decoded world geometry. Rendering rotates its shapes into the camera before invoking family draw callbacks.
typedef struct CityTileGeometry {
  int land_height, land_shape;
  int water_height;
  int surface_height, surface_shape;
  CityWaterKind water;
  CityWaterSurface water_surface;
} CityTileGeometry;

// Resolve surface shapes and water classification. For individual stored fields, use the layer accessors;
// ReadCitySurfaceHeight reads just the surface elevation without decoding shoreline geometry.
CityTileGeometry ReadCityTileGeometry(const City *city, TileIndex tile);
int CityTerrainSlopeAt(const City *city, TileIndex tile);
// Sample a shared vertex from its valid neighboring tile corners, taking the highest contributing height.
// Coordinates may lie on the outer map edge; a point with no neighboring tile returns zero. This is read-only.
int ReadCityTerrainVertexHeight(const City *city, int x, int y);
// Lowest height shared by the two adjacent records along a north/east/south/west edge.
int CityTerrainEdgeHeight(const TileInfo *tp, const TileInfo *neighbor, int direction);
// Native terrain edits preserve the supplied water level and existing salinity.
// Dry land newly covered by water borrows salinity from neighboring water.
// keep_rapids preserves existing sloped river water; landscaping passes false
// so a raised river bed at the water plane becomes dry instead.
// Updates clear/water/river/bank kinds while retaining river behavior independently of salinity.
// Their statistics counter is shared; the caller owns old/new invalidation.
void SetCityTerrainWater(City *city, TileIndex tile, int water_height, int land_shape, bool keep_rapids);

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
