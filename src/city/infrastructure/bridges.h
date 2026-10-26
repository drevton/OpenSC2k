#ifndef SC2K_CITY_INFRASTRUCTURE_BRIDGES_H
#define SC2K_CITY_INFRASTRUCTURE_BRIDGES_H

#include "city/infrastructure/types.h"
#include "city/maps/kinds.h"
#include "city/maps/tile_storage.h"

typedef struct CityTileGeometry CityTileGeometry;

// Native bridge encodings and local record helpers. Head classification accepts any valid tile.
// Style access interprets the head's altitude field; axis access interprets bridge terrain.
// Callers choose the kind/style interpretation and own admission, remaining edits and old/new damage.
enum {
  CITY_BRIDGE_AXIS_MASK = 0x10, CITY_BRIDGE_HEAD_FACING_MASK = 0x20,
  CITY_BRIDGE_HIGHWAY_RIGHT = 0x80,
  CITY_BRIDGE_UNDERPASS_NONE = 0, CITY_BRIDGE_UNDERPASS_RAIL = 1, CITY_BRIDGE_UNDERPASS_ROAD = 2,
  CITY_BRIDGE_UNDERPASS_POWER = 3,
  // Head altitude bit: phase of the two alternating highway artwork pieces.
  CITY_BRIDGE_HIGHWAY_PHASE_MASK = 0x200,
  CITY_BRIDGE_HEAD_NONE = 0, CITY_BRIDGE_HEAD_NORTH = 1, CITY_BRIDGE_HEAD_SOUTH = 2,
};

_Static_assert(CITY_BRIDGE_UNDERPASS_NONE == 0 &&
    CITY_BRIDGE_UNDERPASS_RAIL == (TK_ROAD_BRIDGE_RAIL & 3) &&
    CITY_BRIDGE_UNDERPASS_ROAD == (TK_ROAD_BRIDGE_ROAD & 3) &&
    CITY_BRIDGE_UNDERPASS_POWER == (TK_ROAD_BRIDGE_POWER & 3),
    "bridge kind low bits must match underpass values");

static SC2K_FORCE_INLINE int ReadTileBridgeStyle(const TileInfo *tp) {
  return (tp->altitude >> CITY_ALTITUDE_WATER_SHIFT) & 15;
}

static SC2K_FORCE_INLINE void WriteTileBridgeStyle(TileInfo *tp, int style) {
  tp->altitude = (tp->altitude & ~0x1E0) | style << CITY_ALTITUDE_WATER_SHIFT;
}

static SC2K_FORCE_INLINE bool IsCityBridgeHead(const TileInfo *tp) {
  return IsTileKindBridgeHead(tp->kind);
}

static SC2K_FORCE_INLINE int ReadCityBridgeAxis(const TileInfo *tp) {
  return (tp->terrain & CITY_BRIDGE_AXIS_MASK) != 0;
}

// Readable tile records suffice for local underpass metadata; other kinds have no underpass.
SC2K_PRESERVE_MOST int ReadCityBridgeUnderpass(const TileInfo *tp);
int ReadCityBridgeUnderpassKind(const TileInfo *tp);
// Requires a valid bridge tile. Rejects missing heads, inconsistent styles and mismatched highway lanes
// without repairing storage.
bool ValidateCityBridgeSpan(const City *city, TileIndex tile);
// The stored field is BridgeStyle for road/rail heads and HighwayBridgeStyle for highway heads.
int ReadCityBridgeStyle(const City *city, TileIndex tile);
int ReadCityHighwayBridgePiece(const City *city, TileIndex tile);
// Upper road surface: road/rail approaches rise one level; highway meshes add their raised carriageway above it.
int ReadCityBridgeDeckHeight(const City *city, TileIndex tile);
int CityBridgeHighwayZone(const City *city, TileIndex tile);
int CityBridgeTransportKind(const TileInfo *tp, int direction);
int CityBridgeTransportEdgeHeight(const City *city, TileIndex tile, int direction);
// Scheduled landscape visit; wet middles stop after cover refresh, dry pieces also recover exposed ground.
void TickCityBridgeTile(City *city, TileIndex tile);
// Convert an admitted inclusive span, one lane at a time. Style uses the head kind's corresponding stored enum.
void StoreCityBridgeSpan(City *city, TileIndex first, TileIndex last, int head_kind, int style);

