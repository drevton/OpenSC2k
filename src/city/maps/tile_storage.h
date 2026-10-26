#ifndef SC2K_CITY_MAPS_TILE_STORAGE_H
#define SC2K_CITY_MAPS_TILE_STORAGE_H

#include "base/types.h"
#include <assert.h>

// Native TileInfo storage. Bit numbers are zero-based; stock file bytes are translated at the codec boundary.
// kind[0..7]: one TK_ value, not a sprite ID or a bitset. Water presence comes from the kind's TCLASS_WATER.
// altitude[0..4]: minimum land/bed height, 0..31 map levels, not pixels.
// altitude[5..9]: water plane on wet tiles; construction stage on dry building kinds; see bridges.c for heads.
// altitude[10..14]: road-tunnel depth plus one, with zero meaning no tunnel. altitude[15] is an opaque saved flag.
// zone[4]: slope discriminator. If clear, zone[0..3] is ZONE_; if set, it is slope shape 1..13 and zoning is absent.
// zone[5..7]: family payload: clear-ground recovery counter, tree growth stage, or wind-turbine form in bit 5.
// terrain[0..3]: dry ground/coverage state; on wet occupants, underlying water kind minus TK_WATER (0..2).
// Exposed TK_WATER/TK_RIVER/TK_WATER_BANK carry that choice in kind and do not need the low terrain payload.
// terrain[4..7]: family payload: connections, species, orientation, footprint links or appearance; see module comments.
// flags[0]: salinity; flags[1..2]: permanent region, interpreted through City's landscape preset.
// flags[3]: shared utility traversal mark; flags[4]: supplied water (also stored water in a water tower).
// flags[5]: pipe capability; flags[6]: supplied power; flags[7]: power capability. None encodes water presence.
// overlay[0]: sign present. overlay[1..4]: stationary effect, 0 none, otherwise original overlay value minus 240.
// Used effect codes: 10 border connection, 11 pollution, 12 flood, 13/14 riots, 15 fire. overlay[5..7] are unassigned.
// Sign/facility identities and moving event stacks live outside TileInfo; overlay is not a stock XTXT identity.
// underground[0..5]: TUK_ infrastructure ID. underground[6..7]: opaque saved metadata, not surface metadata;
// native-landscape writes preserve it, while compatibility-mode writes can replace the entire underground byte.
// Fields not assigned a family-specific role keep these shared meanings; module comments describe the exceptions.

typedef struct TileInfo {
  u16 altitude;
  u8 kind, zone, terrain, overlay, flags, underground;
} TileInfo;
_Static_assert(sizeof(TileInfo) == 8 && offsetof(TileInfo, altitude) == 0 && offsetof(TileInfo, kind) == 2 &&
    offsetof(TileInfo, zone) == 3 && offsetof(TileInfo, terrain) == 4 && offsetof(TileInfo, overlay) == 5 &&
    offsetof(TileInfo, flags) == 6 && offsetof(TileInfo, underground) == 7, "native tile record has eight field bytes");

