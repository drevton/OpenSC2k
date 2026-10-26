#include "ui/dialogs/alerts.h"

#include <stdlib.h>

#include "assets/resource_types.h"
#include "city/engine/simulation.h"
#include "city/presentation.h"
#include "city/state.h"
#include "game/commands.h"
#include "game/errors.h"
#include "game/observation.h"
#include "game/state.h"
#include "ui/charts/charts.h"
#include "ui/dialogs/budget.h"
#include "ui/dialogs/dialogs.h"
#include "ui/dialogs/messages.h"
#include "ui/news/newspaper.h"
#include "ui/state.h"
#include "ui/windows/windows.h"

// Simulation keeps each suspended caller and its token. UI owns the matching window, reply and temporary pause
// until its completion runs; resuming uses the live City rather than replaying the work that opened the notice.

static int CreateCityMissionOutcomeWindow(City *city, int outcome);

typedef struct {
  u32 window;
  int token;
  bool budget_opener, mission_notice, paused;
  CityConfirmationState reply;
} CityTickWindowCompletion;

static void CompleteCityTickWindow(City *city, void *context) {
  CityTickWindowCompletion *pending = context;
  CityInterfaceState *ui = city->game->ui;
  ApplicationUpdateDialogPause(city);
  if (pending->budget_opener) {
    if (FindCityBudgetWindowState(city, pending->window)) ApplicationBudgetEnd(city, pending->window);
    if (ui->annual_budget_window == pending->window) ui->annual_budget_window = 0;
    RefreshCityChartPanels(city);
  }
  ApplicationUpdateDialogPause(city);
  if (pending->paused) EndCityDialogPause(city, pending->token);
  if (pending->mission_notice) ui->outcome_dialog_active = 0;
  bool resume = !city->game->close_requested && !city->game->execution_failed;
  int answer = pending->reply.answer;
  const CityTickContinuation *work = FindCityTickContinuation(city, pending->token);
  i64 result = work && work->dialog == CITY_TICK_DIALOG_MILITARY_OFFER && resume ?
      ExecuteRecordedCityCommand(city->game, CITY_COMMAND_ANSWER_MILITARY_OFFER, pending->token, answer, 0) :
      ResumeCityTick(city, pending->token, resume, resume ? answer : 0);
  int pending_token = pending->token;
  free(pending);
  ApplicationUpdateDialogPause(city);
  DeliverCitySimulationEvents(city->game, pending_token, result, !resume);
}

