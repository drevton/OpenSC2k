#include <string.h>

#include "game/state.h"
#include "drawing/maps/map_render.h"
#include "drawing/state.h"

typedef struct {
  CityRect rects[1024];
  i32 count;
  i32 overflow;
  i32 magnification;
} MapFragments;

// Outward rounding may make adjacent exposed display pieces share a source
// pixel at an odd window edge. Subtract the existing union so scroll copies
// never move that pixel twice. These are rectangles, not raster storage.
static void CollectUnique(CityRect rect, MapFragments *fragments, i32 first) {
  for (i32 i = first; i < fragments->count; ++i) {
    CityRect cover = fragments->rects[i];
    CityRect overlap = IntersectCityRects(rect, cover);
    if (overlap.left >= overlap.right || overlap.top >= overlap.bottom) continue;
    if (rect.top < overlap.top)
      CollectUnique((CityRect){rect.left, rect.top, rect.right, overlap.top}, fragments, i + 1);
    if (overlap.bottom < rect.bottom)
      CollectUnique((CityRect){rect.left, overlap.bottom, rect.right, rect.bottom}, fragments, i + 1);
    if (rect.left < overlap.left)
      CollectUnique((CityRect){rect.left, overlap.top, overlap.left, overlap.bottom}, fragments, i + 1);
    if (overlap.right < rect.right)
      CollectUnique((CityRect){overlap.right, overlap.top, rect.right, overlap.bottom}, fragments, i + 1);
    return;
  }
  if (fragments->count == 1024) {
    fragments->overflow = 1;
    return;
  }
  fragments->rects[fragments->count++] = rect;
}

static CityRect SourceRect(CityRect rect, i32 magnification) {
  return (CityRect){rect.left / magnification, rect.top / magnification,
      (rect.right + magnification - 1) / magnification,
      (rect.bottom + magnification - 1) / magnification};
}
static void CollectFragment(CityRect rect, void *context) {
  MapFragments *fragments = context;
  rect = SourceRect(rect, fragments->magnification);
  if (rect.left < rect.right && rect.top < rect.bottom)
    CollectUnique(rect, fragments, 0);
}
typedef struct {
  i32 magnification;
  void (*paint)(CityRect, void *);
  void *context;
} MapSourcePaint;
static void PaintSource(CityRect rect, void *context) {
  MapSourcePaint *source = context;
  source->paint(SourceRect(rect, source->magnification), source->context);
}
static void VisitMapView(const CityCanvasDesktop *desktop, u32 window_id, CityRect area,
    CityCanvasVisiblePaint paint, void *context) {
  if (window_id == 0)
    Sc2kCanvasDesktopVisitMap(desktop, area, paint, context);
  else
    Sc2kCanvasDesktopVisitViewport(desktop, window_id, area, paint, context);
}

void NativeMapCacheVisitView(const CityCanvasDesktop *desktop, u32 window_id, CityRect display_area,
    i32 magnification, void (*paint)(CityRect, void *), void *context) {
  MapFragments fragments = {.magnification = magnification};
  VisitMapView(desktop, window_id, display_area, CollectFragment, &fragments);
  if (fragments.overflow) {
    MapSourcePaint source = {magnification, paint, context};
    VisitMapView(desktop, window_id, display_area, PaintSource, &source);
  } else
    for (i32 i = 0; i < fragments.count; ++i) paint(fragments.rects[i], context);
}

static u64 RectArea(CityRect rect) {
  return (u64)(rect.right - rect.left) * (u64)(rect.bottom - rect.top);
}