// Native surface kinds are independent of artwork and stock file IDs.
// Rubble and entrances store appearance/facing in terrain metadata; hydro plants use kind 198 and terrain facing.
// Remaining variant IDs belong to codecs/artwork.
// Underground IDs use a separate numbering.
typedef enum TileKind {
  TK_CLEAR = 0,
  TK_RUBBLE = 1,
  // Exposed water: ordinary water spreads; rivers remain in their channel.
  // River tiles draw their own banks; a sloped river at water level is a rapid.
  TK_WATER = 2,
  TK_RIVER = 3,
  TK_WATER_BANK = 4,
  TK_RADIOACTIVE_WASTE = 5,
  TK_TREE_FIRST = 6,
  TK_TREE_7 = 7,
  TK_TREE_8 = 8,
  TK_TREE_MATURE_FIRST = 9,
  TK_TREE_10 = 10,
  // The original tree tool selects this densest pair after successive upgrades.
  TK_TREE_DENSE_FIRST = 11,
  TK_TREE_LAST = 12,
  TK_TREE_DENSE_LAST = 12,
  // Native power kinds distinguish cable-only tiles from tiles with a tower.
  // Both use the upper terrain nibble for cardinal connections, on land or water.
  // Directional stock IDs are declared in city/saveload/map_format.h.
  TK_POWER_LINE = 13,
  TK_POWER_LINE_TOWER = 14,
  TK_POWER_LINE_WATER = 15,
  TK_POWER_LINE_TOWER_WATER = 16,
  TK_ROAD = 17,
  TK_RAIL = 18,
  // One kind per ground crossing. Terrain bit 4 selects the road axis, or rail
  // axis for rail/power; the other network is perpendicular.
  TK_ROAD_POWER_CROSSING = 19,
  TK_ROAD_RAIL_CROSSING = 20,
  TK_RAIL_POWER_CROSSING = 21,
  TK_HIGHWAY_ENTRANCE = 22,
  // Highway shape and orientation are independent. Terrain bits 4–5 store orientation.
  TK_HIGHWAY_INCLINE = 23,
  TK_HIGHWAY_BEND = 24,
  TK_HIGHWAY_JUNCTION = 25,
  TK_SUBWAY_TO_RAIL = 26,
  // All land highways store orientation in terrain bits 4–5 and footprint links in bits 6–7.
  TK_HIGHWAY = 27,
  TK_HIGHWAY_RAIL = 28,
  TK_HIGHWAY_ROAD = 29,
  TK_HIGHWAY_POWER = 30,
  TK_TUNNEL_ENTRANCE = 31,
  // Four-aligned middle groups encode clear, rail, road or power underneath in the low two bits.
  // The three consecutive heads immediately follow the middle groups.
  TK_ROAD_BRIDGE_SPAN = 32,
  TK_ROAD_BRIDGE_RAIL = 33,
  TK_ROAD_BRIDGE_ROAD = 34,
  TK_ROAD_BRIDGE_POWER = 35,
  TK_RAIL_BRIDGE_SPAN = 36,
  TK_RAIL_BRIDGE_RAIL = 37,
  TK_RAIL_BRIDGE_ROAD = 38,
  TK_RAIL_BRIDGE_POWER = 39,
  TK_HIGHWAY_BRIDGE_SPAN = 40,
  TK_HIGHWAY_BRIDGE_RAIL = 41,
  TK_HIGHWAY_BRIDGE_ROAD = 42,
  TK_HIGHWAY_BRIDGE_POWER = 43,
  TK_ROAD_BRIDGE_HEAD = 44,
  TK_RAIL_BRIDGE_HEAD = 45,
  TK_HIGHWAY_BRIDGE_HEAD = 46,
  TK_ROAD_BRIDGE_WATER = 47,
  TK_RAIL_BRIDGE_WATER = 48,
  TK_HIGHWAY_BRIDGE_WATER = 49,
  // Trees on the raised part of a wet shoreline retain bank water and one through four trees.
  TK_SHORE_TREE_FIRST = 50,
  TK_SHORE_TREE_2 = 51,
  TK_SHORE_TREE_3 = 52,
  TK_SHORE_TREE_LAST = 53,
  TK_RESIDENTIAL_FIRST = 112,
  TK_LOWER_CLASS_HOMES_FIRST = 112,
  TK_LOWER_CLASS_HOMES_113 = 113,
  TK_LOWER_CLASS_HOMES_114 = 114,
  TK_LOWER_CLASS_HOMES_LAST = 115,
  TK_MIDDLE_CLASS_HOMES_FIRST = 116,
  TK_MIDDLE_CLASS_HOMES_117 = 117,
  TK_MIDDLE_CLASS_HOMES_118 = 118,
  TK_MIDDLE_CLASS_HOMES_LAST = 119,
  TK_LUXURY_HOMES_FIRST = 120,
  TK_LUXURY_HOMES_121 = 121,
  TK_LUXURY_HOMES_122 = 122,
  TK_LUXURY_HOMES_LAST = 123,
  TK_GAS_STATION_1X1_1 = 124,
  TK_BED_BREAKFAST_INN = 125,
  TK_CONVENIENCE_STORE = 126,
  TK_GAS_STATION_1X1_2 = 127,
  TK_SMALL_OFFICE_BUILDING = 128,
  TK_OFFICE_BUILDING_1X1 = 129,
  TK_WAREHOUSE_1X1_1 = 130,
  TK_CASSIDY_S_TOY_STORE = 131,
  TK_WAREHOUSE_1X1_2 = 132,
  TK_CHEMICAL_STORAGE = 133,
  TK_WAREHOUSE_1X1_3 = 134,
  TK_INDUSTRIAL_SUBSTATION = 135,
  TK_CONSTRUCTION_1X1_1 = 136,
  TK_CONSTRUCTION_1X1_2 = 137,
  TK_ABANDONED_BUILDING_1X1_1 = 138,
  TK_ABANDONED_BUILDING_1X1_2 = 139,
  TK_CHEAP_APARTMENTS = 140,
  TK_APARTMENTS_2X2_1 = 141,
  TK_APARTMENTS_2X2_2 = 142,
  TK_NICE_APARTMENTS_2X2_1 = 143,
  TK_NICE_APARTMENTS_2X2_2 = 144,
  TK_CONDOMINIUM_2X2_1 = 145,
  TK_CONDOMINIUM_2X2_2 = 146,
  TK_CONDOMINIUM_2X2_3 = 147,
  TK_SHOPPING_CENTER = 148,
  TK_GROCERY_STORE = 149,
  TK_OFFICE_BUILDING_2X2_1 = 150,
  TK_RESORT_HOTEL = 151,
  TK_OFFICE_BUILDING_2X2_2 = 152,
  TK_OFFICE_RETAIL = 153,
  TK_OFFICE_BUILDING_2X2_3 = 154,
  TK_OFFICE_BUILDING_2X2_4 = 155,
  TK_OFFICE_BUILDING_2X2_5 = 156,
  TK_OFFICE_BUILDING_2X2_6 = 157,
  TK_WAREHOUSE_2X2 = 158,
  TK_CHEMICAL_PROCESSING_2X2 = 159,
  TK_FACTORY_2X2_1 = 160,
  TK_FACTORY_2X2_2 = 161,
  TK_FACTORY_2X2_3 = 162,
  TK_FACTORY_2X2_4 = 163,
  TK_FACTORY_2X2_5 = 164,
  TK_FACTORY_2X2_6 = 165,
  TK_CONSTRUCTION_2X2_1 = 166,
  TK_CONSTRUCTION_2X2_2 = 167,
  TK_CONSTRUCTION_2X2_3 = 168,
  TK_CONSTRUCTION_2X2_4 = 169,
  TK_ABANDONED_BUILDING_2X2_1 = 170,
  TK_ABANDONED_BUILDING_2X2_2 = 171,
  TK_ABANDONED_BUILDING_2X2_3 = 172,
  TK_ABANDONED_BUILDING_2X2_4 = 173,
  TK_LARGE_APARTMENT_BUILDING_3X3_1 = 174,
  TK_LARGE_APARTMENT_BUILDING_3X3_2 = 175,
  TK_CONDOMINIUM_3X3_1 = 176,
  TK_CONDOMINIUM_3X3_2 = 177,
  TK_OFFICE_PARK = 178,
  TK_OFFICE_TOWER_3X3_1 = 179,
  TK_MINI_MALL = 180,
  TK_THEATRE_SQUARE = 181,
  TK_DRIVE_IN_THEATER = 182,
  TK_OFFICE_TOWER_3X3_2 = 183,
  TK_OFFICE_TOWER_3X3_3 = 184,
  TK_PARKING_LOT = 185,
  TK_HISTORIC_OFFICE_BUILDING = 186,
  TK_CORPORATE_HEADQUARTERS = 187,
  TK_CHEMICAL_PROCESSING_3X3 = 188,
  TK_LARGE_FACTORY = 189,
  TK_INDUSTRIAL_THINGAMAJIG = 190,
  TK_FACTORY_3X3 = 191,
  TK_LARGE_WAREHOUSE = 192,
  TK_WAREHOUSE_3X3 = 193,
  TK_CONSTRUCTION_3X3_1 = 194,
  TK_CONSTRUCTION_3X3_2 = 195,
  TK_ABANDONED_BUILDING_3X3_1 = 196,
  TK_ABANDONED_BUILDING_3X3_2 = 197,
  TK_HYDRO_POWER = 198,
  TK_WIND_POWER = 200,
  TK_GAS_POWER_PLANT = 201,
  TK_OIL_POWER_PLANT = 202,
  TK_NUCLEAR_POWER_PLANT = 203,
  TK_SOLAR_POWER_PLANT = 204,
  TK_MICROWAVE_POWER_PLANT = 205,
  TK_FUSION_POWER_PLANT = 206,
  TK_COAL_POWER_PLANT = 207,
  TK_CITY_HALL = 208,
  TK_HOSPITAL = 209,
  TK_POLICE_STATION = 210,
  TK_FIRE_STATION = 211,
  TK_MUSEUM = 212,
  // Reuse the retired second hydro artwork slot without renumbering any building.
  TK_PARK = 199,
  TK_LARGE_PARK = 213,
  TK_SCHOOL = 214,
  TK_STADIUM = 215,
  TK_PRISON = 216,
  TK_COLLEGE = 217,
  TK_ZOO = 218,
  TK_STATUE = 219,
  TK_WATER_PUMP = 220,
  TK_RUNWAY_FIRST = 221,
  TK_RUNWAY_LAST = 222,
  TK_PIER = 223,
  TK_CRANE = 224,
  TK_CONTROL_TOWER_1X1_1 = 225,
  TK_CONTROL_TOWER_1X1_2 = 226,
  TK_WAREHOUSE_1X1_4 = 227,
  TK_BUILDING_1X1_1 = 228,
  TK_BUILDING_1X1_2 = 229,
  TK_TARMAC = 230,
  TK_F_15B = 231,
  TK_HANGAR_1X1 = 232,
  TK_SUBWAY_STATION = 233,
  TK_RADAR = 234,
  TK_WATER_TOWER = 235,
  TK_BUS_STATION = 236,
  TK_RAIL_STATION = 237,
  TK_PARKING_LOT_2X2_1 = 238,
  TK_PARKING_LOT_2X2_2 = 239,
  TK_LOADING_BAY = 240,
  TK_TOP_SECRET = 241,
  TK_CARGO_YARD = 242,
  TK_MAYORS_HOUSE = 243,
  TK_WATER_TREATMENT_PLANT = 244,
  TK_LIBRARY = 245,
  TK_HANGAR_3X3 = 246,
  TK_CHURCH = 247,
  TK_MARINA = 248,
  TK_MISSILE_SILO = 249,
  TK_DESALINATION_PLANT = 250,
  TK_PLYMOUTH_ARCOLOGY = 251,
  TK_FOREST_ARCOLOGY = 252,
  TK_DARCO_ARCOLOGY = 253,
  TK_LAUNCH_ARCOLOGY = 254,
  TK_BRAUN_LLAMA_DOME = 255,
  TK_COUNT = 256,
} TileKind;

