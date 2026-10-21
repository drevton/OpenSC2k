#include "game/state.h"
#include "city/state.h"
#include "drawing/state.h"
#include "ui/map.h"
#include "drawing/drawing.h"
#include "ui/state.h"
#include "ui/text/text.h"

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
