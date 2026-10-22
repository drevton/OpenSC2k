#ifndef SC2K_GAME_PALETTE_H
#define SC2K_GAME_PALETTE_H

#include "base/types.h"

typedef struct Game Game;

// A nonzero selector requests the slow block. Return -1 when paused play suppresses animation, otherwise zero.
int AnimateCityPalette(Game *game, int slow_palette);
// Return false when background animation is disabled; true means a fast-cycle request was made, even if paused.
// refresh_active brackets that request synchronously, without retaining work.
bool RefreshActiveCityPalette(Game *game);
// Synchronize indexed framebuffer colors and dependent UI damage using caller-supplied presentation seconds.
// Ordinary frames supply GAME_TIME_UNPAUSED; comparison callers retain their original clock input.
bool AnimateCityCanvasPalette(Game *game, double now);

#endif
