#ifndef SC2K_DRAWING_MAPS_MAP_CACHE_H
#define SC2K_DRAWING_MAPS_MAP_CACHE_H

#include "drawing/types.h"

typedef struct Drawing Drawing;
typedef struct SpriteSurface SpriteSurface;

enum { CITY_MAP_CACHE_FRAGMENT_LIMIT = 1024 };

typedef struct {
  CityRect fragments[CITY_MAP_CACHE_FRAGMENT_LIMIT];
  i32 count;
  i32 valid;
  i32 origin_x;
  i32 origin_y;
  i32 magnification;
  int target_x, target_y, target_scale;
  u64 painted_pixels;
  u64 reused_pixels;
  u64 copied_pixels;
  u64 full_refreshes;
} NativeMapCache;

typedef struct {
  const SpriteSurface *surface; // Borrowed color and animation buffers; indices must cover the full canvas.
  u32 *scratch_colors;
  u8 *scratch_indices;
  int scale, x, y; // Positive percentage scale and physical position of the logical map origin.
  bool physical_coordinates; // Otherwise rectangles use logical map pixels transformed by scale/x/y.
  bool preserved_source; // Shared frame-start snapshot, before any camera repaints its old screen area.
} MapCachePixels;
// Borrow the currently bound map target and Drawing's reusable scroll scratch.
MapCachePixels CityMapCachePixels(Drawing *drawing);
// Scratch arrays must hold the full color/index strides and surface height. The descriptor is borrowed.
void SaveMapCacheScrollSource(const MapCachePixels *pixels);
// Reuse pixels in disjoint, nonempty visible rectangles before synchronously painting exposed strips.
// UI supplies 0..CITY_MAP_CACHE_FRAGMENT_LIMIT rectangles, rounded and unioned in the target's coordinates:
// physical framebuffer pixels, or logical map pixels when requested. The array is borrowed and must not alias cache.
// magnification is positive and describes display pixels per logical map pixel. pixels and its surface are required;
// paint uses that same surface and coordinate system. Colors and animation identities move together.
// Snapshot scratch is used only when visibility prevents in-place copying; no window state is read here.
void DrawMapCachePixels(NativeMapCache *cache, const CityRect *fragments, int fragment_count,
    int magnification, int origin_x, int origin_y,
    const MapCachePixels *pixels, CityCanvasVisiblePaint paint, void *context);

#endif
