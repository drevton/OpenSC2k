#include "city/engine/city_types.h"
#include "city/tools/types.h"
#include "drawing/maps/map_view.h"
#include "game/application.h"
#include "ui/menus/menu.h"
#include "ui/tools/types.h"
#include "city/tools/tools.h"
#include "city/presentation.h"
#include "ui/tools/tools.h"
#include <string.h>

#include "audio/audio.h"
#include "city/state.h"
#include "city/engine/simulation.h"
#include "game/state.h"
#include "audio/state.h"
#include "drawing/state.h"
#include "assets/state.h"
#include "ui/map.h"
#include "drawing/drawing.h"
#include "drawing/drawing_internal.h"
#include "ui/charts/charts.h"
#include "ui/commands.h"
#include "ui/dialogs/dialogs.h"
#include "ui/state.h"
#include "ui/text/text.h"

#include "city/saveload/file.h"
#include "game/errors.h"

// Toolbar

// Save outgoing map flags and restore the surface/underground bank.
// Every caller discards EAX; only the MFC button presentation is a host service.
void SwitchCityMapOverlayFlagBank(City *city, int refresh_button) {
  Drawing *drawing = city->game->drawing;
  bool underground = drawing->map_state.events_suppressed != 0;
  if (refresh_button != 0)
    SetCityToolToolbarStyle(city, CITY_TOOLBAR_BUTTON_UNDERGROUND,
        underground ? CITY_TOOLBAR_STYLE_CHECKED | CITY_TOOLBAR_STYLE_CHECKBOX : CITY_TOOLBAR_STYLE_CHECKBOX);
  u32 *outgoing = underground ? drawing->map_state.overlay_settings.surface : drawing->map_state.overlay_settings.underground;
  u32 *incoming = underground ? drawing->map_state.overlay_settings.underground : drawing->map_state.overlay_settings.surface;
  for (int index = CITY_MAP_DISPLAY_ZONE_GROUND; index < CITY_MAP_DISPLAY_FLAG_COUNT; ++index) {
    u32 previous = drawing->map_state.display_flags[index];
    outgoing[index] = previous;
    u32 desired = incoming[index];
    if (desired != previous) {
      drawing->map_state.display_flags[index] = desired;
      SetCityToolToolbarStyle(city, CITY_TOOLBAR_BUTTON_UNDERGROUND - index,
          desired != 0 ? CITY_TOOLBAR_STYLE_CHECKED | CITY_TOOLBAR_STYLE_CHECKBOX : CITY_TOOLBAR_STYLE_CHECKBOX);
    }
  }
}

