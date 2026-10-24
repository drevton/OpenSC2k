#ifndef SC2K_UI_SIGNS_H
#define SC2K_UI_SIGNS_H

#include "city/maps/labels.h"

typedef struct CityExtraMapView CityExtraMapView;
typedef struct CanvasSDLEvent CanvasSDLEvent;

typedef struct Game Game;
typedef struct Window Window;
typedef struct City City;

enum {
  CITY_SIGN_LIST_WIDTH = 358, CITY_SIGN_LIST_HEIGHT = 138,
  CITY_SIGN_EDITOR_WIDTH = 264, CITY_SIGN_EDITOR_HEIGHT = 70
};

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

void RefreshCitySignEntries(City *city);
void RebuildCitySigns(City *city);

// Borrowed caption/content help in logical desktop coordinates; NULL requests generic window help.
const char *CitySignTooltipAt(Game *game, const Window *window, int x, int y);

void OpenCitySignList(City *city);
bool HandleCityMapSignInput(City *city, CityExtraMapView *view, const CanvasSDLEvent *event);
void ResetCitySignEditor(City *city);
bool PollCitySigns(City *city);

#endif
