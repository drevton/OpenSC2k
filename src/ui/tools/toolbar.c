#include <string.h>
#include "base/text.h"

#include "assets/state.h"
#include "game/audio.h"
#include "audio/backend.h"
#include "audio/state.h"
#include "city/engine/city_types.h"
#include "city/engine/simulation.h"
#include "city/events/emergency.h"
#include "city/presentation.h"
#include "city/saveload/file.h"
#include "city/state.h"
#include "city/tools/tools.h"
#include "city/tools/types.h"
#include "drawing/maps/map_view.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/state.h"
#include "game/errors.h"
#include "game/state.h"
#include "ui/windows/window_internal.h"
#include "ui/charts/charts.h"
#include "ui/commands.h"
#include "ui/dialogs/budget.h"
#include "ui/dialogs/dialogs.h"
#include "ui/dialogs/map_data.h"
#include "ui/dialogs/messages.h"
#include "ui/map.h"
#include "ui/input.h"
#include "ui/text/status.h"
#include "ui/sdl.h"
#include "ui/state.h"
#include "ui/text/text.h"
#include "ui/tools/tools.h"
#include "ui/tools/types.h"
#include "ui/windows/windows.h"

u32 Sc2kCurrentCityMapPointerTools(const CityInterfaceState *ui) {
  int group = (ui->tool_group);
  u32 variant = group >= CITY_TOOL_GROUP_DEMOLISH && group < CITY_TOOL_GROUP_BUILD_COUNT
      ? ui->selection.value[group]
      : CITY_TOOL_VARIANT_DEFAULT;
  return (u16)group | (variant << 8) | ((u32)(ui->selection.value[0]) << 16);
}

// Toolbar

// Resolve local tool choices without changing City or Audio. Unsupported rail and civic variants keep the
// previous preload, replacing the original undefined choice from an uninitialized stack slot.
int GetCityToolPlacementSoundId(const CityInterfaceState *ui, bool terrain_editor, int previous_sound) {
  if (terrain_editor)
    return ui->terrain_tool >= CITY_TERRAIN_TOOL_TREE && ui->terrain_tool <= CITY_TERRAIN_TOOL_FOREST ?
        CITY_SOUND_TREE_OR_ZONE_PLACEMENT : CITY_SOUND_WATER;
  int group = ui->tool_group;
  switch (group) {
  case CITY_TOOL_GROUP_TREE_WATER:
    return ui->selection.value[group] == CITY_TREE_WATER_TREES ? CITY_SOUND_TREE_OR_ZONE_PLACEMENT : CITY_SOUND_WATER;
  case CITY_TOOL_GROUP_DISPATCH:
    if (ui->selection.value[group] == CITY_DISPATCH_POLICE)
      return CITY_SOUND_POLICE_OR_HOSPITAL;
    return ui->selection.value[group] == CITY_DISPATCH_FIRE ? CITY_SOUND_FIRE_DISPATCH : CITY_SOUND_MILITARY_DISPATCH;
  case CITY_TOOL_GROUP_POWER:
    return ui->selection.value[group] == CITY_POWER_LINES ? CITY_SOUND_POWER_LINES : CITY_SOUND_CONSTRUCTION;
  case CITY_TOOL_GROUP_WATER:
  case CITY_TOOL_GROUP_PORT: return CITY_SOUND_CONSTRUCTION;
  case CITY_TOOL_GROUP_REWARDS: return CITY_SOUND_CIVIC_PLACEMENT;
  case CITY_TOOL_GROUP_ROAD:
    return ui->selection.value[group] == CITY_ROAD_BUS_DEPOT ? CITY_SOUND_BUS_DEPOT : CITY_SOUND_CONSTRUCTION;
  case CITY_TOOL_GROUP_RAIL:
    return ui->selection.value[group] == CITY_RAIL_DEPOT ? previous_sound : CITY_SOUND_CONSTRUCTION;
  case CITY_TOOL_GROUP_RESIDENTIAL:
  case CITY_TOOL_GROUP_COMMERCIAL:
  case CITY_TOOL_GROUP_INDUSTRIAL: return CITY_SOUND_TREE_OR_ZONE_PLACEMENT;
  case CITY_TOOL_GROUP_EDUCATION: return CITY_SOUND_EDUCATION;
  case CITY_TOOL_GROUP_CIVIC:
    switch (ui->selection.value[group]) {
    case CITY_CIVIC_POLICE:
    case CITY_CIVIC_HOSPITAL: return CITY_SOUND_POLICE_OR_HOSPITAL;
    case CITY_CIVIC_FIRE: return CITY_SOUND_FIRE_DISPATCH;
    case CITY_CIVIC_PRISON: return CITY_SOUND_PRISON;
    default: return previous_sound;
    }
  case CITY_TOOL_GROUP_RECREATION:
    return ui->selection.value[group] == CITY_RECREATION_ZOO ? CITY_SOUND_SAILBOAT_OR_ZOO :
        CITY_SOUND_CIVIC_PLACEMENT;
  default: return previous_sound;
  }
}

