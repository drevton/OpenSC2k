#ifndef SC2K_GAME_FRAME_H
#define SC2K_GAME_FRAME_H

#include "city/types.h"

typedef struct Game Game;
typedef struct CanvasSDL CanvasSDL;

// Sequence presentation clocks, UI updates, map painting and composition for this City.
// Map painting borrows City read-only; Drawing and UI retain mutable presentation state.
// A NULL display uses UI's attached display; without one the framebuffer is still prepared.
// Preparation does not upload pixels. Failure retains completed owner updates and partial drawing work.
bool PrepareCityCanvas(City *city, CanvasSDL *display);
// Prepare, then upload when a display is available. Upload failure does not undo the prepared frame.
bool RedrawCityCanvas(City *city, CanvasSDL *display);
// Requires a display and prepared pixels. Update the UI cursor, upload temporary debug overlays, then restore them
// even on failure. Zero means success; only success consumes pending upload damage.
int PresentCityDesktop(City *city, CanvasSDL *display);
// Publish and consume pending City/UI change notifications, then report whether retained pixels need work.
// Does not paint or upload; the published Drawing/UI damage remains pending for the next frame.
bool PrepareCityCanvasDamage(City *city);
// Installed presenter: without a framebuffer, or for an unchanged paused frame, return true without presenting.
// Otherwise prepare/present the canvas, marking Game execution failed if either operation fails.
bool ApplicationPresentMap(Game *game);

#endif
