#include "ui/dialogs/dialogs.h"

#include <stdio.h>

#include "game/audio.h"
#include "audio/types.h"
#include "city/disasters/disasters.h"
#include "city/presentation.h"
#include "city/state.h"
#include "game/observation.h"
#include "game/state.h"
#include "ui/windows/window_internal.h"
#include "ui/charts/charts.h"
#include "ui/commands.h"
#include "ui/dialogs/alerts.h"
#include "ui/dialogs/budget.h"
#include "ui/dialogs/file_dialog.h"
#include "ui/dialogs/information.h"
#include "ui/dialogs/messages.h"
#include "ui/dialogs/name_editor.h"
#include "ui/dialogs/scenario.h"
#include "ui/dialogs/spans.h"
#include "ui/map.h"
#include "ui/sdl.h"
#include "ui/state.h"
#include "ui/tools/tools.h"
#include "ui/windows/windows.h"

// Each retained notice keeps its own scope and token; temporary pauses end only for their matching token.
// Connection questions retain their caller until the matching answer.

bool CityConnectionQuestionWaiting(const CityInterfaceState *ui) {
  return ui->connection.active != NULL && !ui->connection.active->answered;
}

bool AnswerCityConnectionQuestion(City *city, int token, bool accept) {
  CityInterfaceState *ui = city->game->ui;
  CityConnectionDialog *dialog = ui->connection.active;
  if (dialog == NULL || dialog->token != token || dialog->answered) return false;
  dialog->answered = true;
  dialog->answer = accept ? CITY_DIALOG_YES : CITY_DIALOG_NO;
  Sc2kCanvasDesktopDismissWindow(&ui->desktop, dialog->window_id);
  dialog->window_id = 0;
  ApplicationUpdateDialogPause(city);
  return true;
}

int ChooseCityConnectionQuestion(City *city, int kind, int cost) {
  const GameObservation *observer = city->game->observation;
  if (observer && observer->choose_connection) return observer->choose_connection(city, kind, cost);
  bool bond = kind == CITY_CONNECTION_ISSUE_BOND || kind == CITY_CONNECTION_REDEEM_BOND;
  return !bond && city->command_choices.supplied && city->command_choices.border ? CITY_DIALOG_YES : CITY_DIALOG_NO;
}

// Mission and resource notices share the same scope and answer contract.

bool CityMissionNoticeWaiting(const CityInterfaceState *ui) {
  return ui->mission_notice.active != NULL && !ui->mission_notice.active->answered;
}

bool AnswerCityNotice(City *city, int token, int kind) {
  CityInterfaceState *ui = city->game->ui;
  CityMissionNotice *notice = ui->mission_notice.active;
  if (notice == NULL || notice->token != token || notice->answered ||
      notice->kind != kind) return false;
  notice->answered = true;
  Sc2kCanvasDesktopDismissWindow(&ui->desktop, notice->window_id);
  notice->window_id = 0;
  ApplicationUpdateDialogPause(city);
  return true;
}

bool AnswerCityMissionNotice(City *city, int token) { return AnswerCityNotice(city, token, NOTICE_MISSION); }
bool AnswerCityResourceAlert(City *city, int token) { return AnswerCityNotice(city, token, NOTICE_RESOURCE); }
bool AnswerCityTextNotice(City *city, int token) { return AnswerCityNotice(city, token, NOTICE_TEXT); }

static void WaitForNotice(City *city, CityNoticeRequest request) {
  const GameObservation *observer = city->game->observation;
  if (observer && observer->wait_notice) {
    observer->wait_notice(city, &request);
    return;
  }
  fprintf(stderr, "Cannot resume a retained notice without a diagnostic wait handler.\n");
  city->game->execution_failed = true;
}

void WaitForCityMissionNotice(City *city, u32 window_id, int outcome) {
  WaitForNotice(city, (CityNoticeRequest){window_id, outcome, 0, NOTICE_MISSION});
}

void WaitForCityResourceAlert(City *city, u32 window_id, int bitmap_id, int text_id) {
  WaitForNotice(city, (CityNoticeRequest){window_id, bitmap_id, text_id, NOTICE_RESOURCE});
}

void WaitForCityTextNotice(City *city, u32 window_id, int resource) {
  WaitForNotice(city, (CityNoticeRequest){window_id, resource, 0, NOTICE_TEXT});
}

// City-facing notice entry points preserve the original retained caller.

void ShowCityMissionOutcome(City *city, i32 outcome) {
  PresentCityMissionOutcome(city, outcome);
}

// Present the requested bitmap and string together.
void ShowCityResourceAlertDialog(City *city, i32 bitmap_id, i32 text_id) {
  if (!city->game->observation) {
    ReportCityResourceNotice(city, bitmap_id, text_id);
    return;
  }
  PresentCityResourceAlert(city, bitmap_id, text_id);
}

