// TileInfo storage for buried pipes/subways (zero-based bits):
// underground[0..5] is a TUK_ ID, not a literal connection mask: 0 clear; 1..15 subway; 16..30 pipe; 31/32
// perpendicular subway/pipe crossings; 34 silo; 35 subway access. The kind tables derive connection masks/poses.
// underground[6..7] is opaque saved metadata. SetUndergroundTile preserves it with native landscape enabled;
// the stock-compatible path writes the full byte. Silo/access IDs are also installed by building placement.
// Surface kind, terrain[4..7] and zone[5..7] remain the surface family's payload, even when a buried line exists.
// Buried shape selection reads the shared zone slope and altitude heights; road-tunnel depth in altitude[10..14]
// is a separate system. TF_PIPED is pipe capability, not an underground kind. Other fields follow tile_storage.h.

#include "city/infrastructure/networks.h"

#include "city/infrastructure/lines.h"
#include "city/saveload/map_format.h"

#include <assert.h>

#include "city/infrastructure/power_lines.h"
#include "city/infrastructure/surface_lines.h"
#include "city/maps/kinds.h"
#include "city/maps/terrain.h"
#include "city/maps/tile_storage.h"
#include "city/maps/map.h"
#include "city/presentation.h"
#include "city/state.h"
#include "drawing/maps/map_camera.h"

// Preserve the reconnectors' north, south, west, east scan order.
static const struct {
  i8 x, y;
  u8 connection;
} kConnectionNeighbors[4] = {
    {0, -1, CONN_NORTH},
    {0, 1, CONN_SOUTH},
    {-1, 0, CONN_WEST},
    {1, 0, CONN_EAST},
};

// Update an underground tile and its subway statistic. Requires a valid tile and underground kind; maintains subway
// totals and invalidates old artwork. Callers reconnect neighbors and invalidate the completed replacement.
void SetUndergroundTile(City *city, TileIndex tile_index, int value) {
  InvalidateTile(city, tile_index);
  TileInfo *tp = &city->tile[tile_index];
  if (ReadCityZoneKind(tp) != ZONE_MILITARY) {
    int old = tp->underground & CITY_UNDERGROUND_KIND_MASK;
    bool was_subway = IsUndergroundKindSubway(old);
    bool is_subway = IsUndergroundKindSubway(value);
    city->infrastructure.subway_tiles += is_subway - was_subway;
  }
  // Stock classifies the full replacement before narrowing its stored map byte.
  tp->underground = !(city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) ? value :
      (tp->underground & CITY_UNDERGROUND_METADATA_MASK) | (value & CITY_UNDERGROUND_KIND_MASK);
}

// Water terrain slopes constrain surface artwork, not buried network topology. Pipe/subway artwork still follows the
// unchanged underground surface geometry.
static int UndergroundConnectionTerrain(const City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile));
  if ((city->fixes & (1ull << CITY_FIX_BURIED_UTILITY_TERRAIN)) &&
      HasTileWater(&city->tile[tile])) return CITY_TERRAIN_FLAT;
  return ReadLegacyTerrainCode(city, tile);
}

// Test whether an underground kind contains the selected pipe or subway family. Input is an underground value rather
// than a map index; no map state or connection masks are changed.
static bool CityUndergroundHasLine(int kind, bool subway) {
  if (!subway) return kind >= TUK_PIPE_FIRST && kind <= TUK_SUBWAY_NS_PIPE_EW;
  return IsUndergroundKindSubway(kind);
}

