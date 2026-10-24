#ifndef SC2K_DRAWING_MAPS_MAP_TERRAIN_H
#define SC2K_DRAWING_MAPS_MAP_TERRAIN_H

#include "city/types.h"

typedef struct Drawing Drawing;
typedef struct CityTileGeometry CityTileGeometry;

// The bridge family submits its prepared near-side artwork through Drawing's current depth pass.
// palette_offset selects a bridge palette independently of the saved BridgeStyle value.
void DrawCityBridgeTrafficForeground(City *city, TileIndex tile, int sprite, int x, int baseline, int palette_offset);

// Terrain services draw in the current ground pass and preserve the active clip.
bool IsCityMapGroundPass(const Drawing *drawing);
// One valid surface tile at its unelevated logical origin in Drawing's active camera.
// The shared traversal owns visibility and painter order; this composes terrain, the occupant and overlays.
void DrawCityMapTile(City *city, int zoom, TileIndex tile, int screen_x, int screen_y);
// Data overlay for a valid tile at the same unelevated logical origin. Mode comes from the active map target;
// no cursor or derived zoom values are retained between tiles.
void DrawCityMapDataTile(City *city, int zoom, TileIndex tile, int screen_x, int screen_y);
// Buried ground, pipes, subways and road tunnels for a valid tile at its unelevated logical origin.
// DrawCityMapUnderground prepares the palette and traversal; this function preserves the active clip.
void DrawCityMapUndergroundTile(City *city, int zoom, TileIndex tile, int screen_x, int screen_y);
int CityMapSlopeFaceSides(const City *city, TileIndex tile, int shape, int height);
void DrawCityMapTerrainFace(City *city, TileIndex tile, int zoom, int sprite, int screen_x, int ground_y, int sides);
void DrawCityMapTerrainSkirt(City *city, TileIndex tile, int zoom, int screen_x, int ground_y, int base);
// Terrain only, without occupants or underpasses. A negative sprite selects natural terrain;
// overlays may supply a colored surface. Geometry must match the tile.
void DrawCityMapTerrainTile(City *city, TileIndex tile, int zoom, int sprite, int screen_x, int ground_y,
    const CityTileGeometry *geometry);

#endif
