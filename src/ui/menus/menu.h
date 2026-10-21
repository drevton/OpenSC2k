#ifndef SC2K_UI_MENU_H
#define SC2K_UI_MENU_H

#include "ui/tools/types.h"
#include "ui/input.h"
#include "drawing/canvas.h"
#include "city/types.h"

// Original menu positions, command IDs and enabled/check states.
enum {
  CITY_MENU_BY_COMMAND = 0, CITY_MENU_BY_POSITION = 0x400,
  CITY_MENU_GRAYED = 0x001, CITY_MENU_DISABLED = 0x002,
  CITY_MENU_DISABLED_MASK = CITY_MENU_GRAYED | CITY_MENU_DISABLED,
  CITY_MENU_UNCHECKED = 0, CITY_MENU_CHECKED = 0x008,
  CITY_MENU_COMMAND_MAP_DATA = 32783, CITY_MENU_COMMAND_POPULATION = 32786,
  CITY_MENU_COMMAND_INDUSTRY = 32787, CITY_MENU_COMMAND_GRAPHS = 32788,
  CITY_MENU_COMMAND_NEIGHBORS = 32789,
  CITY_MENU_COMMAND_SPEED_FAST = 0x8003, CITY_MENU_COMMAND_SPEED_UNLIMITED = 0x80A6,
  CITY_MAIN_MENU_FILE = 0, CITY_MAIN_MENU_SPEED = 1, CITY_MAIN_MENU_OPTIONS = 2,
  CITY_MAIN_MENU_DISASTERS = 3, CITY_MAIN_MENU_WINDOWS = 4,
  CITY_MAIN_MENU_NEWSPAPER = 5, CITY_MAIN_MENU_HELP = 6,
};

// Original main-menu command IDs, distinct from unified command packets.
typedef enum CityMainMenuCommandId {
  CITY_MENU_LOAD_CITY = 32801,
  CITY_MENU_LOAD_SCENARIO = 32804,
  CITY_MENU_COMMAND_NEW_CITY = 32802,
  CITY_MENU_COMMAND_EDIT_NEW_MAP = 32803,
  CITY_MENU_COMMAND_SAVE_CITY = 32805,
  CITY_MENU_COMMAND_SAVE_CITY_AS = 32806,
  CITY_MENU_COMMAND_EXIT = 57665,
  CITY_MENU_COMMAND_SPEED_PAUSE = 32768,
  CITY_MENU_COMMAND_SPEED_SLOW = 32769,
  CITY_MENU_COMMAND_SPEED_NORMAL = 32770,
  CITY_MENU_COMMAND_AUTO_BUDGET = 32772,
  CITY_MENU_COMMAND_AUTO_GOTO = 32773,
  CITY_MENU_COMMAND_SOUND = 32774,
  CITY_MENU_COMMAND_MUSIC = 32775,
  CITY_MENU_COMMAND_DISASTER_FIRE = 32776,
  CITY_MENU_COMMAND_DISASTER_FLOOD = 32777,
  CITY_MENU_COMMAND_DISASTER_AIR_CRASH = 32778,
  CITY_MENU_COMMAND_DISASTER_TORNADO = 32779,
  CITY_MENU_COMMAND_DISASTER_EARTHQUAKE = 32780,
  CITY_MENU_COMMAND_DISASTER_MONSTER = 32781,
  CITY_MENU_COMMAND_DISASTER_HURRICANE = 32919,
  CITY_MENU_COMMAND_DISASTER_RIOTS = 32920,
  CITY_MENU_COMMAND_DISASTER_MASS_RIOTS = 32922,
  CITY_MENU_COMMAND_DISASTER_VOLCANO = 32923,
  CITY_MENU_COMMAND_DISASTER_POLLUTION = 32924,
  CITY_MENU_COMMAND_DISASTER_FIRE_STORM = 32925,
  CITY_MENU_COMMAND_DISASTER_MELTDOWN = 32926,
  CITY_MENU_COMMAND_DISASTER_MICROWAVE = 32927,
  CITY_MENU_COMMAND_DISASTER_MAJOR_FLOOD = 32928,
  CITY_MENU_COMMAND_NO_DISASTERS = 32782,
  CITY_MENU_COMMAND_BUDGET = 32784,
  CITY_MENU_COMMAND_ORDINANCES = 32785,
  CITY_MENU_COMMAND_NEWSPAPER_SUBSCRIPTION = 32791,
  CITY_MENU_COMMAND_NEWSPAPER_EXTRA = 32792,
  CITY_MENU_COMMAND_NEWSPAPER_EDITION_FIRST = 32793,
  CITY_MENU_COMMAND_HELP_CONTENTS = 57666,
  CITY_MENU_COMMAND_HELP_SEARCH = 32893,
  CITY_MENU_COMMAND_HELP_USING = 57667,
  CITY_MENU_COMMAND_ABOUT = 57664,
} CityMainMenuCommandId;