// Map an admitted non-power, non-cancel span choice to its saved road/rail style; no City, RNG or presentation effects.
BridgeStyle CityBridgeStyleFromChoice(int choice);
int CityNativeRoadBridgeStyles(int length);
// Command-payload projection only; decoding returns -2 for unsupported encodings.
int DecodeCityRoadSpanChoice(int value);
int EncodeCityRoadSpanChoice(int choice);
// Read-only explicit-endpoint admission: diagonal drags project onto the dominant axis (X wins ties), then normalize
// endpoints into increasing tile order. Failure has end == INVALID_TILE and notice/blocker data; success includes
// both heads in length and reports compatible styles. No pricing, payment or presentation effects.
CityInfrastructureSpan FindCityBridgeSpan(const City *city, TileIndex start, TileIndex end, bool rail);
// TEST returns a quote without mutation or presentation effects; execution builds and returns its signed charge/error.
i64 CmdBuildCityBridge(City *city, u32 flags, TileIndex start, TileIndex end, int options);
// Underpass axis -1 admits a click; a drag must cross perpendicular to the bridge.
bool CanPlaceCityBridgeUnderpass(const City *city, TileIndex tile, int kind, int axis);
// Raw placement performs no charging; it updates statistics, connections and old/new damage.
void PlaceCityBridgeUnderpass(City *city, TileIndex tile, int kind);
// Surface bulldozer callback shared by road/rail and highway bridge modules. A middle edits only its underpass;
// a head removes the span. Ordinary highway tiles use the same callback and retain structural demolition.
// CITY_COMMAND_TEST checks admission without effects; execution requires prior payment and old-extent invalidation.
i64 ClearCityBridgeTile(City *city, u32 flags, TileIndex tile);
// Remove only a native bridge, preserving its ground/water and reconnecting both ends. Origin must be valid.
bool RemoveCityNativeBridge(City *city, TileIndex origin, bool animate);

// The lower middle tile is the moving leaf. Non-road bridges and other styles return INVALID_TILE.
TileIndex CityDrawbridgeCenter(const City *city, TileIndex tile);
// True only for an open moving leaf. Queries the existing ship hash, with no saved state or RNG draws.
bool IsCityDrawbridgeOpen(const City *city, TileIndex tile);
// Caller supplies a valid tile with level water. Only middle pieces can pass boats; includes drawbridge state
// and the alternating highway passage pieces. Reads existing metadata/event hash, without mutation or RNG.
bool CityBridgeAllowsBoat(const City *city, TileIndex tile);
// Ship relocation/spawn/removal invalidates only leaves whose open state changes. Either position may be INVALID_TILE.
void InvalidateCityDrawbridgesForShip(City *city, int slot, TileIndex previous, TileIndex current);
// Far/near artwork depth in the bound Drawing camera. Heads use bank height; middle pieces use deck height.
// Requires a valid bridge tile and attached Drawing; changes no state or RNG.
i32 CityMapBridgeArtworkDepth(const City *city, TileIndex tile, bool foreground);
// Camera projection never changes stored metadata. Deck artwork preserves custom overrides and native style palettes.
void DrawCityBridgeDeckArtwork(const City *city, TileIndex tile, int layout, int zoom, int screen_x, int baseline,
    BridgeStyle style);
// Caller owns pass selection and supplies the resolved underpass kind (road, rail or power).
void DrawCityBridgeUnderpass(const City *city, TileIndex tile, int kind, int zoom, int screen_x, int ground_y);
// Ground pass for a valid bridge tile, including its underpass. Handles road, rail and highway bridge families;
// geometry is camera-relative. Returns true after painting, without placement markers or object submission.
bool DrawCityBridgeGround(const City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry);

#endif
