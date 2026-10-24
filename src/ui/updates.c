#include "ui/updates.h"

#include "city/state.h"
#include "drawing/bitmaps.h"
#include "game/audio.h"
#include "game/state.h"
#include "ui/charts/charts.h"
#include "ui/dialogs/budget.h"
#include "ui/dialogs/dialogs.h"
#include "ui/dialogs/map_data.h"
#include "ui/music/jukebox.h"
#include "ui/news/newspaper.h"
#include "ui/state.h"
#include "ui/text/status.h"
#include "ui/text/text.h"
#include "ui/windows/tooltips.h"
#include "ui/windows/windows.h"

// A successful framebuffer resize requires repainting the whole current UI canvas.
void InvalidateCityDesktop(CityInterfaceState *ui) {
  CityCanvasDesktop *desktop = &ui->desktop;
  Sc2kDirtyMarkRect(&desktop->dirty, (CityRect){0, 0, desktop->canvas_width, desktop->canvas_height});
}

void InvalidateCityMapSpritePreviews(CityInterfaceState *ui) {
  if (ui->tree_picker.drawing) InvalidateCityWindowBitmaps(&ui->tree_picker.drawing->base);
  for (int i = 0; i < CITY_STARTUP_WINDOW_LIMIT; ++i)
    if (ui->startup_windows[i].layout.drawing)
      InvalidateCityWindowBitmaps(&ui->startup_windows[i].layout.drawing->base);
}

void InvalidateCityInformation(Game *game, u32 categories) {
  if (game && game->ui) game->ui->desktop.information_updates |= categories;
}

void PrepareCityInformation(Game *game) {
  if (!game->ui || !game->city_active) return;
  CityCanvasDesktop *desktop = &game->ui->desktop;
  u32 updates = desktop->information_updates;
  if (!updates) return;
  desktop->information_updates = 0;
  City *city = game->city;
  bool budget = false, policies = false, map_data = false, land_use = false;
  for (int index = 0; index < desktop->count; ++index) {
    Window *window = &desktop->windows[index];
    if (!(window->update_categories & updates)) continue;
    if (window->visible) Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(window));
    // Only windows with retained presentation data need preparation. Ordinary
    // painters read City directly; no window procedure runs during notification.
    switch (window->kind) {
    case CITY_CANVAS_BUDGET: case CITY_CANVAS_BONDS: budget = true; break;
    case CITY_CANVAS_POLICIES: policies = true; break;
    case CITY_CANVAS_MAP_DATA: map_data = true; break;
    case CITY_CANVAS_LAND_USE: land_use = true; break;
    case CITY_CANVAS_INDUSTRY:
      PrepareCityChartPaintState(city, CITY_PANEL_INDUSTRY);
      break;
    default: break;
    }
  }
  if (budget) {
    CityBudgetPanel panel;
    Sc2kCanvasBudgetFromCity(city, &panel);
    Sc2kCanvasDesktopUpdateBudget(desktop, &panel);
  }
  if (policies) UpdateCityPolicyWindows(city, desktop, city->unlocks.ordinance_flags);
  if (map_data) UpdateCityMapDataWindows(city);
  if (land_use) Sc2kCanvasDesktopUpdateLandUse(desktop, &city->statistics);
  if (updates & CITY_UPDATE_STATUS) {
    UpdateCityStatus(city);
    RefreshCityStatusPanel(city, &game->ui->status, city->settings.speed, 0);
    // Date, population and treasury are drawn directly, without legend strings.
    InvalidateCityStatus(&game->ui->status);
  }
}

bool UpdateCityWindowTimers(Game *game, double now) {
  SyncCityFireSoundPause(game);
  // Retained dialog callers pump their own input. Window presentation timers
  // must keep running there even while the outer game loop is suspended.
  bool dirty = PollCityWindowHighlights(&game->ui->desktop, (u32)(u64)(now * 1000));
  dirty |= PollCityButtonFeedback(game, now);
  dirty |= UpdateCityTooltip(game, now);
  dirty |= UpdateCityTextCarets(game, now);
  dirty |= UpdateCityJukeboxTitle(game->ui, (u32)(u64)(now * 1000));
  CityCanvasDesktop *desktop = &game->ui->desktop;
  bool closed = false;
  WindowIds windows = SnapshotCityWindowIds(desktop);
  for (int index = windows.count - 1; index >= 0; --index) {
    Window *window = FindCityCanvasWindow(desktop, windows.ids[index]);
    if (!window) continue;
    if (window->kind == CITY_CANVAS_COMMAND_ERROR) {
      // OpenTTD's default error duration is five real-time seconds.
      if (!game->ui->command_error_close_at) game->ui->command_error_close_at = now + 5;
      if (now >= game->ui->command_error_close_at) {
        game->ui->command_error_close_at = 0;
        Sc2kCanvasDesktopDismissWindow(desktop, window->window_id);
        closed = true;
      }
      continue;
    }
    if (window->newspaper && window->newspaper->close_at && now >= window->newspaper->close_at) {
      Sc2kCanvasDesktopDismissWindow(desktop, window->window_id);
      closed = true;
    }
  }
  dirty |= UpdateCityNewspaperOpening(game, now);
  if (closed) ApplicationUpdateDialogPause(game->city);
  return dirty || closed;
}
