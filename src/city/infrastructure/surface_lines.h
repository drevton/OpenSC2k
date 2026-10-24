#ifndef SC2K_CITY_INFRASTRUCTURE_SURFACE_LINES_H
#define SC2K_CITY_INFRASTRUCTURE_SURFACE_LINES_H

#include "city/types.h"

typedef struct CityTileGeometry CityTileGeometry;

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
// Ordinary flat line axis, or -1 for slopes, bends, junctions and rail transitions.
// Pass the family when crossing another network; zero accepts any ordinary line.
int ReadCityStraightLineAxis(const City *city, TileIndex tile, int family);
// Adjacent incline in north/south/west/east order, or -1.
int FindCityRailTransition(const City *city, TileIndex tile);
bool IsCityLineBendOrJunction(const City *city, TileIndex tile);

// Read stored ordinary-road connections; crossings derive their eligible directions without changing topology.
int CityRoadConnectionMask(const City *city, TileIndex tile);
// A valid edited cell must have all fields installed before reconnection. Neighbor order is significant for rails.
void UpdateRoadConnections(City *city, TileIndex tile);
void UpdateRailConnections(City *city, TileIndex tile);
// Raw placement updates kind, statistics, crossings, nearby connections and damage. Caller owns admission/charging.
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
int DrawCityMapInfrastructureGround(City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
    CityMapSurface surface, const CityTileGeometry *geometry);
// Compose transport artwork in the current depth pass, delegating bridge/highway geometry to those families.
void DrawCityMapTransportArtwork(City *city, TileIndex tile, int layout, int zoom, int screen_x, int baseline,
    const CityTileGeometry *geometry);
// Fallback for visible transport details not handled by native family artwork. Caller owns visibility/clipping;
// this paints terrain, the transport object, selection and power warning, without changing City.
void DrawCityTransportTileFallback(City *city, TileIndex tile, int layout, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry);

// Shared command admission is read-only in TEST mode; execution retains the family placement/payment order.
i64 CmdPlaceSubwayRailConnection(City *city, u32 flags, TileIndex tile,
    int reserved);

#endif