// Colors, palette indices and tree coverage move together. Scratch storage is
// only used when changing visibility makes in-place fragment copies unsafe.
static void CopyScreenPixels(MapCachePixels *pixels, CityRect rect, int dx, int dy, bool snapshot) {
  if (!pixels) return;
  Canvas *canvas = &pixels->surface->canvas;
  int scale = pixels->physical_coordinates ? 100 : pixels->scale;
  int offset_x = pixels->physical_coordinates ? 0 : pixels->x;
  int offset_y = pixels->physical_coordinates ? 0 : pixels->y;
  rect = (CityRect){offset_x + (int)((i64)rect.left * scale / 100), offset_y + (int)((i64)rect.top * scale / 100),
      offset_x + (int)(((i64)rect.right * scale + 99) / 100), offset_y + (int)(((i64)rect.bottom * scale + 99) / 100)};
  dx = (int)((i64)dx * scale / 100); dy = (int)((i64)dy * scale / 100);
  rect = IntersectCityRects(rect,
      (CityRect){IntMax(0, dx), IntMax(0, dy),
          IntMin(canvas->width, canvas->width + dx), IntMin(canvas->height, canvas->height + dy)});
  if (rect.left >= rect.right || rect.top >= rect.bottom) return;
  if (pixels->surface->upload_damage) MarkScreenDirty(pixels->surface->upload_damage, rect);
  int first = dy > 0 ? rect.bottom - 1 : rect.top;
  int end = dy > 0 ? rect.top - 1 : rect.bottom, step = dy > 0 ? -1 : 1;
  const u32 *colors = snapshot ? pixels->scratch_colors : canvas->pixels;
  const u8 *indices = snapshot ? pixels->scratch_indices : pixels->surface->indices;
  for (int y = first; y != end; y += step) {
    memmove(canvas->pixels + (size_t)y * canvas->stride + rect.left,
        colors + (size_t)(y - dy) * canvas->stride + rect.left - dx,
        (size_t)(rect.right - rect.left) * sizeof(u32));
    if (pixels->surface->row_flags)
      pixels->surface->row_flags[y] |= snapshot ? UINT32_MAX : pixels->surface->row_flags[y - dy];
    memmove(pixels->surface->indices + (size_t)y * pixels->surface->index_stride + rect.left,
        indices + (size_t)(y - dy) * pixels->surface->index_stride + rect.left - dx, rect.right - rect.left);
  }
}

static void ScrollFragment(u8 *buffer, i32 stride, CityRect rect, i32 dx, i32 dy,
    void (*paint)(CityRect, void *), void *context, NativeMapCache *cache, MapCachePixels *pixels) {
  i32 width = rect.right - rect.left, height = rect.bottom - rect.top;
  if (!dx && !dy) {
    cache->reused_pixels += RectArea(rect);
    return;
  }
  if (dx <= -width || dx >= width || dy <= -height || dy >= height) {
    paint(rect, context);
    cache->painted_pixels += RectArea(rect);
    return;
  }
  CityRect retained = {rect.left + IntMax(dx, 0), rect.top + IntMax(dy, 0),
      rect.right + IntMin(dx, 0), rect.bottom + IntMin(dy, 0)};
  i32 first = dy > 0 ? retained.bottom - 1 : retained.top;
  i32 end = dy > 0 ? retained.top - 1 : retained.bottom;
  i32 step = dy > 0 ? -1 : 1;
  if (buffer) for (i32 y = first; y != end; y += step)
    memmove(buffer + y * stride + retained.left,
        buffer + (y - dy) * stride + retained.left - dx, (size_t)(retained.right - retained.left));
  CopyScreenPixels(pixels, retained, dx, dy, false);
  cache->reused_pixels += RectArea(retained);
  cache->copied_pixels += RectArea(retained);
  if (dx != 0) {
    CityRect strip = {dx > 0 ? rect.left : retained.right, rect.top,
        dx > 0 ? retained.left : rect.right, rect.bottom};
    paint(strip, context);
    cache->painted_pixels += RectArea(strip);
  }
  if (dy != 0) {
    CityRect strip = {retained.left, dy > 0 ? rect.top : retained.bottom,
        retained.right, dy > 0 ? retained.top : rect.bottom};
    paint(strip, context);
    cache->painted_pixels += RectArea(strip);
  }
}

// Retain the union of visible fragments, including pixels crossing an artificial
// fragment boundary above or below a window. Scratch prevents one destination
// fragment from overwriting another's source. One-rectangle pans stay in-place.

static void CopyRetained(const u8 *source, CityRect rect, const NativeMapCache *previous, u8 *buffer, int stride,
    int dx, int dy, NativeMapCache *cache, MapCachePixels *pixels) {
  for (int index = 0; index < previous->count; ++index) {
    CityRect cover = previous->fragments[index];
    CityRect copy = IntersectCityRects(rect, cover);
    if (copy.left >= copy.right || copy.top >= copy.bottom) continue;
    if (buffer) for (int y = copy.top; y < copy.bottom; ++y)
      memcpy(buffer + y * stride + copy.left,
          source + (y - dy) * stride + copy.left - dx, (size_t)(copy.right - copy.left));
    CopyScreenPixels(pixels, copy, dx, dy, true);
    cache->reused_pixels += RectArea(copy);
    cache->copied_pixels += RectArea(copy);
  }
}