// Shape a pipe/subway cell from terrain and its neighbors.
static void UpdateUndergroundConnections(City *city, TileIndex tile_index, bool subway) {
  assert(IsMapTileValid(city, tile_index));
  InvalidateTile(city, tile_index);
  int first = subway ? TUK_SUBWAY_FIRST : TUK_PIPE_FIRST;
  int last = subway ? TUK_SUBWAY_LAST : TUK_PIPE_LAST;
  int kind = city->tile[tile_index].underground & CITY_UNDERGROUND_KIND_MASK;
  if (kind < first || kind > last)
    return;
  int terrain_code = UndergroundConnectionTerrain(city, tile_index);
  int terrain = terrain_code <= CITY_TERRAIN_BANK_48 ? terrain_code & CITY_TERRAIN_SHAPE_MASK : 0;
  if (kLineTerrainOverrideBySlope[terrain] != 0) {
    SetUndergroundTile(city, tile_index, first + kLineTileOffsetByTerrain[terrain]);
    if (subway) InvalidateTile(city, tile_index);
    return;
  }
  int mask = CONN_NONE;
  CityPoint point = MapTilePt(city, tile_index);
  for (int direction = 0; direction < 4; ++direction) {
    TileIndex neighbor_index = MapTileXYOrInvalid(city, point.x + kConnectionNeighbors[direction].x,
        point.y + kConnectionNeighbors[direction].y);
    if (neighbor_index == INVALID_TILE) continue;
    int neighbor = ReadTileUndergroundKind(&city->tile[neighbor_index]);
    bool connected = CityUndergroundHasLine(neighbor, subway);
    if (!connected)
      continue;
    int neighbor_terrain = UndergroundConnectionTerrain(city, neighbor_index);
    bool allowed = kConnectionNeighbors[direction].y ? kLineVerticalConnectionAllowed[neighbor_terrain] != 0 :
        kLineHorizontalConnectionAllowed[neighbor_terrain] != 0;
    if (allowed)
      mask |= kConnectionNeighbors[direction].connection;
  }
  if (!subway && mask == CONN_NONE)
    mask = CONN_ALL;
  SetUndergroundTile(city, tile_index, first + kLineTileOffsetByConnectionMask[mask]);
  if (subway) InvalidateTile(city, tile_index);
}

// Add one admitted pipe/subway cell and reconnect its bounded neighborhood. Requires a valid tile and explicit
// underground family; keeps statistics and damage on the common placement path.
static void PlaceCityUndergroundCell(City *city, TileIndex tile, bool subway) {
  TileInfo *tp = &city->tile[tile];
  int kind = tp->underground & CITY_UNDERGROUND_KIND_MASK;
  if (CityUndergroundHasLine(kind, subway)) return;
  int other_ew = subway ? TUK_PIPE_EW : TUK_SUBWAY_EW;
  int other_ns = subway ? TUK_PIPE_NS : TUK_SUBWAY_NS;
  if (kind == TUK_CLEAR)
    SetUndergroundTile(city, tile, subway ? TUK_SUBWAY_EW : TUK_PIPE_EW);
  else if (kind == other_ew)
    SetUndergroundTile(city, tile, subway ? TUK_SUBWAY_NS_PIPE_EW : TUK_SUBWAY_EW_PIPE_NS);
  else if (kind == other_ns)
    SetUndergroundTile(city, tile, subway ? TUK_SUBWAY_EW_PIPE_NS : TUK_SUBWAY_NS_PIPE_EW);
  else return;
  if (!subway) tp->flags |= TF_PIPED;
  UpdateUndergroundConnections(city, tile, subway);
  CityPoint point = MapTilePt(city, tile);
  for (int direction = 0; direction < 4; ++direction) {
    TileIndex neighbor = MapTileXYOrInvalid(city, point.x + kConnectionNeighbors[direction].x,
        point.y + kConnectionNeighbors[direction].y);
    if (neighbor != INVALID_TILE) UpdateUndergroundConnections(city, neighbor, subway);
  }
}

// Add a water-pipe cell through the shared underground placement path.
void PlaceCityWaterPipeCell(City *city, TileIndex origin_tile) {
  PlaceCityUndergroundCell(city, origin_tile, false);
}

// Add a subway cell through the shared underground placement path.
void PlaceCitySubwayCell(City *city, TileIndex origin_tile) {
  PlaceCityUndergroundCell(city, origin_tile, true);
}

// Refresh the four surface connections around an edited cell. Requires a valid edited tile after its fields are
// written; reconnects only admitted neighboring surface families.
void RefreshAdjacentSurfaceConnections(City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile));
  CityPoint point = MapTilePt(city, tile);
  if (IsTileKindPowerLine(city->tile[tile].kind)) {
    UpdatePowerLineConnections(city, tile);
    InvalidateTile(city, tile);
  }
  for (int direction = 0; direction < 4; ++direction) {
    int dx = kConnectionNeighbors[direction].x, dy = kConnectionNeighbors[direction].y;
    if (!IsMapCoordinateValid(city, point.x + dx, point.y + dy)) continue;
    TileIndex neighbor = tile + dy * city->map_width + dx;
    int kind = city->tile[neighbor].kind;
    if (kind == TK_ROAD)
      UpdateRoadConnections(city, neighbor);
    if (kind == TK_RAIL)
      UpdateRailConnections(city, neighbor);
    if (IsTileKindPlainPowerLine(kind))
      UpdatePowerLineConnections(city, neighbor);
  }
}

