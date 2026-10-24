#include "drawing/maps/map_view.h"

#include "drawing/state.h"

// Drawing map-view clipping, display layers and cache invalidation.

void InitializeDrawingMapViews(Drawing *drawing) {
  drawing->map_view_storage = (CityMapViewConfig){0};
  drawing->drag_view_storage = (CityMapViewConfig){0};
  drawing->default_map_view = &drawing->map_view_storage;
  drawing->drag_map_view = &drawing->drag_view_storage;
  drawing->default_map_view->zoom = CITY_MAP_ZOOM_LARGE;
}

void InvalidateCityMapRect(Drawing *drawing, const CityMapViewConfig *view, CityRect rect) {
  if (rect.left >= rect.right || rect.top >= rect.bottom) return;
  int scale = drawing->map_pixel_scale * (view->magnified_large_zoom ? 2 : 1);
  CityPoint offset = drawing->scroll_pixels;
  CityRect screen = {rect.left * scale + offset.x, rect.top * scale + offset.y,
      rect.right * scale + offset.x, rect.bottom * scale + offset.y};
  MarkScreenDirty(&drawing->screen_damage, screen);
  // A scroll can copy the previous overlay along with the retained map pixels.
  const NativeMapCache *cache = &drawing->main_cache;
  if (cache->valid) {
    int dx = (drawing->map_state.origin_x - cache->origin_x) * scale + offset.x - cache->target_x;
    int dy = (drawing->map_state.origin_y - cache->origin_y) * scale + offset.y - cache->target_y;
    if (dx || dy) MarkScreenDirty(&drawing->screen_damage,
        (CityRect){screen.left + dx, screen.top + dy, screen.right + dx, screen.bottom + dy});
  }
}

u32 CityMapDrawOptions(const Drawing *drawing) {
  return (drawing->map_state.buildings_enabled ? CITY_MAP_DRAW_BUILDINGS : 0) |
      (drawing->map_state.tile_details_enabled ? CITY_MAP_DRAW_DETAILS : 0) |
      (drawing->map_state.zone_ground_mode ? CITY_MAP_DRAW_ZONE_GROUND : 0) |
      (drawing->paint_placement ? CITY_MAP_DRAW_PLACEMENT : 0);
}

bool CityMapTileDetailsVisible(const Drawing *drawing) {
  return drawing->map_state.tile_details_enabled != 0;
}

bool CityMapLayerVisible(const Drawing *drawing, CityMapLayer layer) {
  const CityMapDrawingState *map = &drawing->map_state;
  switch (layer) {
  case CITY_MAP_LAYER_BUILDINGS: return map->buildings_enabled != 0;
  case CITY_MAP_LAYER_NETWORKS: return map->networks_enabled != 0;
  case CITY_MAP_LAYER_WATER: return map->water_enabled != 0;
  case CITY_MAP_LAYER_TREES: return map->trees_enabled != 0;
  case CITY_MAP_LAYER_SIGNS: return map->label_display != 0;
  case CITY_MAP_LAYER_VEHICLES: return map->vehicles_enabled != 0;
  default: return false;
  }
}

int CityViewportMapZoom(const CityExtraMapView *view) {
  return IntMin(view->zoom, CITY_MAP_ZOOM_LARGE);
}

int CityViewportMagnification(const CityExtraMapView *view) {
  return view->zoom == CITY_CANVAS_MAP_ZOOM_MAGNIFIED ? 2 : 1;
}

// Shared appearance changes affect every native map viewport.
void InvalidateCityMapViews(Drawing *drawing) {
  drawing->map_snapshot.valid = false;
  drawing->main_cache.valid = false;
  for (int index = 0; index < CITY_EXTRA_MAP_VIEW_COUNT; ++index)
    drawing->views[index].cache.valid = false;
}

void SetDrawingMapClientRect(Drawing *drawing, CityRect rect) {
  drawing->client_rect = rect;
}

CityRect DrawingMapClientRect(const Drawing *drawing) {
  return drawing->client_rect;
}

// Set the renderer clip from the requested and host client rectangles.
bool PrepareCityMapPaintClip(Drawing *drawing, CityRect request) {
  CityRect client = drawing->client_rect;
  CityRect *clip = &drawing->map_state.paint_clip;
  if (request.left >= request.right || request.top >= request.bottom) {
    *clip = client;
  } else {
    clip->left = IntMax(request.left, client.left);
    clip->top = IntMax(request.top, client.top);
    clip->right = IntMin(request.right, client.right);
    clip->bottom = IntMin(request.bottom, client.bottom);
    if (clip->left >= clip->right || clip->top >= clip->bottom) {
      *clip = (CityRect){0};
      return false;
    }
  }
  if (clip->top > 1)
    --clip->top;
  drawing->map_state.paint_rect = *clip;
  return true;
}

// Clear the clip state after an immediate sprite draw.
void ResetCityMapPaintClip(Drawing *drawing) {
  drawing->map_state.paint_clip_active = 0;
  drawing->map_state.paint_clip = (CityRect){0};
}
