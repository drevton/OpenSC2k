#ifndef SC2K_DRAWING_MAPS_MAP_ANIMATION_H
#define SC2K_DRAWING_MAPS_MAP_ANIMATION_H

#include "city/types.h"
#include "city/engine/random.h"
#include "drawing/canvas.h"

typedef struct Drawing Drawing;
typedef struct CityMapViewConfig CityMapViewConfig;

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
  SimulationRandomState random_after, saved_random;
  CityRect *saved_bounds;
  MapAnimationMode mode;
} DrawingAnimations;

// Release captured records and every camera's effect footprints after capture/replay has stopped.
void ReleaseCityMapAnimationStorage(Drawing *drawing);
bool IsCityMapOverlayAnimated(const City *city, int overlay);
CityRect CapturedCityMapAnimationBounds(const Drawing *drawing, TileIndex tile);
// Frame sequence: begin capture, traverse once, end capture, restore the paint City's pre-capture RNG, then replay.
// Capture redirects sprite bounds; replay restores each recorded RNG state around the matching sprite draw.
// Finish restores the post-capture RNG after all clipped repaints. Only one camera is active at a time.
void BeginCityMapAnimationCapture(Drawing *drawing);
void EndCityMapAnimationCapture(const City *city, Drawing *drawing);
void ReplayCityMapAnimations(Drawing *drawing);
void FinishCityMapAnimations(City *city, Drawing *drawing);
void PublishCityMapAnimationDamage(City *city, const CityMapViewConfig *view,
    CityMapAnimationFootprints *previous,
    CityCanvasVisiblePaint mark, void *context);

// A successful sprite/overlay begin must be paired with EndCityMapSpriteAnimation before another begin.
// Begin returns false outside capture/replay, for an unanimated overlay or missing replay record,
// or if recording allocation fails. Tile arguments identify valid map tiles.
bool BeginCityMapOverlayAnimation(City *city, Drawing *drawing, int x, int y, int overlay);
bool BeginCityMapSpriteAnimation(City *city, Drawing *drawing, TileIndex tile);
void EndCityMapSpriteAnimation(City *city, Drawing *drawing);

#endif
