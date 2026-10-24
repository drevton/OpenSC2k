#ifndef SC2K_UI_FILE_BROWSER_H
#define SC2K_UI_FILE_BROWSER_H

#include "city/types.h"

enum { CITY_FILE_BROWSER_WIDTH = 500, CITY_FILE_BROWSER_HEIGHT = 286 };

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

// Reading replaces the listing; failure may leave a partial listing with valid row indices.
bool ReadCityFileBrowserDirectory(CityFileBrowser *browser, const char *display_directory);
// Release owned directory strings, entries and row indices after the browser window closes.
void ReleaseCityFileBrowser(CityFileBrowser *browser);

#endif
