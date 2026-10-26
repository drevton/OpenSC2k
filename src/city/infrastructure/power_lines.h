#ifndef SC2K_CITY_INFRASTRUCTURE_POWER_LINES_H
#define SC2K_CITY_INFRASTRUCTURE_POWER_LINES_H

#include "city/types.h"

typedef struct TileInfo TileInfo;

// Ordinary lines retain their mask; crossings and bridge underpasses derive their fixed wire axis.
// Reads only the supplied record and returns CONN_NONE for kinds without power connections.
int CityPowerConnectionMask(const TileInfo *tp);
// Read-only query for a valid tile: discover dry-line topology without changing stored connections or tower kinds.
// Water spans retain their construction axis; other kinds use CityPowerConnectionMask.
int FindCityPowerConnectionMask(const City *city, TileIndex tile);
// Valid tile; ordinary power lines update their stored mask and tower kind, then invalidate adjoining wire artwork.
// Neighbor scans use map coordinates; tower spacing uses the original geography's pattern coordinates.
void UpdatePowerLineConnections(City *city, TileIndex tile);
// Call after all edited fields are installed. Refresh also invalidates adjoining power artwork.
void RefreshCityPowerConnections(City *city, TileIndex tile);
// Imports call this once after the complete native map is available.
void InitializeCityPowerConnections(City *city);
// Raw single-cell placement; caller owns admission and charging.
void PlaceCityPowerLineCell(City *city, TileIndex tile);

// Remove the complete matching water span, retaining water and its banks. Requires a valid origin.
bool RemoveCityWaterPowerSpan(City *city, TileIndex origin, bool animate);

// Select source artwork from a four-bit camera-relative connection mask and land shape, without zoom/cache selection.
int CityMapPowerLayout(int mask, int shape);

#endif
