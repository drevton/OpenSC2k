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

typedef struct CityTextMessageDialog {
  City *city;
  u32 window;
} CityTextMessageDialog;

enum {
  CITY_SCENARIO_SELECTION_RESOURCE = 145,
  CITY_SCENARIO_NO_SELECTION = -1,
  CITY_SCENARIO_LIST_CONTROL = 245,
  CITY_SCENARIO_LIST_SCROLL = 0x115,
  CITY_SCENARIO_LIST_ROW_HEIGHT = 70,
  CITY_SCENARIO_SELECT_REQUIRED_RESOURCE = 59,
};
CityScenarioSelectionDialog *ConstructCityScenarioSelectionDialog(CityScenarioSelectionDialog *dialog, u32 parent);
void DestroyCityScenarioSelectionDialog(CityScenarioSelectionDialog *dialog);
int ShowCityTextMessageDialog(City *city, const char *text, int option, u32 parent);

// Scenario Window
bool OpenCityScenarioSelection(City *city, CityFileChoiceCallback *callback, void *context);
void CompleteCityScenarioSelection(City *city);
void ClearCityScenarioSelection(City *city);
int RunCityScenarioSelectionDialog(City *city, CityScenarioSelectionDialog *dialog);
int ShowCityScenarioTextDialog(CityTextMessageDialog *dialog, const char *text);
void CloseCityScenarioSelectionDialog(City *city, CityScenarioSelectionDialog *dialog, int result);
bool EditCityScenarioSelectionControl(City *city, int token, int control, int value);
// Read the innermost picker's answer state without advancing or completing its caller.
bool NativeScenarioDialogWaiting(const CityInterfaceState *ui);
void DrawCityScenarioSelectionWindow(City *city, Canvas *canvas, const Window *window);

void ApplicationOpenScenarioInstructions(City *city, const char *path);

#endif
