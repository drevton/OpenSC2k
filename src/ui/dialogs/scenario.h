#ifndef SC2K_DOMAIN_SRC_UI_DIALOGS_SCENARIO_H
#define SC2K_DOMAIN_SRC_UI_DIALOGS_SCENARIO_H

#include "city/types.h"
#include "ui/dialogs/file_dialog.h"
#include "ui/startup/startup.h"

typedef struct Canvas Canvas;
typedef struct Window Window;
typedef struct CityInterfaceState CityInterfaceState;

// Scenario Dialog

typedef struct CityScenarioSelectionDialog {
  CityStartupWindow base;
  City *city;
  int selected_index;
  // Retained pickers admit only paths that fit this field, including the terminator.
  char selected_path[260];
} CityScenarioSelectionDialog;

enum {
  CITY_SCENARIO_SELECTION_RESOURCE = 145,
  CITY_SCENARIO_NO_SELECTION = -1,
  CITY_SCENARIO_LIST_CONTROL = 245,
  CITY_SCENARIO_LIST_SCROLL = 0x115,
  CITY_SCENARIO_LIST_ROW_HEIGHT = 70,
  CITY_SCENARIO_SELECT_REQUIRED_RESOURCE = 59,
};
// Scenario Window
bool OpenCityScenarioSelection(City *city, CityFileChoiceCallback *callback, void *context);
void CompleteCityScenarioSelection(City *city);
void ClearCityScenarioSelection(City *city);
// The caller initializes dialog with the scenario resource and no selection. The picker owns window/list cleanup;
// retained callers keep the dialog and its selected_path alive until their load operation returns.
int RunCityScenarioSelectionDialog(City *city, CityScenarioSelectionDialog *dialog);
// Borrow text through the retained presenter's return. Empty text returns NONE; without a presenter return CANCEL.
// Loading completion and stream cleanup remain the caller's responsibility after dismissal.
int ShowCityScenarioTextDialog(City *city, const char *text);
void CloseCityScenarioSelectionDialog(City *city, CityScenarioSelectionDialog *dialog, int result);
bool EditCityScenarioSelectionControl(City *city, int token, int control, int value);
// Read the innermost picker's answer state without advancing or completing its caller.
bool NativeScenarioDialogWaiting(const CityInterfaceState *ui);
void DrawCityScenarioSelectionWindow(City *city, Canvas *canvas, const Window *window);

void ApplicationOpenScenarioInstructions(City *city, const char *path);

#endif