void OpenCityTickContinuation(City *city, int token) {
  CityInterfaceState *ui = city->game->ui;
  const CityTickContinuation *work = FindCityTickContinuation(city, token);
  if (!work) return;
  CityTickDialog dialog = work->dialog;
  if (dialog == CITY_TICK_ANIMATION) {
    city->game->animation_token = token;
    return;
  }
  int edition = work->edition;
  int notice = work->notice;
  if (dialog == CITY_TICK_DIALOG_MILITARY_RESULT) notice = work->military.notice;
  int outcome = work->outcome;
  CityTickWindowCompletion *pending = calloc(1, sizeof(*pending));
  int slot = -1;
  bool opening = false;
  if (!pending) goto failed;
  pending->token = token;
  pending->mission_notice = dialog == CITY_TICK_DIALOG_MISSION;
  if (pending->mission_notice) ui->outcome_dialog_active = 1;
  pending->paused = BeginCityDialogPause(city, token) > 0;
  if (!pending->paused) goto failed;
  if (dialog == CITY_TICK_DIALOG_BUDGET) {
    slot = FocusCityCanvasWindowKind(&ui->desktop, CITY_CANVAS_BUDGET);
    opening = slot < 0;
    if (opening) slot = OpenCityBudgetWindow(city);
  } else if (dialog == CITY_TICK_DIALOG_MISSION) {
    opening = true;
    slot = CreateCityMissionOutcomeWindow(city, outcome);
    if (slot >= 0) slot = FocusCityCanvasWindow(&ui->desktop, ui->desktop.windows[slot].window_id);
  } else if (dialog == CITY_TICK_DIALOG_ECONOMY_NOTICE || dialog == CITY_TICK_DIALOG_MILITARY_RESULT) {
    if (!OpenCityNoticeWithCallback(city, "City notice", CityNoticeText(notice),
        CITY_MESSAGE_INFORMATION | CITY_MESSAGE_OK, CompleteCityTickWindow, pending)) goto failed;
    return;
  } else if (dialog == CITY_TICK_DIALOG_MILITARY_OFFER) {
    opening = true;
    slot = Sc2kCanvasDesktopOpenConfirm(&ui->desktop, (CityRect){125, 100, 475, 240},
        "Military base", CityNoticeText(CITY_NOTICE_MILITARY_BASE_OFFER),
        CITY_MESSAGE_ERROR | CITY_MESSAGE_YES_NO);
    if (slot >= 0) {
      pending->reply.answer = CITY_DIALOG_NO;
      pending->reply.window_id = ui->desktop.windows[slot].window_id;
      ui->desktop.windows[slot].confirmation = &pending->reply;
    }
  } else {
    bool hidden = dialog == CITY_TICK_DIALOG_MONTHLY_NEWSPAPER ? ui->newspaper_delivery_disabled :
        ui->extra_newspaper_disabled;
    if (!city->game->observation && hidden) {
      ApplyCityNewspaperPageEffects(city, edition);
      CompleteCityTickWindow(city, pending);
      return;
    }
    if (!OpenCityNewspaperWithCallback(city, edition, true, CompleteCityTickWindow, pending)) goto failed;
    return;
  }
  if (slot < 0 || !AttachCityWindowCallback(&ui->desktop.windows[slot], CompleteCityTickWindow, pending))
    goto failed;
  pending->window = ui->desktop.windows[slot].window_id;
  pending->budget_opener = dialog == CITY_TICK_DIALOG_BUDGET && opening;
  if (pending->budget_opener) {
    ui->annual_budget_window = pending->window;
    ApplicationBudgetBegin(city, pending->window, false);
  }
  ApplicationUpdateDialogPause(city);
  RefreshCityChartPanels(city);
  return;
failed:
  if (opening && slot >= 0) Sc2kCanvasDesktopDismissWindow(&ui->desktop, ui->desktop.windows[slot].window_id);
  if (pending && pending->paused)
    EndCityDialogPause(city, token);
  if (pending && pending->mission_notice) ui->outcome_dialog_active = 0;
  free(pending);
  ResumeCityTick(city, token, false, 0);
  SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
}

static int PresentResourceAlertPaused(City *city, void *context) {
  const int *resource = context;
  u32 window_id = OpenCityResourceAlert(city, resource[0], resource[1]);
  WaitForCityResourceAlert(city, window_id, resource[0], resource[1]);
  return CITY_DIALOG_OK;
}

void PresentCityResourceAlert(City *city, i32 bitmap_id, i32 text_id) {
  if (!city->game->observation) {
    OpenCityResourceAlert(city, bitmap_id, text_id);
    return;
  }
  // Preserve the City caller's ordering while the canvas remains responsive.
  int resource[2] = {bitmap_id, text_id};
  RunPausedCityAction(city, PresentResourceAlertPaused, resource);
}

static int CreateCityMissionOutcomeWindow(City *city, int outcome) {
  static const i16 kBitmapIds[3] = {CITY_BITMAP_NOTICE_403, CITY_BITMAP_NOTICE_128, CITY_BITMAP_NOTICE_406};
  static const i16 kTextIds[3] = {CITY_NOTICE_MISSION_FAILED, CITY_NOTICE_MISSION_WON, CITY_NOTICE_BANKRUPT};
  return CreateCityResourceAlert(city, kBitmapIds[outcome], kTextIds[outcome]);
}

static int PresentMissionNoticePaused(City *city, void *context) {
  int outcome = *(const int *)context;
  int slot = CreateCityMissionOutcomeWindow(city, outcome);
  u32 window_id = slot >= 0 ? city->game->ui->desktop.windows[slot].window_id : 0;
  ApplicationUpdateDialogPause(city);
  WaitForCityMissionNotice(city, window_id, outcome);
  return CITY_DIALOG_OK;
}

void PresentCityMissionOutcome(City *city, i32 outcome) {
  ObserveCityPresentation(city->game, CITY_PRESENTATION_MISSION_NOTICE, 0, outcome, 0, 0, NULL);
  RunPausedCityAction(city, PresentMissionNoticePaused, &outcome);
}

void PresentCityDisasterUnlockOffer(City *city) {
  const GameObservation *observer = city->game->observation;
  if (observer && observer->present_unlock_offer) observer->present_unlock_offer(city);
}
