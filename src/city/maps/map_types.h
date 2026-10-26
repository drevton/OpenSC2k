#ifndef SC2K_DOMAIN_SRC_CITY_MAPS_MAP_TYPES_H
#define SC2K_DOMAIN_SRC_CITY_MAPS_MAP_TYPES_H

#include "city/maps/tile_storage.h"
#include <stddef.h>

#include "city/types.h"

// Stable simulation counter slots and range boundaries. Buildings and unlisted kinds retain their numeric slots.
// These are independent of stored TileKind; the stock boundary checks its corresponding family IDs.
typedef enum CityTileStatisticSlot {
  TSTAT_CLEAR = 0,
  TSTAT_RUBBLE = 1,
  TSTAT_PARK = 13,
  TSTAT_POWER_LINE = 14,
  TSTAT_ROAD = 29,
  TSTAT_ROAD_LAST = 43,
  TSTAT_RAIL = 44,
  TSTAT_RAIL_LAST = 62, // Includes the four transition slots.
  TSTAT_TUNNEL_ENTRANCE = 63,
  TSTAT_TUNNEL_ENTRANCE_LAST = 66,
  TSTAT_ROAD_POWER_CROSSING = 67,
  TSTAT_ROAD_RAIL_CROSSING = 69,
  TSTAT_ROAD_RAIL_CROSSING_LAST = 70,
  TSTAT_RAIL_POWER_CROSSING = 71,
  TSTAT_RAIL_POWER_CROSSING_LAST = 72,
  TSTAT_HIGHWAY = 73,
  TSTAT_HIGHWAY_ROAD = 75,
  TSTAT_HIGHWAY_ROAD_LAST = 76,
  TSTAT_HIGHWAY_RAIL = 77,
  TSTAT_HIGHWAY_RAIL_LAST = 78,
  TSTAT_HIGHWAY_POWER = 79,
  TSTAT_HIGHWAY_LAST = 80,
  TSTAT_ROAD_BRIDGE = 81,
  TSTAT_ROAD_BRIDGE_HEAD = 87,
  TSTAT_RAIL_BRIDGE = 90,
  TSTAT_RAIL_BRIDGE_HEAD = 91,
  TSTAT_WATER_POWER_LINE = 92,
  TSTAT_HIGHWAY_ENTRANCE = 93,
  TSTAT_HIGHWAY_ENTRANCE_LAST = 96,
  TSTAT_HIGHWAY_INCLINE = 97,
  TSTAT_HIGHWAY_BEND = 101,
  TSTAT_HIGHWAY_JUNCTION = 105,
  TSTAT_HIGHWAY_BRIDGE = 106,
  TSTAT_HIGHWAY_BRIDGE_LAST = 107,
  TSTAT_SUBWAY_TO_RAIL = 108,
  TSTAT_SUBWAY_TO_RAIL_LAST = 111,
} CityTileStatisticSlot;

// Simulation counters use the stock-compatible slots selected by CityTileStatisticsKey, not native TileKind IDs.
// Signed working values and unsigned counts share the same 32-bit elements.
typedef union CityStatistics {
  i32 value[TK_COUNT];
  u32 count[TK_COUNT];
} CityStatistics;
typedef CityStatistics CityTileCounts;

// Byte-valued military base/unlock mode. Unavailable covers a declined offer
// and failure to find suitable land. Layouts follow the original zone7 scan.
typedef enum CityMilitaryBaseKind {
  CITY_MILITARY_BASE_NONE = 0,
  CITY_MILITARY_BASE_UNAVAILABLE = 1,
  CITY_MILITARY_BASE_ARMY = 2,
  CITY_MILITARY_BASE_AIR_FORCE = 3,
  CITY_MILITARY_BASE_NAVY = 4,
  CITY_MILITARY_BASE_MISSILE_SILOS = 5,
  CITY_MILITARY_BASE_COUNT = 6
} CityMilitaryBaseKind;

// The military offer retains this placement through its result notice.
enum { CITY_MILITARY_FINISH_DONE, CITY_MILITARY_FINISH_ZONE, CITY_MILITARY_FINISH_UNAVAILABLE };
typedef struct {
  TileIndex site;
  u16 notice;
  u8 after_notice;
} CityMilitaryPlacement;

