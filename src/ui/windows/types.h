#ifndef SC2K_UI_WINDOWS_TYPES_H
#define SC2K_UI_WINDOWS_TYPES_H

#include "city/types.h"

// Window vocabulary and borrowed object types. Complete window records belong to state.h.
enum { CITY_CANVAS_WINDOW_CAPACITY = 16 };
typedef enum WindowKind {
  CITY_CANVAS_BUDGET = 1,
  CITY_CANVAS_NOTICE = 2,
  CITY_CANVAS_NEWSPAPER = 3,
  CITY_CANVAS_ADVISOR = 4,
  CITY_CANVAS_CONFIRM = 5,
  CITY_CANVAS_SPAN_STYLE = 6,
  CITY_CANVAS_POLICIES = 7,
  CITY_CANVAS_BONDS = 8,
  CITY_CANVAS_MAP_DATA = 9,
  CITY_CANVAS_LAND_USE = 10,
  CITY_CANVAS_LABEL_EDIT = 11,
  CITY_CANVAS_FACILITY_INFORMATION = 12,
  CITY_CANVAS_TILE_INFORMATION = 13,
  CITY_CANVAS_STADIUM_TEAM = 14,
  CITY_CANVAS_TOOL_STATUS = 15,
  CITY_CANVAS_JUKEBOX = 16,
  CITY_CANVAS_MUSIC_PROGRAMME = 17,
  CITY_CANVAS_GAME_OPTIONS = 18,
  CITY_CANVAS_HISTORY = 19,
  CITY_CANVAS_DEMOGRAPHICS = 20,
  CITY_CANVAS_NEIGHBORS = 21,
  CITY_CANVAS_INDUSTRY = 22,
  CITY_CANVAS_FILE_SAVE = 23,
  CITY_CANVAS_SAVE_QUESTION = 24,
  CITY_CANVAS_FILE_OPEN = 25,
  CITY_CANVAS_FILE_QUESTION = 26,
  CITY_CANVAS_STARTUP_MENU = 27,
  CITY_CANVAS_SCENARIO_LIST = 28,
  CITY_CANVAS_MAP_VIEWPORT = 29,
  CITY_CANVAS_EXIT = 30,
  CITY_CANVAS_SIGN_EDIT = 31,
  CITY_CANVAS_SIGN_LIST = 32,
  CITY_CANVAS_CONSOLE = 33,
  CITY_CANVAS_CONSTRUCTION = 34,
  CITY_CANVAS_ABANDON = 35,
  CITY_CANVAS_COMMAND_ERROR = 36,
  CITY_CANVAS_BUDGET_RATE = 37,
  CITY_CANVAS_WINDOW_KIND_COUNT
} WindowKind;

typedef struct Window Window;
typedef struct WindowEvent WindowEvent;
typedef struct CityCanvasDesktop CityCanvasDesktop;
typedef struct CityDirtyRegions CityDirtyRegions;
typedef struct CityWindowCompletion CityWindowCompletion;
typedef struct Game Game;

// Return true to consume the event. Only unhandled input proceeds to common chrome handling;
// the dispatcher reacquires the window by ID first, since the procedure may have closed it.
typedef bool WindowProcedure(Game *game, const Window *window, const WindowEvent *event);

// Completion runs after the window releases its owned state; context remains caller-owned.
typedef void CityWindowCallback(City *city, void *context);

#endif
