#ifndef SC2K_DOMAIN_SRC_CITY_MAPS_TABLES_H
#define SC2K_DOMAIN_SRC_CITY_MAPS_TABLES_H

#include "city/types.h"

// Terrain-operation result: all four corners rise to the next level. Never stored as a tile slope.
enum { CITY_TERRAIN_RAISE_LEVEL = 50 };

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
extern const i8 kTerrainNeighborYOffsets[9];
extern const i8 kTerrainNeighborXOffsets[9];
extern const u8 kTerrainHigherNeighborBits[8];
extern const u8 kTerrainShapeByHigherNeighbors[16];


#endif
