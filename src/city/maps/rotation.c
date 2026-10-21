#include "city/maps/tile_storage.h"
#include "city/maps/tables.h"

// Rotation Tables

// Return the immutable kind and terrain rotation tables. The pointer is borrowed for the process lifetime and must not
// be modified or freed.
const CityMapRotationTables *ReadCityMapRotationTables(void) {
  static const CityMapRotationTables kRotation = {
    .tile_counterclockwise = {
      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 14,
      17, 18, 19, 16, 21, 22, 23, 20, 25, 26, 27, 24, 28, 30, 29, 32,
      33, 34, 31, 36, 37, 38, 35, 40, 41, 42, 39, 43, 45, 44, 47, 48,
      49, 46, 51, 52, 53, 50, 55, 56, 57, 54, 58, 60, 61, 62, 59, 64,
      65, 66, 63, 68, 67, 70, 69, 72, 71, 74, 73, 76, 75, 78, 77, 80,
      79, 85, 84, 83, 82, 81, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95,
      96, 98, 99, 100, 97, 102, 103, 104, 101, 105, 106, 107, 109, 110, 111, 108,
      112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127,
      128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143,
      144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159,
      160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175,
      176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191,
      192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207,
      208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223,
      224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239,
      240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255
    },
    .terrain_counterclockwise = {
      0, 2, 3, 4, 1, 6, 7, 8, 5, 10, 11, 12, 9, 13, 0, 0,
      16, 18, 19, 20, 17, 22, 23, 24, 21, 26, 27, 28, 25, 29, 0, 0,
      32, 34, 35, 36, 33, 38, 39, 40, 37, 42, 43, 44, 41, 45, 46, 0,
      48, 50, 51, 52, 49, 54, 55, 56, 53, 58, 59, 60, 57, 61, 62, 0,
      65, 64, 67, 68, 69, 66, 0, 0
    },
    .underground_counterclockwise = {
      0, 2, 1, 4, 5, 6, 3, 8, 9, 10, 7, 12, 13, 14, 11, 15,
      17, 16, 19, 20, 21, 18, 23, 24, 25, 22, 27, 28, 29, 26, 30, 32,
      31, 33, 34, 35, 0, 0, 0, 0
    },
    .terrain_clockwise = {
      0, 4, 1, 2, 3, 8, 5, 6, 7, 12, 9, 10, 11, 13, 0, 0,
      16, 20, 17, 18, 19, 24, 21, 22, 23, 28, 25, 26, 27, 29, 0, 0,
      32, 36, 33, 34, 35, 40, 37, 38, 39, 44, 41, 42, 43, 45, 46, 0,
      48, 52, 49, 50, 51, 56, 53, 54, 55, 60, 57, 58, 59, 61, 62, 0,
      65, 64, 69, 66, 67, 68, 0, 0
    },
    .underground_clockwise = {
      0, 2, 1, 6, 3, 4, 5, 10, 7, 8, 9, 14, 11, 12, 13, 15,
      17, 16, 21, 18, 19, 20, 25, 22, 23, 24, 29, 26, 27, 28, 30, 32,
      31, 33, 34, 35, 0, 0, 0, 0
    },
  };
  return &kRotation;
}

// Terrain Tables

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
