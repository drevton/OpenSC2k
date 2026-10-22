#include "ui/map_visibility.h"
#include "ui/cursor/cursor.h"

#include "city/engine/city_types.h"
#include "city/presentation.h"
#include "city/state.h"
#include "drawing/state.h"
#include "game/state.h"
#include "ui/input.h"
#include "ui/map.h"
#include "ui/sdl.h"
#include "ui/state.h"
#include "ui/text/text.h"
#include "ui/tools/types.h"

void SetCityMapCursorPosition(CityInterfaceState *ui, int x, int y) {
  ui->map_cursor_position = (CityPoint){x, y};
  ui->map_cursor_position_set = true;
}
void ReadCityMapCursorRect(Game *game, CityRect *rect) {
  ReadCityMapClientRect(game->drawing, rect);
  if (game->cursor_observed != NULL)
    game->cursor_observed(game, (CityCursorInputRecord){CITY_CURSOR_INPUT_WINDOW_RECT, 0,
        {rect->left, rect->top, rect->right, rect->bottom}});
}
void ReadCityMapCursorPosition(Game *game, int *x, int *y) {
  const CityInterfaceState *ui = game->ui;
  *x = ui->map_cursor_position_set ? ui->map_cursor_position.x : -1;
  *y = ui->map_cursor_position_set ? ui->map_cursor_position.y : -1;
  if (game->cursor_observed != NULL)
    game->cursor_observed(game, (CityCursorInputRecord){CITY_CURSOR_INPUT_POSITION, 0, {*x, *y, 0, 0}});
}

// Original cursor handles are opaque presentation identifiers. The SDL canvas
// draws the selected tool cursor; these leaves retain the previous identifier.
u32 LoadCityWaitCursor(Game *game) {
  if (game->cursor_observed != NULL)
    game->cursor_observed(game,
        (CityCursorInputRecord){CITY_CURSOR_INPUT_LOAD_WAIT, 0, {CITY_SYSTEM_CURSOR_WAIT, 0, 0, 0}});
  return CITY_SYSTEM_CURSOR_WAIT;
}
u32 InstallCityMapCursor(Game *game, u32 handle) {
  CityInterfaceState *ui = game->ui;
  u32 previous = ui->installed_cursor_handle;
  ui->installed_cursor_handle = handle;
  if (game->cursor_observed != NULL)
    game->cursor_observed(game, (CityCursorInputRecord){CITY_CURSOR_INPUT_INSTALL, handle,
        {(int)previous, 0, 0, 0}});
  return previous;
}
bool ShowCityMapCursor(Game *game) {
  if (game->cursor_observed != NULL)
    game->cursor_observed(game, (CityCursorInputRecord){CITY_CURSOR_INPUT_SHOW, 0, {1, 0, 0, 0}});
  return true;
}
void RefreshCityCursorStatus(City *city, int mode) {
  RefreshCityStatusPanel(city, &city->game->ui->status,
      city->settings.speed, mode);
}
void InvalidateCityMapSelection(City *city, const CityRect *rect) {
  InvalidateCityMap(city->game, rect);
}

void Sc2kRefreshCityStatusPanel(City *city) {
  RefreshCityCursorStatus(city, 0);
}

// Install or restore the city's selected cursor.
u32 SetCityCursor(City *city, int cursor_id, int restore) {
  CityInterfaceState *ui = city->game->ui;
  u32 ranked_events_suppressed = city->game->ranked_events_suppressed;
  if (ui->wait_cursor_active != 0) {
    InstallCityMapCursor(city->game, LoadCityWaitCursor(city->game));
    int count;
    do {
      count = ShowCityMapCursor(city->game);
    } while (count == 0);
    return (u32)count;
  }
  if (restore == 0 || ranked_events_suppressed != 0) {
    u32 result = ranked_events_suppressed;
    ui->cursor_id = cursor_id;
    if (ranked_events_suppressed == 0) {
      if (cursor_id != CITY_CURSOR_ARROW) {
        RefreshCityCursorStatus(city, 0);
      }
      result = InstallCityMapCursor(city->game, ui->cursor_handle[cursor_id]);
    }
    ui->selected_cursor = cursor_id;
    return result;
  }
  if (ui->selected_cursor != CITY_CURSOR_ARROW) {
    RefreshCityCursorStatus(city, 0);
  }
  return InstallCityMapCursor(city->game, ui->cursor_handle[ui->selected_cursor]);
}

