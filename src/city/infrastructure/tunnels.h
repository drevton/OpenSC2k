#ifndef SC2K_CITY_INFRASTRUCTURE_TUNNELS_H
#define SC2K_CITY_INFRASTRUCTURE_TUNNELS_H

#include "city/infrastructure/types.h"

typedef struct CityTileGeometry CityTileGeometry;

enum { CITY_ROAD_TUNNEL_COST = 150 };

// Read-only entrance/exit search. An unsupported tunnel returns INVALID_TILE with the existing notice code.
CityInfrastructureSpan FindCityRoadTunnel(const City *city, TileIndex origin);
// Retained comparison callers keep their confirmation continuation; execution uses the live construction cost.
bool PlaceCityRoadTerrainSpan(City *city, TileIndex tile);
// Remove a complete admitted tunnel without charging. Returns the far entrance for the caller's final cleanup area.
// Entrance mutation, rubble RNG, effects and power cleanup retain their traversal order.
TileIndex RemoveCityRoadTunnel(City *city, TileIndex tile, bool animate);
// Shared map traversal handles clipping and hidden layers first; sprite is the resolved Drawing cache slot with zoom.
void DrawCityRoadTunnelEntrance(City *city, TileIndex tile, int sprite, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry);

// TEST quotes without mutation. Execution uses the resolved confirmation, charges once and returns success or an error.
i64 CmdPlaceRoadTerrainSpan(City *city, u32 flags, TileIndex tile, int options);

#endif
