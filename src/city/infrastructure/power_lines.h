#ifndef SC2K_CITY_INFRASTRUCTURE_POWER_LINES_H
#define SC2K_CITY_INFRASTRUCTURE_POWER_LINES_H

#include "city/types.h"

typedef struct TileInfo TileInfo;

// Ordinary lines retain their mask; crossings and bridge underpasses derive their fixed wire axis.
int CityPowerConnectionMask(const TileInfo *tp);
// Discover dry-line topology after edits. Water spans retain their construction axis.
int FindCityPowerConnectionMask(const City *city, TileIndex tile);
void UpdatePowerLineConnections(City *city, TileIndex tile);
// Call after all edited fields are installed. Refresh also invalidates adjoining power artwork.
void RefreshCityPowerConnections(City *city, TileIndex tile);
// Imports call this once after the complete native map is available.
void InitializeCityPowerConnections(City *city);
// Raw single-cell placement; caller owns admission and charging.
void PlaceCityPowerLineCell(City *city, TileIndex tile);

// Remove the complete matching water span, retaining water and its banks. Requires a valid origin.
bool RemoveCityWaterPowerSpan(City *city, TileIndex origin, bool animate);

// Select the source artwork from a camera-relative connection mask and slope.
int CityMapPowerLayout(int mask, int shape);

#endif
