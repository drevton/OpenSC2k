#ifndef SC2K_DOMAIN_SRC_UI_STARTUP_STARTUP_H
#define SC2K_DOMAIN_SRC_UI_STARTUP_STARTUP_H

#include "city/engine/city_types.h"
#include "city/types.h"

typedef struct Game Game;
typedef struct Canvas Canvas;
typedef struct CanvasSDL CanvasSDL;
typedef struct Window Window;
typedef struct CityCanvasDesktop CityCanvasDesktop;
typedef struct CityTerrainMap CityTerrainMap;
typedef struct CityDropdownItem CityDropdownItem;

typedef struct CityStartupWindow {
  u32 resource_id;
  u32 window_id;
} CityStartupWindow;

typedef enum CitySetupDifficulty {
  CITY_SETUP_DIFFICULTY_EASY = 0, CITY_SETUP_DIFFICULTY_MEDIUM = 1,
  CITY_SETUP_DIFFICULTY_HARD = 2,
  CITY_SETUP_DIFFICULTY_COUNT = 3,
} CitySetupDifficulty;

typedef enum CitySetupEra {
  CITY_SETUP_ERA_1900 = 0, CITY_SETUP_ERA_1950 = 1,
  CITY_SETUP_ERA_2000 = 2, CITY_SETUP_ERA_2050 = 3,
  CITY_SETUP_ERA_COUNT = 4,
} CitySetupEra;

// Dialog State

typedef struct CityNewCitySetupDialog {
  CityStartupWindow base;
  char city_name[33];
  int era, difficulty;
  int start_year; // Native year selection; zero retains the original era choice.
  CityName previous_name;
} CityNewCitySetupDialog;

// Startup Layout

enum { CITY_STARTUP_CANVAS_CONTROLS = 64, CITY_STARTUP_CANVAS_TEXT = 256 };
enum {
  CITY_RESOURCE_CONTROL_BUTTON = 0x80, CITY_RESOURCE_CONTROL_EDIT = 0x81,
  CITY_RESOURCE_CONTROL_SLIDER = 0x90, CITY_RESOURCE_CONTROL_DROPDOWN = 0x91,
  CITY_RESOURCE_STYLE_VISIBLE = 0x10000000, CITY_RESOURCE_STYLE_DISABLED = 0x08000000,
  CITY_RESOURCE_BUTTON_DEFAULT = 1, CITY_RESOURCE_BUTTON_TYPE_MASK = 0x0F,
  CITY_RESOURCE_BUTTON_RADIO = 9, CITY_RESOURCE_BUTTON_CHECKBOX = 3, CITY_RESOURCE_BUTTON_GROUP = 7,
};
typedef struct {
  i32 id, visible, checked;
  u16 control_class;
  u32 style;
  CityRect bounds; // Relative to the client area, in logical UI pixels.
  char text[CITY_STARTUP_CANVAS_TEXT];
  int text_caret, text_scroll;
  const CityDropdownItem *items;
  int item_count;
  const char *tooltip; // Borrowed help, or NULL to use the control caption.
} CityCanvasStartupControl;
typedef struct CityCanvasStartupWindow {
  char title[CITY_STARTUP_CANVAS_TEXT];
  const CityTerrainMap *terrain_preview;
  struct CityTerrainPreviewDrawing *drawing;
  const u32 *water_colors; // Borrowed immutable landscape palette from Assets.
  CityRect client_bounds;
  i32 control_count, pressed_control, focused_control, selected_text;
  double caret_blink_at;
  int caret_control;
  bool caret_visible;
  bool terrain_setup, year_query, main_menu;
  int map_width, map_height;
  CityCanvasStartupControl controls[CITY_STARTUP_CANVAS_CONTROLS];
} CityCanvasStartupWindow;

// Registry

enum { CITY_STARTUP_WINDOW_LIMIT = 16 };

// The registry borrows the base record until unregister; setup records embed it as their first member.
typedef struct {
  CityStartupWindow *dialog;
  u32 window_id;
  CityCanvasStartupWindow layout;
  u32 resource;
} CityStartupResourceWindow;

// Startup Canvas

bool PrepareCityStartupIcons(Game *game);
void Sc2kCanvasDrawStartupWindow(Game *game, Canvas *canvas, const Window *window);
i32 Sc2kCanvasStartupControlAt(const CityCanvasStartupWindow *layout,
    const Window *window, i32 x, i32 y);

