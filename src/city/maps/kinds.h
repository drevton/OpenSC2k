#ifndef SC2K_CITY_MAPS_KINDS_H
#define SC2K_CITY_MAPS_KINDS_H

#include "city/maps/tile_storage.h"
#include <assert.h>

// Immutable kind metadata and inline classification. These queries read no City or tile state.
// Bridge deck/underpass routing still needs the occupant metadata; these queries classify kind values only.

// Stored surface-kind classes may overlap at crossings. Bridge routes still
// require deck/underpass resolution. This is not a stock artwork classifier:
// retired road/rail IDs can now name native highways and bridges.
typedef enum TileKindClasses {
  TCLASS_ROAD = 1 << 0,
  TCLASS_RAIL = 1 << 1,
  TCLASS_POWER = 1 << 2,
  TCLASS_VARIANT = 1 << 3,
  TCLASS_HIGHWAY = 1 << 4,
  TCLASS_HIGHWAY_AXIS = 1 << 5,
  TCLASS_TREE = 1 << 6,
  TCLASS_PLAIN_TRANSPORT = 1 << 7,
  TCLASS_SURFACE_BRIDGE = 1 << 8,
  TCLASS_NATIVE_BRIDGE = 1 << 9,
  TCLASS_RAIL_BRIDGE = 1 << 10,
  TCLASS_ROUTE_STATION = 1 << 11,
  TCLASS_SINGLE_FOOTPRINT = 1 << 12,
  TCLASS_GROWTH_OBSTRUCTION = 1 << 13,
  TCLASS_GROUND_CROSSING = 1 << 17,
  TCLASS_WATER = 1 << 18,
  // Low airport surfaces, including parked aircraft, paint before raised objects.
  TCLASS_GROUND_ARTWORK = 1 << 23,
} TileKindClasses;

// Three selector bits share the immutable kind table with the classification flags.
// The axis selector covers ground crossings and highways, including directional ramps.
enum {
  CITY_TILE_LAYOUT_SHIFT = 14,
  CITY_TILE_LAYOUT_MASK = 7 << CITY_TILE_LAYOUT_SHIFT,
  CITY_TILE_LAYOUT_FIXED = 0,
  CITY_TILE_LAYOUT_VARIANT = 1,
  CITY_TILE_LAYOUT_AXIS = 2,
  CITY_TILE_LAYOUT_TRANSPORT = 3,
  CITY_TILE_LAYOUT_POWER = 4,
  CITY_TILE_LAYOUT_BUILDING = 5,
  CITY_TILE_LAYOUT_BRIDGE = 6,
};

extern const u32 kCityTileClasses[TK_COUNT];

// Surface kinds must fit the stored byte; keep misuse visible in debug builds.
static SC2K_FORCE_INLINE u32 ReadCityTileClasses(int kind) {
  assert((unsigned)kind < TK_COUNT);
  return kCityTileClasses[kind];
}

static SC2K_FORCE_INLINE bool IsTileKindShoreTree(int kind) {
  return kind >= TK_SHORE_TREE_FIRST && kind <= TK_SHORE_TREE_LAST;
}

// These kinds retain bank ground cover rather than an underlying-water payload.
static SC2K_FORCE_INLINE bool IsTileKindShoreBank(int kind) {
  return kind == TK_WATER_BANK || IsTileKindShoreTree(kind);
}

// Exposed water has no occupant; water infrastructure and buildings are excluded.
static SC2K_FORCE_INLINE bool IsTileKindExposedWater(int kind) {
  return kind >= TK_WATER && kind <= TK_WATER_BANK;
}

static SC2K_FORCE_INLINE bool IsTileKindTree(int kind) {
  return (kind >= TK_TREE_FIRST && kind <= TK_TREE_LAST) || IsTileKindShoreTree(kind);
}

// Keep the few deliberate contiguous categories here. Callers describe what
// they need instead of relying on an unrelated kind as a numeric boundary.
static SC2K_FORCE_INLINE bool IsTileKindBuilding(int kind) {
  return kind >= TK_RESIDENTIAL_FIRST && kind != TK_PARK;
}

// Dry buildings use the altitude water-height field for construction progress.
// Water-class facilities keep a water plane.
static SC2K_FORCE_INLINE bool IsTileKindDryBuilding(int kind) {
  return IsTileKindBuilding(kind) && (ReadCityTileClasses(kind) & TCLASS_WATER) == 0;
}

static SC2K_FORCE_INLINE bool IsTileKindZoneBuilding(int kind) {
  return kind >= TK_RESIDENTIAL_FIRST && kind < TK_HYDRO_POWER;
}

static SC2K_FORCE_INLINE bool IsTileKindFacility(int kind) {
  return kind >= TK_HYDRO_POWER && kind != TK_PARK;
}