bool PreloadSelectedCityToolSound(Game *game) {
  bool terrain_editor = game->city->settings.timer_mode == CITY_TIMER_STOPPED;
  int sound_id = GetCityToolPlacementSoundId(game->ui, terrain_editor, game->audio->preloaded_sound_id);
  return PreloadCityToolSound(game->audio, sound_id);
}

// Save outgoing map flags and restore the surface/underground bank.
// Every caller discards EAX; only the MFC button presentation is a host service.
void SwitchCityMapOverlayFlagBank(City *city, int refresh_button) {
  Drawing *drawing = city->drawing;
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
  static const u8 kPanels[5] = {
      CITY_PANEL_MAP_DATA, CITY_PANEL_DEMOGRAPHICS, CITY_PANEL_NEIGHBORS, CITY_PANEL_HISTORY, CITY_PANEL_INDUSTRY};
  if (group < CITY_TOOL_GROUP_SIGN) {
    u32 name_index = group == CITY_TOOL_GROUP_REWARDS && variant > CITY_REWARD_ARCOLOGY_PICKER
        ? CITY_REWARD_ARCOLOGY_PICKER
        : variant;
    const char *name = CityToolName(group, name_index);
    CopyText(ui->tool_labels[group], sizeof(ui->tool_labels[group]), name);
    ui->tool_indices[group] = (int)name_index;
    ui->selected_cursor = group == CITY_TOOL_GROUP_TREE_WATER && variant != CITY_TREE_WATER_TREES ?
        CITY_CURSOR_WATER : kCityToolCursors[group];
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
      SetCityPanelVisibilityByCode(city, kPanels[group - CITY_TOOLBAR_BUTTON_MAP]);
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
  PreloadSelectedCityToolSound(game);
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

bool SelectCityToolWithModifiers(Game *game, int group, int variant, u32 modifiers) {
  ClearGameError(game);
  CityInterfaceState *ui = game->ui;
  SetCityAltKeyPressed(ui, (modifiers & CITY_POINTER_MODIFIER_ALT) != 0);
  SetCityShiftKeyPressed(ui, (modifiers & CITY_POINTER_MODIFIER_SHIFT) != 0);
  SetCityCtrlKeyPressed(ui, (modifiers & CITY_POINTER_MODIFIER_CONTROL) != 0);
  SelectCityToolGroupAndMode(game, group, variant);
  if (game->city->settings.timer_mode == CITY_TIMER_STOPPED) {
    if (group == CITY_TOOL_GROUP_TREE_WATER) {
      ui->terrain_tool = variant == CITY_TREE_WATER_WATER ? CITY_TERRAIN_TOOL_WATER : CITY_TERRAIN_TOOL_TREE;
    } else if (group == CITY_TOOL_GROUP_DEMOLISH) {
      switch (variant) {
      case CITY_DEMOLISH_RAISE: ui->terrain_tool = CITY_TERRAIN_TOOL_RAISE; break;
      case CITY_DEMOLISH_LOWER: ui->terrain_tool = CITY_TERRAIN_TOOL_LOWER; break;
      case CITY_DEMOLISH_LEVEL: ui->terrain_tool = CITY_TERRAIN_TOOL_LEVEL; break;
      default: ui->terrain_tool = CITY_TERRAIN_TOOL_NONE; break;
      }
    } else ui->terrain_tool = CITY_TERRAIN_TOOL_CENTER;
  }
  return !GetGameError(game);
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
  PreloadSelectedCityToolSound(game);
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

// Disable the owned toolbar item without changing its other style bits.
void DisableCityToolbarItem(City *city, int index) {
  CityInterfaceState *ui = city->game->ui;
  ui->city_toolbar[index].style |= CITY_TOOLBAR_STYLE_DISABLED;
}

// Clear the reward button and select the ordinary center tool.
void ResetCityRewardSelection(City *city) {
  CityInterfaceState *ui = city->game->ui;
  ui->city_toolbar[CITY_TOOL_GROUP_REWARDS].style = CITY_TOOLBAR_STYLE_NORMAL;
  SelectCityToolGroupAndMode(city->game, CITY_TOOLBAR_BUTTON_CENTER, CITY_TOOL_VARIANT_DEFAULT);
}

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
    CopyText(ui->tool_labels[CITY_TOOL_GROUP_REWARDS], sizeof(ui->tool_labels[CITY_TOOL_GROUP_REWARDS]),
        CityToolName(CITY_TOOL_GROUP_REWARDS, variant));
    break;
  }
}

// Retain each item style while a dialog temporarily disables tools.
void SuspendCityToolbarForDialog(CityInterfaceState *ui) {
  for (int index = 0; index < CITY_TOOLBAR_BUTTON_COUNT; ++index) {
    int style = ui->city_toolbar[index].style;
    ui->saved_toolbar_styles[index] = style;
    if ((style & CITY_TOOLBAR_STYLE_SEPARATOR) == 0)
      ui->city_toolbar[index].style = CITY_TOOLBAR_STYLE_DISABLED;
  }
}

// Restore nonseparators using their current separator flag.
void RestoreCityToolbarAfterDialog(CityInterfaceState *ui) {
  for (int index = 0; index < CITY_TOOLBAR_BUTTON_COUNT; ++index) {
    if ((ui->city_toolbar[index].style & CITY_TOOLBAR_STYLE_SEPARATOR) == 0)
      ui->city_toolbar[index].style = ui->saved_toolbar_styles[index];
  }
}

void RefreshCityToolbarButtonStyles(City *city, bool reset_display_flags) {
  CityInterfaceState *ui = city->game->ui;
  Drawing *drawing = city->drawing;
  const CityMapViewConfig *view = drawing->default_map_view;
  for (int index = 0; index < CITY_TOOLBAR_BUTTON_COUNT; ++index) {
    bool visible = false;
    switch (index) {
    case CITY_TOOLBAR_BUTTON_MAP:
      visible = ui->map_data.visible;
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

void ShowCityMapControls(City *city, bool visible) {
  if (city->game->ui) SetCityMapControlsVisible(city->game->ui, visible);
}

void RestoreCityMapControls(City *city) {
  if (city->drawing->map_state.scroll_redraw) ShowCityMapControls(city, true);
}

static void CanvasDrawToolStatus(Canvas *canvas, const Window *window) {
  CityRect bounds = window->bounds;
  CanvasBeveledBox(canvas, bounds.left, bounds.top, bounds.right, bounds.bottom, 0xFFBDBDBDu, 1);
  CanvasTextFont(canvas, bounds.left + 5, bounds.top + 4, window->message, canvas_text, 1,
      kCanvasHeadingFont);
  if (window->policy_flags)
    CanvasTextFont(canvas, bounds.left + 5, bounds.top + 20, "*PAUSED*", 0xFF00CC00u, 1,
        kCanvasHeadingFont);
}

// Handles this window's painting and input; paint coordinates are local to the window.
bool ToolStatusWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    Canvas *canvas = event->canvas;
    CanvasDrawToolStatus(canvas, window);
    return true;
  }
  if (event->kind != WINDOW_INPUT) return false;
  const CanvasSDLEvent *input = event->input;
  if (input->kind == CANVAS_SDL_POINTER_DOWN && input->button == CANVAS_SDL_BUTTON_LEFT) {
    CityCanvasDesktop *desktop = &game->ui->desktop;
    desktop->dragging = desktop->count - 1;
    desktop->drag_x = event->x - window->bounds.left;
    desktop->drag_y = event->y - window->bounds.top;
    return true;
  }
  if (input->kind == CANVAS_SDL_KEY_DOWN && input->key == CITY_KEY_ESCAPE) return true;
  return false;
}