// Apply the selected toolbar tool.
void SelectCityToolGroupAndMode(Game *game, u32 group, u32 variant) {
  City *city = game->city;
  Drawing *drawing = game->drawing;
  CityMapViewConfig *view = drawing->default_map_view;
  CityInterfaceState *ui = game->ui;
  CityResourceState *resources = game->resources;
  static const u8 cursors[CITY_TOOL_GROUP_BUILD_COUNT] = {CITY_CURSOR_DEMOLISH, CITY_CURSOR_TREE, CITY_CURSOR_DISPATCH,
      CITY_CURSOR_POWER, CITY_CURSOR_WATER_UTILITY, CITY_CURSOR_REWARDS, CITY_CURSOR_ROAD, CITY_CURSOR_RAIL,
      CITY_CURSOR_PORT, CITY_CURSOR_RESIDENTIAL, CITY_CURSOR_COMMERCIAL, CITY_CURSOR_INDUSTRIAL, CITY_CURSOR_EDUCATION,
      CITY_CURSOR_CIVIC, CITY_CURSOR_RECREATION};
  static const u8 panels[5] = {
      CITY_PANEL_MAP_DATA, CITY_PANEL_DEMOGRAPHICS, CITY_PANEL_NEIGHBORS, CITY_PANEL_HISTORY, CITY_PANEL_INDUSTRY};
  if (group < CITY_TOOL_GROUP_SIGN) {
    u32 name_index = group == CITY_TOOL_GROUP_REWARDS && variant > CITY_REWARD_ARCOLOGY_PICKER
        ? CITY_REWARD_ARCOLOGY_PICKER
        : variant;
    const char *name = group == CITY_TOOL_GROUP_ROAD && variant == CITY_ROAD_BRIDGE ? "Road bridge" :
        group == CITY_TOOL_GROUP_RAIL && variant == CITY_RAIL_BRIDGE ? "Rail bridge" :
        resources->tool_names.name[group][name_index];
    CopyText(ui->tool_labels[group], sizeof(ui->tool_labels[group]), name);
    ui->tool_indices[group] = (int)name_index;
    ui->selected_cursor =
        group == CITY_TOOL_GROUP_TREE_WATER && variant != CITY_TREE_WATER_TREES ? CITY_CURSOR_WATER : cursors[group];
    ui->tool_group = group;
    ui->selection.value[group] = variant;
    if (group == CITY_TOOL_GROUP_REWARDS)
      SetCityToolToolbarStyle(city, CITY_TOOLBAR_BUTTON_CENTER, CITY_TOOLBAR_STYLE_CHECKBOX);
  } else {
    switch (group) {
    case CITY_TOOL_GROUP_SIGN:
      ui->selected_cursor = CITY_CURSOR_SIGN;
      ui->tool_group = CITY_TOOL_GROUP_SIGN;
      break;
    case CITY_TOOL_GROUP_QUERY:
      ui->selected_cursor = CITY_CURSOR_QUERY;
      ui->tool_group = CITY_TOOL_GROUP_QUERY;
      break;
    case CITY_TOOLBAR_BUTTON_ROTATE_CLOCKWISE:
    case CITY_TOOLBAR_BUTTON_ROTATE_COUNTERCLOCKWISE:
      if (group == CITY_TOOLBAR_BUTTON_ROTATE_CLOCKWISE)
        RotateFocusedCityMapView(city, true);
      else
        RotateFocusedCityMapView(city, false);
      SetCityToolToolbarStyle(city, (int)group, CITY_TOOLBAR_STYLE_NORMAL);
      break;
    case CITY_TOOLBAR_BUTTON_ZOOM_OUT:
      ZoomCityMapViewOut(city, view);
      SetCityToolToolbarStyle(city, CITY_TOOLBAR_BUTTON_ZOOM_OUT,
          view->zoom != CITY_MAP_ZOOM_SMALL ? CITY_TOOLBAR_STYLE_NORMAL : CITY_TOOLBAR_STYLE_DISABLED);
      SetCityToolToolbarStyle(city, CITY_TOOLBAR_BUTTON_ZOOM_IN,
          view->zoom + view->magnified_large_zoom < CityMapZoomLimit(drawing->map_pixel_scale * 100) ?
          CITY_TOOLBAR_STYLE_NORMAL : CITY_TOOLBAR_STYLE_DISABLED);
      break;
    case CITY_TOOLBAR_BUTTON_ZOOM_IN:
      ZoomCityMapViewIn(city, view);
      SetCityToolToolbarStyle(city, CITY_TOOLBAR_BUTTON_ZOOM_IN,
          view->zoom + view->magnified_large_zoom >= CityMapZoomLimit(drawing->map_pixel_scale * 100) ?
          CITY_TOOLBAR_STYLE_DISABLED : CITY_TOOLBAR_STYLE_NORMAL);
      if (view->magnified_large_zoom == 0 && view->zoom == CITY_MAP_ZOOM_MEDIUM)
        SetCityToolToolbarStyle(city, CITY_TOOLBAR_BUTTON_ZOOM_OUT, CITY_TOOLBAR_STYLE_NORMAL);
      break;
    case CITY_TOOLBAR_BUTTON_CENTER:
      ui->selected_cursor = CITY_CURSOR_CENTER;
      ui->tool_group = CITY_TOOL_GROUP_CENTER;
      break;
    case CITY_TOOLBAR_BUTTON_MAP:
    case CITY_TOOLBAR_BUTTON_POPULATION:
    case CITY_TOOLBAR_BUTTON_NEIGHBORS:
    case CITY_TOOLBAR_BUTTON_GRAPHS:
    case CITY_TOOLBAR_BUTTON_INDUSTRY:
      SetCityPanelVisibilityByCode(city, panels[group - CITY_TOOLBAR_BUTTON_MAP]);
      break;
    case CITY_TOOLBAR_BUTTON_BUDGET: {
      ShowBudgetDialogManually(city);
      SetCityToolToolbarStyle(city, CITY_TOOLBAR_BUTTON_BUDGET, CITY_TOOLBAR_STYLE_NORMAL);
      break;
    }
    case CITY_TOOLBAR_BUTTON_SHOW_BUILDINGS:
    case CITY_TOOLBAR_BUTTON_SHOW_SIGNS:
    case CITY_TOOLBAR_BUTTON_SHOW_TILE_DETAILS:
    case CITY_TOOLBAR_BUTTON_ZONE_GROUND:
    case CITY_TOOLBAR_BUTTON_UNDERGROUND:
      drawing->map_state.display_flags[CITY_TOOLBAR_BUTTON_UNDERGROUND - group] =
          drawing->map_state.display_flags[CITY_TOOLBAR_BUTTON_UNDERGROUND - group] == 0;
      if (group == CITY_TOOLBAR_BUTTON_UNDERGROUND)
        SwitchCityMapOverlayFlagBank(city, 0);
      RefreshCityMapViews(city->game);
      break;
    case CITY_TOOLBAR_BUTTON_HELP:
      ShowCityToolHelp(city, CITY_HELP_TOPIC_MAP_TOOLS);
      SetCityToolToolbarStyle(city, CITY_TOOLBAR_BUTTON_HELP, CITY_TOOLBAR_STYLE_NORMAL);
      break;
    default:
      break;
    }
  }
  Sc2kRefreshCityStatusPanel(city);
  PreloadSelectedCityToolSound(city);
  // Defined replacement for stock signed-group out-of-bounds access on malformed high-bit input.
  if (group < CITY_TOOL_GROUP_SIGN) {
    int selected = ui->selection.value[group];
    if ((group == CITY_TOOL_GROUP_WATER && selected == CITY_WATER_PIPES) ||
        (group == CITY_TOOL_GROUP_RAIL && selected == CITY_RAIL_SUBWAY)) {
      if (drawing->map_state.events_suppressed == 0) {
        drawing->map_state.events_suppressed = 1;
        SwitchCityMapOverlayFlagBank(city, 1);
        RefreshCityMapViews(city->game);
      }
    } else if (group != CITY_TOOL_GROUP_DEMOLISH &&
        (group != CITY_TOOL_GROUP_RAIL ||
            (selected != CITY_RAIL_SUBWAY_STATION && selected != CITY_RAIL_SUBWAY_RAIL_CONNECTION)) &&
        (group != CITY_TOOL_GROUP_WATER || selected != CITY_WATER_PUMP)) {
      if (drawing->map_state.events_suppressed != 0) {
        drawing->map_state.events_suppressed = 0;
        SwitchCityMapOverlayFlagBank(city, 1);
        RefreshCityMapViews(city->game);
      }
    }
  }
}

