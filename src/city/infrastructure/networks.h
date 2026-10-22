#ifndef SC2K_CITY_INFRASTRUCTURE_NETWORKS_H
#define SC2K_CITY_INFRASTRUCTURE_NETWORKS_H

#include "city/types.h"

// Requires a valid tile. Invalidates the old artwork, updates subway statistics outside military zones, then writes.
// The caller reconnects neighbors and invalidates the final artwork after all related fields are installed.
void SetUndergroundTile(City *city, TileIndex tile, int kind);
void PlaceCityWaterPipeCell(City *city, TileIndex origin_tile);
void PlaceCitySubwayCell(City *city, TileIndex origin_tile);

void RefreshAdjacentSurfaceConnections(City *city, TileIndex tile);
void RefreshAdjacentUndergroundConnections(City *city, TileIndex tile);
void PrepareInfrastructureTerrainCell(City *city, TileIndex tile_index);

#endif
