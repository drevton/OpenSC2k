#ifndef SC2K_GAME_APPLICATION_H
#define SC2K_GAME_APPLICATION_H

#include "game/types.h"

typedef struct Game Game;

typedef struct {
  const char *city_path, *script_path, *live_script_path;
  bool headless;
} GameStartupOptions;

// A launcher may observe execution or supply headless fixtures. The run function
// borrows these hooks and their context; native Game/City ownership is unchanged.
typedef struct {
  void (*usage)(void);
  // 0: unrecognized, 1/2: argument count consumed, -1: invalid.
  int (*parse_option)(void *context, const char *option, const char *value);
  bool (*validate)(void *context, const GameStartupOptions *startup);
  bool (*initialize_city)(Game *game, void *context, const char *path);
  bool (*prepare)(Game *game, void *context, const GameStartupOptions *startup);
  i64 (*advance_phase)(Game *game, void *context);
  bool (*finish)(Game *game, void *context);
  void (*print_status)(Game *game, void *context);
  bool (*close_streams)(void *context);
  void (*release)(Game *game, void *context);
} GameApplicationHooks;

// Defer teardown until any retained Quit/Save caller has returned.
void RequestCityApplicationExit(Game *game);

int RunGameApplication(int argc, char **argv, const GameApplicationHooks *hooks, void *context);
// Read/dispatch one bounded script line, borrowing Game's stream and city path.
// Blank/comment and handled lines return true, including actions that set execution_failed or request close.
// False stops on EOF/read failure, an existing stop flag, or an unknown action. The caller owns stream closure.
bool ApplicationNextScriptAction(Game *game);

#endif
