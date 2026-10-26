#ifndef SC2K_DOMAIN_SRC_CITY_MAPS_TABLES_H
#define SC2K_DOMAIN_SRC_CITY_MAPS_TABLES_H

#include "base/types.h"

// Terrain-operation result: all four corners rise to the next level. Never stored as a tile slope.
enum { CITY_TERRAIN_RAISE_LEVEL = 50 };

// Cardinal steps in west, south, east, north order. Disaster walks and tile scans retain this order.
extern const i8 kCardinalNeighborY[4];
extern const i8 kCardinalNeighborX[4];

// Eight adjacent cells clockwise from north: N, NE, E, SE, S, SW, W, NW.
// Even entries are cardinal directions; generation and climate scans retain this order.
// Keep these small constants visible to the compiler for neighbor-loop folding and unrolling.
static const i8 kClockwiseNeighborX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
static const i8 kClockwiseNeighborY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

// Terrain Tables

// Shared native vertex order: (0,0), (0,1), (1,1), (1,0). Raised-corner bit indexes use this order.
static const i8 kTerrainCornerX[4] = {0, 0, 1, 1};
static const i8 kTerrainCornerY[4] = {0, 1, 1, 0};

// Immutable numerical data, independent of executable backing and original element widths.
// Raised-corner bits follow vertex offsets (0,0), (0,1), (1,1), (1,0). Index by terrain shape 0..15.
// The caller chooses world or camera space. Underground selection shapes have a separate numbering.
extern const u8 kTerrainRaisedCorners[16];
extern const i8 kTerrainNeighborYOffsets[9];
extern const i8 kTerrainNeighborXOffsets[9];
extern const u8 kTerrainHigherNeighborBits[8];
extern const u8 kTerrainShapeByHigherNeighbors[16];


#endif
