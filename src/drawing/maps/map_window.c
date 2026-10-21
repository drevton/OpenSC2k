#include "city/events/events.h"
#include "city/presentation.h"
#include "city/growth/growth.h"
#include "city/maps/layers.h"
#include "city/tools/tools.h"
#include "game/state.h"
#include "audio/state.h"
#include "city/state.h"
#include "drawing/state.h"
#include "assets/state.h"
#include "ui/state.h"
#include "ui/input.h"
#include "ui/map.h"
#include "drawing/drawing_internal.h"
#include "drawing/maps/map_render.h"
#include "drawing/maps/tables.h"


// Drawing subsystem: map window.

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

// Moving sprites retain old/new screen footprints in the normal frame renderer. Demolition may supply an unset
// last-disaster slot; reject it before reading the event array.
void InvalidateCityMapEvent(City *city, int slot) {
  if (slot < 0 || slot >= city->events.capacity) return;
  const CityMapEvent *event = &city->events.event[slot];
  if (event->type == CITY_MAP_EVENT_UNUSED) return;
  InvalidateTile(city, event->tile);
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

void ToggleCityMapLayer(Game *game, CityMapLayer layer) {
  CityMapDrawingState *map = &game->drawing->map_state;
  switch (layer) {
  case CITY_MAP_LAYER_BUILDINGS: map->buildings_enabled = !map->buildings_enabled; break;
  case CITY_MAP_LAYER_NETWORKS: map->networks_enabled = !map->networks_enabled; break;
  case CITY_MAP_LAYER_WATER: map->water_enabled = !map->water_enabled; break;
  case CITY_MAP_LAYER_TREES: map->trees_enabled = !map->trees_enabled; break;
  case CITY_MAP_LAYER_SIGNS: map->label_display = !map->label_display; break;
  case CITY_MAP_LAYER_VEHICLES:
    map->vehicles_enabled = !map->vehicles_enabled;
    if (!map->vehicles_enabled) ClearCityVehicleVisualization(game->drawing);
    break;
  default: return;
  }
  RefreshCityMapViews(game);
}

// Shared appearance changes affect every native map viewport.
void InvalidateCityMapViews(Drawing *drawing) {
  drawing->map_snapshot.valid = false;
  drawing->main_cache.valid = false;
  for (int index = 0; index < CITY_EXTRA_MAP_VIEW_COUNT; ++index)
    drawing->views[index].cache.valid = false;
}

void RefreshCityMapViews(Game *game) {
  Drawing *drawing = game->drawing;
  InvalidateCityMapViews(drawing);
  MarkScreenDirty(&drawing->screen_damage, (CityRect){0, 0,
      drawing->framebuffer.width, drawing->framebuffer.height});
}

void RefreshCityMapTiles(City *city) {
  for (int layer = 0; layer < CITY_MAP_CHANGE_COUNT; ++layer) InvalidateMapLayer(city, layer);
}

// Copy the clipped rectangle and paint arguments to the renderer globals.
int StoreCityMapPaintParameters(Drawing *drawing, CityPaintBuffer pixels, CitySpriteCatalog catalog, u32 pixel_stride,
    int mode_word, CityRect *rect) {
  CityMapPaintParameters *parameters = &drawing->map_state.paint_parameters;
  CityRect copy = *rect;
  parameters->pixels = pixels;
  parameters->catalog = catalog;
  parameters->mode_word = mode_word;
  parameters->pixel_stride = pixel_stride;
  parameters->rect = copy;
  return copy.right;
}

void Sc2kSetCityMapClientRect(Drawing *drawing, CityRect rect) {
  drawing->client_rect = rect;
}

void Sc2kGetCityMapClientRect(Drawing *drawing, CityRect *rect) {
  *rect = drawing->client_rect;
}

// Set the renderer clip from the requested and host client rectangles.
bool PrepareCityMapClippedPaint(Drawing *drawing, CityPaintBuffer pixels,
    u32 pixel_stride, int mode_word, CityRect *requested) {
  CityRect client;
  ReadCityMapClientRect(drawing, &client);
  const CityRect *ClientRect = &client;
  CityRect *clip = &drawing->map_state.paint_clip;
  CityRect request = *requested;
  if (request.left >= request.right || request.top >= request.bottom) {
    *clip = *ClientRect;
  } else {
    clip->left = IntMax(request.left, ClientRect->left);
    clip->top = IntMax(request.top, ClientRect->top);
    clip->right = IntMin(request.right, ClientRect->right);
    clip->bottom = IntMin(request.bottom, ClientRect->bottom);
    if (clip->left >= clip->right || clip->top >= clip->bottom) {
      *clip = (CityRect){0};
      return false;
    }
  }
  if (clip->top > 1)
    --clip->top;
  StoreCityMapPaintParameters(drawing, pixels, drawing->map_state.sprite_catalog,
      pixel_stride, mode_word, &drawing->map_state.paint_clip);
  return true;
}

// Clear the clip state after an immediate sprite draw.
void ResetCityMapPaintClip(Drawing *drawing) {
  drawing->map_state.paint_clip_active = 0;
  drawing->map_state.paint_clip = (CityRect){0};
}

void Sc2kRefreshCityStatusPanel(City *city) {
  RefreshCityCursorStatus(city, 0);
}

// Install or restore the city's selected cursor.
u32 SetCityCursor(City *city, int cursor_id, int restore) {
  CityInterfaceState *ui = city->game->ui;
  u32 ranked_events_suppressed = city->game->ranked_events_suppressed;
  if (ui->wait_cursor_active != 0) {
    InstallCityMapCursor(city->game, LoadCityWaitCursor(city->game));
    int count;
    do {
      count = ShowCityMapCursor(city->game);
    } while (count == 0);
    return (u32)count;
  }
  if (restore == 0 || ranked_events_suppressed != 0) {
    u32 result = ranked_events_suppressed;
    ui->cursor_id = cursor_id;
    if (ranked_events_suppressed == 0) {
      if (cursor_id != CITY_CURSOR_ARROW) {
        RefreshCityCursorStatus(city, 0);
      }
      result = InstallCityMapCursor(city->game, ui->cursor_handle[cursor_id]);
    }
    ui->selected_cursor = cursor_id;
    return result;
  }
  if (ui->selected_cursor != CITY_CURSOR_ARROW) {
    RefreshCityCursorStatus(city, 0);
  }
  return InstallCityMapCursor(city->game, ui->cursor_handle[ui->selected_cursor]);
}


void InvalidateCityMapSelectionTile(City *city, int x, int y) {
  TileIndex tile = MapTileXYOrInvalid(city, x, y);
  InvalidateTile(city, tile);
}

// Invalidate an inclusive, normalized selection footprint.
void InvalidateCityMapSelectionRectangle(City *city, int first_x, int first_y,
    int last_x, int last_y) {
  if (!IsMapCoordinateValid(city, first_x, first_y) || !IsMapCoordinateValid(city, last_x, last_y))
    return;
  if (last_y < first_y) {
    int swap = first_y;
    first_y = last_y;
    last_y = swap;
  }
  if (last_x < first_x) {
    int swap = first_x;
    first_x = last_x;
    last_x = swap;
  }
  for (int y = first_y; y <= last_y; ++y) {
    for (int x = first_x; x <= last_x; ++x)
      InvalidateCityMapSelectionTile(city, x, y);
  }
}

// Schedule erasure of the previous selection footprint.
void FlushCityMapSelectionRedraw(City *city) {
  Drawing *drawing = city->game->drawing;
  if (drawing->map_state.redraw_state == 0)
    return;
  CityMapSelectionBounds *bounds = &drawing->map_state.selection_bounds;
  InvalidateCityMapSelectionRectangle(city, bounds->min_x, bounds->min_y, bounds->max_x, bounds->max_y);
  drawing->map_state.redraw_state = 0;
}

bool PresentCityMapView(Game *game) {
  Drawing *drawing = game->drawing;
  if (drawing->present_map != NULL)
    return drawing->present_map(game);
  // A canvas-free native owner has nothing to flush.
  return true;
}
