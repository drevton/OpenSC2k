#ifndef SC2K_GAME_PALETTE_H
#define SC2K_GAME_PALETTE_H

#include "base/types.h"

typedef struct Game Game;

// A nonzero selector requests the slow block. Return -1 when paused play suppresses animation, otherwise zero.
int AnimateCityPalette(Game *game, int slow_palette);
// Consume queued fast then slow requests, clearing each after its animation call. Return the fast request observed
// on entry, even when paused play suppresses its animation; idle uses that request for its separate event timer.
bool AnimateDueCityPalettes(Game *game);
// Queue fast animation and every eighth slow animation when animate is true. Direct mode consumes pending requests
// immediately, including requests already queued when animate is false. Counter is the captured cadence value.
void AdvanceCityPaletteCadence(Game *game, u32 counter, bool animate);
// Return false when background animation is disabled; true means a fast-cycle request was made, even if paused.
// refresh_active brackets that request synchronously, without retaining work.
bool RefreshActiveCityPalette(Game *game);
bool AnimateCityCanvasPalette(Game *game, double now);

#endif
