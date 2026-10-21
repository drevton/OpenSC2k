#ifndef SC2K_UI_COMMANDS_H
#define SC2K_UI_COMMANDS_H

#include "city/economy/types.h"
#include "city/presentation.h"

#include "city/saveload/file.h"
#include "city/economy/economy.h"
#include "ui/startup/services.h"

#include "game/state.h"
#include "ui/startup/startup.h"

void SelectCityToolGroupAndMode(Game *game, u32 group, u32 variant);

typedef struct CityResourceNotice {
  struct CityResourceNotice *next;
  int bitmap, text;
  u32 window_id;
} CityResourceNotice;

// Borrowed by UI for one player command. Native gameplay errors come from its
// return value; this scope carries presentation details and legacy diagnostic feedback.
typedef struct CityCommandFeedback {
  i64 error;
  TileIndex blocker, stadium_tile, inspection_tile;
  CityResourceNotice *notices;
  bool construction, reported, cancelled, notice_shown;
} CityCommandFeedback;

// UI commands and presentation services. City mutations use the command dispatcher.

void SetCityMapControlsVisible(CityInterfaceState *ui, bool visible);
void ShowCityMapToolWindows(City *city);
// Finish the detached budget caller before city replacement or host teardown.
void CloseCityManualBudget(City *city);
void OpenCityBudget(City *city);
void CloseCityWindowsForReplacement(Game *game);
void RecordGameInput(Game *game);
void ConfigureNewCityFromSetupDialog(City *city);
bool EnterCityPaletteModalState(City *city);
bool LeaveCityPaletteModalState(City *city);
int HandleNewCityCommand(City *city);
void InitializeNewCityTerrainSession(City *city);
void ResetCityToolboxForTerrainEditing(City *city);
void OpenCityFromPath(City *city, const char *path);
void OpenCityFromFileDialog(City *city);

const CityBudgetState *ReadCityBudgetWindowState(const City *city);
const CityBudgetState *FindCityBudgetWindowState(const City *city, int id);
void OpenCityAdvisor(City *city, CityBudgetCategory category);

#endif
