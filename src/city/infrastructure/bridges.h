#ifndef SC2K_CITY_INFRASTRUCTURE_BRIDGES_H
#define SC2K_CITY_INFRASTRUCTURE_BRIDGES_H

#include "city/infrastructure/types.h"
#include "city/maps/terrain.h"
#include "city/maps/tile_storage.h"

// Readable tile records suffice for local head/underpass metadata; other kinds have no bridge head/underpass.
SC2K_PRESERVE_MOST int ReadCityBridgeHead(const TileInfo *tp);
SC2K_PRESERVE_MOST int ReadCityBridgeUnderpass(const TileInfo *tp);
int ReadCityBridgeUnderpassKind(const TileInfo *tp);
// Span queries require a valid bridge tile. End searches retain the terminal matching tile for malformed spans;
// validation rejects missing heads, inconsistent styles and mismatched highway lanes without repairing storage.
TileIndex FindCityBridgeHead(const City *city, TileIndex tile, int side);
bool ValidateCityBridgeSpan(const City *city, TileIndex tile);
// The stored field is BridgeStyle for road/rail heads and HighwayBridgeStyle for highway heads.
int ReadCityBridgeStyle(const City *city, TileIndex tile);
int ReadCityBridgePiece(const City *city, TileIndex tile);
int ReadCityHighwayBridgePiece(const City *city, TileIndex tile);
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
int CityRoadBridgePiece(int north, int south);
// Command-payload projection only; decoding returns -2 for unsupported encodings.
int DecodeCityRoadSpanChoice(int value);
int EncodeCityRoadSpanChoice(int choice);
// Admission is read-only; TEST returns a quote without mutation or presentation effects.
CityInfrastructureSpan FindCityBridgeSpan(const City *city, TileIndex start, TileIndex end, bool rail);
i64 CmdBuildCityBridge(City *city, u32 flags, TileIndex start, TileIndex end, int options);
// Underpass axis -1 admits a click; a drag must cross perpendicular to the bridge.
bool CanPlaceCityBridgeUnderpass(const City *city, TileIndex tile, int kind, int axis);
// Raw placement performs no charging; it updates statistics, connections and old/new damage.
void PlaceCityBridgeUnderpass(City *city, TileIndex tile, int kind);
// Clear an admitted middle's underlying transport, or start ground-cover recovery when the underpass is empty.
// Performs no payment; preserves the bridge and water, updates counters/damage/effect, and leaves reconnection to caller.
void ClearCityBridgeUnderpass(City *city, TileIndex tile);
// Remove only a native bridge, preserving its ground/water and reconnecting both ends. Origin must be valid.
bool RemoveCityNativeBridge(City *city, TileIndex origin, bool animate);

// Camera projection never changes stored metadata. Deck artwork preserves custom overrides and native style palettes.
SC2K_PRESERVE_MOST int CityBridgeViewZone(const City *city, TileIndex tile);
// The lower middle tile is the moving leaf. Non-road bridges and other styles return INVALID_TILE.
TileIndex CityDrawbridgeCenter(const City *city, TileIndex tile);
// True only for an open moving leaf. Queries the existing ship hash, with no saved state or RNG draws.
bool IsCityDrawbridgeOpen(const City *city, TileIndex tile);
// Caller supplies a valid tile with level water. Only middle pieces can pass boats; includes drawbridge state
// and the alternating highway passage pieces. Reads existing metadata/event hash, without mutation or RNG.
bool CityBridgeAllowsBoat(const City *city, TileIndex tile);
// Ship relocation/spawn/removal invalidates only leaves whose open state changes. Either position may be INVALID_TILE.
void InvalidateCityDrawbridgesForShip(City *city, int slot, TileIndex previous, TileIndex current);
void DrawCityBridgeDeckArtwork(City *city, TileIndex tile, int layout, int zoom, int screen_x, int baseline,
    BridgeStyle style);
// Caller owns pass selection and supplies the resolved underpass kind (road, rail or power).
void DrawCityBridgeUnderpass(City *city, TileIndex tile, int kind, int zoom, int screen_x, int ground_y);
// Ground pass for a valid bridge tile, including its underpass. Handles road, rail and highway bridge families;
// geometry is camera-relative. Returns true after painting, without placement markers or object submission.
bool DrawCityBridgeGround(City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry);

#endif