// Startup Host

enum {
  NATIVE_STARTUP_CHECKPOINT_ENTER = 13, NATIVE_STARTUP_CHECKPOINT_LEAVE = 14,
  NATIVE_STARTUP_DIALOG_RESOURCE_TYPE = 5,
};
bool SetCityStartupMenuPresentation(City *city, CityCanvasDesktop *desktop);
void ClearCityStartupMenuPresentation(City *city);
struct CityCreationOptions;
typedef struct { const char *path; bool current; } CityCreationTerrain;
// NULL options means cancellation. Values and the optional host path are borrowed only for the callback.
typedef void CityCreationCallback(City *city, const struct CityCreationOptions *options,
    CityCreationTerrain terrain, void *context);
bool OpenCityCreationWindow(City *city, bool terrain, CityCreationCallback *callback, void *context);
void CompleteCityCreationWindow(City *city);
bool OpenCityStartupMenu(City *city);
bool TakeCityStartupMenuChoice(City *city, int *choice);
bool NativeStartupMenuWaiting(City *city);
bool UpdateCityTextCarets(Game *game, double now);

// Canvas registration; callers own dialog values, control text and lifetime.
bool RegisterCityStartupWindow(City *city, CityStartupWindow *window, u32 resource);
bool UnregisterCityStartupWindow(City *city, CityStartupWindow *window);
const CityCanvasStartupWindow *ReadCityStartupWindowLayout(City *city, u32 canvas_window_id);

// Startup Menu

typedef CityStartupWindow CityStartupMenuDialog;
typedef enum CityStartupMenuChoice {
  CITY_STARTUP_LOAD_CITY = 1, CITY_STARTUP_NEW_CITY = 2,
  CITY_STARTUP_EDIT_MAP = 3, CITY_STARTUP_LOAD_SCENARIO = 4,
  CITY_STARTUP_QUIT = 5,
  CITY_STARTUP_CHOICE_LAST = CITY_STARTUP_QUIT
} CityStartupMenuChoice;
enum {
  CITY_STARTUP_CONTROL_LOAD_CITY = 111, CITY_STARTUP_CONTROL_NEW_CITY = 112,
  CITY_STARTUP_CONTROL_EDIT_MAP = 113, CITY_STARTUP_CONTROL_LOAD_SCENARIO = 114,
  CITY_STARTUP_CONTROL_QUIT = 115,
  CITY_STARTUP_CONTROL_WATCH_WILLTV = 117, CITY_STARTUP_CONTROL_WEBSITE = 118,
  CITY_STARTUP_CONTROL_SETTINGS = 119, CITY_STARTUP_CONTROL_JUKEBOX = 120
};

int ShowCityStartupMenu(City *city);
bool InitializeCityStartupMenuDialog(City *city, CityStartupMenuDialog *dialog);
bool CenterCityStartupWindow(City *city, u32 window_id);

typedef enum CityIntroStage {
  CITY_INTRO_LOADING_CITY, CITY_INTRO_LOADING_GRAPHICS, CITY_INTRO_PREPARING_GRAPHICS,
  CITY_INTRO_PREPARING_SPRITES, CITY_INTRO_STAGE_COUNT
} CityIntroStage;

// The startup backdrop streams into the caller's canvas; loading also prepares and presents the shared framebuffer.
void DrawCityStartupBackdrop(Game *game, Canvas *canvas, CityRect area);
bool DrawCityLoadingScreen(Game *game, CanvasSDL *display, CityIntroStage stage,
    int completed, int total, bool initial);

// Reserve the baked identity area when positioning startup content.
int CityStartupContentTop(int width, int height);

// Setup Dialog

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

// Startup Dialog

bool CityStartupMenuServiceAvailable(City *city);
i32 ChooseCityStartupMenuDialog(City *city, CityStartupWindow *dialog);
bool AnswerCityStartupMenuDialog(City *city, i32 token, i32 choice);
bool EndCityStartupDialog(City *city, u32 window, i32 result);

// Help is borrowed for this call; generated startup text uses the caller's buffer.
const char *CityStartupTooltipAt(const Window *window, int x, int y, char *text, size_t size);

#endif
