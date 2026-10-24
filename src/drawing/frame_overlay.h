#ifndef SC2K_DRAWING_FRAME_OVERLAY_H
#define SC2K_DRAWING_FRAME_OVERLAY_H

#include "city/types.h"

typedef struct Drawing Drawing;
typedef struct CityInterfaceFont CityInterfaceFont;

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

#endif
