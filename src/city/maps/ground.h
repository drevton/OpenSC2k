#ifndef SC2K_CITY_MAPS_GROUND_H
#define SC2K_CITY_MAPS_GROUND_H

#include "city/maps/map.h"

// Generation follows tree/climate initialization and borrows its landscape RNG and prior full-map invalidation.
void GenerateCityGround(City *city);
// Import settles paving without RNG. Scheduled visits preserve traversal order; recovery precedes tree growth.
void InitializeCityTransportPaving(City *city);
void GrowCityTransportGround(City *city, TileIndex tile);
void GrowCityGround(City *city, TileIndex tile, u32 cycle, bool tree);

// Sprite-cache indexes include the requested zoom. Geometry and shape are camera-relative; queries change no state.
int CityGroundSpriteForShape(const City *city, TileIndex tile, int zoom, int shape);
// Paint native clear/tree ground and zoning in the ground pass without selection. Returns false for other kinds
// and stock artwork; callers draw any tree silhouettes and placement marker in their existing object phase.
bool DrawCityGroundSurface(City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry);
// Handles native clear/tree ground; returns false for other kinds and stock artwork.
bool DrawCityGroundTile(City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry);
// Fallback for source layouts below the road artwork range (clear, water, trees, remains and the small park).
// Caller owns layer visibility and clipping. Geometry/layout are camera-relative; this reads City without mutation.
// Terrain, placement/park selection, optional details and the power warning retain their drawing order.
void DrawCityGroundTileFallback(City *city, TileIndex tile, int layout, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry);

#endif