// Road/rail bridge-head altitude[5..8]. These are saved values, independent of chooser order and artwork IDs.
typedef enum BridgeStyle {
  BRIDGE_STYLE_WOOD = 0,
  BRIDGE_STYLE_CONCRETE = 1,
  BRIDGE_STYLE_ARCH_STEEL = 2,
  BRIDGE_STYLE_SUSPENSION_CONCRETE = 3,
  BRIDGE_STYLE_SUSPENSION_STEEL = 4,
  BRIDGE_STYLE_SUSPENSION_YELLOW = 5,
  BRIDGE_STYLE_CANTILEVER_STEEL = 6,
  BRIDGE_STYLE_CANTILEVER_BROWN = 7,
  BRIDGE_STYLE_CANTILEVER_RED = 8,
  BRIDGE_STYLE_GIRDER_STEEL = 9,
  BRIDGE_STYLE_TUBULAR_STEEL = 10,
  BRIDGE_STYLE_TUBULAR_YELLOW = 11,
  BRIDGE_STYLE_TUBULAR_SILICON = 12,
  BRIDGE_STYLE_DRAWBRIDGE = 13,
  BRIDGE_STYLE_COUNT = 14,
} BridgeStyle;
_Static_assert(BRIDGE_STYLE_COUNT <= 16, "road/rail bridge style fits the head's four-bit field");

