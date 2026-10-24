#ifndef SC2K_DRAWING_DISPLAY_H
#define SC2K_DRAWING_DISPLAY_H

#include "city/types.h"
#include "drawing/types.h"

typedef struct Drawing Drawing;
typedef struct CityMapPaint CityMapPaint;

// Prepare Drawing's framebuffer and raster targets for positive physical pixel dimensions.
// Failed allocation preserves the existing storage. Host sizing and UI invalidation belong to Game.
bool PrepareDrawingFramebuffer(Drawing *drawing, int width, int height);
// Release pixel planes and their borrowed raster targets after painting stops. Safe to repeat.
// Cameras retain their parameters, but their pixel caches become invalid until the framebuffer is prepared again.
void ReleaseDrawingFramebuffer(Drawing *drawing);

// Bind the prepared scale (a positive percentage) at a physical framebuffer position.
// Failure leaves the target unchanged. UI resolves window geometry, pending scroll and screen shake first.
bool BindDrawingMapTarget(Drawing *drawing, int scale, CityPoint position);

// Prepare event footprints and random-artwork replay for the bound target/camera before painting any damage.
// mark records map-pixel damage synchronously with paint as its context. Capture restores the supplied City's RNG;
// normal frames use a paint copy of City, and FinishCityMapAnimations ends replay after the camera is drawn.
void PrepareCityMapSpriteDamage(CityMapPaint *paint, CityCanvasVisiblePaint mark);

// Paint map sprites, signs and transient effects through the borrowed camera context and bound target.
// rect uses physical framebuffer pixels. UI composes retained tool feedback after this returns.
void PaintDrawingMapPixels(CityRect rect, CityMapPaint *paint);

// Damage the old and new placement bounds; combine them only within both pixel-work limits.
void MarkCityPlacementDamage(Drawing *drawing, CityRect previous, CityRect current);

#endif
