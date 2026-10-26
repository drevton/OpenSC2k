#ifndef SC2K_UI_SCENARIO_INTERNAL_H
#define SC2K_UI_SCENARIO_INTERNAL_H

#include "ui/dialogs/scenario.h"

// The retained presenter borrows this record until its instruction window closes.
typedef struct CityTextMessageDialog {
  City *city;
  u32 window;
} CityTextMessageDialog;

// List entries and cached rows live only while the UI picker is open.
typedef struct ScenarioListModel ScenarioListModel;
struct ScenarioListModel {
  ScenarioListModel *previous;
  CityScenarioSelectionDialog *dialog;
  char **paths;
  size_t count, capacity;
  i32 answer;
  u32 palette[256];
  i32 token, pressed_control, first_visible;
  u32 canvas_id;
  CityRect list_bounds;
  i32 row_width;
  u32 *row_pixels;
  i64 wheel_remainder;
  CityFileChoiceCallback *callback;
  void *context;
  CityScenarioSelectionDialog owned_dialog;
};
// Shared lifecycle; the normal UI owns the model until its callback completes.
bool BeginCityScenarioSelection(City *city, ScenarioListModel *model, CityScenarioSelectionDialog *dialog);
int FinishCityScenarioSelection(City *city, ScenarioListModel *model);

#endif
