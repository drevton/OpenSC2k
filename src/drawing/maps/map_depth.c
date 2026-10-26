#include "drawing/maps/map_depth.h"

#include "assets/state.h"
#include "city/state.h"
#include "drawing/maps/map_render.h"
#include "drawing/maps/road_cars.h"
#include "drawing/state.h"
#include "game/errors.h"
#include "game/state.h"

// Depth-pass orchestration and specialized sorting. Cached sprite admission and emission remain
// beside source lookup in map_sprites.c, keeping that hot drawing path in one translation unit.

// Camera depth uses the footprint centre, with one elevation step contributing
// a quarter tile. Sprite height never shifts a building towards the viewer.
i32 CityMapObjectDepth(int x, int y, int height) {
  return x + y + height / 4;
}

// Unique keys preserve submission order even though the radix partition itself is not stable.
static inline u32 PreparedSpriteKey(const MapSpriteWork *work, u16 index) {
  return (u32)work->sprites[index].depth << 16 | index;
}

// In-place MSD radix sorting touches at most four key bytes. Tiny buckets avoid
// histogram setup; their insertion loop uses the same direct integer key.
static void SortPreparedSpriteRange(MapSpriteWork *work, int first, int end, int shift) {
  if (end - first <= 24) {
    for (int i = first + 1; i < end; ++i) {
      u16 index = work->order[i];
      u32 key = PreparedSpriteKey(work, index);
      int at = i;
      while (at > first && PreparedSpriteKey(work, work->order[at - 1]) > key) {
        work->order[at] = work->order[at - 1];
        --at;
      }
      work->order[at] = index;
    }
    return;
  }
  u16 ends[256] = {0}, next[256];
  for (int i = first; i < end; ++i) ++ends[(PreparedSpriteKey(work, work->order[i]) >> shift) & 255];
  int total = first;
  for (int bucket = 0; bucket < 256; ++bucket) {
    next[bucket] = total;
    total += ends[bucket];
    ends[bucket] = total;
  }
  for (int bucket = 0; bucket < 256; ++bucket) {
    while (next[bucket] < ends[bucket]) {
      int at = next[bucket];
      u16 index = work->order[at];
      int destination = (PreparedSpriteKey(work, index) >> shift) & 255;
      if (destination == bucket) ++next[bucket];
      else {
        int target = next[destination]++;
        work->order[at] = work->order[target];
        work->order[target] = index;
      }
    }
  }
  if (!shift) return;
  int start = first;
  for (int bucket = 0; bucket < 256; ++bucket) {
    if (ends[bucket] - start > 1) SortPreparedSpriteRange(work, start, ends[bucket], shift - 8);
    start = ends[bucket];
  }
}

static void PaintGroundAndCollectDepthObjects(const City *city, const CityMapViewConfig *view, int zoom) {
  Drawing *drawing = city->drawing;
  DrawingMapDepth *paint = &drawing->map_depth;
  paint->phase = MAP_DEPTH_COLLECT;
  paint->count = 0;
  paint->overflow = false;
  paint->preparing = true;
  paint->clip = drawing->map_target.canvas.clip;
  PaintVisibleCityMapTiles(city, view, zoom);
  CollectCityRoadTraffic(city, zoom);
  paint->preparing = false;
}

static void PaintDepthArea(const City *city, const CityMapViewConfig *view, int zoom, CityRect area, CityRect clip);

static bool SubdivideDepthArea(const City *city, const CityMapViewConfig *view, int zoom,
    CityRect area, CityRect clip) {
  Drawing *drawing = city->drawing;
  int width = area.right - area.left, height = area.bottom - area.top;
  bool horizontal = width >= height;
  for (int attempt = 0; attempt < 2; ++attempt, horizontal = !horizontal) {
    int length = horizontal ? width : height;
    if (length <= 1) continue;
    int middle = (horizontal ? area.left : area.top) + length / 2;
    int pixel = middle * drawing->target_scale;
    int boundary = (horizontal ? drawing->target_x : drawing->target_y) +
        pixel / 100 - (pixel < 0 && pixel % 100 != 0);
    int low = horizontal ? clip.left : clip.top, high = horizontal ? clip.right : clip.bottom;
    if (boundary <= low || boundary >= high) continue;
    CityRect first = area, second = area, first_clip = clip, second_clip = clip;
    if (horizontal) {
      first.right = second.left = middle;
      first_clip.right = second_clip.left = boundary;
    } else {
      first.bottom = second.top = middle;
      first_clip.bottom = second_clip.top = boundary;
    }
    PaintDepthArea(city, view, zoom, first, first_clip);
    if (!city->game->execution_failed) PaintDepthArea(city, view, zoom, second, second_clip);
    return true;
  }
  return false;
}

static void PaintDepthArea(const City *city, const CityMapViewConfig *view, int zoom, CityRect area, CityRect clip) {
  if (clip.left >= clip.right || clip.top >= clip.bottom) return;
  Drawing *drawing = city->drawing;
  DrawingMapDepth *paint = &drawing->map_depth;
  // The queue holds map objects, so magnifying framebuffer pixels must not
  // multiply its subdivision cost. Keep coordinate limits in physical pixels.
  // Our zoom enum grows towards the nearest view; OpenTTD's grows towards the furthest.
  i64 pixels = (i64)(area.right - area.left) * (area.bottom - area.top) * (1 << (2 - zoom));
  if ((pixels > CITY_MAP_DEPTH_PIXEL_LIMIT || clip.right - clip.left > 16000 || clip.bottom - clip.top > 16000) &&
      SubdivideDepthArea(city, view, zoom, area, clip)) return;
  drawing->map_state.paint_rect = area;
  drawing->map_target.canvas.clip = clip;
  SimulationRandomState random = drawing->animation.random;
  PaintGroundAndCollectDepthObjects(city, view, zoom);
  if (paint->overflow) {
    drawing->animation.random = random;
    if (!SubdivideDepthArea(city, view, zoom, area, clip)) {
      SetGameError(city->game, SC2K_ERROR_MAP_SPRITE_CAPACITY);
      city->game->execution_failed = true;
    }
    return;
  }
  if (drawing->frame_stats.calls.active) ++drawing->frame_stats.repaint_regions;
  SortPreparedSpriteRange(&drawing->map_sprite_work, 0, paint->count, 24);
  paint->phase = MAP_DEPTH_OBJECTS;
  DrawPreparedCityMapSprites(drawing);
}

void DrawDepthSortedCityMap(const City *city, const CityMapViewConfig *view, int zoom) {
  Drawing *drawing = city->drawing;
  DrawingMapDepth previous = drawing->map_depth;
  CityRect area = drawing->map_state.paint_rect;
  CityRect clip = drawing->map_target.canvas.clip;
  drawing->map_depth = (DrawingMapDepth){.bridge_palettes = city->game->resources->bridge_palettes,
      .underworld_palettes = city->game->resources->underworld_palettes,
      .car_palettes = city->game->resources->road_vehicle_palettes};
  PaintDepthArea(city, view, zoom, area, clip);
  drawing->map_depth = previous;
  drawing->map_state.paint_rect = area;
  drawing->map_target.canvas.clip = clip;
}
