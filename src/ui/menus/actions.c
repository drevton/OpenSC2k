#include "ui/menus/menu.h"

#include <stdio.h>

#include "audio/state.h"
#include "base/version.h"
#include "city/disasters/types.h"
#include "city/presentation.h"
#include "city/state.h"
#include "game/commands.h"
#include "game/settings.h"
#include "game/state.h"
#include "ui/charts/charts.h"
#include "ui/city_lifecycle.h"
#include "ui/console.h"
#include "ui/dialogs/budget.h"
#include "ui/dialogs/dialogs.h"
#include "ui/dialogs/file_dialog.h"
#include "ui/dialogs/game_options.h"
#include "ui/dialogs/map_data.h"
#include "ui/dialogs/messages.h"
#include "ui/map.h"
#include "ui/map_views.h"
#include "ui/music/jukebox.h"
#include "ui/news/newspaper.h"
#include "ui/signs.h"
#include "ui/state.h"
#include "ui/tools/tools.h"
#include "ui/windows/windows.h"

// Execute chosen menu values through their domain operations; UI keeps menu layout and pointer tracking.

static void OpenCityPanel(Game *game, int panel) {
  CityCanvasDesktop *desktop = &game->ui->desktop;
  for (int i = 0; i < desktop->count; ++i) {
    WindowKind kind = desktop->windows[i].kind;
    if ((panel == CITY_PANEL_MAP_DATA && kind == CITY_CANVAS_MAP_DATA) || Sc2kCanvasChartCode(kind) == panel) {
      FocusCityCanvasWindowKind(desktop, kind);
      return;
    }
  }
  SetCityPanelVisibilityByCode(game->city, panel);
  if (panel == CITY_PANEL_MAP_DATA) UpdateCityMapDataWindows(game->city);
}