void ShowCityMessage(City *city, u32 resource_id) {
  const GameObservation *observer = city->game->observation;
  if (!observer) {
    if (IsCityCommandFailureResource(resource_id)) ReportCityCommandResult(city, -(i64)resource_id);
    else ReportCityResourceNotice(city, 0, resource_id);
    return;
  }
  if (observer->show_message) observer->show_message(city, resource_id);
}

void SetCityPaletteDialogState(City *city, bool paused) {
  if (paused) SuspendCityToolbarForDialog(city->game->ui);
  else RestoreCityToolbarAfterDialog(city->game->ui);
}

void SetCityCanvasPaletteDialogState(City *city, bool paused) {
  SetCityPaletteDialogState(city, paused);
}

// Only the observation adapter answers this retained request; absent input remains unanswered.
static i32 ChooseCityCanvasDialog(City *city, void *dialog) {
  const GameObservation *observer = city->game->observation;
  if (observer && observer->choose_canvas_dialog) return observer->choose_canvas_dialog(city, dialog);
  return CITY_DIALOG_UNANSWERED;
}

bool BeginCityDialogPause(City *city, int token) {
  CityInterfaceState *ui = city->game->ui;
  if (token <= 0) return false;
  int free_slot = -1;
  for (int index = 0; index < CITY_DIALOG_PAUSE_TOKEN_CAPACITY; ++index) {
    if (ui->dialog_pause_tokens[index] == token)
      return false;
    if (ui->dialog_pause_tokens[index] == 0 && free_slot == -1)
      free_slot = index;
  }
  if (free_slot == -1)
    return false;
  // Optional comparison retains the original activation wait; native pauses never pump input.
  const GameObservation *observer = city->game->observation;
  if (observer && observer->before_dialog_pause && !observer->before_dialog_pause(city)) return false;
  // The first temporary pause saves the live speed for final dismissal.
  if (ui->dialog_pause_depth == 0) {
    ui->dialog_input_blocked = 1;
    SetCityCanvasPaletteDialogState(city, 1);
    ui->dialog_pause_saved_speed = city->settings.speed;
    if (observer) city->settings.speed = CITY_SPEED_PAUSED;
    Sc2kRefreshCityStatusPanel(city);
  }
  ++ui->dialog_pause_depth;
  ui->dialog_pause_tokens[free_slot] = token;
  return 1;
}
bool EndCityDialogPause(City *city, int token) {
  CityInterfaceState *ui = city->game->ui;
  if (token <= 0) return false;
  for (int index = 0; index < CITY_DIALOG_PAUSE_TOKEN_CAPACITY; ++index) {
    if (ui->dialog_pause_tokens[index] != token)
      continue;
    ui->dialog_pause_tokens[index] = 0;
    // Restore only after every temporary pause has ended.
    if (--ui->dialog_pause_depth == 0) {
      ui->dialog_input_blocked = 0;
      SetCityCanvasPaletteDialogState(city, 0);
      if (city->game->observation) city->settings.speed = ui->dialog_pause_saved_speed;
      Sc2kRefreshCityStatusPanel(city);
    }
    return 1;
  }
  return false;
}

// Invoke the retained blocking caller through an installed comparison observation adapter. Callback/context are
// borrowed; without that adapter returns CITY_DIALOG_UNANSWERED instead of inventing a native modal reply.
int RunPausedCityAction(City *city, int (*action)(City *city, void *context), void *context) {
  const GameObservation *observer = city->game->observation;
  if (observer && observer->run_paused_action) return observer->run_paused_action(city, action, context);
  // Original blocking callers require an explicit diagnostic adapter.
  return CITY_DIALOG_UNANSWERED;
}

// Native canvas requests share the original gate/pause ordering. Original framework-object dispatch remains in the
// comparison backend. Callback/context remain borrowed; without a retained blocking adapter the unanswered result is
// propagated unchanged.
i32 RunPausedCityModalDialog(City *city, void *dialog) {
  return RunPausedCityAction(city, ChooseCityCanvasDialog, dialog);
}

// Simulation-visible state of the original mission/bankruptcy callback. Outcome must be supported; cleanup follows the
// actual notice return and requires the attached UI owner to retain the caller's pause lifetime.
void FailRadio(City *city, i32 outcome) {
  city->game->ui->outcome_dialog_active = 1;
  int result = outcome & 0xFFFF;
  if (result <= CITY_OUTCOME_BANKRUPT) {
    PlayCitySound(city, INVALID_TILE,
        result == CITY_OUTCOME_MISSION_WON ? CITY_SOUND_CIVIC_PLACEMENT : CITY_SOUND_DEMOLITION);
    ShowCityMissionOutcome(city, result);
    FinishCityMissionOutcome(city, result);
  }
  city->game->ui->outcome_dialog_active = 0;
}

