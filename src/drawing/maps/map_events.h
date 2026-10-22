#ifndef SC2K_DRAWING_MAPS_MAP_EVENTS_H
#define SC2K_DRAWING_MAPS_MAP_EVENTS_H

#include "drawing/canvas.h"
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

// Positions are large-zoom pixels with eight fractional bits. This history is
// presentation-only: XTHG records keep their original deterministic cadence.
typedef struct { int x, y, z; } VehiclePosition3D;

typedef struct {
  VehiclePosition3D from, target, offset;
  double started;
  u8 type, train_heading, train_path, train_previous_path; // The cached path uses existing record padding.
  u8 train_reversed; // Retain corrected path history until copied City targets catch up; uses existing padding.
} VehicleMotion;
_Static_assert(sizeof(VehicleMotion) <= 56, "vehicle history remains within its existing 64-bit record size");

typedef struct {
  VehicleMotion *slots;
  int capacity;
  double time;
  int rotation;
  u8 helicopter_frame;
  bool enabled, initialized, animating;
} DrawingVehicles;

// Reusable Drawing storage follows event capacity, independently of City event simulation state.
// Allocation failure can leave some buffers grown; all remain owned by Drawing and can be released normally.
bool PrepareCityEventDrawingStorage(const City *city);
// Release interpolation, traversal anchors and every camera's event history after drawing has stopped.
void ReleaseCityEventDrawingStorage(Drawing *drawing);
// Compare canonical events with this camera's retained footprints. The bound target/camera must match the history;
// mark receives map-pixel rectangles synchronously and must only record damage, without changing City or the camera.
// Random effects are handled by the separate animation capture. Hash links do not affect visual equality.
void PublishCityMapEventDamage(City *city, MapSpriteFootprints *sprites, CityCanvasVisiblePaint mark, void *context);
// Clear visual vehicles while retaining allocated storage. Updates use GAME_TIME_UNPAUSED.
void ClearCityVehicleVisualization(Drawing *drawing);
bool UpdateCityVehicleAnimation(const City *city, double now);
i32 CityMapOverlayDepth(const City *city, TileIndex tile);

// x/y are world tile coordinates. Draw into the bound target/camera.
// Random artwork advances the supplied City's draw RNG;
// native frame code supplies a paint copy of City, preserving live simulation RNG.
void DrawCityMapAnimatedOverlay(City *city, int zoom, int x, int y, int overlay);
void DrawCityMapEventOverlay(City *city, int zoom, int x, int y, int slot);
// Collect bounds without pixel writes. Call only for live, nonrandom event sprites;
// random effects use CapturedCityMapAnimationBounds from the frame's animation capture.
CityRect CityMapEventSpriteBounds(City *city, int zoom, int x, int y, int slot);
// Bounds for an airplane slot in an explicit camera; empty when its zoom does not display airplanes.
CityRect CityAirplaneBounds(const City *city, int slot, CityMapCamera camera);

#endif