enum { CITY_NEWSPAPER_MENU_EDITION_FIRST = 3 };

typedef struct Game Game;
typedef struct City City;
typedef struct CityCanvasDesktop CityCanvasDesktop;
typedef struct Window Window;
typedef struct CityDropdownSelection CityDropdownSelection;

enum { CITY_MENU_BAR_HEIGHT = 24, CITY_MENU_ROW_HEIGHT = 10, CITY_MENU_OVERLAY_COUNT = 4,
    CITY_TOOLBAR_DISABLED_ICON_COUNT = 3, CITY_TOOLBAR_SWITCH_ICON_COUNT = 1, CITY_CONSTRUCTION_GROUP_COUNT = 5,
    CITY_TOOL_SUBMENU_COUNT = 6, CITY_DROPDOWN_DISMISS_MARGIN = 8 };
// The first thirty IDs match the stable icon atlas; additional controls borrow artwork.
typedef enum CityTopbarButton {
  CITY_TOPBAR_PAUSE, CITY_TOPBAR_SPEED, CITY_TOPBAR_FILE, CITY_TOPBAR_SETTINGS,
  CITY_TOPBAR_BUDGET, CITY_TOPBAR_ORDINANCES, CITY_TOPBAR_GRAPHS, CITY_TOPBAR_REPORTS, CITY_TOPBAR_NEWSPAPER,
  CITY_TOPBAR_MAP, CITY_TOPBAR_ZOOM_IN, CITY_TOPBAR_ZOOM_OUT, CITY_TOPBAR_ROTATE, CITY_TOPBAR_LAYERS,
  CITY_TOPBAR_BULLDOZE, CITY_TOPBAR_LANDSCAPE, CITY_TOPBAR_RESIDENTIAL, CITY_TOPBAR_COMMERCIAL,
  CITY_TOPBAR_INDUSTRIAL, CITY_TOPBAR_ROADS, CITY_TOPBAR_RAIL, CITY_TOPBAR_PORTS,
  CITY_TOPBAR_POWER, CITY_TOPBAR_WATER, CITY_TOPBAR_EDUCATION, CITY_TOPBAR_PUBLIC_SERVICES,
  CITY_TOPBAR_RECREATION, CITY_TOPBAR_REWARDS, CITY_TOPBAR_EMERGENCY, CITY_TOPBAR_HELP,
  CITY_TOPBAR_ROTATE_COUNTERCLOCKWISE, CITY_TOPBAR_TREES, CITY_TOPBAR_UNDERWORLD,
  CITY_TOPBAR_TERRAIN_RAISE, CITY_TOPBAR_TERRAIN_LOWER, CITY_TOPBAR_TERRAIN_STRETCH, CITY_TOPBAR_TERRAIN_LEVEL,
  CITY_TOPBAR_TERRAIN_WATER, CITY_TOPBAR_TERRAIN_CLEAR, CITY_TOPBAR_TERRAIN_CENTER, CITY_TOPBAR_TERRAIN_STREAM,
  CITY_TOPBAR_COUNT
} CityTopbarButton;
// Packed artwork retains its existing tool slots; additional icons follow the original atlas.
enum { CITY_TOPBAR_ICON_COUNT = CITY_TOPBAR_HELP + 1, CITY_TOOLBAR_TOOL_ICON_COUNT = CITY_CANVAS_TOOL_RAIL_BRIDGE + 1 };
enum {
  CITY_TOOLBAR_UNDERWORLD_ICON = CITY_TOPBAR_ICON_COUNT + CITY_TOOLBAR_TOOL_ICON_COUNT + 1 +
      CITY_TOOLBAR_DISABLED_ICON_COUNT + CITY_TOOLBAR_SWITCH_ICON_COUNT,
  CITY_TOOLBAR_STRETCH_TERRAIN_ICON = CITY_TOOLBAR_UNDERWORLD_ICON + 1,
  CITY_TOOLBAR_WEBSITE_ICON = CITY_TOOLBAR_STRETCH_TERRAIN_ICON + 1,
  CITY_TOOLBAR_JUKEBOX_ICON = CITY_TOOLBAR_WEBSITE_ICON + 1,
  CITY_TOOLBAR_ICON_COUNT = CITY_TOOLBAR_JUKEBOX_ICON + 1,
};
enum { CITY_EMERGENCY_RECALL = 1 << 3, CITY_EMERGENCY_GOTO = 1 << 4 };