// SAR r16 masks the count to five bits, then saturates its sign for counts >= 16.
static int ToolboxOriginShift(int value, int count) {
  u32 shift = (u32)count & 31;
  return (value >> shift);
}

// Select terrain pointer, reset zoom and remove the first event overlay.
void ResetCityToolboxForTerrainEditing(City *city) {
  Game *game = city->game;
  CityInterfaceState *ui = game->ui;
  Drawing *drawing = game->drawing;
  CityMapViewConfig *view = drawing->default_map_view;

  ui->selected_cursor = CITY_CURSOR_CENTER;
  ui->terrain_tool = CITY_TERRAIN_TOOL_CENTER;
  PreloadSelectedCityToolSound(city);
  ui->terrain_toolbar[CITY_TERRAIN_BUTTON_CENTER].style = CITY_TOOLBAR_STYLE_CHECKED_CHECKBOX;
  for (int index = 0; index < CITY_TERRAIN_BUTTON_CENTER; ++index)
    if (index != CITY_TERRAIN_BUTTON_RAISE_WATER_LEVEL && index != CITY_TERRAIN_BUTTON_LOWER_WATER_LEVEL)
      ui->terrain_toolbar[index].style = CITY_TOOLBAR_STYLE_CHECKBOX;
  view->zoom = CITY_MAP_ZOOM_SMALL;
  if (view->magnified_large_zoom != 0) {
    view->magnified_large_zoom = 0;
    int x = ToolboxOriginShift(drawing->map_state.save_x, 2 - drawing->map_state.origin_x);
    int y = ToolboxOriginShift(drawing->map_state.save_y, 2 - drawing->map_state.origin_y);
    drawing->map_state.origin_x = drawing->map_state.origin_x - (x < 0 ? -x : x);
    drawing->map_state.origin_y = drawing->map_state.origin_y - (y < 0 ? -y : y);
  }
  if (drawing->map_state.events_suppressed != 0) {
    drawing->map_state.events_suppressed = 0;
    RefreshCityMapViews(city->game);
  }
  ui->terrain_toolbar[CITY_TERRAIN_BUTTON_ZOOM_OUT].style = CITY_TOOLBAR_STYLE_DISABLED;
  ui->terrain_toolbar[CITY_TERRAIN_BUTTON_ZOOM_IN].style = CITY_TOOLBAR_STYLE_NORMAL;
}

