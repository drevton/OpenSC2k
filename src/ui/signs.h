#ifndef SC2K_UI_SIGNS_H
#define SC2K_UI_SIGNS_H

#include "city/maps/tile_storage.h"
#include "city/maps/map_types.h"
#include "drawing/canvas.h"

typedef struct Game Game;
typedef struct City City;

enum {
  CITY_SIGN_LIST_WIDTH = 358, CITY_SIGN_LIST_HEIGHT = 138,
  CITY_SIGN_EDITOR_WIDTH = 264, CITY_SIGN_EDITOR_HEIGHT = 70,
  CITY_SIGN_ROW_HEIGHT = 12, CITY_SIGN_SCROLL_WIDTH = 12
};

typedef enum CitySignControl {
  CITY_SIGN_CONTROL_NONE, CITY_SIGN_OK, CITY_SIGN_CANCEL, CITY_SIGN_DELETE, CITY_SIGN_MOVE,
  CITY_SIGN_PREVIOUS, CITY_SIGN_NEXT, CITY_SIGN_LOCATION, CITY_SIGN_TEXT, CITY_SIGN_CLEAR,
  CITY_SIGN_MATCH_CASE, CITY_SIGN_SCROLL_UP, CITY_SIGN_SCROLL_DOWN, CITY_SIGN_SCROLL_TRACK,
  CITY_SIGN_SHADE, CITY_SIGN_DEFAULT_SIZE, CITY_SIGN_STICKY,
  CITY_SIGN_COLOR_FIRST, CITY_SIGN_COLOR_LAST = CITY_SIGN_COLOR_FIRST + CITY_SIGN_COLOR_COUNT - 1
} CitySignControl;

typedef struct {
  char text[CITY_OVERLAY_NAME_BYTES];
  u8 cursor, anchor;
} CitySignText;

typedef struct {
  TileIndex tile;
  char name[CITY_OVERLAY_NAME_BYTES];
} CitySignEntry;

typedef struct {
  CitySignEntry *entries;
  int *order;
  int capacity, count, first, slot;
  u32 revision;
  u8 color;
  TileIndex tile;
  CitySignText name, filter;
  char caption[64];
  u32 list_window, editor_window, pressed_window, release_time;
  u8 pressed;
  u8 indexed, dirty, match_case, filter_focus, moving, released, scroll_drag;
  i16 previous_tool;
  int scroll_grab;
} CitySignState;

typedef struct Window Window;
void RefreshCitySignEntries(City *city);
void RebuildCitySigns(City *city);
void SortCitySigns(CitySignState *signs);
void DrawCitySignWindow(Game *game, Canvas *canvas, const Window *window);

#endif
