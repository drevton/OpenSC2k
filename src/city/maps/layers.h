#ifndef SC2K_SIMULATION_MAP_LAYERS_H
#define SC2K_SIMULATION_MAP_LAYERS_H

#include "city/infrastructure/types.h"
#include "city/maps/module.h"
#include "city/maps/terrain.h"
#include "city/maps/tile_storage.h"
#include "city/state.h"
#include <assert.h>

// Replace a valid tile's occupant and update ordinary/military counters plus category-specific metadata.
// Entering trees consumes one landscape RNG draw. Same-kind calls still perform category resets.
// Invalidates the old sprite; the caller finishes placement/flags/overlays and invalidates the final extents.
// Earlier metadata/elevation writes need their own pre-write invalidation.
void ReplaceCityTileAndStatistics(City *city, TileIndex tile, u8 kind);
// As above, followed by installation of an explicit two-bit variant.
void ReplaceCityTileVariant(City *city, TileIndex tile, int kind, int variant);

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

// Writes are plain assignments. Mutating operations invalidate tiles explicitly.
// All cell accessors use full-resolution city tiles. Coarse layers select the
// cell containing that tile; callers do not pass layer-relative indices.

// The destination is written only through this restricted pointer.
static SC2K_FORCE_INLINE void WriteByte(u8 *restrict destination, int value) {
  *destination = value;
}

// Section scans clear visited state for both utilities. Towers retain stored water.
void ClearCityUtilitySection(City *city, int section, bool water);

static SC2K_FORCE_INLINE u8 ReadTile(const City *city, TileIndex tile) {
  return city->tile[tile].kind;
}

static SC2K_FORCE_INLINE void WriteTile(City *city, TileIndex tile, int value) {
  WriteByte(&city->tile[tile].kind, value);
}

static SC2K_FORCE_INLINE int ReadCityZoneKind(const TileInfo *tile) {
  return tile->zone & CITY_TILE_SLOPED ? ZONE_NONE : tile->zone & CITY_ZONE_KIND_MASK;
}

static SC2K_FORCE_INLINE u8 ReadTerrainCode(const TileInfo *tile) {
  return tile->zone & CITY_TILE_SLOPED ? tile->zone & CITY_TERRAIN_SHAPE_MASK : CITY_TERRAIN_FLAT;
}

static SC2K_FORCE_INLINE void WriteTerrainCode(TileInfo *tile, int shape) {
  assert(shape >= 0 && shape <= CITY_TERRAIN_SHAPE_13);
  tile->zone = (tile->zone & CITY_TILE_GROWTH_MASK) |
      (shape ? CITY_TILE_SLOPED | shape : ReadCityZoneKind(tile));
}

static SC2K_FORCE_INLINE void WriteCityZoneKind(TileInfo *tile, int zone) {
  assert(!zone || !ReadTerrainCode(tile));
  tile->zone = (tile->zone & CITY_TILE_GROWTH_MASK) |
      (ReadTerrainCode(tile) ? tile->zone & 31 : zone & CITY_ZONE_KIND_MASK);
}

// Resolved zone and occupant metadata; the stored zone/slope discriminator is never exposed as a zone kind.
static SC2K_FORCE_INLINE u8 ReadTileZoneByte(const TileInfo *tile) {
  return (tile->terrain & CITY_ZONE_METADATA_MASK) | ReadCityZoneKind(tile);
}

static SC2K_FORCE_INLINE void WriteTileZoneByte(TileInfo *tile, int value) {
  WriteCityZoneKind(tile, value & CITY_ZONE_KIND_MASK);
  tile->terrain = (tile->terrain & 15) | (value & CITY_ZONE_METADATA_MASK);
}

static SC2K_FORCE_INLINE u8 ReadZoneByte(const City *city, TileIndex tile) {
  return city->native_map_storage ? ReadTileZoneByte(&city->tile[tile]) : city->tile[tile].zone;
}