typedef struct {
  bool held;
  bool show_shortcuts;
  u8 selected, hovered; // One based dropdown and row; zero means no selection.
  u8 category, pressed; // One based category; pressed IDs also cover construction tools.
  u8 tree_climate; // Filters tree choices; planting leaves ground climate unchanged.
  u8 tree_species; // Zero is random; 1..12 choose a tree species.
  u8 toolbar_page; // Narrow toolbars switch button sets instead of wrapping.
  u8 remembered_group[CITY_CONSTRUCTION_GROUP_COUNT]; // Last placement tool in each construction toolbar.
  u8 remembered[CITY_TOPBAR_COUNT]; // Frontend tool + 1, independently for each category.
  u8 remembered_submenu[CITY_TOOL_SUBMENU_COUNT]; // Tool + 1, independently for each variant button.
  bool released, submenu_pending, submenu_clock_started;
  u32 release_time, press_time, pressed_window, submenu_window;
  u8 last_zoom;
  i8 last_tool;
  double tool_status_until; // Recent tool selections take priority over other status messages.
  bool last_paused, last_rewards_enabled, last_underworld, last_editing;
  u8 emergency_enabled; // Service bits0..2, recall bit3 and disaster location bit4.
  char date[32], notice[256], money[32];
  i16 demand[3];
} CityMenuState;

typedef enum CityMenuAction {
  CITY_MENU_ACTION_COMMAND, CITY_MENU_ACTION_SAVE, CITY_MENU_ACTION_LOAD, CITY_MENU_ACTION_QUIT,
  CITY_MENU_ACTION_AUTO_GOTO, CITY_MENU_ACTION_SUBSCRIPTION, CITY_MENU_ACTION_EXTRA_NEWSPAPER,
  CITY_MENU_ACTION_MAP_LAYER, CITY_MENU_ACTION_PANEL, CITY_MENU_ACTION_TOOLBAR,
  CITY_MENU_ACTION_BUDGET, CITY_MENU_ACTION_POLICIES,
  CITY_MENU_ACTION_MAP_DATA, CITY_MENU_ACTION_MUSIC, CITY_MENU_ACTION_JUKEBOX,
  CITY_MENU_ACTION_OPTIONS, CITY_MENU_ACTION_VIEWPORT, CITY_MENU_ACTION_LAND_USE,
  CITY_MENU_ACTION_NEWSPAPER, CITY_MENU_ACTION_SIGN_LIST, CITY_MENU_ACTION_CONSOLE,
  CITY_MENU_ACTION_TOOL, CITY_MENU_ACTION_PAUSE, CITY_MENU_ACTION_SOUND_OPTIONS,
  CITY_MENU_ACTION_HELP, CITY_MENU_ACTION_DISASTERS, CITY_MENU_ACTION_CATEGORY, CITY_MENU_ACTION_SUBMENU,
  CITY_MENU_ACTION_ADVISOR, CITY_MENU_ACTION_GOTO_DISASTER, CITY_MENU_ACTION_TREE_SPECIES,
  CITY_MENU_ACTION_SEPARATOR, CITY_MENU_ACTION_ABANDON
} CityMenuAction;

typedef struct CityMenuItem {
  const char *label;
  CityHotkey shortcut;
  CityMenuAction action;
  u16 command;
  int parameter;
} CityMenuItem;

CityRect CityMenuBounds(const CityCanvasDesktop *desktop, int overlay);
CityRect CityMenuPixelBounds(const CityCanvasDesktop *desktop, int overlay);
// Shared dismissal geometry; all coordinates and the margin use the caller's scale.
CityRect CityDropdownCaptureBounds(CityRect popup, CityRect anchor, int margin);
bool CityMenuContains(const CityCanvasDesktop *desktop, int x, int y);
void CloseCityMenu(CityCanvasDesktop *desktop);
void SetCityShortcutHints(Game *game, bool visible);
// Points are UI coordinates independent of window origins and map scale.
bool PressCityMenu(CityCanvasDesktop *desktop, int x, int y);
bool MoveCityMenu(CityCanvasDesktop *desktop, int x, int y);
const CityMenuItem *ReleaseCityMenu(CityCanvasDesktop *desktop, int x, int y);
bool HandleCityToolbarShortcut(Game *game, CityHotkey key);
const CityMenuItem *CityMenuKey(CityCanvasDesktop *desktop, CityHotkey key);
void CityMenuTooltipAt(const Game *game, int x, int y, char *text, size_t capacity);
bool IsCityMenuItemChecked(const Game *game, const CityMenuItem *item);
void DrawCityMenu(Game *game, Canvas *canvas, const CityCanvasDesktop *desktop);
void UpdateCityToolbar(Game *game);
bool PollCityToolbar(Game *game, u32 milliseconds);
void ReleaseCityToolbarFeedback(CityCanvasDesktop *desktop, u32 milliseconds);
bool PrepareCityConstructionIcons(Game *game);
void DrawCityConstructionWindow(Game *game, Canvas *canvas, const Window *window);
bool CityToolSupportsPlacementRotation(int tool);
bool HandleCityPlacementRotationKey(Game *game, CityHotkey key);
void CityConstructionTooltipAt(const Game *game, const Window *window, int x, int y,
    char *text, size_t capacity);

#endif
