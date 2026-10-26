#ifndef SC2K_CITY_INFRASTRUCTURE_SURFACE_LINES_H
#define SC2K_CITY_INFRASTRUCTURE_SURFACE_LINES_H

#include "city/maps/tile_storage.h"
#include "city/types.h"

typedef struct CityTileGeometry CityTileGeometry;

// Primary axis: road for road/power and road/rail, rail for rail/power.
// Zero is east/west, one is north/south. The second network is perpendicular.
static SC2K_FORCE_INLINE int ReadCityCrossingAxis(const TileInfo *tp) {
  return (tp->terrain & CITY_CROSSING_AXIS_MASK) != 0;
}

static SC2K_FORCE_INLINE void WriteCityCrossingAxis(TileInfo *tp, int axis) {
  tp->terrain = (tp->terrain & ~CITY_CROSSING_AXIS_MASK) | (axis ? CITY_CROSSING_AXIS_MASK : 0);
}

static SC2K_FORCE_INLINE int ReadCityCrossingConnections(const TileInfo *tp) {
  return ReadCityCrossingAxis(tp) ? CONN_NS : CONN_EW;
}

// Resolved transport surface, derived from TileInfo rather than retained drawing state.
// shape is camera-relative for view queries; raised is a lift in map levels. layout names source artwork,
// foundation is its source artwork bank (-1 means none), and bridge_approach preserves the original slope below it.
typedef struct CityMapSurface {
  int shape, raised, layout, foundation;
  bool bridge_approach;
} CityMapSurface;

// Recipe IDs shared by the artwork-derived road lanes and rail centrelines. Entry/exit use west/south/east/north.
enum CitySurfaceLinePathKind {
  CITY_LINE_PATH_CURVE = 0, CITY_LINE_PATH_DIAGONAL = 2, CITY_LINE_PATH_TRANSITION = 8,
  CITY_LINE_PATH_APPROACH = 12, CITY_LINE_PATH_SHARED = 16, CITY_LINE_PATH_TURNAROUND = 32,
  CITY_LINE_PATH_STRAIGHT = 33
};
typedef struct { u8 kind, rotation; } CitySurfaceLinePath;
// Read-only world-space path selection, including prepared diagonal approaches. Rail selects the track centreline.
CitySurfaceLinePath ResolveCitySurfaceLinePath(const City *city, TileIndex tile, int entry, int exit, bool rail);

typedef struct Drawing Drawing;

// Read-only topology queries for a valid tile; these do not reconnect neighbors or alter stored terrain.
// Stored ordinary-line/plain-highway axis: 0 X/east-west, 1 Y/north-south, or -1 for unsupported kinds/shapes.
// Callers select allowed kinds; rail transitions require adjacent-incline queries.
int ReadCityStraightLineAxis(const City *city, TileIndex tile);
// Adjacent incline side: 0 north, 1 south, 2 west, 3 east, or -1 when none matches.
int FindCityRailTransition(const City *city, TileIndex tile);
bool IsCityLineBendOrJunction(const City *city, TileIndex tile);

// Read stored ordinary-road connections; crossings derive their eligible directions without changing topology.
int CityRoadConnectionMask(const City *city, TileIndex tile);
// The valid edited cell must have all fields installed. Only ordinary road/rail kinds are reshaped; same-kind
// replacement still resets metadata before storing the new mask. Where connections determine a rail's axis, the first
// matching incline in north, south, west, east order fixes it. Both operations invalidate affected artwork.
void UpdateRoadConnections(City *city, TileIndex tile);
void UpdateRailConnections(City *city, TileIndex tile);
// Raw placement at a valid tile delegates bridge underpasses to the bridge module and preserves eligible crossings.
// Other cells already carrying the requested family, or unsupported occupants, are unchanged. Reconnect the cell,
// then north, south, west and east, then power connections. Caller owns tool admission, grading and payment;
// placement owns kind, statistics, topology and damage.
void PlaceCityRoadCell(City *city, TileIndex tile);
void PlaceCityRailCell(City *city, TileIndex tile);
// Shared landscape visit for lines/crossings: refresh cover first, then paving for ground transport only.
void TickCitySurfaceLineGround(City *city, TileIndex tile);

// Read-only transition admission; invalid tiles return false. Surface rail takes precedence over buried subway.
bool CanPlaceCitySubwayRailConnection(const City *city, TileIndex tile);
// Requires a valid tile. Connects the first eligible neighbor, preserving subway placement and statistics updates.
// Returns success or a negative gameplay error; there is no separate transition charge.
i64 PlaceCitySubwayRailConnection(City *city, TileIndex tile);

// These read-only queries accept any valid tile; non-line tiles retain their ordinary surface description.
// View rotation is explicit and never changes world storage. Heights and lifts are in map levels.
CityMapSurface ResolveCitySurfaceLineGround(const City *city, TileIndex tile);
CityMapSurface ResolveCitySurfaceLineViewGround(const City *city, TileIndex tile, int rotation);
// Return a Drawing cache slot including zoom. Its diagonal layout also defines the decorative cars' centreline.
int CitySurfaceLineSprite(const City *city, TileIndex tile, int layout, int zoom, int rotation);
// Composite prepared diagonal halves through the normal sprite API; ordinary sprites use the same direct blit.
bool DrawCitySurfaceLineSprite(Drawing *drawing, int sprite, int x, int baseline, int flipped);

// Paint foundations/ground in the current ground pass; return the raised object's baseline in every pass.
// Geometry is camera-relative and must describe the same tile. No City mutation or RNG consumption.
int DrawCityMapInfrastructureGround(const City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
    CityMapSurface surface, const CityTileGeometry *geometry);
// Compose transport artwork in the current depth pass, delegating bridge/highway geometry to those families.
void DrawCityMapTransportArtwork(const City *city, TileIndex tile, int layout, int zoom, int screen_x, int baseline,
    const CityTileGeometry *geometry);
// Fallback for visible transport details not handled by native family artwork. Caller owns visibility/clipping;
// this paints terrain, the transport object, selection and power warning, without changing City.
void DrawCityTransportTileFallback(const City *city, TileIndex tile, int layout, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry);

// Shared command admission is read-only in TEST mode; execution retains the family placement/payment order.
i64 CmdPlaceSubwayRailConnection(City *city, u32 flags, TileIndex tile,
    int reserved);

#endif
