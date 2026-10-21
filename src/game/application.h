#ifndef SC2K_GAME_APPLICATION_H
#define SC2K_GAME_APPLICATION_H

#include "city/types.h"

// Native startup choices and simulation dispatch states.
typedef enum CityApplicationIdleState {
  CITY_IDLE_PLAY = -1,
  CITY_IDLE_TOOL_PREVIEW = 0,
  CITY_IDLE_STARTUP_MENU = 7,
  CITY_IDLE_LOAD_CITY = 10,
  CITY_IDLE_NEW_CITY = 11,
  CITY_IDLE_TERRAIN = 12,
  CITY_IDLE_LOAD_SCENARIO = 13,
  CITY_IDLE_STARTUP_CHOICE = 14,
  CITY_IDLE_COMMAND_LINE_FILE = 15,
} CityApplicationIdleState;

// Application command-line dispatch selects either a city or scenario path.
typedef enum CityPathOpenMode {
  CITY_PATH_OPEN_NONE = 0, CITY_PATH_OPEN_CITY = 1, CITY_PATH_OPEN_SCENARIO = 2
} CityPathOpenMode;

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

int RunGameApplication(int argc, char **argv, const GameApplicationHooks *hooks, void *context);
#endif