// A retained connection prompt answers only its innermost active caller.
// The prompt procedure is installed when that caller opens its window.
bool ConnectionWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    DrawCityMessageBox(event->canvas, window);
    return true;
  }
  if (event->kind != WINDOW_INPUT) return false;
  CityConnectionDialog *dialog = game->ui->connection.active;
  if (!dialog || dialog->window_id != window->window_id) return true;
  const CanvasSDLEvent *input = event->input;
  if (input->kind == CANVAS_SDL_KEY_DOWN) {
    if (input->key == CITY_KEY_RETURN || input->key == CITY_KEY_ESCAPE)
      AnswerCityConnectionQuestion(game->city, dialog->token, input->key == CITY_KEY_RETURN);
    return true;
  }
  if (input->kind == CANVAS_SDL_POINTER_DOWN && input->button == CANVAS_SDL_BUTTON_LEFT) {
    int answer = CityMessageAnswerAt(window, event->x, event->y);
    if (answer == CITY_DIALOG_YES || answer == CITY_DIALOG_NO) {
      AnswerCityConnectionQuestion(game->city, dialog->token, answer == CITY_DIALOG_YES);
      return true;
    }
  }
  return false;
}

// Closing a retained notice must answer its caller synchronously before it resumes.
bool NoticeWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    DrawCityMessageBox(event->canvas, window);
    return true;
  }
  if (event->kind != WINDOW_INPUT) return false;
  CityMissionNotice *notice = game->ui->mission_notice.active;
  if (!notice || notice->window_id != window->window_id) return true;
  const CanvasSDLEvent *input = event->input;
  if (input->kind == CANVAS_SDL_KEY_DOWN) {
    if (input->key == CITY_KEY_RETURN || input->key == CITY_KEY_ESCAPE)
      AnswerCityNotice(game->city, notice->token, notice->kind);
    return true;
  }
  if (input->kind == CANVAS_SDL_POINTER_DOWN && input->button == CANVAS_SDL_BUTTON_LEFT) {
    Sc2kCanvasDesktopPointerDown(&game->ui->desktop, event->x, event->y);
    if (!FindCityCanvasWindow(&game->ui->desktop, notice->window_id))
      AnswerCityNotice(game->city, notice->token, notice->kind);
    ApplicationUpdateDialogPause(game->city);
    return true;
  }
  return false;
}

void ApplicationSyncDialogPause(City *city) {
  CityInterfaceState *ui = city->game->ui;
  if (!ui->dialog_pause_sync_enabled || ui->dialog_pause_syncing || city->game->command_depth ||
      ui->dialog_pause_active == !!ui->dialog_pause_requested)
    return;
  ui->dialog_pause_syncing = 1;
  if (ui->dialog_pause_requested ? BeginCityDialogPause(city, 1) : EndCityDialogPause(city, 1))
    ui->dialog_pause_active = !!ui->dialog_pause_requested;
  ui->dialog_pause_syncing = 0;
}

void ApplicationUpdateDialogPause(City *city) {
  CityInterfaceState *ui = city->game->ui;
  ApplicationBudgetSync(city);
  ReconcileCityPanelWindows(city);
  ui->dialog_pause_requested = 0;
  CityEditorCancellations cancelled;
  i32 annual_open = 0;
  for (i32 index = 0; index < ui->desktop.count; ++index) {
    annual_open |= ui->desktop.windows[index].window_id == ui->annual_budget_window;
    i32 kind = ui->desktop.windows[index].kind;
    ui->dialog_pause_requested |= kind == CITY_CANVAS_BUDGET || kind == CITY_CANVAS_POLICIES ||
        kind == CITY_CANVAS_NOTICE || kind == CITY_CANVAS_EXIT || kind == CITY_CANVAS_ABANDON ||
        kind == CITY_CANVAS_ADVISOR || kind == CITY_CANVAS_CONFIRM || kind == CITY_CANVAS_NEWSPAPER;
  }
  if (!annual_open)
    ui->annual_budget_window = 0;
  ui->dialog_pause_requested |= annual_open;
  ui->dialog_pause_requested |= CollectClosedCityNameEditors(ui, &cancelled);
  ui->dialog_pause_requested |= ReconcileCityTileInformationWindow(ui);
  ui->dialog_pause_requested |= NativeSpanDialogWaiting(ui) || CityConnectionQuestionWaiting(ui) ||
      CityMissionNoticeWaiting(ui) || ui->startup_waiting || CityFileDialogWaiting(ui) ||
      NativeScenarioDialogWaiting(ui);
  ApplicationSyncDialogPause(city);
  CancelClosedCityNameEditors(city->game, &cancelled);
}