// Choose the pointer cursor from the mode, tool, and modifier keys.
u32 UpdateCityMapCursor(Game *game) {
  City *city = game->city;
  CityInterfaceState *ui = game->ui;
  static const u8 kCityToolCursors[20] = {
      CITY_CURSOR_DEMOLISH, CITY_CURSOR_TREE, CITY_CURSOR_DISPATCH, CITY_CURSOR_POWER, CITY_CURSOR_WATER_UTILITY,
      CITY_CURSOR_REWARDS, CITY_CURSOR_ROAD, CITY_CURSOR_RAIL, CITY_CURSOR_PORT, CITY_CURSOR_RESIDENTIAL,
      CITY_CURSOR_COMMERCIAL, CITY_CURSOR_INDUSTRIAL, CITY_CURSOR_EDUCATION, CITY_CURSOR_CIVIC,
      CITY_CURSOR_RECREATION, CITY_CURSOR_SIGN, CITY_CURSOR_QUERY, CITY_CURSOR_CENTER, CITY_CURSOR_ARROW,
      CITY_CURSOR_ARROW};
  static const u8 kCityTerrainToolCursors[11] = {
      CITY_CURSOR_ARROW, CITY_CURSOR_RAISE_TERRAIN, CITY_CURSOR_LOWER_TERRAIN, CITY_CURSOR_STRETCH_TERRAIN,
      CITY_CURSOR_LEVEL_TERRAIN, CITY_CURSOR_WATER, CITY_CURSOR_RIVER, CITY_CURSOR_TREE, CITY_CURSOR_FOREST,
      CITY_CURSOR_CENTER, CITY_CURSOR_PORT};

  int cursor_id = CITY_CURSOR_ARROW;
  if (ui->pointer_mode == CITY_MAP_POINTER_HOVER) {
    if (city->settings.timer_mode == CITY_TIMER_STOPPED) {
      cursor_id = IsCtrlKeyPressed(ui) ? CITY_CURSOR_CENTER : kCityTerrainToolCursors[ui->terrain_tool];
    } else if (city->settings.timer_mode >= CITY_TIMER_RUNNING &&
        city->settings.timer_mode <= CITY_TIMER_EVENT) {
      if (IsCtrlKeyPressed(ui))
        cursor_id = CITY_CURSOR_DEMOLISH;
      else if (IsShiftKeyPressed(ui))
        cursor_id = CITY_CURSOR_QUERY;
      else if (ui->tool_group == CITY_TOOL_GROUP_TREE_WATER &&
          (i16)ui->selection.value[CITY_TOOL_GROUP_TREE_WATER] == CITY_TREE_WATER_WATER)
        cursor_id = CITY_CURSOR_WATER;
      else
        cursor_id = kCityToolCursors[ui->tool_group];
    }
  }
  CityRect window_rect = {0};
  int pointer_x = 0;
  int pointer_y = 0;
  ReadCityMapCursorRect(city->game, &window_rect);

  ReadCityMapCursorPosition(city->game, &pointer_x, &pointer_y);
  if (pointer_x < window_rect.left || pointer_x >= window_rect.right ||
      pointer_y < window_rect.top || pointer_y >= window_rect.bottom)
    return 0;
  return SetCityCursor(city, cursor_id, 0);
}

// Restore the selected cursor after a dialog, retaining wait and suppression policy.
void RestoreCityCursorAfterDialog(City *city) {
  SetCityCursor(city, CITY_CURSOR_ARROW, 1);
}

// Select the host tool cursor only over an unobscured, interactive map.
void UpdateCityToolCursor(City *city) {
  CityInterfaceState *ui = city->game->ui;
  Sc2kSDLCursorTool(ui->display,
      ui->placement_hover_ctrl ? CITY_CANVAS_TOOL_DEMOLISH : ui->desktop.selected_tool,
      ui->placement_hover_valid && !ui->placement_hover_shift && ui->desktop.dragging == CITY_CANVAS_DRAG_NONE &&
      !ui->options_slider && !ui->jukebox_slider &&
      IsCityMainMapInteractiveAt(city, ui->placement_hover.x, ui->placement_hover.y));
}