// The native canvas handles the default message; restore the selected city cursor.
void RestoreCityCursorAfterDialog(City *city) {
  SetCityCursor(city, CITY_CURSOR_ARROW, 1);
}

void DisableCityToolbarButton(City *city, int index) {
  CityInterfaceState *ui = city->game->ui;
  ui->city_toolbar[index].style |= CITY_TOOLBAR_STYLE_DISABLED;
}

// Disable the owned toolbar item.
void DisableCityToolbarItem(City *city, int index) {
  DisableCityToolbarButton(city, index);
}

// Clear the special-facility button, then run the original selection.
void SelectCityBulldozerFromToolbar(City *city, int group) {
  CityInterfaceState *ui = city->game->ui;
  ui->city_toolbar[CITY_TOOL_GROUP_REWARDS].style = CITY_TOOLBAR_STYLE_NORMAL;
  SelectCityToolGroupAndMode(city->game, (u32)group, CITY_TOOL_VARIANT_DEFAULT);
}

// Rebuild native tool choices; original menu receivers are comparison-only.
void RebuildCityToolbar(City *city) {
  Game *game = city->game;
  CityInterfaceState *ui = game->ui;
  Drawing *drawing = game->drawing;
  CityMapViewConfig *view = drawing->default_map_view;
  if (city->settings.timer_mode == CITY_TIMER_STOPPED)
    ResetCityToolboxForTerrainEditing(city);
  for (int index = 0; index < CITY_TOOLBAR_BUTTON_COUNT; ++index)
    if ((ui->city_toolbar[index].style & CITY_TOOLBAR_STYLE_SEPARATOR) == 0)
      ui->city_toolbar[index].style &= ~CITY_TOOLBAR_STYLE_INTERACTION_MASK;
  if (view->zoom == CITY_MAP_ZOOM_SMALL)
    ui->city_toolbar[CITY_TOOLBAR_BUTTON_ZOOM_OUT].style |= CITY_TOOLBAR_STYLE_DISABLED;
  if (view->magnified_large_zoom != 0)
    ui->city_toolbar[CITY_TOOLBAR_BUTTON_ZOOM_IN].style |= CITY_TOOLBAR_STYLE_DISABLED;
  if (city->settings.timer_mode == CITY_TIMER_RUNNING) {
    if (ui->tool_group == CITY_TOOL_GROUP_DISPATCH)
      ui->tool_group = CITY_TOOL_GROUP_CENTER;
    DisableCityToolbarItem(city, CITY_TOOL_GROUP_DISPATCH);
  }
  if (city->settings.timer_mode != CITY_TIMER_STOPPED) {
    if (city->unlocks.tools.shared.special_facility_mask == 0)
      DisableCityToolbarItem(city, CITY_TOOL_GROUP_REWARDS);
    int index = ui->tool_group;
    if (index > CITY_TOOL_GROUP_SIGN)
      index += CITY_TOOLBAR_BUTTON_CENTER - CITY_TOOL_GROUP_CENTER;
    ui->city_toolbar[index].style |= CITY_TOOLBAR_STYLE_CHECKED;
    for (int flag = 0; flag < CITY_MAP_DISPLAY_FLAG_COUNT; ++flag)
      ui->city_toolbar[CITY_TOOLBAR_BUTTON_UNDERGROUND - flag].style = CITY_TOOLBAR_STYLE_CHECKBOX |
          (drawing->map_state.display_flags[flag] != 0 ? CITY_TOOLBAR_STYLE_CHECKED : 0);
  }
  int available = city->unlocks.tools.slot[CITY_TOOL_GROUP_REWARDS];
  for (int variant = 0; variant < CITY_REWARD_ARCOLOGY_PICKER + 1; ++variant) {
    if ((available & (1 << variant)) == 0)
      continue;
    ui->tool_indices[CITY_TOOL_GROUP_REWARDS] = variant;
    memcpy(ui->tool_labels[CITY_TOOL_GROUP_REWARDS], game->resources->tool_names.name[CITY_TOOL_GROUP_REWARDS][variant],
        sizeof(ui->tool_labels[CITY_TOOL_GROUP_REWARDS]));
    break;
  }
}

