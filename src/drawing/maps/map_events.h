#ifndef SC2K_DRAWING_MAPS_MAP_EVENTS_H
#define SC2K_DRAWING_MAPS_MAP_EVENTS_H

#include "drawing/types.h"
#include "drawing/maps/map_camera.h"

typedef struct CityMapEvent CityMapEvent;

typedef struct {
  CityRect *event_bounds;
  CityMapEvent *events;
  int capacity;
  int animation_phase;
  int origin_x, origin_y, zoom;
  u8 helicopter_frame;
  bool initialized;
} MapSpriteFootprints;

// Reusable Drawing storage follows event capacity, independently of City event simulation state.
// Allocation failure can leave some buffers grown; all remain owned by Drawing and can be released normally.
bool PrepareCityEventDrawingStorage(const City *city);
// Release interpolation, traversal anchors and every camera's event history after drawing has stopped.
void ReleaseCityEventDrawingStorage(Drawing *drawing);
// Compare canonical events with this camera's retained footprints. The bound target/camera must match the history;
// mark receives map-pixel rectangles synchronously and must only record damage, without changing City or the camera.
// Random effects are handled by the separate animation capture. Hash links do not affect visual equality.
void PublishCityMapEventDamage(City *city, MapSpriteFootprints *sprites, CityCanvasVisiblePaint mark, void *context);

// tile is a valid world tile. Draw into the bound target/camera using the supplied artwork zoom.
// Random artwork advances the supplied City's draw RNG;
// native frame code supplies a paint copy of City, preserving live simulation RNG.
void DrawCityMapAnimatedOverlay(City *city, TileIndex tile, int zoom, int overlay);
// slot is a valid event index. A mismatched tile suppresses artwork except for surface train engines and coaches.
void DrawCityMapEventOverlay(City *city, TileIndex tile, int zoom, int slot);
// Collect bounds without pixel writes. Call only for live, nonrandom event sprites;
// random effects use CapturedCityMapAnimationBounds from the frame's animation capture.
CityRect CityMapEventSpriteBounds(City *city, TileIndex tile, int zoom, int slot);
// Bounds for an airplane slot in an explicit camera; empty when its zoom does not display airplanes.
CityRect CityAirplaneBounds(const City *city, int slot, CityMapCamera camera);

#endif
