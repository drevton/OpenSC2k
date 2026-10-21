#ifndef SC2K_UI_FILE_BROWSER_H
#define SC2K_UI_FILE_BROWSER_H

#include "city/saveload/summary.h"
#include "drawing/drawing.h"

enum { CITY_FILE_BROWSER_WIDTH = 500, CITY_FILE_BROWSER_HEIGHT = 286, CITY_FILE_BROWSER_ROW_HEIGHT = 10 };
typedef enum CityFileBrowserControl {
  CITY_FILE_FILTER, CITY_FILE_CLEAR_FILTER, CITY_FILE_SORT_NAME, CITY_FILE_SORT_DATE, CITY_FILE_HOME,
  CITY_FILE_LIST, CITY_FILE_SCROLL_UP, CITY_FILE_SCROLL_DOWN, CITY_FILE_SCROLL_TRACK,
  CITY_FILE_NAME, CITY_FILE_CLEAR_NAME, CITY_FILE_DELETE, CITY_FILE_ACCEPT,
  CITY_FILE_RESIZE, CITY_FILE_DEFAULT_SIZE, CITY_FILE_CONTROL_COUNT
} CityFileBrowserControl;

typedef struct {
  char name[256], native_name[256];
  i64 modified;
  u64 size;
  bool directory, parent;
} CityFileBrowserEntry;

typedef struct CityFileBrowser {
  CityFileBrowserEntry *entries;
  int *visible;
  size_t count, capacity;
  int visible_count, selected, first;
  char *directory, *home;
  char filename[1024], filter[256];
  char free_space[64], details[9][320];
  int detail_count;
  int caret[2], text_scroll[2], select_all[2], focus;
  int scrollbar_grab;
  i64 wheel_remainder;
  u32 last_click_time;
  int last_click_row;
  bool open, sort_date, descending, dragging_scroll;
} CityFileBrowser;

CityRect CityFileBrowserBounds(const Window *window, CityFileBrowserControl control);
int CityFileBrowserRows(const Window *window);
CityRect CityFileBrowserThumb(const Window *window);
bool ReadCityFileBrowserDirectory(CityFileBrowser *browser, const char *display_directory);
void FilterCityFileBrowser(CityFileBrowser *browser);
void SortCityFileBrowser(CityFileBrowser *browser);
void SelectCityFileBrowserEntry(CityFileBrowser *browser, int selected);
void DrawCityFileBrowser(Canvas *canvas, const Window *window);
void ReleaseCityFileBrowser(CityFileBrowser *browser);

#endif