// Highway heads use the same altitude field with their own meaning; these are not road/rail styles.
typedef enum HighwayBridgeStyle {
  HIGHWAY_BRIDGE_STYLE_STANDARD = 1,
  HIGHWAY_BRIDGE_STYLE_REINFORCED = 2,
} HighwayBridgeStyle;
_Static_assert(HIGHWAY_BRIDGE_STYLE_REINFORCED < 16, "highway bridge style fits the head's four-bit field");

_Static_assert(TK_HIGHWAY_RAIL == TK_HIGHWAY + 1 && TK_HIGHWAY_ROAD == TK_HIGHWAY + 2 &&
    TK_HIGHWAY_POWER == TK_HIGHWAY + 3, "native highway kind layout");
_Static_assert(TK_RESIDENTIAL_FIRST == 112 && TK_HYDRO_POWER == 198 && TK_WIND_POWER == 200 &&
    TK_LARGE_PARK == 213 && TK_BRAUN_LLAMA_DOME == 255, "established building IDs stay fixed");

// Bridge predicates and family/underpass arithmetic depend on this layout.
// Stock artwork IDs are independent and must pass through the map codecs.
_Static_assert((TK_ROAD_BRIDGE_SPAN & 3) == 0, "bridge groups must be four-aligned");
_Static_assert(TK_ROAD_BRIDGE_RAIL == TK_ROAD_BRIDGE_SPAN + 1 &&
    TK_ROAD_BRIDGE_ROAD == TK_ROAD_BRIDGE_SPAN + 2 &&
    TK_ROAD_BRIDGE_POWER == TK_ROAD_BRIDGE_SPAN + 3, "road bridge underpass layout");
_Static_assert(TK_RAIL_BRIDGE_SPAN == TK_ROAD_BRIDGE_SPAN + 4 &&
    TK_RAIL_BRIDGE_RAIL == TK_RAIL_BRIDGE_SPAN + 1 &&
    TK_RAIL_BRIDGE_ROAD == TK_RAIL_BRIDGE_SPAN + 2 &&
    TK_RAIL_BRIDGE_POWER == TK_RAIL_BRIDGE_SPAN + 3, "rail bridge underpass layout");
_Static_assert(TK_HIGHWAY_BRIDGE_SPAN == TK_ROAD_BRIDGE_SPAN + 8 &&
    TK_HIGHWAY_BRIDGE_RAIL == TK_HIGHWAY_BRIDGE_SPAN + 1 &&
    TK_HIGHWAY_BRIDGE_ROAD == TK_HIGHWAY_BRIDGE_SPAN + 2 &&
    TK_HIGHWAY_BRIDGE_POWER == TK_HIGHWAY_BRIDGE_SPAN + 3, "highway bridge underpass layout");
_Static_assert(TK_ROAD_BRIDGE_HEAD == TK_ROAD_BRIDGE_SPAN + 12 &&
    TK_RAIL_BRIDGE_HEAD == TK_ROAD_BRIDGE_HEAD + 1 &&
    TK_HIGHWAY_BRIDGE_HEAD == TK_ROAD_BRIDGE_HEAD + 2,
    "bridge heads must follow the three middle groups in family order");