// Military-zone counters have their own sixteen-entry ordering.
// OTHER is incremented/decremented for all unlisted surface IDs.
typedef enum CitySpecialTileStatistic {
  CITY_SPECIAL_TILE_OTHER = 0, CITY_SPECIAL_TILE_RUNWAY_FIRST = 1,
  CITY_SPECIAL_TILE_RUNWAY_LAST = 2, CITY_SPECIAL_TILE_PARKING_LOT = 3,
  CITY_SPECIAL_TILE_CARGO_YARD = 4, CITY_SPECIAL_TILE_RADAR = 5,
  CITY_SPECIAL_TILE_WAREHOUSE = 6, CITY_SPECIAL_TILE_BUILDING_FIRST = 7,
  CITY_SPECIAL_TILE_BUILDING_SECOND = 8, CITY_SPECIAL_TILE_TOP_SECRET = 9,
  CITY_SPECIAL_TILE_CRANE = 10, CITY_SPECIAL_TILE_CONTROL_TOWER = 11,
  CITY_SPECIAL_TILE_F_15B = 12, CITY_SPECIAL_TILE_HANGAR_SMALL = 13,
  CITY_SPECIAL_TILE_HANGAR_LARGE = 14, CITY_SPECIAL_TILE_MISSILE_SILO = 15,
  CITY_SPECIAL_TILE_COUNT = 16
} CitySpecialTileStatistic;

typedef struct {
  u32 count[CITY_SPECIAL_TILE_COUNT];
} CitySpecialTileCounts;

// Four statistics for each 2x2 block of map tiles.
typedef struct {
  u8 pollution, traffic_density, crime, land_value;
} CityHalfMapStatistics;

// Four statistics for each 4x4 block of map tiles.
typedef struct {
  u8 density, police_coverage, fire_coverage, density_history;
} CityQuarterMapStatistics;

_Static_assert(sizeof(CityHalfMapStatistics) == 4 && sizeof(CityQuarterMapStatistics) == 4,
    "coarse map records have no padding");

// Four-way neighbor selection in UpdateCityMapTileStates; separate from eight-way event headings.
typedef enum CityMapNeighborDirection {
  CITY_NEIGHBOR_NORTH = 0, CITY_NEIGHBOR_WEST = 1,
  CITY_NEIGHBOR_SOUTH = 2, CITY_NEIGHBOR_EAST = 3,
} CityMapNeighborDirection;

// Four view orientations also select the network scanner's visit order.

typedef enum CityMapRotation {
  CITY_MAP_ROTATION_0 = 0, CITY_MAP_ROTATION_90 = 1,
  CITY_MAP_ROTATION_180 = 2, CITY_MAP_ROTATION_270 = 3,
  CITY_MAP_ROTATION_COUNT = 4,
} CityMapRotation;

// Physical corner indices in the stored map, independent of camera rotation.
enum {
  CITY_BUILDING_CORNER_TOP_LEFT, CITY_BUILDING_CORNER_BOTTOM_LEFT,
  CITY_BUILDING_CORNER_BOTTOM_RIGHT, CITY_BUILDING_CORNER_TOP_RIGHT,
};

// Corner masks use two different bit orders. Do not interchange them.
typedef enum CityFootprintCornerMask {
  CITY_FOOTPRINT_CORNER_NW = 1, CITY_FOOTPRINT_CORNER_SW = 2,
  CITY_FOOTPRINT_CORNER_SE = 4, CITY_FOOTPRINT_CORNER_NE = 8,
  CITY_FOOTPRINT_CORNERS_WEST = 3, CITY_FOOTPRINT_CORNERS_SOUTH = 6,
  CITY_FOOTPRINT_CORNERS_NORTH = 9, CITY_FOOTPRINT_CORNERS_EAST = 12,
  CITY_FOOTPRINT_CORNERS_WITHOUT_NE = 7, CITY_FOOTPRINT_CORNERS_WITHOUT_SE = 11,
  CITY_FOOTPRINT_CORNERS_WITHOUT_SW = 13, CITY_FOOTPRINT_CORNERS_WITHOUT_NW = 14,
  CITY_FOOTPRINT_CORNERS_ALL = 15,
} CityFootprintCornerMask;

typedef enum CityHeightCornerMask {
  CITY_HEIGHT_CORNER_NW = 1, CITY_HEIGHT_CORNER_NE = 2,
  CITY_HEIGHT_CORNER_SW = 4, CITY_HEIGHT_CORNER_SE = 8,
  CITY_HEIGHT_CORNERS_NORTH = 3, CITY_HEIGHT_CORNERS_EAST = 10,
  CITY_HEIGHT_CORNERS_SOUTH = 12, CITY_HEIGHT_CORNERS_WEST = 5,
  CITY_HEIGHT_CORNERS_NW_SE = 9, CITY_HEIGHT_CORNERS_NE_SW = 6,
  CITY_HEIGHT_CORNERS_ALL = 15,
} CityHeightCornerMask;

// Layer Types

