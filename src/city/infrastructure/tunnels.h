#ifndef SC2K_CITY_INFRASTRUCTURE_TUNNELS_H
#define SC2K_CITY_INFRASTRUCTURE_TUNNELS_H

#include "city/infrastructure/types.h"

typedef struct CityTileGeometry CityTileGeometry;

enum { CITY_ROAD_TUNNEL_COST = 150 };

// Read-only entrance/exit search; invalid or unsuitable origins return a span with end == INVALID_TILE.
// Failure may carry a notice code in error; zero denotes a generic blocker. Success length includes both portals,
// and direction uses CityInfrastructureDirection. Does not change terrain, treasury, RNG or construction state.
CityInfrastructureSpan FindCityRoadTunnel(const City *city, TileIndex origin);
// Retained callers keep the admitted span and opening geometry across confirmation. Do not retry or re-quote after
// dismissal: execution charges the live construction cost, including changes made by nested actions.
bool PlaceCityRoadTerrainSpan(City *city, TileIndex tile);
// Remove a complete admitted tunnel without charging. Returns the far entrance for the caller's final cleanup area.
// Entrance mutation, rubble RNG, effects and power cleanup retain their traversal order.
TileIndex RemoveCityRoadTunnel(City *city, TileIndex tile, bool animate);
// Camera-relative bore axis (0 X, 1 Y) for a valid tunnel tile at the supplied floor height in map levels.
// Requires bound Drawing; reads local connections and, on ties, connected straight spans to their portals.
// No map, camera, RNG or allocation changes.
int CityRoadTunnelViewAxis(const City *city, TileIndex tile, int floor);
// Shared map traversal handles clipping and hidden layers first; sprite is the resolved Drawing cache slot with zoom.
void DrawCityRoadTunnelEntrance(const City *city, TileIndex tile, int sprite, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry);

// TEST quotes without mutation. Execution uses the resolved confirmation, charges once and returns success or an error.
i64 CmdPlaceRoadTerrainSpan(City *city, u32 flags, TileIndex tile, int options);

#endif
