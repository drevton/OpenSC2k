#ifndef SC2K_MAP_VIEW_H
#define SC2K_MAP_VIEW_H

#include "city/types.h"

typedef struct CityExtraMapView CityExtraMapView;

// Borrowed for one synchronous paint/damage visit. A null view selects Drawing's main map target.
typedef struct CityMapPaint {
  City *city;
  CityExtraMapView *view;
} CityMapPaint;

// Main-map renderer selector: distinct from the dialog's combined codes.
typedef enum CityMapDataMode {
  CITY_DATA_MODE_NONE = 0,
  CITY_DATA_MODE_TRAFFIC = 1,
  CITY_DATA_MODE_DENSITY = 2,
  CITY_DATA_MODE_DENSITY_HISTORY = 3,
  CITY_DATA_MODE_CRIME = 4,
  CITY_DATA_MODE_POLICE_COVERAGE = 5,
  CITY_DATA_MODE_POLLUTION = 6,
  CITY_DATA_MODE_LAND_VALUE = 7,
  CITY_DATA_MODE_FIRE_COVERAGE = 8,
  CITY_DATA_MODE_POWER = 9,
  CITY_DATA_MODE_WATER = 10,
  CITY_DATA_MODE_GROWTH = 11,
  CITY_DATA_MODE_COUNT = 12
} CityMapDataMode;

// Navigation

enum { CITY_MAP_ORIGIN_BASE_ALIGNMENT = 4 };

static inline int CityMapOriginAlignment(int zoom) {
  return CITY_MAP_ORIGIN_BASE_ALIGNMENT << zoom;
}

// Original scrollbar notification values and axis/control selectors.
typedef enum CityScrollCode {
  CITY_SCROLL_LINE_BACKWARD = 0,
  CITY_SCROLL_LINE_FORWARD = 1,
  CITY_SCROLL_PAGE_BACKWARD = 2,
  CITY_SCROLL_PAGE_FORWARD = 3,
  CITY_SCROLL_THUMB_POSITION = 4,
  CITY_SCROLL_THUMB_TRACK = 5,
  CITY_SCROLL_START = 6,
  CITY_SCROLL_END = 7,
  CITY_SCROLL_FINISHED = 8
} CityScrollCode;

enum {
  CITY_SCROLL_HORIZONTAL = 0,
  CITY_SCROLL_VERTICAL = 1,
  CITY_SCROLL_CONTROL = 2,
  CITY_SCROLL_KEEP_POSITION = -1
};

enum {
  CITY_MAP_ZOOM_SMALL = 0, CITY_MAP_ZOOM_MEDIUM = 1, CITY_MAP_ZOOM_LARGE = 2,
  CITY_MAP_MAX_TILE_WIDTH = 256,
  CITY_MAP_POINTER_INACTIVE = 0, CITY_MAP_POINTER_HOVER = 1,
  CITY_MAP_DISPLAY_UNDERGROUND = 0, CITY_MAP_DISPLAY_ZONE_GROUND = 1,
  CITY_MAP_DISPLAY_TILE_DETAILS = 2, CITY_MAP_DISPLAY_SIGNS = 3,
  CITY_MAP_DISPLAY_BUILDINGS = 4, CITY_MAP_DISPLAY_FLAG_COUNT = 5,
};

typedef struct CityMapViewConfig CityMapViewConfig;

typedef enum CityMapLayer {
  CITY_MAP_LAYER_BUILDINGS, CITY_MAP_LAYER_NETWORKS, CITY_MAP_LAYER_WATER,
  CITY_MAP_LAYER_TREES, CITY_MAP_LAYER_SIGNS, CITY_MAP_LAYER_VEHICLES, CITY_MAP_LAYER_COUNT
} CityMapLayer;

typedef struct Drawing Drawing;
bool CityMapLayerVisible(const Drawing *drawing, CityMapLayer layer);
bool CityMapTileDetailsVisible(const Drawing *drawing);
typedef enum CityMapDrawOption {
  CITY_MAP_DRAW_BUILDINGS = 1 << 0, CITY_MAP_DRAW_DETAILS = 1 << 1,
  CITY_MAP_DRAW_ZONE_GROUND = 1 << 2, CITY_MAP_DRAW_PLACEMENT = 1 << 3,
} CityMapDrawOption;
u32 CityMapDrawOptions(const Drawing *drawing);

typedef struct {
  u32 surface[5];
  u32 underground[5];
} CityMapOverlaySettings;

// Loaded camera parameters awaiting restoration; no executable layout or reserved fields.
typedef struct {
  u16 x, y, zoom;
} CityLoadedMapView;

typedef struct {
  i16 min_y, max_y, min_x, max_x;
} CityMapSelectionBounds;

// Camera and pointer state; pixel targets belong to the bound canvas, not to a viewport.
struct CityMapViewConfig {
  int vertical_scroll_position, horizontal_scroll_position;
  CityRect pointer_exclusion_rect, view_rect;
  int pointer_x, pointer_y;
  u8 zoom, rotation;
  u8 pointer_command_stage, pointer_drag_active, pointer_moved, pan_capture_active, magnified_large_zoom;
};

// Logical sprite zoom and display magnification for an extra map camera; no window or input state is read.
int CityViewportMapZoom(const CityExtraMapView *view);
int CityViewportMagnification(const CityExtraMapView *view);

// Original camera levels 0..3; level 3 magnifies the large sprite set.
// Presentation levels include the extra magnified view of the large map.
// Keep these separate from the original three CITY_MAP_ZOOM_* values.
typedef enum CityCanvasMapZoom {
  CITY_CANVAS_MAP_ZOOM_SMALL = 0,
  CITY_CANVAS_MAP_ZOOM_MEDIUM = 1,
  CITY_CANVAS_MAP_ZOOM_LARGE = 2,
  CITY_CANVAS_MAP_ZOOM_MAGNIFIED = 3,
  CITY_CANVAS_MAP_ZOOM_COUNT = 4,
} CityCanvasMapZoom;
// Scale is a percentage of the logical 8/16/32/64-pixel tile widths.
static inline int CityMapZoomLimit(int scale) {
  int zoom = CITY_CANVAS_MAP_ZOOM_MAGNIFIED;
  while (zoom > CITY_CANVAS_MAP_ZOOM_SMALL && (8 << zoom) * scale > CITY_MAP_MAX_TILE_WIDTH * 100)
    --zoom;
  return zoom;
}

#endif
