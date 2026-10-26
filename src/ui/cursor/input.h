#ifndef SC2K_UI_CURSOR_INPUT_H
#define SC2K_UI_CURSOR_INPUT_H

#include "city/types.h"

typedef struct Game Game;
typedef struct CityInterfaceState CityInterfaceState;

// Cursor read/results observed by optional diagnostics; no native pointer identities.
typedef enum CityCursorInputKind {
  CITY_CURSOR_INPUT_WINDOW_RECT = 1,
  CITY_CURSOR_INPUT_POSITION,
  CITY_CURSOR_INPUT_LOAD_WAIT,
  CITY_CURSOR_INPUT_INSTALL,
  CITY_CURSOR_INPUT_SHOW,
  CITY_CURSOR_INPUT_CAPTURE,
  CITY_CURSOR_INPUT_RELEASE,
} CityCursorInputKind;

typedef struct CityCursorInputRecord {
  u32 kind;
  u32 handle;
  i32 value[4];
} CityCursorInputRecord;

enum { CITY_SYSTEM_CURSOR_WAIT = 0x7F02 };

void ReadCityMapCursorRect(Game *game, CityRect *rect);
void ReadCityMapCursorPosition(Game *game, int *x, int *y);
u32 LoadCityWaitCursor(Game *game);
u32 InstallCityMapCursor(Game *game, u32 handle);
bool ShowCityMapCursor(Game *game);
void RefreshCityCursorStatus(City *city, i32 mode);

// Native presentation input, in the map's canvas coordinates.
void SetCityMapCursorPosition(CityInterfaceState *ui, int x, int y);

#endif