_Static_assert(TK_ROAD_BRIDGE_SPAN > TK_HIGHWAY_POWER &&
    TK_HIGHWAY_BRIDGE_HEAD < TK_RESIDENTIAL_FIRST,
    "dry bridge groups must stay between ordinary highway and building kinds");
_Static_assert(TK_RAIL_BRIDGE_WATER == TK_ROAD_BRIDGE_WATER + 1 &&
    TK_HIGHWAY_BRIDGE_WATER == TK_ROAD_BRIDGE_WATER + 2, "water bridge families must be consecutive");

// Low nibble of the zone byte. The upper nibble holds tile-specific metadata.
typedef enum ZoneKind {
  ZONE_NONE = 0,
  ZONE_RESIDENTIAL_LOW = 1,
  ZONE_RESIDENTIAL_HIGH = 2,
  ZONE_COMMERCIAL_LOW = 3,
  ZONE_COMMERCIAL_HIGH = 4,
  ZONE_INDUSTRIAL_LOW = 5,
  ZONE_INDUSTRIAL_HIGH = 6,
  ZONE_MILITARY = 7,
  ZONE_AIRPORT = 8,
  ZONE_SEAPORT = 9,
  ZONE_RESERVED_FIRST = 10
} ZoneKind;

enum {
  CITY_ZONE_KIND_MASK = 0x0F,
  // Resolved zone-byte metadata comes from terrain's high nibble in native tiles.
  // Stock codecs use these positions for their original zone/corner payload.
  CITY_ZONE_METADATA_MASK = 0xF0,
  CITY_ZONE_METADATA_SHIFT = 4,
  CITY_ZONE_METADATA_MAX = 15,
};

// Native building payload in terrain. Links point toward the current top-left
// footprint anchor; facing is relative to the current map coordinates.
enum {
  CITY_BUILDING_CONTINUE_X = 0x40, CITY_BUILDING_CONTINUE_Y = 0x80,
  CITY_BUILDING_CONTINUATION_MASK = 0xC0,
  CITY_BUILDING_ORIENTATION_MASK = 0x30, CITY_BUILDING_ORIENTATION_SHIFT = 4,
  CITY_BUILDING_ORIENTATION_AXIS = 0x10,
  CITY_CROSSING_AXIS_MASK = 0x10,
  CITY_HIGHWAY_ORIENTATION_MASK = 0x30, CITY_HIGHWAY_ORIENTATION_SHIFT = 4,
  CITY_HIGHWAY_AXIS_X = 0, CITY_HIGHWAY_AXIS_Y = 1,
};

static SC2K_FORCE_INLINE int EncodeBuildingOrientation(int orientation) {
  return (orientation << CITY_BUILDING_ORIENTATION_SHIFT) & CITY_BUILDING_ORIENTATION_MASK;
}

static SC2K_FORCE_INLINE int DecodeBuildingOrientation(int metadata) {
  return (metadata & CITY_BUILDING_ORIENTATION_MASK) >> CITY_BUILDING_ORIENTATION_SHIFT;
}

// Native rubble appearance and entrance direction share a two-bit terrain payload.
// Highway ramps additionally store their mirror state in terrain bit 6.
enum {
  CITY_TILE_VARIANT_MASK = 0x30, CITY_TILE_VARIANT_SHIFT = 4,
  CITY_HIGHWAY_RAMP_MIRROR_MASK = 0x40,
};

static SC2K_FORCE_INLINE int ReadTileVariant(const TileInfo *tp) {
  return (tp->terrain & CITY_TILE_VARIANT_MASK) >> CITY_TILE_VARIANT_SHIFT;
}

// XUND IDs: the shared connection table uses west/south/east/north
// mask bits 1/2/4/8. Unlike XBLD, these IDs describe underground infrastructure.
// 31/32 combine the perpendicular subway/pipe straights. 34 is written by
// missile-silo placement; 35 by both subway stations and subway-to-rail access.
// The original subway predicates also include silo cells; retain that behavior.
typedef enum TileUndergroundKind {
  TUK_CLEAR = 0,
  TUK_SUBWAY_FIRST = 1,
  TUK_SUBWAY_EW = 1,
  TUK_SUBWAY_NS = 2,
  TUK_SUBWAY_SLOPE_FIRST = 3,
  TUK_SUBWAY_SLOPE_LAST = 6,
  TUK_SUBWAY_SW = 7,
  TUK_SUBWAY_SE = 8,
  TUK_SUBWAY_NE = 9,
  TUK_SUBWAY_NW = 10,
  TUK_SUBWAY_NSW = 11,
  TUK_SUBWAY_ESW = 12,
  TUK_SUBWAY_NES = 13,
  TUK_SUBWAY_NEW = 14,
  TUK_SUBWAY_NESW = 15,
  TUK_SUBWAY_LAST = 15,
  TUK_PIPE_FIRST = 16,
  TUK_PIPE_EW = 16,
  TUK_PIPE_NS = 17,
  TUK_PIPE_SLOPE_FIRST = 18,
  TUK_PIPE_SLOPE_LAST = 21,
  TUK_PIPE_SW = 22,
  TUK_PIPE_SE = 23,
  TUK_PIPE_NE = 24,
  TUK_PIPE_NW = 25,
  TUK_PIPE_NSW = 26,
  TUK_PIPE_ESW = 27,
  TUK_PIPE_NES = 28,
  TUK_PIPE_NEW = 29,
  TUK_PIPE_NESW = 30,
  TUK_PIPE_LAST = 30,
  TUK_SUBWAY_EW_PIPE_NS = 31,
  TUK_SUBWAY_NS_PIPE_EW = 32,
  TUK_MISSILE_SILO = 34,
  TUK_SUBWAY_ACCESS = 35
} TileUndergroundKind;

