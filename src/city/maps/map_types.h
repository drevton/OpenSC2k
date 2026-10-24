#ifndef SC2K_DOMAIN_SRC_CITY_MAPS_MAP_TYPES_H
#define SC2K_DOMAIN_SRC_CITY_MAPS_MAP_TYPES_H

#include "city/maps/tile_storage.h"
#include <stddef.h>

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

#endif
