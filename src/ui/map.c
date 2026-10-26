#include "ui/map.h"
#include "ui/map_visibility.h"

#include "city/events/events.h"
#include "city/maps/terrain.h"
#include "city/maps/map.h"
#include "city/presentation.h"
#include "city/state.h"
#include "drawing/maps/map_camera.h"
#include "drawing/maps/map_effects.h"
#include "drawing/maps/map_vehicles.h"
#include "drawing/maps/map_damage.h"
#include "drawing/maps/map_render.h"
#include "drawing/maps/road_cars.h"
#include "drawing/maps/map_view.h"
#include "drawing/state.h"
#include "game/state.h"
#include "ui/charts/charts.h"
#include "ui/dialogs/map_data.h"
#include "ui/map_views.h"
#include "ui/state.h"
#include "ui/updates.h"
#include "ui/windows/windows.h"

bool InvalidateCityMap(Game *game, const CityRect *rect) {
  Drawing *drawing = game->drawing;
  if (!drawing->map_navigation_enabled || game->ui == NULL) {
    return false;
  }
  CityRect bounds;
  if (rect != NULL)
    bounds = *rect;
  else
    bounds = DrawingMapClientRect(drawing);
  // Map renderer coordinates become native window pixels at this boundary.
  int scale = drawing->map_pixel_scale > 0 ? drawing->map_pixel_scale : 1;
  bounds = (CityRect){bounds.left * scale, bounds.top * scale, bounds.right * scale, bounds.bottom * scale};
  return InvalidateCityCanvasWindow(game, 0, &bounds);
}

