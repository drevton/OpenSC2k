#ifndef SC2K_UI_UPDATES_H
#define SC2K_UI_UPDATES_H

#include "city/presentation.h"

// Consume the UI-owned category mask and refresh subscribed windows from live City state
// before collecting canvas damage. Domain modules prepare their retained presentation values.
void PrepareCityInformation(Game *game);

// Host-time UI timers also run while a retained dialog caller pumps events. Return whether presentation changed.
bool UpdateCityWindowTimers(Game *game, double now);

#endif