enum { kInPlaceScrollFragments = 8 };

// A common translation can move all visible pieces in place when scanlines
// follow the scroll direction. Copy every retained pixel before repainting any
// exposed strip, including pieces separated by the toolbar or another window.
static u64 CopyVisibleScrollPixels(const MapFragments *fragments, const NativeMapCache *previous,
    MapCachePixels *pixels, int dx, int dy) {
  SpriteSurface *surface = pixels->surface;
  Canvas *canvas = &surface->canvas;
  CityRect bounds = {canvas->width, canvas->height, 0, 0};
  u64 copied = 0;
  for (int i = 0; i < fragments->count; ++i)
    for (int j = 0; j < previous->count; ++j) {
      CityRect copy = IntersectCityRects(fragments->rects[i], previous->fragments[j]);
      if (copy.left >= copy.right || copy.top >= copy.bottom) continue;
      bounds = UnionCityRects(bounds, copy);
      copied += RectArea(copy);
      if (surface->upload_damage) MarkScreenDirty(surface->upload_damage, copy);
    }
  if (!copied) return 0;
  int first = dy > 0 ? bounds.bottom - 1 : bounds.top;
  int end = dy > 0 ? bounds.top - 1 : bounds.bottom, step = dy > 0 ? -1 : 1;
  for (int y = first; y != end; y += step) {
    // Two disjoint sets of at most eight intervals have at most fifteen
    // intersection intervals. These 128 bytes are scanline-local scratch.
    CityPoint spans[2 * kInPlaceScrollFragments];
    int count = 0;
    for (int i = 0; i < fragments->count; ++i) {
      CityRect destination = fragments->rects[i];
      if (y < destination.top || y >= destination.bottom) continue;
      for (int j = 0; j < previous->count; ++j) {
        CityRect source = previous->fragments[j];
        if (y < source.top || y >= source.bottom) continue;
        int left = IntMax(destination.left, source.left), right = IntMin(destination.right, source.right);
        if (left >= right) continue;
        int at = count++;
        while (at > 0 && (dx > 0 ? spans[at - 1].x < left : spans[at - 1].x > left)) {
          spans[at] = spans[at - 1];
          --at;
        }
        spans[at] = (CityPoint){left, right};
      }
    }
    for (int span = 0; span < count; ++span) {
      int left = spans[span].x, width = spans[span].y - left;
      memmove(canvas->pixels + (size_t)y * canvas->stride + left,
          canvas->pixels + (size_t)(y - dy) * canvas->stride + left - dx, (size_t)width * sizeof(u32));
      memmove(surface->indices + (size_t)y * surface->index_stride + left,
          surface->indices + (size_t)(y - dy) * surface->index_stride + left - dx, width);
    }
    if (count && surface->row_flags) surface->row_flags[y] |= surface->row_flags[y - dy];
  }
  return copied;
}

// Subtract the previous visible rectangles. Only newly uncovered map pixels need painting.
static void PaintNewlyVisible(CityRect rect, const NativeMapCache *cache, i32 previous,
    void (*paint)(CityRect, void *), void *context, u64 *painted) {
  for (i32 index = previous; index < cache->count; ++index) {
    CityRect cover = cache->fragments[index];
    CityRect overlap = IntersectCityRects(rect, cover);
    if (overlap.left >= overlap.right || overlap.top >= overlap.bottom)
      continue;
    if (rect.top < overlap.top)
      PaintNewlyVisible((CityRect){rect.left, rect.top, rect.right, overlap.top}, cache, index + 1,
          paint, context, painted);
    if (overlap.bottom < rect.bottom)
      PaintNewlyVisible((CityRect){rect.left, overlap.bottom, rect.right, rect.bottom}, cache, index + 1,
          paint, context, painted);
    if (rect.left < overlap.left)
      PaintNewlyVisible((CityRect){rect.left, overlap.top, overlap.left, overlap.bottom}, cache, index + 1,
          paint, context, painted);
    if (overlap.right < rect.right)
      PaintNewlyVisible((CityRect){overlap.right, overlap.top, rect.right, overlap.bottom}, cache, index + 1,
          paint, context, painted);
    return;
  }
  paint(rect, context);
  *painted += RectArea(rect);
}

void SaveMapCacheScrollSource(MapCachePixels *pixels) {
  Canvas *canvas = &pixels->surface->canvas;
  memcpy(pixels->scratch_colors, canvas->pixels, (size_t)canvas->stride * canvas->height * sizeof(u32));
  memcpy(pixels->scratch_indices, pixels->surface->indices,
      (size_t)pixels->surface->index_stride * canvas->height);
}