static SC2K_FORCE_INLINE bool IsTileKindPowerPlant(int kind) {
  return kind >= TK_HYDRO_POWER && kind <= TK_COAL_POWER_PLANT && kind != TK_PARK;
}

// Plants with an annual operating-age counter and renewal/demolition handling.
static SC2K_FORCE_INLINE bool IsTileKindAgingPowerPlant(int kind) {
  switch (kind) {
  case TK_GAS_POWER_PLANT:
  case TK_OIL_POWER_PLANT:
  case TK_NUCLEAR_POWER_PLANT:
  case TK_SOLAR_POWER_PLANT:
  case TK_MICROWAVE_POWER_PLANT:
  case TK_FUSION_POWER_PLANT:
  case TK_COAL_POWER_PLANT:
    return true;
  default:
    return false;
  }
}

// The final reward category includes the Braun Llama Dome.
static SC2K_FORCE_INLINE bool IsTileKindArcology(int kind) {
  return kind >= TK_PLYMOUTH_ARCOLOGY;
}

// Unbuilt surfaces include clear ground, exposed water, rubble, waste and trees.
// Landscape also includes the small park. Occupied water retains its surface kind.
static SC2K_FORCE_INLINE bool IsTileKindUnbuilt(int kind) {
  return kind <= TK_TREE_LAST || IsTileKindShoreTree(kind);
}

static SC2K_FORCE_INLINE bool IsTileKindLandscape(int kind) {
  return IsTileKindUnbuilt(kind) || kind == TK_PARK;
}

static SC2K_FORCE_INLINE bool IsTileKindBareGround(int kind) {
  return kind < TK_TREE_FIRST;
}

static SC2K_FORCE_INLINE bool IsTileKindSparseTree(int kind) {
  return kind >= TK_TREE_FIRST && kind < TK_TREE_DENSE_FIRST;
}

static SC2K_FORCE_INLINE bool IsTileKindTreeOrPark(int kind) {
  return IsTileKindTree(kind) || kind == TK_PARK;
}

// Power lines precede this category; road, rail, highways, bridges, entrances
// and buildings belong to it. Exclude landscape kinds interleaved with transport,
// including shoreline trees and the small park.
static SC2K_FORCE_INLINE bool IsTileKindTransportOrBuilding(int kind) {
  return kind >= TK_ROAD && !IsTileKindLandscape(kind);
}

static SC2K_FORCE_INLINE bool IsTileKindTunnel(int kind) {
  return kind == TK_TUNNEL_ENTRANCE;
}

static SC2K_FORCE_INLINE bool IsTileKindRunway(int kind) {
  return kind == TK_RUNWAY_FIRST || kind == TK_RUNWAY_LAST;
}

static SC2K_FORCE_INLINE bool IsTileKindRunwayOrPier(int kind) {
  return IsTileKindRunway(kind) || kind == TK_PIER || kind == TK_CRANE;
}

// Stock train movement also treats the first house kind as a transition.
// Preserve that rule explicitly, independently of the connection's native ID.
static SC2K_FORCE_INLINE bool IsTileKindTrainTransition(int kind) {
  return kind == TK_SUBWAY_TO_RAIL || kind == TK_LOWER_CLASS_HOMES_FIRST;
}

static SC2K_FORCE_INLINE bool IsTileKindVariant(int kind) {
  return (ReadCityTileClasses(kind) & TCLASS_VARIANT) != 0;
}

static SC2K_FORCE_INLINE bool IsTileKindBridgeMiddle(int kind) {
  return (kind >= TK_ROAD_BRIDGE_SPAN && kind <= TK_HIGHWAY_BRIDGE_POWER) ||
      (kind >= TK_ROAD_BRIDGE_WATER && kind <= TK_HIGHWAY_BRIDGE_WATER);
}

static SC2K_FORCE_INLINE bool IsTileKindBridgeHead(int kind) {
  return kind >= TK_ROAD_BRIDGE_HEAD && kind <= TK_HIGHWAY_BRIDGE_HEAD;
}

static SC2K_FORCE_INLINE bool IsTileKindBridge(int kind) {
  return (ReadCityTileClasses(kind) & TCLASS_NATIVE_BRIDGE) != 0;
}

static SC2K_FORCE_INLINE bool IsTileKindHighwayBridge(int kind) {
  return (kind >= TK_HIGHWAY_BRIDGE_SPAN && kind <= TK_HIGHWAY_BRIDGE_POWER) ||
      kind == TK_HIGHWAY_BRIDGE_HEAD || kind == TK_HIGHWAY_BRIDGE_WATER;
}

static SC2K_FORCE_INLINE int CityBridgeHeadKind(int kind) {
  if (kind >= TK_ROAD_BRIDGE_WATER && kind <= TK_HIGHWAY_BRIDGE_WATER)
    return TK_ROAD_BRIDGE_HEAD + kind - TK_ROAD_BRIDGE_WATER;
  if (!IsTileKindBridgeMiddle(kind)) return kind;
  return TK_ROAD_BRIDGE_HEAD + ((kind - TK_ROAD_BRIDGE_SPAN) >> 2);
}