// Retain each item style while a dialog temporarily disables tools.
bool EnterCityPaletteModalState(City *city) {
  CityInterfaceState *ui = city->game->ui;
  for (int index = 0; index < CITY_TOOLBAR_BUTTON_COUNT; ++index) {
    int style = ui->city_toolbar[index].style;
    ui->saved_toolbar_styles[index] = style;
    if ((style & CITY_TOOLBAR_STYLE_SEPARATOR) == 0)
      ui->city_toolbar[index].style = CITY_TOOLBAR_STYLE_DISABLED;
  }
  return true;
}

// Restore nonseparators using their current separator flag.
bool LeaveCityPaletteModalState(City *city) {
  CityInterfaceState *ui = city->game->ui;
  for (int index = 0; index < CITY_TOOLBAR_BUTTON_COUNT; ++index) {
    if ((ui->city_toolbar[index].style & CITY_TOOLBAR_STYLE_SEPARATOR) == 0)
      ui->city_toolbar[index].style = ui->saved_toolbar_styles[index];
  }
  return true;
}

// Rebuild native tool choices; menu handles have no City effect.
void RefreshCityToolbarAndClearOverlayMenuChecks(City *city) {
  RebuildCityToolbar(city);
}

void RefreshCityToolbarButtonStyles(City *city, bool reset_display_flags) {
  CityInterfaceState *ui = city->game->ui;
  Drawing *drawing = city->game->drawing;
  const CityMapViewConfig *view = drawing->default_map_view;
  for (int index = 0; index < CITY_TOOLBAR_BUTTON_COUNT; ++index) {
    bool visible = false;
    switch (index) {
    case CITY_TOOLBAR_BUTTON_MAP:
      visible = IsCityMapDataVisible(city);
      break;
    case CITY_TOOLBAR_BUTTON_POPULATION:
      visible = ui->charts.visible[CITY_CANVAS_CHART_DEMOGRAPHICS];
      break;
    case CITY_TOOLBAR_BUTTON_NEIGHBORS:
      visible = ui->charts.visible[CITY_CANVAS_CHART_NEIGHBORS];
      break;
    case CITY_TOOLBAR_BUTTON_GRAPHS:
      visible = ui->charts.visible[CITY_CANVAS_CHART_HISTORY];
      break;
    case CITY_TOOLBAR_BUTTON_INDUSTRY:
      visible = ui->charts.visible[CITY_CANVAS_CHART_INDUSTRY];
      break;
    default:
      break;
    }
    if (visible)
      break;
    if ((ui->city_toolbar[index].style & CITY_TOOLBAR_STYLE_SEPARATOR) == 0)
      ui->city_toolbar[index].style &= ~CITY_TOOLBAR_STYLE_INTERACTION_MASK;
  }
  if (view->zoom == CITY_MAP_ZOOM_SMALL)
    ui->city_toolbar[CITY_TOOLBAR_BUTTON_ZOOM_OUT].style |= CITY_TOOLBAR_STYLE_DISABLED;
  if (view->zoom + view->magnified_large_zoom >= CityMapZoomLimit(drawing->map_pixel_scale * 100))
    ui->city_toolbar[CITY_TOOLBAR_BUTTON_ZOOM_IN].style |= CITY_TOOLBAR_STYLE_DISABLED;
  if (reset_display_flags) {
    if (drawing->map_state.events_suppressed != 0) {
      drawing->map_state.events_suppressed = 0;
      ui->city_toolbar[CITY_TOOLBAR_BUTTON_UNDERGROUND].style = CITY_TOOLBAR_STYLE_CHECKBOX;
    }
    for (int index = CITY_MAP_DISPLAY_ZONE_GROUND; index < CITY_MAP_DISPLAY_FLAG_COUNT; ++index) {
      if (drawing->map_state.display_flags[index] == 0) {
        drawing->map_state.display_flags[index] = 1;
        ui->city_toolbar[CITY_TOOLBAR_BUTTON_UNDERGROUND - index].style = CITY_TOOLBAR_STYLE_CHECKED_CHECKBOX;
      }
    }
  }
}

