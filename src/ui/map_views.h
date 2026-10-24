#ifndef SC2K_UI_MAP_VIEWS_H
#define SC2K_UI_MAP_VIEWS_H

#include "drawing/maps/map_camera.h"

typedef struct Game Game;
typedef struct RoadCar RoadCar;
typedef struct CityExtraMapView CityExtraMapView;
typedef struct CityMapPaint CityMapPaint;
typedef struct Window Window;
typedef struct CanvasSDLEvent CanvasSDLEvent;
typedef struct CityCanvasDesktop CityCanvasDesktop;

enum {
  CITY_VIEWPORT_WIDTH = 300, CITY_VIEWPORT_HEIGHT = 268,
  CITY_VIEWPORT_TOP = 17, CITY_VIEWPORT_BOTTOM = 36
};

typedef enum CityViewportControl {
  CITY_VIEWPORT_CONTROL_NONE,
  CITY_VIEWPORT_ZOOM_IN, CITY_VIEWPORT_ZOOM_OUT,
  CITY_VIEWPORT_ROTATE_CLOCKWISE, CITY_VIEWPORT_ROTATE_COUNTERCLOCKWISE,
  CITY_VIEWPORT_MAIN_CENTER, CITY_VIEWPORT_CENTER,
  CITY_VIEWPORT_SHADE, CITY_VIEWPORT_DEFAULT_SIZE, CITY_VIEWPORT_STICKY
} CityViewportControl;

// Logical window geometry shared by viewport painting, hit testing and hover help.
CityRect CityViewportContentBounds(const Window *window);
CityViewportControl CityViewportControlAt(const Window *window, int x, int y);

// UI map windows coordinate viewport damage and Drawing targets. Map pixel rectangles are view-local
// unless an operation explicitly converts them to desktop pixels.
void OpenCityMapView(City *city);
void PublishCityMapChanges(City *city);
void PublishCityRoadCarDamage(City *city, const RoadCar *previous, const RoadCar *current);
// Paint the main and extra map views using one shared frame-start scroll source.
// Requires the prepared main target, collected sprite/effect/placement damage and a scoped paint City.
// Main animation replay must be prepared. Restore main camera/target state and disable its paint clip on all returns.
// Return false on target-binding or retained Game error; do not clear the error or advance live City state.
bool RenderCityMapViews(City *city);
// Synchronous sprite-damage callback. Context is a CityMapPaint; converts map pixels and clips to its window.
void MarkCityMapSpriteDamage(CityRect rect, void *context);
void MarkCityMapRect(CityRect rect, void *context);
// Resolve the viewport's physical content position, scroll and shake before binding Drawing's raster target.
bool BindCityMapTarget(const CityMapPaint *paint);
bool HandleCityMainMapPan(City *city, const CanvasSDLEvent *event);

// Recompute logical viewport dimensions and constrain the camera after resize, rotation or zoom changes.
void SyncCityMapViewSize(const City *city, CityExtraMapView *view, Window *window);
void OffsetCityMapPixels(City *city, CityExtraMapView *view, int dx, int dy);
void PanCityMapWithScroll(City *city, CityExtraMapView *view, const CanvasSDLEvent *event);
void UpdateCityMapZoomLimits(Game *game);
bool PollCityMapViewControls(City *city);

#endif