// Four ordinary highway families share axis and footprint metadata.
static SC2K_FORCE_INLINE bool IsTileKindStraightHighwayFamily(int kind) {
  return kind >= TK_HIGHWAY && kind <= TK_HIGHWAY_POWER;
}

static SC2K_FORCE_INLINE bool IsTileKindMultiHighway(int kind) {
  return kind == TK_HIGHWAY_INCLINE || kind == TK_HIGHWAY_BEND || kind == TK_HIGHWAY_JUNCTION;
}

static SC2K_FORCE_INLINE bool IsTileKindHighwayIncline(int kind) {
  return kind == TK_HIGHWAY_INCLINE;
}

static SC2K_FORCE_INLINE bool IsTileKindHighwayBendOrJunction(int kind) {
  return kind == TK_HIGHWAY_BEND || kind == TK_HIGHWAY_JUNCTION;
}

static SC2K_FORCE_INLINE bool IsTileKindHighwayFootprint(int kind) {
  return (ReadCityTileClasses(kind) & TCLASS_HIGHWAY) != 0;
}

static SC2K_FORCE_INLINE bool IsTileKindStraightHighway(int kind) {
  return IsTileKindStraightHighwayFamily(kind);
}

// Curves/junctions use both axes; ramps have their own directional variant.
static SC2K_FORCE_INLINE bool IsTileKindHighwayAxis(int kind) {
  return (ReadCityTileClasses(kind) & TCLASS_HIGHWAY_AXIS) != 0;
}

static SC2K_FORCE_INLINE bool IsTileKindSingleTileFootprint(int kind) {
  return (ReadCityTileClasses(kind) & TCLASS_SINGLE_FOOTPRINT) != 0;
}

static SC2K_FORCE_INLINE bool IsTileKindGroundCrossing(int kind) {
  return (ReadCityTileClasses(kind) & TCLASS_GROUND_CROSSING) != 0;
}

static SC2K_FORCE_INLINE bool IsTileKindPlainPowerLine(int kind) {
  return kind >= TK_POWER_LINE && kind <= TK_POWER_LINE_TOWER_WATER;
}

// These power-line properties depend only on the kind, not connection, altitude or service fields.
static SC2K_FORCE_INLINE bool IsTileKindWaterPowerLine(int kind) {
  return IsTileKindPlainPowerLine(kind) && (ReadCityTileClasses(kind) & TCLASS_WATER) != 0;
}

static SC2K_FORCE_INLINE bool IsTileKindPowerLineTower(int kind) {
  return kind == TK_POWER_LINE_TOWER || kind == TK_POWER_LINE_TOWER_WATER;
}

static SC2K_FORCE_INLINE bool IsTileKindPlainTransport(int kind) {
  return kind == TK_ROAD || kind == TK_RAIL;
}

// Native route families. Bridge decks/underpasses are resolved separately.
static SC2K_FORCE_INLINE bool IsTileKindRoadRoute(int kind) {
  return (ReadCityTileClasses(kind) & TCLASS_ROAD) != 0;
}

// Surface roads and rails retain ground cover, including beneath elevated highways.
static SC2K_FORCE_INLINE bool IsTileKindGroundTransport(int kind) {
  return kind == TK_ROAD || kind == TK_RAIL ||
      IsTileKindGroundCrossing(kind) ||
      kind == TK_HIGHWAY_ROAD || kind == TK_HIGHWAY_RAIL;
}

static SC2K_FORCE_INLINE bool IsTileKindRailRoute(int kind) {
  return (ReadCityTileClasses(kind) & TCLASS_RAIL) != 0;
}

static SC2K_FORCE_INLINE bool IsTileKindPowerLine(int kind) {
  return (ReadCityTileClasses(kind) & TCLASS_POWER) != 0;
}

static SC2K_FORCE_INLINE bool IsTileKindRailBridge(int kind) {
  return (ReadCityTileClasses(kind) & TCLASS_RAIL_BRIDGE) != 0;
}

static SC2K_FORCE_INLINE int CityTreeCount(int kind) {
  return IsTileKindShoreTree(kind) ? kind - TK_SHORE_TREE_FIRST + 1 :
      kind <= TK_TREE_8 ? 1 : kind <= TK_TREE_10 ? 2 : kind == TK_TREE_DENSE_FIRST ? 3 : 4;
}

static SC2K_FORCE_INLINE int CityTreeTile(int count) {
  return count == 1 ? TK_TREE_FIRST : count == 2 ? TK_TREE_MATURE_FIRST :
      count == 3 ? TK_TREE_DENSE_FIRST : TK_TREE_LAST;
}

#endif
