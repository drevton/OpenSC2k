#ifndef SC2K_GAME_SCRIPTING_H
#define SC2K_GAME_SCRIPTING_H

#include "base/types.h"

typedef struct Game Game;

// Local scripting input; the UI caller checks its Lua-enabled preference before entering.
// Borrow terminated source or a .lua path for this call. Reuse the Game-owned interpreter, refusing nested runs.
// Commands use the existing City command API. Output/errors append to the UI console. After loading/running a
// chunk, clear its result stack and remove the instruction hook. No window, input or rendering state is retained.
// Format returned values in a protected call under the same instruction budget; conversion errors use normal cleanup.
void RunCityLua(Game *game, const char *code, bool file);
// Close after execution finishes. Lua finalizers run synchronously; clear the Game's interpreter pointer afterward.
void CloseCityScripting(Game *game);

#endif
