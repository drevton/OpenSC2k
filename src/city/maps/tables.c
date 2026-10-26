#include "city/maps/tables.h"
#include "city/maps/tile_storage.h"

// Shared map-neighbor steps.

const i8 kCardinalNeighborY[4] = {
  0, 1, 0, -1,
};

const i8 kCardinalNeighborX[4] = {
  -1, 0, 1, 0,
};

// Terrain Tables

const u8 kTerrainRaisedCorners[16] = {
  [CITY_TERRAIN_FLAT] = 0,
  [CITY_TERRAIN_SHAPE_1] = 9, [CITY_TERRAIN_SHAPE_2] = 3,
  [CITY_TERRAIN_SHAPE_3] = 6, [CITY_TERRAIN_SHAPE_4] = 12,
  [CITY_TERRAIN_SHAPE_5] = 11, [CITY_TERRAIN_SHAPE_6] = 7,
  [CITY_TERRAIN_SHAPE_7] = 14, [CITY_TERRAIN_SHAPE_8] = 13,
  [CITY_TERRAIN_SHAPE_9] = 1, [CITY_TERRAIN_SHAPE_10] = 2,
  [CITY_TERRAIN_SHAPE_11] = 4, [CITY_TERRAIN_SHAPE_12] = 8,
  [CITY_TERRAIN_SHAPE_13] = 15, [CITY_TERRAIN_SHAPE_14] = 15,
  [CITY_TERRAIN_SHAPE_15] = 0,
};

const i8 kTerrainNeighborYOffsets[9] = {
  0, 1, 1, 1, 0, -1, -1, -1, 0,
};

const i8 kTerrainNeighborXOffsets[9] = {
  -1, -1, 0, 1, 1, 1, 0, -1, 0,
};

const u8 kTerrainHigherNeighborBits[8] = {
  3, 2, 6, 4, 12, 8, 9, 1,
};

// The eight neighbor flags combine four corner bits: only indices 0..15 are reachable.
const u8 kTerrainShapeByHigherNeighbors[16] = {
  CITY_TERRAIN_FLAT, CITY_TERRAIN_SHAPE_9, CITY_TERRAIN_SHAPE_10, CITY_TERRAIN_SHAPE_2,
  CITY_TERRAIN_SHAPE_11, CITY_TERRAIN_SHAPE_13, CITY_TERRAIN_SHAPE_3, CITY_TERRAIN_SHAPE_6,
  CITY_TERRAIN_SHAPE_12, CITY_TERRAIN_SHAPE_1, CITY_TERRAIN_SHAPE_13, CITY_TERRAIN_SHAPE_5,
  CITY_TERRAIN_SHAPE_4, CITY_TERRAIN_SHAPE_8, CITY_TERRAIN_SHAPE_7, CITY_TERRAIN_RAISE_LEVEL,
};
