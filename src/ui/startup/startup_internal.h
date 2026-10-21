#ifndef SC2K_UI_STARTUP_INTERNAL_H
#define SC2K_UI_STARTUP_INTERNAL_H

#include "ui/startup/startup.h"

// Window-owned choices; retained comparison callers borrow the same lifecycle.
typedef struct CityStartupMenuState {
  struct CityStartupMenuState *parent;
  CityStartupWindow *dialog;
  int token, choice;
  bool answered, detached;
  CityStartupWindow owned_dialog;
} CityStartupMenuState;
typedef struct CitySetupWindowState {
  CityNewCitySetupDialog *dialog;
  int token, choice;
  bool answered, edit_terrain;
  bool preserve_terrain;
  char *terrain_path;
  u32 year_window;
  CityCreationCallback *callback;
  void *context;
  CityNewCitySetupDialog owned_dialog;
} CitySetupWindowState;

bool BeginCityStartupMenu(City *city, CityStartupMenuState *pending, CityStartupWindow *dialog);
int FinishCityStartupMenu(City *city, CityStartupMenuState *pending);
bool BeginCitySetupWindow(City *city, CitySetupWindowState *pending, CityNewCitySetupDialog *dialog);
int FinishCitySetupWindow(City *city, CitySetupWindowState *pending);

#endif
