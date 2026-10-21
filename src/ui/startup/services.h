#ifndef SC2K_UI_STARTUP_SERVICES_H
#define SC2K_UI_STARTUP_SERVICES_H

#include "city/types.h"
#include "ui/dialogs/dialog_state.h"
#include "ui/dialogs/dialogs.h"
#include "ui/startup/startup.h"

// Load Command Services

typedef struct CityResourceState CityResourceState;
typedef struct CityFileRuntime CityFileRuntime;

// Native file UI uses canvas windows; OS menu/message replay is comparison-only.

void NotifyCityLoaded(City *city);
i32 ChangeCityLoadDirectory(CityFileRuntime *file, const char *path);
bool ShowCityLoadResourceMessage(City *city, u32 resource, u32 flags, u32 help);

// Setup Dialog
typedef struct City City;

CityNewCitySetupDialog *ConstructNewCitySetupDialog(City *city, CityNewCitySetupDialog *dialog);
void DestroyNewCitySetupDialog(CityNewCitySetupDialog *dialog);
int ChooseNewCitySetupDialog(City *city, CityNewCitySetupDialog *dialog);

enum {
  CITY_SETUP_NAME = 101, CITY_SETUP_ERA_FIRST = 104, CITY_SETUP_DIFFICULTY_FIRST = 109,
  CITY_SETUP_YEAR = 250, CITY_SETUP_YEAR_PREVIOUS, CITY_SETUP_YEAR_NEXT,
  CITY_SETUP_YEAR_INPUT, CITY_SETUP_YEAR_CLEAR, CITY_SETUP_YEAR_DEFAULT, CITY_SETUP_DIFFICULTY, CITY_SETUP_RANDOM_NAME,
  CITY_SETUP_YEAR_QUERY_RESOURCE = 65536,
  CITY_SETUP_MAYOR = 200, CITY_SETUP_REGENERATE = 201,
  CITY_SETUP_HILLS = 202, CITY_SETUP_WATER = 203, CITY_SETUP_TREES = 204,
  CITY_SETUP_SMOOTH = 205, CITY_SETUP_FEATURE_FIRST = 210,
  CITY_SETUP_MAP_WIDTH = 230, CITY_SETUP_MAP_HEIGHT = 231, CITY_SETUP_MAP_SIZE_FIRST = 232,
  CITY_SETUP_MAP_SIZE_COUNT = 6,
  CITY_SETUP_CLIMATE = 239,
  CITY_SETUP_SNOW_COVERAGE = 240, CITY_SETUP_DESERT_COVERAGE = 241,
  CITY_SETUP_EDIT_TERRAIN = 276, CITY_SETUP_LOAD_MAP,
  CITY_SETUP_COAST = 272, CITY_SETUP_RIVER, CITY_SETUP_LANDFORM, CITY_SETUP_VARIETY,
  CITY_SETUP_NAME_LIMIT = 32, CITY_SETUP_CHECKPOINT_ENTER = 17, CITY_SETUP_CHECKPOINT_LEAVE = 18,
};
void InitializeCitySetupName(City *city, CityNewCitySetupDialog *dialog);
void CaptureCitySetupName(City *city, CityNewCitySetupDialog *dialog);
void ReleaseCitySetupName(CityNewCitySetupDialog *dialog);

int ChooseCitySetupWindow(City *city, CityNewCitySetupDialog *dialog);

// UI stages pending controls; acceptance copies them into the setup choices.
bool EditCitySetupControl(City *city, i32 token, i32 control, i32 value, const u8 *text, u32 length);
bool AnswerCitySetupDialog(City *city, i32 token, i32 answer);

// Span Dialog

// Direct modal/continuation leaves; the retained game caller owns placement.
bool AnswerCitySpanDialog(City *city, int token, int style);

// Startup Dialog

typedef struct City City;

void SetCityStartupDialogResource(CityStartupWindow *dialog, u32 resource);
bool CityStartupMenuServiceAvailable(City *city);
i32 ChooseCityStartupMenuDialog(City *city, CityStartupWindow *dialog);
bool AnswerCityStartupMenuDialog(City *city, i32 token, i32 choice);
bool EndCityStartupDialog(City *city, u32 window, i32 result);

#endif
