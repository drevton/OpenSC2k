#ifndef SC2K_DRAWING_MAPS_MAP_ANIMATION_H
#define SC2K_DRAWING_MAPS_MAP_ANIMATION_H

#include "city/types.h"
#include "city/engine/random.h"
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
  SimulationRandomState random_after, saved_random;
  CityRect *saved_bounds;
  MapAnimationMode mode;
} DrawingAnimations;

// Release captured records and every camera's effect footprints after capture/replay has stopped.
void ReleaseCityMapAnimationStorage(Drawing *drawing);
bool IsCityMapOverlayAnimated(const City *city, int overlay);
CityRect CapturedCityMapAnimationBounds(const Drawing *drawing, TileIndex tile);
// City supplies its borrowed Drawing context; use the passed paint City, never Game.city.
// Frame sequence: begin capture, traverse once, end capture, publish damage, then replay.
// Pass the state returned by begin to end, which retains the post-capture RNG and restores the pre-capture RNG.
// Capture redirects sprite bounds; replay restores each recorded RNG state around the matching sprite draw.
// Finish restores the post-capture RNG after all clipped repaints. Only one camera is active at a time.
SimulationRandomState BeginCityMapAnimationCapture(const City *city);
void EndCityMapAnimationCapture(City *city, SimulationRandomState before);
void ReplayCityMapAnimations(Drawing *drawing);
void FinishCityMapAnimations(City *city);
void PublishCityMapAnimationDamage(City *city, CityMapAnimationFootprints *previous,
    CityCanvasVisiblePaint mark, void *context);

// A successful sprite/overlay begin must be paired with EndCityMapSpriteAnimation before another begin.
// Begin returns false outside capture/replay, for an unanimated overlay or missing replay record,
// or if recording allocation fails. Tile arguments identify valid map tiles.
bool BeginCityMapOverlayAnimation(City *city, TileIndex tile, int overlay);
bool BeginCityMapSpriteAnimation(City *city, TileIndex tile);
void EndCityMapSpriteAnimation(City *city);

#endif