void RefreshCityDrawing(City *city) {
  RefreshCityMapViews(city->game);
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

void RefreshCityMapViews(Game *game) {
  Drawing *drawing = game->drawing;
  InvalidateCityMapViews(drawing);
  MarkScreenDirty(&drawing->screen_damage, (CityRect){0, 0,
      drawing->framebuffer.width, drawing->framebuffer.height});
}

// Moving sprites retain old/new screen footprints in the normal frame renderer. Demolition may supply an unset
// last-disaster slot; reject it before reading the event array.
void InvalidateCityMapEvent(City *city, int slot) {
  if (slot < 0 || slot >= city->events.capacity) return;
  const CityMapEvent *event = &city->events.event[slot];
  if (event->type == CITY_MAP_EVENT_UNUSED) return;
  InvalidateTile(city, event->tile);
}

void RefreshCityMapTiles(City *city) {
  for (int layer = 0; layer < CITY_MAP_CHANGE_COUNT; ++layer) InvalidateMapLayer(city, layer);
}

void SetCityTileScanPresentation(City *city, int direction_mask) {
  if (city->drawing) city->drawing->map_state.tile_scan_mask = direction_mask;
}

u32 BeginCityVolcanoPresentation(Game *game, TileIndex location) {
  Drawing *drawing = game->drawing;
  CityMapViewConfig *view = drawing->default_map_view;
  u32 label = drawing->map_state.label_display;
  drawing->map_state.label_display = 0;
  City *city = game->city;
  CenterCityMapViewAtTile(city, NULL, MapTileX(city, location), MapTileY(city, location));
  if (view->zoom == CITY_MAP_ZOOM_SMALL) ZoomCityMapViewIn(city, view);
  return label;
}

void RestoreCityVolcanoPresentation(City *city, u32 labels) {
  city->drawing->map_state.label_display = labels;
}

void StartCityEarthquakePresentation(City *city) {
  StartCityScreenShake(city->drawing);
}

bool SetCityDemolitionSuppressed(City *city, bool suppressed) {
  Drawing *drawing = city->drawing;
  bool previous = drawing && drawing->demolition_suppressed;
  if (drawing) drawing->demolition_suppressed = suppressed;
  return previous;
}

void ShowCityDemolitionEffect(City *city, TileIndex tile, bool large) {
  if (!IsMapTileValid(city, tile)) return;
  int elevation = ReadCitySurfaceHeight(&city->tile[tile]);
  StartCityDemolitionEffect(city->drawing, tile, elevation, large);
}

void MarkCityMapSpriteDamage(CityRect rect, void *context) {
  CityMapPaint *paint = context;
  const City *city = paint->city;
  Drawing *drawing = city->drawing;
  if (rect.left >= rect.right || rect.top >= rect.bottom) return;
  rect = CityMapScreenRect(rect, paint);
  MarkScreenDirty(&drawing->screen_damage, rect);
  MarkScreenDirty(&drawing->world_damage, rect);
}

void MarkCityMapRect(CityRect rect, void *context) {
  CityMapPaint *paint = context;
  const City *city = paint->city;
  Drawing *drawing = city->drawing;
  if (rect.left >= rect.right || rect.top >= rect.bottom) return;
  MarkScreenDirty(&drawing->screen_damage, CityMapScreenRect(rect, paint));
}

void PublishCityMapTileDamage(City *city, TileIndex tile) {
  Drawing *drawing = city->game ? city->drawing : NULL;
  CityInterfaceState *ui = city->game ? city->game->ui : NULL;
  if (!drawing || !drawing->tile_damage_enabled || !ui) return;
  ResizeScreenDamage(&drawing->screen_damage, drawing->framebuffer.width, drawing->framebuffer.height);
  int magnification = drawing->default_map_view->magnified_large_zoom ? 2 : 1;
  ProjectCityMapTileDamage(city, tile,
      (CityMapCamera){drawing->map_state.origin_x, drawing->map_state.origin_y, drawing->default_map_view->zoom,
          drawing->map_width / magnification, drawing->map_height / magnification,
          CityMapViewRotation(drawing->default_map_view)},
      MarkCityMapRect, &(CityMapPaint){city, NULL});
  for (int index = 0; index < CITY_EXTRA_MAP_VIEW_COUNT; ++index) {
    CityExtraMapView *view = &drawing->views[index];
    Window *window = FindCityCanvasWindow(&ui->desktop, view->window_id);
    if (!window || !window->visible || window->shaded) continue;
    SyncCityMapViewSize(city, view, window);
    ProjectCityMapTileDamage(city, tile,
        (CityMapCamera){view->origin_x, view->origin_y, CityViewportMapZoom(view),
            view->width, view->height, view->rotation},
        MarkCityMapRect, &(CityMapPaint){city, view});
  }
}

static void MarkCityRoadCarDamage(const RoadCar *car, CityMapCamera camera, const CityMapPaint *paint) {
  Drawing *drawing = paint->city->drawing;
  CityExtraMapView *view = paint->view;
  int magnification = view ? CityViewportMagnification(view) :
      drawing->default_map_view->magnified_large_zoom ? 2 : 1;
  CityRect rect = CityRoadCarPixelBounds(paint->city, car, camera, drawing->map_pixel_scale * magnification);
  if (rect.left >= rect.right || rect.top >= rect.bottom) return;
  CityPoint origin = view ? view->scroll_pixels : drawing->scroll_pixels;
  if (view) {
    CityRect pixels = Sc2kCanvasDesktopPixelBounds(&paint->city->game->ui->desktop, view->window_id);
    int ui_scale = drawing->options.scale;
    origin.x += pixels.left + (3 * ui_scale + 99) / 100;
    origin.y += pixels.top + (CITY_VIEWPORT_TOP * ui_scale + 99) / 100;
  } else origin.x += drawing->screen_shake_offset * drawing->map_pixel_scale;
  rect.left += origin.x; rect.right += origin.x;
  rect.top += origin.y; rect.bottom += origin.y;
  MarkScreenDirty(&drawing->screen_damage, ClipCityMapViewportPixels(rect, paint));
}

void PublishCityRoadCarDamage(const City *city, const RoadCar *previous, const RoadCar *current) {
  Drawing *drawing = city->drawing;
  CityInterfaceState *ui = city->game->ui;
  if (!drawing->tile_damage_enabled || !ui) return;
  ResizeScreenDamage(&drawing->screen_damage, drawing->framebuffer.width, drawing->framebuffer.height);
  int magnification = drawing->default_map_view->magnified_large_zoom ? 2 : 1;
  CityMapCamera camera = {drawing->map_state.origin_x, drawing->map_state.origin_y, drawing->default_map_view->zoom,
      drawing->map_width / magnification, drawing->map_height / magnification,
      CityMapViewRotation(drawing->default_map_view)};
  CityMapPaint paint = {city, NULL};
  MarkCityRoadCarDamage(previous, camera, &paint);
  MarkCityRoadCarDamage(current, camera, &paint);
  for (int index = 0; index < CITY_EXTRA_MAP_VIEW_COUNT; ++index) {
    CityExtraMapView *view = &drawing->views[index];
    Window *window = FindCityCanvasWindow(&ui->desktop, view->window_id);
    if (!window || !window->visible || window->shaded) continue;
    SyncCityMapViewSize(city, view, window);
    camera = (CityMapCamera){view->origin_x, view->origin_y, CityViewportMapZoom(view),
        view->width, view->height, view->rotation};
    paint.view = view;
    MarkCityRoadCarDamage(previous, camera, &paint);
    MarkCityRoadCarDamage(current, camera, &paint);
  }
}

void HideCitySimulationPanels(City *city) {
  HideCityChartPanel(city, CITY_PANEL_HISTORY);
  HideCityChartPanel(city, CITY_PANEL_DEMOGRAPHICS);
  HideCityMapDataDialog(city);
  HideCityChartPanel(city, CITY_PANEL_NEIGHBORS);
  HideCityChartPanel(city, CITY_PANEL_INDUSTRY);
}