// Refresh the four underground connections around an edited cell. Requires a valid edited tile; neighboring probes
// skip map edges and update their stored underground topology.
void RefreshAdjacentUndergroundConnections(City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile));
  CityPoint point = MapTilePt(city, tile);
  for (int direction = 0; direction < 4; ++direction) {
    int dx = kConnectionNeighbors[direction].x, dy = kConnectionNeighbors[direction].y;
    if (!IsMapCoordinateValid(city, point.x + dx, point.y + dy)) continue;
    TileIndex neighbor = tile + dy * city->map_width + dx;
    int kind = ReadTileUndergroundKind(&city->tile[neighbor]);
    if (kind >= TUK_SUBWAY_FIRST && kind <= TUK_SUBWAY_LAST)
      UpdateUndergroundConnections(city, neighbor, true);
    if (kind >= TUK_PIPE_FIRST && kind <= TUK_PIPE_LAST)
      UpdateUndergroundConnections(city, neighbor, false);
  }
}

// The legacy reconnectors force a climbing kind from terrain, even when the
// neighboring utility runs across that slope. Native artwork follows those
// actual neighbors; crossing tiles still keep their two utility axes separate.
int CityUndergroundViewConnections(const City *city, TileIndex tile, bool pipes) {
  static const u8 kUtilityConnections[] = {
      CONN_EW, CONN_NS, CONN_NS, CONN_EW,
      CONN_NS, CONN_EW, CONN_WS, CONN_SE,
      CONN_NE, CONN_NW, CONN_ALL ^ CONN_EAST,
      CONN_ALL ^ CONN_NORTH, CONN_ALL ^ CONN_WEST,
      CONN_ALL ^ CONN_SOUTH, CONN_ALL,
  };
  int first = pipes ? TUK_PIPE_FIRST : TUK_SUBWAY_FIRST;
  int last = pipes ? TUK_PIPE_LAST : TUK_SUBWAY_LAST;
  int kind = CityMapViewUnderground(city, tile);
  if (kind == TUK_SUBWAY_EW_PIPE_NS) return pipes ? CONN_NS : CONN_EW;
  if (kind == TUK_SUBWAY_NS_PIPE_EW) return pipes ? CONN_EW : CONN_NS;
  bool regular = kind >= first && kind <= last;
  if (!regular && (pipes || (kind != TUK_SUBWAY_ACCESS && kind != TUK_MISSILE_SILO)))
    return CONN_NONE;
  static const struct { i8 x, y; u8 connection, opposite; } kNeighbors[] = {
      {-1, 0, CONN_WEST, CONN_EAST},
      {0, 1, CONN_SOUTH, CONN_NORTH},
      {1, 0, CONN_EAST, CONN_WEST},
      {0, -1, CONN_NORTH, CONN_SOUTH},
  };
  int mask = CONN_NONE;
  for (int side = 0; side < 4; ++side) {
    TileIndex next = CityMapViewNeighbor(city, tile, kNeighbors[side].x, kNeighbors[side].y);
    if (next == INVALID_TILE) continue;
    int neighbor = CityMapViewUnderground(city, next);
    bool connected = neighbor >= first && neighbor <= last;
    if (!pipes && (neighbor == TUK_SUBWAY_ACCESS || neighbor == TUK_MISSILE_SILO))
      connected = true;
    if (neighbor == TUK_SUBWAY_EW_PIPE_NS)
      connected = ((pipes ? CONN_NS : CONN_EW) & kNeighbors[side].opposite) != 0;
    else if (neighbor == TUK_SUBWAY_NS_PIPE_EW)
      connected = ((pipes ? CONN_EW : CONN_NS) & kNeighbors[side].opposite) != 0;
    if (connected) mask |= kNeighbors[side].connection;
  }
  return mask ? mask : regular ? kUtilityConnections[kind - first] : CONN_ALL;
}