// Input advances a waiting startup screen; SDL already dispatches controls.
void RecordGameInput(Game *game) {
  if (game->idle_state != CITY_IDLE_PLAY) game->idle_input = 1;
}

int GetCityToolbarStyle(const CityInterfaceState *ui, int index) {
  return index >= 0 && index < CITY_TOOLBAR_BUTTON_COUNT ? ui->city_toolbar[index].style : 0;
}

// Toolbar State

void SetCityToolbarStyle(CityInterfaceState *ui, int index, int style) {
  if (index >= 0 && index < CITY_TOOLBAR_BUTTON_COUNT) ui->city_toolbar[index].style = style;
}

void InitializeCityToolbars(CityInterfaceState *ui) {
  memset(ui->city_toolbar, 0, sizeof(ui->city_toolbar));
  memset(ui->terrain_toolbar, 0, sizeof(ui->terrain_toolbar));
  for (int index = 0; index < CITY_TOOL_BUTTON_CAPACITY; ++index) {
    ui->city_toolbar[index].style = CITY_TOOLBAR_STYLE_CHECKBOX;
    ui->terrain_toolbar[index].style = CITY_TOOLBAR_STYLE_CHECKBOX;
  }
}

// Frame Tools
// Toolbar visibility is interface policy, outside deterministic City state.

void SetCityToolsVisible(Game *game, bool visible) {
  if (visible && game->ranked_events_suppressed != 0)
    return;
  game->ui->tools_visible = visible;
}

// Native control visibility is UI policy.
void SetCityMapControlsVisible(CityInterfaceState *ui, bool visible) {
  ui->map_controls_visible = visible;
}

// Enter map drawing once, using the owned controls.
void ShowCityMapToolWindows(City *city) {
  Game *game = city->game;
  Drawing *drawing = game->drawing;
  if (drawing->map_state.scroll_redraw != 0)
    return;
  drawing->map_state.scroll_redraw = 1;
  SetCityMapControlsVisible(game->ui,
      game->idle_state != CITY_IDLE_TERRAIN && city->settings.timer_mode != CITY_TIMER_STOPPED);
}