void DrawMapCachePixels(Drawing *drawing, NativeMapCache *cache, const CityCanvasDesktop *desktop, u32 window_id,
    u8 *buffer, i32 stride, i32 width, i32 height, i32 magnification, i32 origin_x, i32 origin_y,
    MapCachePixels *pixels, void (*paint)(CityRect, void *), void *context) {
  bool physical = pixels && pixels->physical_coordinates;
  MapFragments fragments = {.magnification = physical ? 1 : magnification};
  if (physical) VisitCanvasMapPixels(desktop, window_id, (CityRect){0, 0, width, height}, CollectFragment, &fragments);
  else VisitMapView(desktop, window_id, (CityRect){0, 0, width, height}, CollectFragment, &fragments);
  if (fragments.overflow) {
    cache->valid = 0;
    if (physical) VisitCanvasMapPixels(desktop, window_id, (CityRect){0, 0, width, height}, paint, context);
    else NativeMapCacheVisitView(desktop, window_id, (CityRect){0, 0, width, height}, magnification, paint, context);
    return;
  }
  if (cache->magnification != magnification || (physical && cache->target_scale != pixels->scale))
    cache->valid = 0;
  i32 reuse = cache->valid && cache->count == fragments.count &&
      memcmp(cache->fragments, fragments.rects, (size_t)fragments.count * sizeof(CityRect)) == 0;
  int dx = origin_x - cache->origin_x, dy = origin_y - cache->origin_y;
  if (pixels && (((i64)dx * pixels->scale) % 100 || ((i64)dy * pixels->scale) % 100)) {
    cache->valid = false; reuse = false;
  }
  if (physical) {
    dx = (int)((i64)dx * pixels->scale / 100) + pixels->x - cache->target_x;
    dy = (int)((i64)dy * pixels->scale / 100) + pixels->y - cache->target_y;
  }
  bool scroll_union = cache->valid && (!reuse || cache->count > 1) && (dx != 0 || dy != 0) &&
      (pixels || (size_t)stride * height <= sizeof(drawing->scroll_source));
  bool in_place_union = scroll_union && physical && !buffer && !pixels->preserved_source &&
      fragments.count <= kInPlaceScrollFragments && cache->count <= kInPlaceScrollFragments;
  NativeMapCache previous;
  if (scroll_union) {
    if (buffer) memcpy(drawing->scroll_source, buffer, (size_t)stride * height);
    if (pixels && !pixels->preserved_source && !in_place_union) SaveMapCacheScrollSource(pixels);
    previous = *cache;
    for (int index = 0; index < previous.count; ++index) {
      CityRect *rect = &previous.fragments[index];
      rect->left += dx; rect->right += dx;
      rect->top += dy; rect->bottom += dy;
    }
    if (in_place_union) {
      u64 copied = CopyVisibleScrollPixels(&fragments, &previous, pixels, dx, dy);
      cache->reused_pixels += copied;
      cache->copied_pixels += copied;
    }
  }
  if (!cache->valid) ++cache->full_refreshes;
  for (i32 index = 0; index < fragments.count; ++index) {
    CityRect rect = fragments.rects[index];
    if (scroll_union) {
      if (!in_place_union)
        CopyRetained(drawing->scroll_source, rect, &previous, buffer, stride, dx, dy, cache, pixels);
      PaintNewlyVisible(rect, &previous, 0, paint, context, &cache->painted_pixels);
    } else if (cache->valid && dx == 0 && dy == 0) {
      u64 painted = 0;
      PaintNewlyVisible(rect, cache, 0, paint, context, &painted);
      cache->painted_pixels += painted;
      cache->reused_pixels += RectArea(rect) - painted;
    } else if (reuse)
      ScrollFragment(buffer, stride, rect, dx, dy,
          paint, context, cache, pixels);
    else {
      paint(rect, context);
      cache->painted_pixels += RectArea(rect);
    }
  }
  memcpy(cache->fragments, fragments.rects, (size_t)fragments.count * sizeof(CityRect));
  cache->count = fragments.count;
  cache->origin_x = origin_x;
  cache->origin_y = origin_y;
  cache->magnification = magnification;
  if (physical) {
    cache->target_x = pixels->x; cache->target_y = pixels->y; cache->target_scale = pixels->scale;
  }
  cache->valid = 1;
}