enum {
  CITY_MAP_MIN_SIZE = 128,
  CITY_MAP_MAX_SIZE = 4096,
  CITY_MAP_SECTION_SIZE = 128,
  CITY_MAP_SECTION_CELL_COUNT = CITY_MAP_SECTION_SIZE * CITY_MAP_SECTION_SIZE,
  CITY_MAP_LOG_SIZE = 7,
  CITY_MAP_SIZE = 1 << CITY_MAP_LOG_SIZE,
  CITY_MAP_HALF_SIZE = CITY_MAP_SIZE / 2,
  CITY_MAP_QUARTER_SIZE = CITY_MAP_SIZE / 4,
  CITY_MAP_LAST_INDEX = CITY_MAP_SIZE - 1,
  CITY_MAP_CELL_COUNT = CITY_MAP_SIZE * CITY_MAP_SIZE
};

// Valid cardinal neighbors, in north/west/south/east order. Only entries below count are initialized.
typedef struct TileNeighbors {
  TileIndex tiles[4];
  u8 count;
} TileNeighbors;

enum { CITY_WATER_SOURCE_SORT_LIMIT = 8192 };

// Each native map axis is a power of two within these supported bounds.
static inline bool IsSupportedMapAxis(int size) {
  return size >= CITY_MAP_MIN_SIZE && size <= CITY_MAP_MAX_SIZE && !(size & (size - 1));
}
size_t CityMapStorageSize(int width, int height);

// Pending map changes and retained scan work

// City owns these pending presentation notifications; they are not saved simulation data. Tile invalidations
// call the direct UI damage hook immediately. Bulk simulation passes notify layers once.
typedef struct SimulationMapChanges {
  u16 invalidated_layers;
  bool tiles_changed;
  bool full;
  bool enabled;
} SimulationMapChanges;

// Retained environment stage/cursor and city-center sums. Zero initializes the pollution-input stage.
// City owns this record for incremental work; whole-pass callers can keep it on the stack.
typedef struct CityEnvironmentScan {
  int row, stage, count, center_x, center_y;
  i64 sum_x, sum_y;
} CityEnvironmentScan;

// Shared traversal ring positions and capacity-minus-one mask, initialized when City binds its map backing.
typedef struct CityTileQueueCursor {
  u16 read, write, mask;
} CityTileQueueCursor;
_Static_assert(sizeof(CityTileQueueCursor) == 6 && offsetof(CityTileQueueCursor, mask) == 4,
    "tile queue cursor layout");

// Labels and sparse sign storage

// Fixed native UTF-8 slots; stock file widths belong to saveload/types.h.
enum {
  CITY_OVERLAY_NAME_COUNT = 256,
  CITY_OVERLAY_NAME_BYTES = 32,
  CITY_SIGN_COLOR_COUNT = 32,
  CITY_OVERLAY_NAME_MAX_LENGTH = CITY_OVERLAY_NAME_BYTES - 1,
};

typedef struct CityOverlayName {
  u8 character[CITY_OVERLAY_NAME_BYTES];
} CityOverlayName;

// Sparse associations use indices as stable command tokens; hash links are rebuilt on load.
typedef struct {
  TileIndex tile;
  u32 next;
  CityOverlayName name;
} CitySignRecord;

typedef struct {
  CitySignRecord *record;
  u32 *buckets;
  // capacity is zero or a power of two. used is the exclusive high-water slot, not a live count;
  // slot zero is reserved. free_slot is a search hint, and cleared slots remain reusable below used.
  int capacity, used, free_slot;
  u32 revision;
} CitySigns;

// Native UTF-8 label/team/mayor slots. Stock XLAB records are converted by the file codec.

typedef struct {
  CityOverlayName entry[CITY_OVERLAY_NAME_COUNT];
} CityOverlayNames;
_Static_assert(sizeof(CityOverlayName) == 32 && offsetof(CityOverlayName, character) == 0,
    "native UTF-8 name record");
_Static_assert(sizeof(CityOverlayNames) == 8192, "native XLAB name table");

// Geography

typedef struct { u8 character[4]; } CityCountryTag;

// Four terminated neighbor labels. A zero saved name index selects Ocean; geography owns the fixed storage.
typedef struct CityNeighborNames { char name[4][32]; } CityNeighborNames;

typedef struct {
  CityNeighborNames neighbor_names;
  u32 ocean;
  u32 river;
  u32 settings_preserved;
  u16 rotation;
  u16 center_x;
  u16 center_y;
  i16 start_year;
  u16 edit_height;
  u16 hills;
  u16 water;
  u16 trees;
} CityGeographyState;

#endif