// Remember interactive loading; original message flags and menu are unused.
void MarkSelectedCityMenuCommand(Game *game, int command) {
  if (command == CITY_MENU_LOAD_CITY || command == CITY_MENU_LOAD_SCENARIO) {
    game->ui->restore_frame_tools = true;
  }
}

// Tool Presentation

void SetCityToolToolbarStyle(City *city, int button, int style) {
  if (button < 0 || button >= CITY_TOOLBAR_BUTTON_COUNT) {
    SetGameError(city->game, SC2K_ERROR_GUEST_BUFFER_TOO_SMALL);
    return;
  }
  SetCityToolbarStyle(city->game->ui, button, style);
}

void EnableCityFacilityToolbarItem(City *city, int index) {
  // Clears only the disabled bit, preserving the other style bits.
  SetCityToolToolbarStyle(city, index, GetCityToolbarStyle(city->game->ui, index) & ~CITY_TOOLBAR_STYLE_DISABLED);
}

void RefreshCityRewardToolbar(City *city) {
  if (city->unlocks.tools.shared.special_facility_mask)
    EnableCityFacilityToolbarItem(city, CITY_TOOL_GROUP_REWARDS);
  else
    DisableCityToolbarItem(city, CITY_TOOL_GROUP_REWARDS);
}

void ShowCityToolHelp(City *city, int topic) {
  CityCanvasDesktop *desktop = &city->game->ui->desktop;
  if (topic != CITY_HELP_TOPIC_MAP_TOOLS) {
    return;
  }
  static const char kText[] =
      "Choose a tool in the toolbar. Drag on the map to build or change terrain. "
      "Use the magnifying glass buttons to zoom and the arrow buttons to rotate. "
      "The information tool describes a tile; the sign tool adds a label.";
  int width = IntMin(desktop->canvas_width, 360);
  int height = IntMin(desktop->canvas_height, 150);
  if (width <= 0 || height <= 0) {
    return;
  }
  int x = (desktop->canvas_width - width) / 2;
  int y = (desktop->canvas_height - height) / 2;
  if (Sc2kCanvasDesktopOpenNotice(desktop, (CityRect){x, y, x + width, y + height},
      "Map tools", kText, CITY_MESSAGE_INFORMATION | CITY_MESSAGE_OK) < 0)
    SetGameError(city->game, SC2K_ERROR_GUEST_ALLOCATION_FAILURE);
}

void Sc2kCanvasSetMapZoom(CityCanvasDesktop *desktop, int level) {
  if (level >= CITY_CANVAS_MAP_ZOOM_SMALL && level < CITY_CANVAS_MAP_ZOOM_COUNT)
    desktop->map_zoom = level;
}

void ResetCityNewTools(City *city) {
  CityInterfaceState *ui = city->game->ui;
  if (!ui) return;
  memset(ui->selection.value, 0, sizeof(ui->selection.value));
  ui->tool_group = CITY_TOOL_GROUP_CENTER;
  if (city->game->idle_state == CITY_IDLE_TERRAIN) return;
  ui->selection.value[CITY_TOOL_GROUP_RESIDENTIAL] = 1;
  ui->selection.value[CITY_TOOL_GROUP_COMMERCIAL] = 1;
  ui->selection.value[CITY_TOOL_GROUP_INDUSTRIAL] = 1;
}

void SelectCityDispatchTool(City *city, int tool) {
  if (city->game->ui) city->game->ui->selection.value[CITY_TOOL_GROUP_DISPATCH] = tool;
}

TileIndex TrackCitySelectedInfrastructureLine(Game *game, TileIndex start, int group) {
  int cost = kCityToolCosts[group * CITY_TOOL_VARIANT_CAPACITY + game->ui->selection.value[group]];
  return TrackInfrastructureLinePlacement(game, start, cost);
}

void ShowCityMapControls(City *city, bool visible) {
  if (city->game->ui) SetCityMapControlsVisible(city->game->ui, visible);
}

void RestoreCityMapControls(City *city) {
  if (city->game->drawing->map_state.scroll_redraw) ShowCityMapControls(city, true);
}
