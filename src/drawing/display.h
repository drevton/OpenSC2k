#ifndef SC2K_DRAWING_DISPLAY_H
#define SC2K_DRAWING_DISPLAY_H

#include "city/types.h"

typedef struct Drawing Drawing;
typedef struct CityMapPaint CityMapPaint;
typedef struct CityInterfaceFont CityInterfaceFont;

// Prepare Drawing's framebuffer and raster targets for positive physical pixel dimensions.
// Failed allocation preserves the existing storage. Host sizing and UI invalidation belong to Game.
bool PrepareDrawingFramebuffer(Drawing *drawing, int width, int height);
// Release pixel planes and their borrowed raster targets after painting stops. Safe to repeat.
// Cameras retain their parameters, but their pixel caches become invalid until the framebuffer is prepared again.
void ReleaseDrawingFramebuffer(Drawing *drawing);

// Replace source bindings from immutable Assets, retaining partial results on failure.
// The cache borrows Assets until replaced or released. Game invalidates dependent UI previews first.
// The new revision and map invalidation are published only on success.
bool InstallDrawingMapSpriteSources(Drawing *drawing, const CityAssets *assets);
// Prepare the required zoom scales and construction previews from already installed sources.
bool PrepareDrawingMapSprites(Drawing *drawing);
int CityMapSpriteScales(const Drawing *drawing, int scales[2]);
// Bind the prepared scale (a positive percentage) at a physical framebuffer position.
// Failure leaves the target unchanged. UI resolves window geometry, pending scroll and screen shake first.
bool BindDrawingMapTarget(Drawing *drawing, int scale, CityPoint position);
// Paint map sprites, signs and transient effects through the borrowed camera context and bound target.
// rect uses physical framebuffer pixels. UI composes retained tool feedback after this returns.
void PaintDrawingMapPixels(CityRect rect, CityMapPaint *paint);

// One synchronous upload's temporary overlays, in physical framebuffer pixels. No storage is owned here.
typedef struct DrawingFrameOverlay {
  CityRect statistics, current, upload;
} DrawingFrameOverlay;
// After scrolling finishes, preserve covered pixels in the existing scroll scratch and draw the debug overlays.
// upload includes both current and previous overlay bounds. Game uploads the framebuffer and normal upload damage.
// Finish with EndDrawingFrameOverlay even when upload fails. Flags, dimensions and scratch must stay unchanged;
// no painting or input dispatch may occur between these calls. Font glyphs are borrowed only while drawing.
DrawingFrameOverlay BeginDrawingFrameOverlay(Drawing *drawing, const CityInterfaceFont *font);
// Restore retained pixels. Only a successful upload publishes overlay bounds and consumes pending frame damage.
void EndDrawingFrameOverlay(Drawing *drawing, const DrawingFrameOverlay *overlay, bool uploaded);
// Damage the old and new placement bounds; combine them only within both pixel-work limits.
void MarkCityPlacementDamage(Drawing *drawing, CityRect previous, CityRect current);

#endif