// XUND infrastructure IDs occupy six bits. The upper two bits are independent
// persistent tile metadata, including when the infrastructure ID is clear.
enum { CITY_UNDERGROUND_KIND_MASK = 0x3F, CITY_UNDERGROUND_METADATA_MASK = 0xC0 };

// Return the stored infrastructure kind without the independent upper metadata bits.
static SC2K_FORCE_INLINE u8 ReadTileUndergroundKind(const TileInfo *tp) {
  return tp->underground & CITY_UNDERGROUND_KIND_MASK;
}

// Permanent region occupies bits 1..2. Stock orientation and water presence are codec-only flags.
typedef enum TileFlags {
  TF_SALTWATER = 0x01,
  TF_REGION_MASK = 0x06,
  TF_VISITED = 0x08,
  TF_WATERED = 0x10,
  TF_PIPED = 0x20,
  TF_POWERED = 0x40,
  TF_POWERABLE = 0x80,
  TF_WATER_SERVICE = TF_WATERED | TF_PIPED,
  TF_POWER_SERVICE = TF_POWERED | TF_POWERABLE,
  TF_UTILITY_CAPABILITY = TF_PIPED | TF_POWERED | TF_POWERABLE,
} TileFlags;

// Native road, rail and power connections occupy the upper terrain nibble.
// Stored bits use west/south/east/north order, independently of event headings.
typedef enum TileConnMask {
  CONN_NONE = 0,
  CONN_WEST = 0x01,
  CONN_SOUTH = 0x02,
  CONN_EAST = 0x04,
  CONN_NORTH = 0x08,
  CONN_WS = 0x03, CONN_SE = 0x06,
  CONN_NE = 0x0C, CONN_NW = 0x09,
  CONN_EW = 0x05,
  CONN_NS = 0x0A,
  CONN_ALL = 0x0F,
} TileConnMask;

// Upper zone/slope timing payload on wind-power buildings only.
enum { CITY_WIND_MODERN_MASK = 0x20 };
enum { CITY_TILE_SLOPED = 0x10, CITY_TILE_GROWTH_MASK = 0xE0, CITY_TILE_GROWTH_SHIFT = 5 };

// Ground/coverage in terrain's low nibble. Transport paving shares the rough-ground value.
typedef enum TileGroundKind {
  CITY_GROUND_GRASS = 0,
  CITY_GROUND_SNOW_FIRST = 1, CITY_GROUND_SNOW_LAST = 4,
  CITY_GROUND_ROUGH = 5, CITY_TRANSPORT_GROUND_PAVED = CITY_GROUND_ROUGH,
  CITY_GROUND_ROCKS = 6,
  CITY_GROUND_RECOVERY_FIRST = 7, CITY_GROUND_RECOVERY_GRASS_FIRST = 8,
  CITY_GROUND_RECOVERY_LAST = 10,
  CITY_GROUND_DESERT_HALF = 11, CITY_GROUND_DESERT_FULL = 12, CITY_GROUND_SHORE = 13,
} TileGroundKind;

// Tree growth in zone bits 5..7. Clear ground uses those bits for its recovery counter instead.
typedef enum TileTreeGrowth {
  CITY_TREE_GROWING_1 = 0, CITY_TREE_GROWING_2 = 1, CITY_TREE_GROWING_3 = 2,
  CITY_TREE_GROWN = 3, CITY_TREE_DYING_1 = 4, CITY_TREE_DYING_2 = 5, CITY_TREE_DEAD = 6,
} TileTreeGrowth;

// ALTM tunnel-depth field after shifting, distinct from XUND infrastructure.
typedef enum CityTunnelDepth { CITY_TUNNEL_DEPTH_NONE = 0, CITY_TUNNEL_DEPTH_ENTRANCE = 1 } CityTunnelDepth;

