#ifndef SC2K_DRAWING_MAPS_PLACEMENT_H
#define SC2K_DRAWING_MAPS_PLACEMENT_H

#include "city/types.h"

// Marker bounds in the main map's logical pixels, including magnification and cached sprite anchors.
// Requires a valid tile and prepared default view. Reads Drawing resources without changing selection state.
CityRect CityMapPlacementBounds(const City *city, TileIndex tile, bool point);

// Draw one valid tile's point or diamond marker using the bound target and current depth pass.
// UI chooses point/diamond and blocked styling; this does not read or change UI selection state.
void DrawCityMapPlacementMarker(const City *city, TileIndex tile, bool point, bool blocked);

// Invalidate the main-map tile marker in screen pixels; invalid tiles or an unprepared view are ignored.
void InvalidateCityMapPlacement(const City *city, TileIndex tile);

#endif
