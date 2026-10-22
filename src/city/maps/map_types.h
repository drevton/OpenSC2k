#ifndef SC2K_DOMAIN_SRC_CITY_MAPS_MAP_TYPES_H
#define SC2K_DOMAIN_SRC_CITY_MAPS_MAP_TYPES_H

#include "city/economy/types.h"
#include "city/maps/tile_storage.h"
#include <stddef.h>

#include "city/events/events.h"
#include "city/types.h"

// Simulation counters use the stock-compatible slots selected by CityTileStatisticsKind, not native TileKind IDs.
// Signed working values and unsigned counts share the same 32-bit elements.
typedef union CityStatistics {
  i32 value[TK_COUNT];
  u32 count[TK_COUNT];
} CityStatistics;
typedef CityStatistics CityTileCounts;

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

// Retained environment stage/cursor and city-center sums. Zero initializes the pollution-input stage.
// City owns this record for incremental work; whole-pass callers can keep it on the stack.
typedef struct CityEnvironmentScan {
  int row, stage, count, center_x, center_y;
  i64 sum_x, sum_y;
} CityEnvironmentScan;

// One-based land-use summary buckets; zero excludes construction/abandoned tiles.
typedef enum CityLandUseCategory {
  CITY_LAND_USE_NONE = 0, CITY_LAND_USE_TRANSPORTATION = 1, CITY_LAND_USE_POWER = 2,
  CITY_LAND_USE_WATER = 3, CITY_LAND_USE_RESIDENTIAL = 4, CITY_LAND_USE_COMMERCIAL = 5,
  CITY_LAND_USE_INDUSTRIAL = 6, CITY_LAND_USE_PORTS_AIRPORTS = 7, CITY_LAND_USE_EDUCATION = 8,
  CITY_LAND_USE_HEALTH_SAFETY = 9, CITY_LAND_USE_RECREATION = 10, CITY_LAND_USE_ARCOLOGIES = 11,
  CITY_LAND_USE_COUNT = 11,
} CityLandUseCategory;

typedef enum CityTileWaterSupply {
  CITY_TILE_WATER_SERVICE,
  CITY_TILE_WATER_PRODUCTION,
  CITY_TILE_WATER_STORAGE,
} CityTileWaterSupply;

// Read-only tile query values. UI owns the retained copy and its localized text/artwork.
typedef struct {
  TileIndex tile;
  TileInfo cell;
  int x, y, altitude_feet, traffic, water;
  u32 facility_years;
  bool has_facility_age;
  u8 density, density_history, traffic_density, land_value, crime, pollution, police, fire;
  u8 crime_rating, pollution_rating, water_supply;
  bool underwater, rocky, show_traffic, building_stage, show_services, powered, watered;
} CityTileInformation;

// Four-way neighbor selection in UpdateCityMapTileStates; separate from eight-way event headings.
typedef enum CityMapNeighborDirection {
  CITY_NEIGHBOR_NORTH = 0, CITY_NEIGHBOR_WEST = 1,
  CITY_NEIGHBOR_SOUTH = 2, CITY_NEIGHBOR_EAST = 3,
} CityMapNeighborDirection;

// Tile categories; the string resource ID is 593 + category.
// The road/rail crossing and highway families intentionally share labels.
typedef enum CityTileCategory {
  CITY_TILE_CATEGORY_CLEAR = 0, CITY_TILE_CATEGORY_RUBBLE = 1,
  CITY_TILE_CATEGORY_RADIOACTIVE_WASTE = 2, CITY_TILE_CATEGORY_TREES = 3,
  CITY_TILE_CATEGORY_PARK = 4, CITY_TILE_CATEGORY_POWER_LINES = 5,
  CITY_TILE_CATEGORY_ROAD = 6, CITY_TILE_CATEGORY_RAIL = 7,
  CITY_TILE_CATEGORY_TUNNEL = 8, CITY_TILE_CATEGORY_ROAD_CROSSING = 9,
  CITY_TILE_CATEGORY_RAIL_CROSSING = 10, CITY_TILE_CATEGORY_HIGHWAY = 11,
  CITY_TILE_CATEGORY_SUSPENSION_BRIDGE = 12, CITY_TILE_CATEGORY_BRIDGE = 13,
  CITY_TILE_CATEGORY_POWER_BRIDGE = 14, CITY_TILE_CATEGORY_HIGHWAY_ENTRANCE = 15,
  CITY_TILE_CATEGORY_HIGHWAY_MULTI = 16, CITY_TILE_CATEGORY_HIGHWAY_BRIDGE = 17,
  CITY_TILE_CATEGORY_SUBWAY_TO_RAIL = 18, CITY_TILE_CATEGORY_LOWER_CLASS_HOMES = 19,
  CITY_TILE_CATEGORY_MIDDLE_CLASS_HOMES = 20, CITY_TILE_CATEGORY_LUXURY_HOMES = 21,
  CITY_TILE_CATEGORY_COUNT = 22
} CityTileCategory;

// Overlay

// Native UTF-8 slots and stock XLAB records have independent sizes.
enum {
  CITY_OVERLAY_NAME_COUNT = 256,
  CITY_OVERLAY_NAME_BYTES = 32,
  CITY_OVERLAY_LEGACY_NAME_BYTES = 25,
  CITY_SIGN_COLOR_COUNT = 32,
  CITY_OVERLAY_NAME_MAX_LENGTH = CITY_OVERLAY_NAME_BYTES - 1,
  CITY_OVERLAY_LEGACY_NAME_MAX_LENGTH = CITY_OVERLAY_LEGACY_NAME_BYTES - 2,
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
  int capacity, used, free_slot;
  u32 revision;
} CitySigns;

// Four view orientations also select the network scanner's visit order.

typedef enum CityMapRotation {
  CITY_MAP_ROTATION_0 = 0, CITY_MAP_ROTATION_90 = 1,
  CITY_MAP_ROTATION_180 = 2, CITY_MAP_ROTATION_270 = 3,
  CITY_MAP_ROTATION_COUNT = 4,
} CityMapRotation;

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

// Keep this unsigned sentinel separate from signed coordinate dimensions.
#define INVALID_TILE UINT32_MAX

typedef struct {
  u32 sample[CITY_HISTORY_SAMPLE_COUNT];
} CityHistoryRow;

enum { CITY_WATER_SOURCE_SORT_LIMIT = 8192 };

// Each native map axis is a power of two within these supported bounds.
static inline bool IsSupportedMapAxis(int size) {
  return size >= CITY_MAP_MIN_SIZE && size <= CITY_MAP_MAX_SIZE && !(size & (size - 1));
}
size_t CityMapStorageSize(int width, int height);

// Geography

typedef struct { u8 character[4]; } CityCountryTag;

typedef struct { u8 name[4][32]; } CityOceanOverlayNames;

typedef struct {
  CityOceanOverlayNames neighbor_names;
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

// Names

// Native UTF-8 label/team/mayor slots. Stock XLAB records are converted by the file codec.

typedef struct {
  CityOverlayName entry[CITY_OVERLAY_NAME_COUNT];
} CityOverlayNames;
_Static_assert(sizeof(CityOverlayName) == 32 && offsetof(CityOverlayName, character) == 0,
    "native UTF-8 name record");
_Static_assert(sizeof(CityOverlayNames) == 8192, "native XLAB name table");

#endif
