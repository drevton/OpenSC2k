#include <stdio.h>

#include "assets/resource_types.h"
#include "game/audio.h"
#include "city/events/news.h"
#include "city/state.h"
#include "drawing/fonts/fonts.h"
#include "game/errors.h"
#include "game/observation.h"
#include "game/state.h"
#include "ui/dialogs/dialogs.h"
#include "ui/map.h"
#include "ui/news/newspaper.h"
#include "ui/news/newspaper_capture.h"
#include "ui/news/newspaper_state.h"
#include "ui/state.h"
#include "ui/windows/windows.h"

// Drop the borrowed picture, update the active wait timer/damage and reconcile the UI dialog pause.
void DestroyCityNewspaperWindow(City *city, CityNewspaperDialog *dialog) {
  dialog->picture = NULL;
  CityInterfaceState *ui = city->game->ui;
  if (ui->waiting_newspaper != NULL && ui->newspaper_modal_owner == ui->waiting_newspaper)
    ui->waiting_newspaper_timer_active = false;
  if (dialog == ui->waiting_newspaper)
    for (int index = 0; index < ui->desktop.count; ++index)
      if (ui->desktop.windows[index].window_id == ui->waiting_newspaper_window)
        Sc2kDirtyMarkRect(&ui->desktop.dirty, ui->desktop.windows[index].bounds);
  ApplicationUpdateDialogPause(city);
}

// Prepare picture state and the ranking prefix before the first paint. The caller retains any opening notice.
CityNewspaperInitialization BeginCityNewspaperDialog(City *city, CityNewspaperDialog *dialog) {
  if (!AreCityNewsResourcesAvailable(city)) {
    CityInterfaceState *ui = city->game->ui;
    if (dialog == ui->initializing_newspaper)
      ui->newspaper_closed_during_initialization = true;
    if (dialog == ui->waiting_newspaper)
      Sc2kCanvasDesktopDismissWindow(&ui->desktop, ui->waiting_newspaper_window);
  }
  dialog->picture = NULL;
  dialog->picture_id = 0;
  dialog->picture_attempted = 0;
  if (city->game->ui->main_window_iconic)
    return CITY_NEWSPAPER_INITIALIZATION_SKIPPED;
  StopCityMusicSequence(city->game);
  return BeginCityNewspaperProblemRankings(city) ? CITY_NEWSPAPER_INITIALIZATION_NOTICE :
      CITY_NEWSPAPER_INITIALIZATION_READY;
}

// Run the opening prefix and its notice before completing City initialization.
void InitializeCityNewspaperDialog(City *city, CityNewspaperDialog *dialog) {
  CityNewspaperInitialization state = BeginCityNewspaperDialog(city, dialog);
  if (state == CITY_NEWSPAPER_INITIALIZATION_NOTICE)
    ShowCityResourceAlertDialog(city, CITY_BITMAP_NOTICE_407, CITY_NOTICE_MAYOR_PARADE);
  if (state != CITY_NEWSPAPER_INITIALIZATION_SKIPPED)
    FinishCityNewspaperInitialization(city, dialog->edition_index);
}

// Each paint receives its destination page explicitly.

void PresentCityNewspaperText(Game *game, NativeNewspaperDialog *dialog, const char *text, const CityRect *bounds,
    int size, int alignment) {
  if (dialog == NULL) {
    return;
  }
  i32 index = dialog->page.text_run_count;
  if (index == CITY_NEWSPAPER_TEXT_RUN_CAPACITY) {
    SetGameError(game, -3);
    return;
  }
  snprintf(dialog->text_run_buffers[index], sizeof(dialog->text_run_buffers[index]), "%s", text);
  dialog->text_runs[index] = (CityCanvasNewspaperTextRun){*bounds, size, alignment, dialog->text_run_buffers[index]};
  ++dialog->page.text_run_count;
}

void DrawCityNewspaperRectangle(Game *game, NativeNewspaperDialog *dialog, const CityRect *bounds) {
  if (dialog == NULL) {
    return;
  }
  CityCanvasNewspaperPage *page = &dialog->page;
  if (page->filler_count == CITY_NEWSPAPER_FILLER_CAPACITY) {
    SetGameError(game, -3);
    return;
  }
  dialog->filler[page->filler_count++] = *bounds;
}

i32 MeasureCityNewspaperText(void *context, const char *text, i32 length) {
  (void)length;
  int scale = context != NULL ? *(const int *)context : 1;
  return CanvasTextWidth(text) * scale;
}

void ShowRankedCityEventModal(City *city, u32 event_code, u32 event_data) {
  PresentRankedCityEvent(city, event_code, event_data);
}

static i32 RunNewspaperHost(City *city, void *dialog) {
  const GameObservation *observer = city->game->observation;
  if (observer && observer->choose_newspaper) return observer->choose_newspaper(city, dialog);
  return CITY_DIALOG_UNANSWERED;
}

i32 RunPausedCityNewspaperDialog(City *city, CityNewspaperDialog *dialog) {
  return RunPausedCityAction(city, RunNewspaperHost, dialog);
}
