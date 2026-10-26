#ifndef SC2K_CITY_INFRASTRUCTURE_NETWORKS_H
#define SC2K_CITY_INFRASTRUCTURE_NETWORKS_H

#include "city/maps/tile_storage.h"
#include "city/types.h"

// Classify an underground kind value, not a map index. Accepts unnarrowed replacement integers;
// no masking or byte conversion occurs before classification, including subway access and silo kinds.
static SC2K_FORCE_INLINE bool IsUndergroundKindSubway(int kind) {
  return (kind >= TUK_SUBWAY_FIRST && kind <= TUK_SUBWAY_LAST) ||
      kind == TUK_SUBWAY_ACCESS || kind == TUK_SUBWAY_EW_PIPE_NS ||
      kind == TUK_SUBWAY_NS_PIPE_EW || kind == TUK_MISSILE_SILO;
}

// Requires a valid tile. Invalidates the old artwork, updates subway statistics outside military zones, then writes.
// Native landscape preserves the saved metadata bits; the stock-compatible path replaces the complete byte.
// The caller reconnects neighbors and invalidates the final artwork after all related fields are installed.
void SetUndergroundTile(City *city, TileIndex tile, int kind);
// Raw placement at a valid tile: install into clear underground or cross an existing straight line. An existing
// line of the requested family or unsupported occupant is unchanged. Update statistics and reconnect the cell, then
// north, south, west and east; pipes also set TF_PIPED. Caller owns tool admission, grading and payment.
void PlaceCityWaterPipeCell(City *city, TileIndex origin_tile);
void PlaceCitySubwayCell(City *city, TileIndex origin_tile);

// Fields at the valid edited tile must be complete. Reconnect neighboring roads, rails and plain power lines in
// north, south, west, east order, and the edited power line before them. Updates topology and drawing damage.
void RefreshAdjacentSurfaceConnections(City *city, TileIndex tile);
// The valid edited tile must be complete. Reshape neighboring ordinary pipes/subways in the same order, updating
// topology and damage without reshaping the edited cell itself.
void RefreshAdjacentUndergroundConnections(City *city, TileIndex tile);

// Camera-relative pipe/subway connections for cutaway artwork. Requires a valid tile and bound Drawing camera;
// pipes selects the pipe family, otherwise subway. Reads native neighbors, preserving crossing axes and access links.
// No tile, Drawing, RNG or allocation changes; simulation topology is handled by the reconnectors above.
int CityUndergroundViewConnections(const City *city, TileIndex tile, bool pipes);

#endif
