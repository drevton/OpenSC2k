#include "city/engine/command_types.h"
#include "game/application.h"
#include "ui/tools/types.h"
#include "drawing/drawing_internal.h"
#include "game/application_internal.h"
#include "ui/frontend.h"

#include "audio/audio.h"
#include "city/state.h"
#include "city/saveload/path.h"
#include "drawing/drawing.h"
#include "ui/commands.h"
#include "ui/state.h"
#include "ui/tools/tools.h"

// Application Exit

// Native lifecycle UI. The original Quit/Save continuation lives in comparison.
static void OpenCityLeaveDialog(Game *game, WindowKind kind) {
  CityCanvasDesktop *desktop = &game->ui->desktop;
  CloseCityMenu(desktop);
  if (FocusCityCanvasWindowKind(desktop, kind) >= 0) return;
  for (int index = 0; index < desktop->count; ++index) {
    Window *window = &desktop->windows[index];
    if (window->kind != CITY_CANVAS_EXIT && window->kind != CITY_CANVAS_ABANDON) continue;
    Sc2kCanvasDesktopDismissWindow(desktop, window->window_id);
    game->exit_requested = false;
    break;
  }
  int index = OpenCityMessageBox(desktop, (Window){.kind = kind,
      .title = kind == CITY_CANVAS_ABANDON ? "Abandon Game" : "Exit",
      .message = kind == CITY_CANVAS_ABANDON ? "Are you sure you want to abandon\nthis game?" :
          "Are you sure you want to exit\nOpenSC2K?"}, CITY_MESSAGE_ERROR | CITY_MESSAGE_YES_NO);
  game->exit_requested = kind == CITY_CANVAS_EXIT && index >= 0;
}

void HandleCityExitCommand(City *city) {
  OpenCityLeaveDialog(city->game, CITY_CANVAS_EXIT);
}

void OpenCityAbandonDialog(Game *game) {
  if (game->city_active) OpenCityLeaveDialog(game, CITY_CANVAS_ABANDON);
}

void CompleteCityAbandon(Game *game) {
  CityInterfaceState *ui = game->ui;
  if (!ui->abandon_requested || game->command_depth) return;
  CloseCityManualBudget(game->city);
  ui->abandon_requested = false;
  game->city_active = false;
  FreeCityPathStorage(&game->city_path);
  game->path_open_flag = CITY_PATH_OPEN_NONE;
  ui->restore_frame_tools = false;
  ui->save_prompt_active = false;
  ui->placement_hover_valid = false;
  ui->pointer_stroke_count = 0;
  ui->desktop.selected_tool = CITY_CANVAS_TOOL_NONE;
  SetCityToolsVisible(game, false);
  SetCityMapControlsVisible(ui, false);
  StopAllCitySound(game);
  game->idle_state = CITY_IDLE_STARTUP_MENU;
  game->idle_entry = 1;
  game->idle_input = 0;
}

void AnswerCityLeaveDialog(Game *game, bool accept) {
  CityCanvasDesktop *desktop = &game->ui->desktop;
  if (!desktop->count) return;
  Window *window = &desktop->windows[desktop->count - 1];
  WindowKind kind = window->kind;
  if (kind != CITY_CANVAS_EXIT && kind != CITY_CANVAS_ABANDON) return;
  Sc2kCanvasDesktopDismissWindow(desktop, window->window_id);
  game->exit_requested = false;
  if (!accept) return;
  if (kind == CITY_CANVAS_EXIT) {
    RequestCityApplicationExit(game);
  } else {
    // Closing retained questions lets their existing cancellation paths unwind.
    // Switch to startup only after any enclosing command has returned.
    while (desktop->count) Sc2kCanvasDesktopDismissTop(desktop);
    game->ui->abandon_requested = true;
    CompleteCityAbandon(game);
  }
}

// Handles this window's painting and input; paint coordinates are local to the window.
bool LeaveWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    Canvas *canvas = event->canvas;
    DrawCityMessageBox(canvas, window);
    return true;
  }
  if (event->kind != WINDOW_INPUT) return false;
  const CanvasSDLEvent *input = event->input;
  City *city = game->city;
  if (input->kind == CANVAS_SDL_KEY_DOWN) {
    if (input->key == CITY_KEY_ESCAPE || input->key == 'N') AnswerCityLeaveDialog(game, false);
    else if (input->key == CITY_KEY_RETURN || input->key == 'Y') AnswerCityLeaveDialog(game, true);
    else if (input->key == (CITY_HOTKEY_CTRL | 'Q')) HandleCityExitCommand(city);
    ApplicationUpdateDialogPause(city);
    return true;
  }
  if (input->kind == CANVAS_SDL_POINTER_DOWN && input->button == CANVAS_SDL_BUTTON_LEFT) {
    int answer = CityMessageAnswerAt(window, event->x, event->y);
    if (answer != CITY_DIALOG_NONE) {
      AnswerCityLeaveDialog(game, answer == CITY_DIALOG_YES);
      ApplicationUpdateDialogPause(city);
      return true;
    }
  }
  return false;
}
