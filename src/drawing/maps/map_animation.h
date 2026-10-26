#ifndef SC2K_DRAWING_MAPS_MAP_ANIMATION_H
#define SC2K_DRAWING_MAPS_MAP_ANIMATION_H

#include "city/types.h"
#include "city/engine/city_types.h"
#include "drawing/types.h"

typedef struct Drawing Drawing;

// Map Animation

// Presentation history only; reusable storage follows visible effects.
typedef struct {
  CityRect *bounds;
  int count, capacity;
  int origin_x, origin_y, zoom;
} CityMapAnimationFootprints;

typedef struct {
  TileIndex tile;
  SimulationRandomState random;
  CityRect bounds;
} MapAnimation;

typedef enum MapAnimationMode { ANIMATION_INACTIVE, ANIMATION_CAPTURE, ANIMATION_REPLAY } MapAnimationMode;

typedef struct {
  MapAnimation *records;
  int count, capacity;
  SimulationRandomState random_after;
  SimulationRandomState saved_random; // Resume state for capture or one replayed sprite; these scopes do not overlap.
  CityRect *saved_bounds;
  MapAnimationMode mode;
  SimulationRandomState random; // Frame-local painting stream, seeded from City before capturing any camera.
} DrawingAnimations;

// Release captured records and every camera's effect footprints after capture/replay has stopped.
void ReleaseCityMapAnimationStorage(Drawing *drawing);
bool IsCityMapOverlayAnimated(const City *city, int overlay);
CityRect CapturedCityMapAnimationBounds(const Drawing *drawing, TileIndex tile);
// Game seeds Drawing's painting stream once per frame from City's current random state.
// For each camera: begin capture, traverse once, end capture, publish damage, repaint, then finish.
// Begin saves the painting stream in Drawing. End sorts the records, retains the post-capture stream,
// restores the saved stream and enables replay. Damage callbacks only record rectangles; they must not paint.
// Capture redirects sprite bounds; replay restores each recorded RNG state around the matching sprite draw.
// Finish restores the post-capture stream after all clipped repaints, ready for the next camera. City is unchanged.
// Only one camera is active at a time.
void BeginCityMapAnimationCapture(Drawing *drawing);
void EndCityMapAnimationCapture(Drawing *drawing);
void FinishCityMapAnimations(Drawing *drawing);
void PublishCityMapAnimationDamage(const City *city, CityMapAnimationFootprints *previous,
    CityCanvasVisiblePaint mark, void *context);

// A successful sprite/overlay begin must be paired with EndCityMapSpriteAnimation before another begin.
// Begin returns false outside capture/replay, for an unanimated overlay or missing replay record,
// or if recording allocation fails. Tile arguments identify valid map tiles.
bool BeginCityMapOverlayAnimation(const City *city, TileIndex tile, int overlay);
bool BeginCityMapSpriteAnimation(const City *city, TileIndex tile);
void EndCityMapSpriteAnimation(Drawing *drawing);

#endif