// Derived landscape appearance. These values are not stored in a tile byte.
typedef enum TileClimate {
  CLIMATE_SIMCITY = 0, CLIMATE_TEMPERATE = 1, CLIMATE_ARCTIC = 2,
  CLIMATE_SNOW_25 = 3, CLIMATE_SNOW_50 = 4,
  CLIMATE_SNOW_75 = 5, CLIMATE_SNOW_100 = 6,
  CLIMATE_TROPICAL = 7, CLIMATE_RAINFOREST = 8,
  CLIMATE_DESERT_50 = 9, CLIMATE_DESERT_100 = 10,
  CLIMATE_TEMPERATE_ARCTIC = 11, CLIMATE_TEMPERATE_TROPICAL = 12,
  CLIMATE_ARCTIC_TROPICAL = 13,
  CLIMATE_SNOW_TEMPERATE_ARCTIC_50 = 14, CLIMATE_SNOW_ARCTIC_TROPICAL_50 = 15,
  CLIMATE_COUNT = 16,
} TileClimate;
_Static_assert(CLIMATE_COUNT == 16, "tree tool climate options fit four bits");

// Stock XTXT IDs also identify resolved overlay categories. Native tiles store the independent bits below.
// Labels 251..255 in XLAB are team names, not these effect types.
typedef enum CityMapOverlay {
  CITY_OVERLAY_NONE = 0,
  CITY_OVERLAY_SIGN_FIRST = 1, CITY_OVERLAY_SIGN_LAST = 50,
  CITY_OVERLAY_FACILITY_FIRST = 51, CITY_OVERLAY_FACILITY_LAST = 200,
  CITY_OVERLAY_EVENT_FIRST = 201, CITY_OVERLAY_EVENT_LAST = 240,
  CITY_OVERLAY_EVENT_END = CITY_OVERLAY_EVENT_LAST + 1,
  CITY_OVERLAY_BORDER_CONNECTION = 250,
  CITY_OVERLAY_POLLUTION = 251, CITY_OVERLAY_FLOOD = 252,
  CITY_OVERLAY_RIOTS_SOUTH_EAST = 253, CITY_OVERLAY_RIOTS_NORTH_WEST = 254,
  CITY_OVERLAY_FIRE = 255,
  CITY_OVERLAY_SIGN_COUNT = CITY_OVERLAY_SIGN_LAST - CITY_OVERLAY_SIGN_FIRST + 1,
  CITY_OVERLAY_FACILITY_COUNT = CITY_OVERLAY_FACILITY_LAST - CITY_OVERLAY_FACILITY_FIRST + 1,
  CITY_OVERLAY_EVENT_COUNT = CITY_OVERLAY_EVENT_LAST - CITY_OVERLAY_EVENT_FIRST + 1
} CityMapOverlay;

// ALTM: land height, water height/building stage, tunnel depth, and bit15.
// Dry building tiles always use bits5..9 for stage; actual water uses water height.
// Bit15 remains independent and survives field edits and SC2/SC2K saving.
#define CITY_ALTITUDE_LAND_MASK 0x001Fu
#define CITY_ALTITUDE_WATER_MASK 0x03E0u
#define CITY_ALTITUDE_BUILDING_STAGE_MASK CITY_ALTITUDE_WATER_MASK
#define CITY_ALTITUDE_TUNNEL_MASK 0x7C00u
#define CITY_ALTITUDE_PERSISTENT_FLAG 0x8000u
#define CITY_ALTITUDE_VALUE_MASK 0x7FFFu
#define CITY_ALTITUDE_CLEAR_LAND 0xFFE0u
#define CITY_ALTITUDE_CLEAR_WATER 0xFC1Fu
#define CITY_ALTITUDE_CLEAR_TUNNEL 0x83FFu
#define CITY_ALTITUDE_SURFACE_MASK 0x03FFu
#define CITY_ALTITUDE_TUNNEL_ENTRANCE 0x0400u
#define CITY_ALTITUDE_WATER_STEP 0x0020u
#define CITY_ALTITUDE_WATER_LEVEL_TWO 0x0040u

enum {
  CITY_ALTITUDE_WATER_SHIFT = 5,
  CITY_ALTITUDE_BUILDING_STAGE_SHIFT = 5,
  CITY_ALTITUDE_TUNNEL_SHIFT = 10,
  CITY_BUILDING_CONSTRUCTION_STAGE_MAX = 31,
};

