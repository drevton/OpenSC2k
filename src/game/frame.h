#ifndef SC2K_GAME_FRAME_H
#define SC2K_GAME_FRAME_H

#include "city/types.h"

typedef struct Game Game;
typedef struct CanvasSDL CanvasSDL;

// Sequence presentation clocks, UI updates, map painting and composition for this City.
// Map painting uses a scoped scalar copy; borrowed map storage remains read-only.
// A NULL display uses UI's attached display; without one the framebuffer is still prepared.
bool PrepareCityCanvas(City *city, CanvasSDL *display);
bool RedrawCityCanvas(City *city, CanvasSDL *display);
// Update the UI cursor, then upload prepared pixels and temporary debug overlays. Zero means success.
int PresentCityDesktop(City *city, CanvasSDL *display);
// Publish pending City/UI damage before deciding whether retained pixels need work.
bool PrepareCityCanvasDamage(City *city);
// Installed presenter: skip an unchanged paused frame, otherwise prepare and present the current canvas.
bool ApplicationPresentMap(Game *game);

#endif
