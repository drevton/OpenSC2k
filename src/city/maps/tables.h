#ifndef SC2K_DOMAIN_SRC_CITY_MAPS_TABLES_H
#define SC2K_DOMAIN_SRC_CITY_MAPS_TABLES_H

#include "base/types.h"

// Terrain-operation result: all four corners rise to the next level. Never stored as a tile slope.
enum { CITY_TERRAIN_RAISE_LEVEL = 50 };

// Cardinal steps in west, south, east, north order. Disaster walks and tile scans retain this order.
extern const i8 kCardinalNeighborY[4];
extern const i8 kCardinalNeighborX[4];

// Rotation Tables

// Include the original zero padding, but never neighboring lookup data.
enum {
  CITY_TILE_ROTATION_COUNT = 256,
  CITY_TERRAIN_ROTATION_COUNT = 72,
  CITY_UNDERGROUND_ROTATION_COUNT = 40
};

typedef struct {
  u8 tile_counterclockwise[CITY_TILE_ROTATION_COUNT];
  u8 terrain_counterclockwise[CITY_TERRAIN_ROTATION_COUNT];
  u8 underground_counterclockwise[CITY_UNDERGROUND_ROTATION_COUNT];
  u8 terrain_clockwise[CITY_TERRAIN_ROTATION_COUNT];
  u8 underground_clockwise[CITY_UNDERGROUND_ROTATION_COUNT];
} CityMapRotationTables;

// Borrowed immutable artwork mappings, independent of map storage.
const CityMapRotationTables *ReadCityMapRotationTables(void);


// Terrain Tables

// Immutable numerical data, independent of executable backing and original element widths.
// Raised-corner bits follow vertex offsets (0,0), (0,1), (1,1), (1,0). Index by terrain shape 0..15.
// The caller chooses world or camera space. Underground selection shapes have a separate numbering.
extern const u8 kTerrainRaisedCorners[16];
extern const i8 kTerrainNeighborYOffsets[9];
extern const i8 kTerrainNeighborXOffsets[9];
extern const u8 kTerrainHigherNeighborBits[8];
extern const u8 kTerrainShapeByHigherNeighbors[16];


#endif