// Native overlay storage: sign presence and one stationary effect.
enum {
  CITY_TILE_HAS_SIGN = 1,
  CITY_TILE_EFFECT_SHIFT = 1, CITY_TILE_EFFECT_MASK = 15 << CITY_TILE_EFFECT_SHIFT,
  CITY_TILE_POLLUTION_EFFECT = (CITY_OVERLAY_POLLUTION - CITY_OVERLAY_EVENT_END + 1) << CITY_TILE_EFFECT_SHIFT,
  CITY_SIGN_OVERLAY_EXTENDED = 0x10000000,
  CITY_FACILITY_OVERLAY_EXTENDED = 0x20000000,
};
static inline int CitySignOverlay(int slot) {
  return slot <= CITY_OVERLAY_SIGN_LAST ? slot : CITY_SIGN_OVERLAY_EXTENDED + slot;
}
static inline bool IsCitySignOverlay(int value) {
  return (value >= CITY_OVERLAY_SIGN_FIRST && value <= CITY_OVERLAY_SIGN_LAST) ||
      (value >= CITY_SIGN_OVERLAY_EXTENDED && value < CITY_FACILITY_OVERLAY_EXTENDED);
}
static inline int CitySignOverlaySlot(int value) {
  return value < CITY_SIGN_OVERLAY_EXTENDED ? value : value - CITY_SIGN_OVERLAY_EXTENDED;
}
static inline int CityFacilityOverlay(int slot) {
  return slot < CITY_OVERLAY_FACILITY_COUNT ? CITY_OVERLAY_FACILITY_FIRST + slot :
      CITY_FACILITY_OVERLAY_EXTENDED + slot;
}
static inline bool IsCityFacilityOverlay(int value) {
  return (value >= CITY_OVERLAY_FACILITY_FIRST && value <= CITY_OVERLAY_FACILITY_LAST) ||
      value >= CITY_FACILITY_OVERLAY_EXTENDED;
}
static inline int CityFacilityOverlaySlot(int value) {
  return value < CITY_FACILITY_OVERLAY_EXTENDED ? value - CITY_OVERLAY_FACILITY_FIRST :
      value - CITY_FACILITY_OVERLAY_EXTENDED;
}

// Native corner shapes. Values 14 and 15 are intermediate terrain-operation results; stored slopes use 0..13.
typedef enum CityTerrainShape {
  CITY_TERRAIN_FLAT = 0,
  CITY_TERRAIN_SHAPE_1 = 1, CITY_TERRAIN_SHAPE_2 = 2,
  CITY_TERRAIN_SHAPE_3 = 3, CITY_TERRAIN_SHAPE_4 = 4,
  CITY_TERRAIN_SHAPE_5 = 5, CITY_TERRAIN_SHAPE_6 = 6,
  CITY_TERRAIN_SHAPE_7 = 7, CITY_TERRAIN_SHAPE_8 = 8,
  CITY_TERRAIN_SHAPE_9 = 9, CITY_TERRAIN_SHAPE_10 = 10,
  CITY_TERRAIN_SHAPE_11 = 11, CITY_TERRAIN_SHAPE_12 = 12,
  CITY_TERRAIN_SHAPE_13 = 13, CITY_TERRAIN_SHAPE_14 = 14,
  CITY_TERRAIN_SHAPE_15 = 15,
} CityTerrainShape;

#define CITY_TERRAIN_SHAPE_MASK 0x0Fu

// Packed record access changes only the supplied TileInfo; callers own statistics, topology and damage.
// Store slopes 0..13, and assign nonzero zoning only to flat tiles. Growth/timing bits retain their existing rules.
static SC2K_FORCE_INLINE int ReadCityZoneKind(const TileInfo *tp) {
  return tp->zone & CITY_TILE_SLOPED ? ZONE_NONE : tp->zone & CITY_ZONE_KIND_MASK;
}

static SC2K_FORCE_INLINE u8 ReadTerrainCode(const TileInfo *tp) {
  return tp->zone & CITY_TILE_SLOPED ? tp->zone & CITY_TERRAIN_SHAPE_MASK : CITY_TERRAIN_FLAT;
}

static SC2K_FORCE_INLINE void WriteTerrainCode(TileInfo *tp, int shape) {
  assert(shape >= 0 && shape <= CITY_TERRAIN_SHAPE_13);
  tp->zone = (tp->zone & CITY_TILE_GROWTH_MASK) |
      (shape ? CITY_TILE_SLOPED | shape : ReadCityZoneKind(tp));
}

static SC2K_FORCE_INLINE void WriteCityZoneKind(TileInfo *tp, int zone) {
  assert(!zone || !ReadTerrainCode(tp));
  tp->zone = (tp->zone & CITY_TILE_GROWTH_MASK) |
      (ReadTerrainCode(tp) ? tp->zone & 31 : zone & CITY_ZONE_KIND_MASK);
}

// Resolved zone and occupant metadata; the stored zone/slope discriminator is never exposed as a zone kind.
static SC2K_FORCE_INLINE u8 ReadTileZoneByte(const TileInfo *tp) {
  return (tp->terrain & CITY_ZONE_METADATA_MASK) | ReadCityZoneKind(tp);
}

static SC2K_FORCE_INLINE void WriteTileZoneByte(TileInfo *tp, int value) {
  WriteCityZoneKind(tp, value & CITY_ZONE_KIND_MASK);
  tp->terrain = (tp->terrain & 15) | (value & CITY_ZONE_METADATA_MASK);
}

// Ordinary road/rail/power payloads use the four cardinal connection bits.
static SC2K_FORCE_INLINE int ReadTransportConnections(const TileInfo *tp) {
  return tp->terrain >> CITY_ZONE_METADATA_SHIFT;
}

static SC2K_FORCE_INLINE void WriteTransportConnections(TileInfo *tp, int mask) {
  tp->terrain = (tp->terrain & CITY_ZONE_KIND_MASK) |
      mask << CITY_ZONE_METADATA_SHIFT;
}

#endif
