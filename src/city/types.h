#ifndef SC2K_CITY_TYPES_H
#define SC2K_CITY_TYPES_H

#include "base/types.h"

typedef u32 TileIndex;

typedef struct City City;
typedef struct CityResourceState CityAssets;

typedef struct {
  int x;
  int y;
} CityPoint;
_Static_assert(sizeof(CityPoint) == 8, "coordinate pair size");

// A map footprint starts at its smallest tile index, independent of view rotation.
typedef struct {
  TileIndex start;
  u16 width, height;
} CityTileArea;
_Static_assert(sizeof(CityTileArea) == 8, "tile area size");

typedef struct {
  int left, top, right, bottom;
} CityRect;
_Static_assert(sizeof(CityRect) == 16, "rectangle size");

// Keep raw coordinate bounds, including empty or reversed rectangles.
static inline CityRect IntersectCityRects(CityRect a, CityRect b) {
  return (CityRect){IntMax(a.left, b.left), IntMax(a.top, b.top),
      IntMin(a.right, b.right), IntMin(a.bottom, b.bottom)};
}

static inline CityRect UnionCityRects(CityRect a, CityRect b) {
  return (CityRect){IntMin(a.left, b.left), IntMin(a.top, b.top),
      IntMax(a.right, b.right), IntMax(a.bottom, b.bottom)};
}

#endif
