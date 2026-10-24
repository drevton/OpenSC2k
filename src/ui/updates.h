#ifndef SC2K_UI_UPDATES_H
#define SC2K_UI_UPDATES_H

#include "city/presentation.h"

typedef struct CityInterfaceState CityInterfaceState;

// Mark the current desktop canvas dirty after framebuffer preparation succeeds. Uses UI's current bounds;
// does not resize windows, prepare pixels or change City. No callbacks or allocations.
void InvalidateCityDesktop(CityInterfaceState *ui);

// Invalidate tree-picker and startup terrain previews before replacing their map sprite sources.
// Keep windows and their selections; invalidate or release prepared images for their painters to rebuild on demand.
// No callbacks, City mutation or allocations. Repeat on a failed source installation's next attempt.
void InvalidateCityMapSpritePreviews(CityInterfaceState *ui);

// Consume the UI-owned category mask and refresh subscribed windows from live City state
// before collecting canvas damage. Domain modules prepare their retained presentation values.
void PrepareCityInformation(Game *game);

// Host-time UI timers also run while a retained dialog caller pumps events. Return whether presentation changed.
bool UpdateCityWindowTimers(Game *game, double now);

#endif