// Permanent water is a property of the stored kind, including occupied water.
static SC2K_FORCE_INLINE bool HasTileWater(const TileInfo *tp) {
  return (ReadCityTileClasses(tp->kind) & TCLASS_WATER) != 0;
}

static SC2K_FORCE_INLINE CityWaterKind ReadTileWaterKind(const TileInfo *tp) {
  return !HasTileWater(tp) ? CITY_WATER_NONE : tp->flags & TF_SALTWATER ? CITY_WATER_SALT : CITY_WATER_FRESH;
}

static SC2K_FORCE_INLINE bool IsTileKindTree(int tile) {
  return (tile >= TK_TREE_FIRST && tile <= TK_TREE_LAST) || IsTileKindShoreTree(tile);
}

// Keep the few deliberate contiguous categories here. Callers describe what
// they need instead of relying on an unrelated kind as a numeric boundary.
static SC2K_FORCE_INLINE bool IsTileKindBuilding(int kind) {
  return kind >= TK_RESIDENTIAL_FIRST && kind != TK_PARK;
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
// and buildings belong to it. Stored transport kinds need not be adjacent.
static SC2K_FORCE_INLINE bool IsTileKindTransportOrBuilding(int kind) {
  return kind >= TK_ROAD && kind != TK_PARK;
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

// Retained algorithms write the resolved zone/metadata byte; unfinished stock imports still write raw XZON.
static SC2K_FORCE_INLINE void WriteZoneByte(City *city, TileIndex tile, int value) {
  if (city->native_map_storage) WriteTileZoneByte(&city->tile[tile], value);
  else city->tile[tile].zone = value;
}

static SC2K_FORCE_INLINE bool IsTileKindVariant(int kind) {
  return (ReadCityTileClasses(kind) & TCLASS_VARIANT) != 0;
}

static SC2K_FORCE_INLINE bool ReadCityHighwayRampMirror(const TileInfo *tile) {
  return (tile->terrain & CITY_HIGHWAY_RAMP_MIRROR_MASK) != 0;
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

// Every land highway shape stores the same two-bit orientation. Opposite
// orientations share an axis; only directional shapes distinguish their ends.
static SC2K_FORCE_INLINE int ReadCityHighwayOrientation(const TileInfo *tp) {
  return (tp->terrain & CITY_HIGHWAY_ORIENTATION_MASK) >> CITY_HIGHWAY_ORIENTATION_SHIFT;
}

static SC2K_FORCE_INLINE void WriteCityHighwayOrientation(TileInfo *tp, int orientation) {
  tp->terrain = (tp->terrain & ~CITY_HIGHWAY_ORIENTATION_MASK) |
      ((orientation & 3) << CITY_HIGHWAY_ORIENTATION_SHIFT);
}

static SC2K_FORCE_INLINE int ReadCityHighwayAxis(const TileInfo *tp) {
  return ReadCityHighwayOrientation(tp) & 1;
}

// Placement selectors describe a complete footprint, rather than stored IDs.
static SC2K_FORCE_INLINE int CityHighwayFootprintKind(int shape) {
  if (shape < CITY_TRANSPORT_FOOTPRINT_GRADED_4) return TK_HIGHWAY;
  if (shape < CITY_TRANSPORT_FOOTPRINT_MULTI_8) return TK_HIGHWAY_INCLINE;
  if (shape < CITY_TRANSPORT_FOOTPRINT_MULTI_12) return TK_HIGHWAY_BEND;
  return TK_HIGHWAY_JUNCTION;
}

static SC2K_FORCE_INLINE int CityHighwayFootprintOrientation(int shape) {
  if (shape < CITY_TRANSPORT_FOOTPRINT_GRADED_4) return shape & 1;
  if (shape < CITY_TRANSPORT_FOOTPRINT_MULTI_8) return (shape + 1) & 3;
  if (shape < CITY_TRANSPORT_FOOTPRINT_MULTI_12) return shape & 3;
  return 0;
}

static SC2K_FORCE_INLINE int ReadCityHighwayFootprintShape(const TileInfo *tp) {
  int orientation = ReadCityHighwayOrientation(tp);
  if (tp->kind == TK_HIGHWAY_INCLINE)
    return CITY_TRANSPORT_FOOTPRINT_GRADED_4 + ((orientation + 3) & 3);
  if (tp->kind == TK_HIGHWAY_BEND) return CITY_TRANSPORT_FOOTPRINT_MULTI_8 + orientation;
  return CITY_TRANSPORT_FOOTPRINT_MULTI_12;
}

static SC2K_FORCE_INLINE bool IsTileKindSingleTileFootprint(int kind) {
  return (ReadCityTileClasses(kind) & TCLASS_SINGLE_FOOTPRINT) != 0;
}

// Primary axis: road for road/power and road/rail, rail for rail/power.
// Zero is east/west, one is north/south. The second network is perpendicular.
static SC2K_FORCE_INLINE bool IsTileKindGroundCrossing(int kind) {
  return (ReadCityTileClasses(kind) & TCLASS_GROUND_CROSSING) != 0;
}

static SC2K_FORCE_INLINE int ReadCityCrossingAxis(const TileInfo *tile) {
  return (tile->terrain & CITY_CROSSING_AXIS_MASK) != 0;
}

static SC2K_FORCE_INLINE void WriteCityCrossingAxis(TileInfo *tile, int axis) {
  tile->terrain = (tile->terrain & ~CITY_CROSSING_AXIS_MASK) | (axis ? CITY_CROSSING_AXIS_MASK : 0);
}

static SC2K_FORCE_INLINE int ReadCityCrossingConnections(const TileInfo *tile) {
  return ReadCityCrossingAxis(tile) ? CONN_NS : CONN_EW;
}

// Ordinary road/rail/power payloads use the four cardinal connection bits.
static SC2K_FORCE_INLINE bool IsTileKindPlainPowerLine(int kind) {
  return kind >= TK_POWER_LINE && kind <= TK_POWER_LINE_TOWER_WATER;
}

static SC2K_FORCE_INLINE bool IsCityWaterPowerLine(const TileInfo *tile) {
  return IsTileKindPlainPowerLine(tile->kind) && HasTileWater(tile);
}

static SC2K_FORCE_INLINE bool CityPowerLineHasTower(const TileInfo *tile) {
  return tile->kind == TK_POWER_LINE_TOWER || tile->kind == TK_POWER_LINE_TOWER_WATER;
}

static SC2K_FORCE_INLINE bool IsTileKindPlainTransport(int kind) {
  return kind == TK_ROAD || kind == TK_RAIL;
}

static SC2K_FORCE_INLINE int ReadTransportConnections(const TileInfo *tile) {
  return tile->terrain >> CITY_ZONE_METADATA_SHIFT;
}

static SC2K_FORCE_INLINE void WriteTransportConnections(TileInfo *tile, int mask) {
  tile->terrain = (tile->terrain & CITY_ZONE_KIND_MASK) |
      mask << CITY_ZONE_METADATA_SHIFT;
}

// Underground classification also accepts an unnarrowed replacement value.
static SC2K_FORCE_INLINE bool IsSubwayTile(int kind) {
  return (kind >= TUK_SUBWAY_FIRST && kind <= TUK_SUBWAY_LAST) ||
      kind == TUK_SUBWAY_ACCESS || kind == TUK_SUBWAY_EW_PIPE_NS ||
      kind == TUK_SUBWAY_NS_PIPE_EW || kind == TUK_MISSILE_SILO;
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

static SC2K_FORCE_INLINE int ReadTileVariant(const TileInfo *tile) {
  return (tile->terrain & CITY_TILE_VARIANT_MASK) >> CITY_TILE_VARIANT_SHIFT;
}

// Ordinary flat line axis, or -1 for slopes, bends, junctions and rail transitions.
// Pass the family when crossing another network; zero accepts any ordinary line.
int ReadCityStraightLineAxis(const City *city, TileIndex tile, int family);
// Adjacent incline in north/south/west/east order, or -1.
int FindCityRailTransition(const City *city, TileIndex tile);
bool IsCityLineBendOrJunction(const City *city, TileIndex tile);

static SC2K_FORCE_INLINE bool IsTileKindPowerLine(int kind) {
  return (ReadCityTileClasses(kind) & TCLASS_POWER) != 0;
}

// Physical corner indices in the stored map, independent of camera rotation.
enum {
  CITY_BUILDING_CORNER_TOP_LEFT, CITY_BUILDING_CORNER_BOTTOM_LEFT,
  CITY_BUILDING_CORNER_BOTTOM_RIGHT, CITY_BUILDING_CORNER_TOP_RIGHT,
};

bool IsCityFootprintCorner(const City *city, TileIndex tile, int corner);

static SC2K_FORCE_INLINE int EncodeBuildingOrientation(int orientation) {
  return (orientation << CITY_BUILDING_ORIENTATION_SHIFT) & CITY_BUILDING_ORIENTATION_MASK;
}

static SC2K_FORCE_INLINE int DecodeBuildingOrientation(int zone) {
  return (zone & CITY_BUILDING_ORIENTATION_MASK) >> CITY_BUILDING_ORIENTATION_SHIFT;
}

// Native facing is relative to the current map. New buildings on non-building
// tiles start with the stock default facing in that map rotation.
static SC2K_FORCE_INLINE int ReadBuildingOrientation(const City *city, TileIndex tile) {
  return IsTileKindBuilding(ReadTile(city, tile)) ?
      DecodeBuildingOrientation(city->tile[tile].terrain) :
      city->geography.rotation;
}

// Decode native footprint markers in their tile-specific role.
int ReadFootprintCorners(const City *city, TileIndex tile);

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

static SC2K_FORCE_INLINE int ReadTileBridgeStyle(const TileInfo *tile) {
  return (tile->altitude >> CITY_ALTITUDE_WATER_SHIFT) & 15;
}

static SC2K_FORCE_INLINE void WriteTileBridgeStyle(TileInfo *tile, int style) {
  tile->altitude = (tile->altitude & ~0x1E0) | style << CITY_ALTITUDE_WATER_SHIFT;
}

static SC2K_FORCE_INLINE bool IsCityBridgeHead(const TileInfo *tile) {
  return IsTileKindBridgeHead(tile->kind);
}

static SC2K_FORCE_INLINE int ReadCityBridgeAxis(const TileInfo *tile) {
  return (tile->terrain & CITY_BRIDGE_AXIS_MASK) != 0;
}

static SC2K_FORCE_INLINE bool IsTileKindRailBridge(int kind) {
  return (ReadCityTileClasses(kind) & TCLASS_RAIL_BRIDGE) != 0;
}

// Tree-local species, 0..15, interpreted in the tile climate.
static SC2K_FORCE_INLINE int ReadTreeSpecies(const TileInfo *tile) {
  return tile->terrain >> CITY_ZONE_METADATA_SHIFT;
}

static SC2K_FORCE_INLINE void WriteTreeSpecies(TileInfo *tile, int species) {
  tile->terrain = (tile->terrain & CITY_ZONE_KIND_MASK) |
      ((species & CITY_ZONE_METADATA_MAX) << CITY_ZONE_METADATA_SHIFT);
}

static SC2K_FORCE_INLINE int ReadGroundRecoveryCounter(const TileInfo *tile) {
  return tile->zone >> CITY_TILE_GROWTH_SHIFT;
}

static SC2K_FORCE_INLINE void WriteGroundRecoveryCounter(TileInfo *tile, int counter) {
  tile->zone = (tile->zone & 31) | counter << CITY_TILE_GROWTH_SHIFT;
}

// Unbuilt includes rubble, radioactive waste and trees. Callers add any
// tool-specific restrictions on those kinds, zoning or underground contents.
static SC2K_FORCE_INLINE bool IsFlatDryUnbuiltTile(const TileInfo *tp) {
  return IsTileKindUnbuilt(tp->kind) && ReadTerrainCode(tp) == CITY_TERRAIN_FLAT &&
      !HasTileWater(tp);
}

enum {
  CITY_GROUND_GRASS = 0, CITY_GROUND_ROUGH = 5, CITY_GROUND_ROCKS = 6,
  CITY_TRANSPORT_GROUND_PAVED = CITY_GROUND_ROUGH,
  CITY_GROUND_RECOVERY_FIRST = 7, CITY_GROUND_RECOVERY_GRASS_FIRST = 8,
  CITY_GROUND_RECOVERY_LAST = 10,
  CITY_GROUND_SNOW_FIRST = 1, CITY_GROUND_SNOW_LAST = 4,
  CITY_GROUND_DESERT_HALF = 11, CITY_GROUND_DESERT_FULL = 12, CITY_GROUND_SHORE = 13,
  CITY_TREE_GROWING_1 = 0, CITY_TREE_GROWING_2 = 1, CITY_TREE_GROWING_3 = 2,
  CITY_TREE_GROWN = 3, CITY_TREE_DYING_1 = 4, CITY_TREE_DYING_2 = 5, CITY_TREE_DEAD = 6,
};

static SC2K_FORCE_INLINE bool CityClimateHasGroundCover(int climate) {
  return (climate >= CLIMATE_SNOW_25 && climate <= CLIMATE_SNOW_100) ||
      (climate >= CLIMATE_DESERT_50 && climate <= CLIMATE_DESERT_100) ||
      climate == CLIMATE_SNOW_TEMPERATE_ARCTIC_50 || climate == CLIMATE_SNOW_ARCTIC_TROPICAL_50;
}

// Exposed water stores its behavior in kind. Occupants retain that ground kind
// in terrain's low nibble so demolition restores the same spreading rule.
static SC2K_FORCE_INLINE int ReadCityWaterGroundKind(const TileInfo *tp) {
  return IsTileKindShoreTree(tp->kind) ? TK_WATER_BANK : IsTileKindExposedWater(tp->kind) ? tp->kind :
      HasTileWater(tp) ? (tp->terrain & 15) + TK_WATER : TK_CLEAR;
}

int ReadGroundKind(const TileInfo *tile);

void WriteGroundKind(TileInfo *tile, int kind);

// Resolve appearance from the permanent region and stored ground, without RNG or mutation.
int ReadCityTileBaseClimate(const City *city, const TileInfo *tile);
int ReadTileClimate(const City *city, const TileInfo *tile);
int ReadCityTileClimate(const City *city, TileIndex tile);
void WriteCityTileRegion(City *city, TileInfo *tile, int climate);
void RefreshCityTileGroundCover(City *city, TileIndex tile, bool immediate);

static SC2K_FORCE_INLINE int CityClimateSnowCoverage(int climate) {
  if (climate == CLIMATE_SNOW_TEMPERATE_ARCTIC_50 || climate == CLIMATE_SNOW_ARCTIC_TROPICAL_50) return 2;
  return climate >= CLIMATE_SNOW_25 && climate <= CLIMATE_SNOW_100 ? climate - CLIMATE_ARCTIC : 0;
}

static SC2K_FORCE_INLINE int ReadTileSnowCoverage(const TileInfo *tile) {
  int state = tile->terrain & 15;
  return (!HasTileWater(tile) || IsTileKindShoreBank(tile->kind)) && state >= 1 && state <= 4 ? state : 0;
}

void BeginCityGroundRecovery(City *city, TileInfo *tile);
void BeginCityBareGroundRecovery(City *city, TileInfo *tile);

static SC2K_FORCE_INLINE int ReadTreeGrowth(const TileInfo *tile) {
  return tile->zone >> CITY_TILE_GROWTH_SHIFT;
}

static SC2K_FORCE_INLINE void WriteTreeGrowth(TileInfo *tile, int growth) {
  tile->zone = (tile->zone & 31) | growth << CITY_TILE_GROWTH_SHIFT;
}

static SC2K_FORCE_INLINE int CityTreeCount(int tile) {
  return IsTileKindShoreTree(tile) ? tile - TK_SHORE_TREE_FIRST + 1 :
      tile <= 8 ? 1 : tile <= 10 ? 2 : tile == 11 ? 3 : 4;
}

static SC2K_FORCE_INLINE int CityTreeTile(int count) {
  return count == 1 ? TK_TREE_FIRST : count == 2 ? TK_TREE_MATURE_FIRST :
      count == 3 ? TK_TREE_DENSE_FIRST : TK_TREE_LAST;
}

static SC2K_FORCE_INLINE u8 ReadPollution(const City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  return city->half[(point.y >> 1) * (city->map_width >> 1) + (point.x >> 1)].pollution;
}

static SC2K_FORCE_INLINE void WritePollution(City *city, TileIndex tile, int value) {
  CityPoint point = MapTilePt(city, tile);
  WriteByte(&city->half[(point.y >> 1) * (city->map_width >> 1) + (point.x >> 1)].pollution, value);
}

static SC2K_FORCE_INLINE u8 ReadTrafficDensity(const City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  return city->half[(point.y >> 1) * (city->map_width >> 1) + (point.x >> 1)].traffic_density;
}

static SC2K_FORCE_INLINE void WriteTrafficDensity(City *city, TileIndex tile, int value) {
  CityPoint point = MapTilePt(city, tile);
  WriteByte(&city->half[(point.y >> 1) * (city->map_width >> 1) + (point.x >> 1)].traffic_density, value);
}

static SC2K_FORCE_INLINE u8 ReadCrime(const City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  return city->half[(point.y >> 1) * (city->map_width >> 1) + (point.x >> 1)].crime;
}

static SC2K_FORCE_INLINE void WriteCrime(City *city, TileIndex tile, int value) {
  CityPoint point = MapTilePt(city, tile);
  WriteByte(&city->half[(point.y >> 1) * (city->map_width >> 1) + (point.x >> 1)].crime, value);
}

static SC2K_FORCE_INLINE u8 ReadLandValue(const City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  return city->half[(point.y >> 1) * (city->map_width >> 1) + (point.x >> 1)].land_value;
}

static SC2K_FORCE_INLINE void WriteLandValue(City *city, TileIndex tile, int value) {
  CityPoint point = MapTilePt(city, tile);
  WriteByte(&city->half[(point.y >> 1) * (city->map_width >> 1) + (point.x >> 1)].land_value, value);
}

static SC2K_FORCE_INLINE u8 ReadDensity(const City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  return city->quarter[(point.y >> 2) * (city->map_width >> 2) + (point.x >> 2)].density;
}

static SC2K_FORCE_INLINE void WriteDensity(City *city, TileIndex tile, int value) {
  CityPoint point = MapTilePt(city, tile);
  WriteByte(&city->quarter[(point.y >> 2) * (city->map_width >> 2) + (point.x >> 2)].density, value);
}

static SC2K_FORCE_INLINE u8 ReadPoliceCoverage(const City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  return city->quarter[(point.y >> 2) * (city->map_width >> 2) + (point.x >> 2)].police_coverage;
}

static SC2K_FORCE_INLINE void WritePoliceCoverage(City *city, TileIndex tile, int value) {
  CityPoint point = MapTilePt(city, tile);
  WriteByte(&city->quarter[(point.y >> 2) * (city->map_width >> 2) + (point.x >> 2)].police_coverage, value);
}

static SC2K_FORCE_INLINE u8 ReadFireCoverage(const City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  return city->quarter[(point.y >> 2) * (city->map_width >> 2) + (point.x >> 2)].fire_coverage;
}

static SC2K_FORCE_INLINE void WriteFireCoverage(City *city, TileIndex tile, int value) {
  CityPoint point = MapTilePt(city, tile);
  WriteByte(&city->quarter[(point.y >> 2) * (city->map_width >> 2) + (point.x >> 2)].fire_coverage, value);
}

static SC2K_FORCE_INLINE u8 ReadDensityHistory(const City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  return city->quarter[(point.y >> 2) * (city->map_width >> 2) + (point.x >> 2)].density_history;
}

static SC2K_FORCE_INLINE void WriteDensityHistory(City *city, TileIndex tile, int value) {
  CityPoint point = MapTilePt(city, tile);
  WriteByte(&city->quarter[(point.y >> 2) * (city->map_width >> 2) + (point.x >> 2)].density_history, value);
}

static SC2K_FORCE_INLINE u8 ReadUnderground(const City *city, TileIndex tile) {
  return city->tile[tile].underground & CITY_UNDERGROUND_KIND_MASK;
}

static SC2K_FORCE_INLINE void WriteUnderground(City *city, TileIndex tile, int value) {
  TileInfo *tp = &city->tile[tile];
  WriteByte(&tp->underground, !(city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE)) ? value :
      (tp->underground & CITY_UNDERGROUND_METADATA_MASK) | (value & CITY_UNDERGROUND_KIND_MASK));
}

static SC2K_FORCE_INLINE u16 ReadAltitudeWord(const City *city, TileIndex tile) {
  return city->tile[tile].altitude;
}

static SC2K_FORCE_INLINE void WriteAltitudeWord(City *city, TileIndex tile, int value) {
  city->tile[tile].altitude = value;
}

// Read the bed's minimum height, excluding water level and construction progress.
static SC2K_FORCE_INLINE int ReadCityLandHeight(const TileInfo *tp) {
  return tp->altitude & CITY_ALTITUDE_LAND_MASK;
}

static SC2K_FORCE_INLINE bool CityTileCanStoreBuildingStage(const TileInfo *tile) {
  return IsTileKindBuilding(tile->kind) && !HasTileWater(tile);
}

// Use this for elevation calculations unless the caller already knows that the
// tile contains real water. A construction stage must never become a height.
static SC2K_FORCE_INLINE int ReadCityWaterHeight(const TileInfo *tile) {
  int altitude = tile->altitude;
  return CityTileCanStoreBuildingStage(tile) || IsCityBridgeHead(tile) ? altitude & CITY_ALTITUDE_LAND_MASK :
      (altitude & CITY_ALTITUDE_WATER_MASK) >> CITY_ALTITUDE_WATER_SHIFT;
}

// Read the water plane on wet tiles and the land height on dry tiles.
// Building stages and stale water bits never contribute to the surface height.
static SC2K_FORCE_INLINE int ReadCitySurfaceHeight(const TileInfo *tp) {
  return HasTileWater(tp) ? ReadCityWaterHeight(tp) : ReadCityLandHeight(tp);
}

static inline CityHistoryRow *GetCityHistoryRow(City *city, int series) { return &city->history[series]; }

static inline u32 ReadHistorySample(const City *city, int series, int index) {
  return city->history[series].sample[index];
}

static inline void WriteHistorySample(City *city, int series, int index, u32 value) {
  city->history[series].sample[index] = value;
}

static inline int ReadScratch(const City *city, int x, int y) {
  const i16 *cells = (const i16 *)city->scratch;
  return cells[y * city->map_width + x];
}

static inline void WriteScratch(City *city, int x, int y, int value) {
  city->scratch[y * city->map_width + x] = value;
}

#endif