void ExecuteCityMenuItem(Game *game, const CityMenuItem *item) {
  if (!item) return;
  PlayCityButtonBeep(game);
  City *city = game->city;
  CityInterfaceState *ui = game->ui;
  switch (item->action) {
  case CITY_MENU_ACTION_TREE_SPECIES:
  case CITY_MENU_ACTION_TOOL: {
    int tool = item->action == CITY_MENU_ACTION_TREE_SPECIES ? CITY_CANVAS_TOOL_TREES : item->parameter;
    int packed = Sc2kCanvasMapPointerTools(tool, 0);
    int button = tool == CITY_CANVAS_TOOL_CENTER ? CITY_TOOLBAR_BUTTON_CENTER : packed & 255;
    if (SelectCityToolWithModifiers(game, button, (packed >> 8) & 255, 0)) {
      ui->desktop.selected_tool = tool;
      if (tool == CITY_CANVAS_TOOL_STRETCH_TERRAIN) ui->terrain_tool = CITY_TERRAIN_TOOL_STRETCH;
      if (tool == CITY_CANVAS_TOOL_STREAM) ui->terrain_tool = CITY_TERRAIN_TOOL_RIVER;
    }
    break;
  }
  case CITY_MENU_ACTION_PAUSE:
    if (city->settings.timer_mode == CITY_TIMER_STOPPED) {
      if (!city->name.length) {
        RequestCityConfiguration(game);
        break;
      }
      RunCityCommand(game, CITY_COMMAND_START_CITY, INVALID_TILE, 0, 0, 0, NULL, NULL);
      break;
    }
    if (ui->dialog_pause_requested) break;
    if (city->settings.speed > CITY_SPEED_PAUSED) ui->resume_speed = city->settings.speed;
    RunCityCommand(game, CITY_COMMAND_SET_SIMULATION_SPEED,
        INVALID_TILE, city->settings.speed == CITY_SPEED_PAUSED ? ui->resume_speed : CITY_SPEED_PAUSED, 0, 0, NULL, NULL);
    break;
  case CITY_MENU_ACTION_SOUND_OPTIONS:
    game->ui->settings.tab = GAME_OPTIONS_TAB_SOUND;
    OpenCityGameOptions(game);
    DirtyCityMusicWindows(game);
    break;
  case CITY_MENU_ACTION_HELP: {
    static const char *const kTitles[] = {"Tool Help", "Keyboard Shortcuts", "About"};
    static const char *const kText[] = {
      "Use the icon toolbar at the top of the screen. Hover a button to see its name. "
      "Click a construction category to open its choices, then select a tool or variant. "
      "Power, water and reward choosers show previews, names and prices. "
      "Pin a window to keep it when opening another category. Each category remembers its last-used tool. "
      "Click its toolbar button again to reset the window position. Drag on the map to build. "
      "Zoning includes all zone types. Transport includes roads, rail and subway. Both include Demolish. "
      "Utilities contains power and water. Civic & Special Buildings contains schools, services, parks and rewards. "
      "Landscape, Zone, Transportation, Utilities, Civic and Emergency open green movable toolbars. "
      "Adjust & Review contains the budget, ordinances and advisors. Charts contains graphs and city statistics. "
      "Settings contains preferences and music. File contains Save, Load, Abandon and Exit.",
      "R/Shift+R: rotate placement clockwise/counterclockwise. Shift+click reverses the rotation button. "
      "Ctrl+R: road. Ctrl+Shift+R: highway. T: rail. Shift+T: subway. L: power lines. I: pipes. "
      "B: demolish. V: de-zone. Q/E: light/dense residential. A/D: commercial. Z/C: industrial. "
      "U/Shift+U: trees/water. K/Shift+K: school/college. P/Shift+P: police/prison. F: fire. H: hospital. "
      "J/Shift+J: small/large park. Ctrl+E: power. Ctrl+I: water. Ctrl+Alt+R: rewards. "
      "Ctrl+B: budget. Ctrl+O: ordinances. Ctrl+P: settings. Ctrl+L: load. Ctrl+S: save. "
      "Space: pause. +/-: zoom. PageUp/PageDown: rotate. F10: menus, arrows/Enter: choose. "
      "Alt+F/S/H: file/settings/help. Alt+Z/N/U/C: zoning/transport/utilities/civic. "
      "Alt+L/E/M/A: landscape/emergency/meet/review. F8: viewport. F12: inspect.",
      "Software-rendered city, construction tools and canvas windows."
    };
    char about[SC2K_BUILD_VERSION_CAPACITY + 96];
    const char *text = kText[item->parameter];
    if (item->parameter == 2) {
      char version[SC2K_BUILD_VERSION_CAPACITY];
      FormatBuildVersion(version, sizeof(version));
      snprintf(about, sizeof(about), "%s. %s", version, text);
      text = about;
    }
    Sc2kCanvasDesktopOpenNotice(&ui->desktop, (CityRect){40, 45, 540, 300},
        kTitles[item->parameter], text, CITY_MESSAGE_INFORMATION | CITY_MESSAGE_OK);
    break;
  }
  case CITY_MENU_ACTION_CATEGORY: case CITY_MENU_ACTION_SUBMENU:
  case CITY_MENU_ACTION_DISASTERS: break; // Opens a UI dropdown before dispatch.
  case CITY_MENU_ACTION_SEPARATOR: break;
  case CITY_MENU_ACTION_ABANDON: OpenCityAbandonDialog(game); break;
  case CITY_MENU_ACTION_CONSOLE: OpenCityDeveloperConsole(game); break;
  case CITY_MENU_ACTION_SAVE: RequestCitySave(game); break;
  case CITY_MENU_ACTION_LOAD:
    ui->restore_frame_tools = true;
    RequestCityFileOpen(game);
    break;
  case CITY_MENU_ACTION_QUIT:
    ui->exit_cancel_enabled = true;
    HandleCityExitCommand(city);
    break;
  case CITY_MENU_ACTION_PANEL: OpenCityPanel(game, item->parameter); break;
  case CITY_MENU_ACTION_AUTO_GOTO:
    ui->auto_goto_disabled = !ui->auto_goto_disabled;
    StoreGameSettings(game);
    break;
  case CITY_MENU_ACTION_SUBSCRIPTION:
    ui->newspaper_delivery_disabled = !ui->newspaper_delivery_disabled;
    StoreGameSettings(game);
    break;
  case CITY_MENU_ACTION_EXTRA_NEWSPAPER:
    ui->extra_newspaper_disabled = !ui->extra_newspaper_disabled;
    StoreGameSettings(game);
    break;
  case CITY_MENU_ACTION_MAP_LAYER: ToggleCityMapLayer(game, item->parameter); break;
  case CITY_MENU_ACTION_TOOLBAR: SelectCityToolWithModifiers(game, item->parameter, 0, 0); break;
  case CITY_MENU_ACTION_COMMAND:
    RunCityCommand(game, item->command, INVALID_TILE, item->parameter, 0, 0, NULL, NULL);
    break;
  case CITY_MENU_ACTION_GOTO_DISASTER:
    if (city->disasters.pending_type != CITY_DISASTER_NONE || city->disasters.map_update_pending ||
        city->disaster_work.type != CITY_DISASTER_NONE) {
      TileIndex tile = MapTileXYOrInvalid(city, city->disasters.location.x, city->disasters.location.y);
      if (tile != INVALID_TILE)
        CenterCityMapViewAtTile(city, NULL, MapTileX(city, tile), MapTileY(city, tile));
    }
    break;
  case CITY_MENU_ACTION_ADVISOR:
    OpenCityAdvisor(city, item->parameter);
    break;
  case CITY_MENU_ACTION_BUDGET:
    OpenCityBudget(city);
    break;
  case CITY_MENU_ACTION_POLICIES:
    if (FocusCityCanvasWindowKind(&ui->desktop, CITY_CANVAS_POLICIES) >= 0) break;
    InitializeCityOrdinancesDialog(game->city);
    Sc2kCanvasDesktopOpenPolicies(&ui->desktop, (CityRect){70, 18, 530, 472}, city->unlocks.ordinance_flags);
    ApplicationUpdateDialogPause(city);
    break;
  case CITY_MENU_ACTION_MAP_DATA:
    OpenCityPanel(game, CITY_PANEL_MAP_DATA);
    break;
  case CITY_MENU_ACTION_MUSIC:
    ApplyCityMusicAction(game, game->audio->jukebox.playing ? JUKEBOX_STOP : JUKEBOX_PLAY, NULL);
    break;
  case CITY_MENU_ACTION_JUKEBOX:
    OpenCityMusicWindow(game, 0);
    break;
  case CITY_MENU_ACTION_OPTIONS:
    OpenCityGameOptions(game);
    break;
  case CITY_MENU_ACTION_VIEWPORT:
    OpenCityMapView(city);
    break;
  case CITY_MENU_ACTION_SIGN_LIST:
    OpenCitySignList(city);
    break;
  case CITY_MENU_ACTION_LAND_USE:
    Sc2kCanvasDesktopOpenLandUse(&ui->desktop, (CityRect){110, 18, 450, 305},
        &city->statistics);
    break;
  case CITY_MENU_ACTION_NEWSPAPER:
    if (FocusCityCanvasWindowKind(&ui->desktop, CITY_CANVAS_NEWSPAPER) < 0)
      OpenCityManualNewspaper(city, city->newspaper.edition_index);
    break;
  }
}

