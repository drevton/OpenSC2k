#include "ui/news/newspaper.h"

#include "city/engine/simulation.h"
#include "ui/news/newspaper_capture.h"
#include "ui/news/newspaper_state.h"

#include <stdlib.h>

#include "assets/resource_types.h"
#include "assets/state.h"
#include "base/time.h"
#include "city/disasters/disasters.h"
#include "city/events/news.h"
#include "city/state.h"
#include "drawing/newspaper.h"
#include "drawing/state.h"
#include "game/errors.h"
#include "game/observation.h"
#include "game/state.h"
#include "ui/charts/charts.h"
#include "ui/dialogs/alerts.h"
#include "ui/dialogs/dialogs.h"
#include "ui/state.h"
#include "ui/windows/windows.h"

// Prepare the page and its completion before pause reconciliation can deliver nested UI work.
// Existing newspaper windows keep their earlier completions when their page is replaced.
static int CreateCityNewspaperPage(City *city, CityNewspaperDialog *paint, int edition, double close_at,
    CityWindowCallback *callback, void *context) {
  CityInterfaceState *ui = city->game->ui;
  ui->startup_newspaper_open_at = 0;
  NativeNewspaperDialog *page = calloc(1, sizeof(*page));
  if (!page) {
    SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    return -1;
  }
  bool captured = paint ? NativeCaptureNewspaper(city, paint, page) :
      ComposeCityNewspaperPreview(city, edition, page);
  int width = IntMin(530, ui->desktop.canvas_width - 24);
  int height = IntMin(425, ui->desktop.canvas_height - 32);
  int left = (ui->desktop.canvas_width - width) / 2;
  int top = IntMax(22, (ui->desktop.canvas_height - height) / 2);
  int count = ui->desktop.count;
  int slot = captured ? Sc2kCanvasDesktopOpen(&ui->desktop,
      (Window){.kind = CITY_CANVAS_NEWSPAPER, .bounds = {left, top, left + width, top + height},
          .title = "City Newspaper", .newspaper_page = page->page}) : -1;
  if (slot >= 0 && callback && !AttachCityWindowCallback(&ui->desktop.windows[slot], callback, context)) {
    // Discard only a newly created window. A reused window still owns its previous page and callers.
    if (ui->desktop.count > count)
      Sc2kCanvasDesktopDismissWindow(&ui->desktop, ui->desktop.windows[slot].window_id);
    slot = -1;
  }
  if (slot < 0) {
    NativeReleaseNewspaperPage(page);
    free(page);
    SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    return -1;
  }
  Window *window = &ui->desktop.windows[slot];
  Drawing *drawing = city->drawing;
  if (drawing->newspaper_opening_window) {
    MarkScreenDirty(&drawing->screen_damage, drawing->newspaper_opening_bounds);
    Sc2kDirtyMarkRect(&ui->desktop.window_damage, drawing->newspaper_opening_bounds);
    ReleaseCityNewspaperOpening(drawing);
  }
  if (window->newspaper) {
    NativeReleaseNewspaperPage(window->newspaper);
    free(window->newspaper);
  }
  window->newspaper = page;
  window->newspaper_page = page->page;
  page->opening_started = MonotonicSeconds();
  page->opening_frame = 0;
  Sc2kDirtyMarkRect(&ui->desktop.dirty, window->bounds);
  page->close_at = close_at;
  return slot;
}

// The window owns the captured page; opening it never retains the caller.
u32 OpenCityNewspaperPage(City *city, CityNewspaperDialog *paint, int edition, double close_at) {
  int slot = CreateCityNewspaperPage(city, paint, edition, close_at, NULL, NULL);
  if (slot < 0) return 0;
  u32 id = city->game->ui->desktop.windows[slot].window_id;
  ApplicationUpdateDialogPause(city);
  return id;
}

// Ranking and edition selection have already run in City. Retained presenters consume the event identity;
// the native page uses the captured edition and keeps the same paused caller through dismissal.
void PresentRankedCityEvent(City *city, u32 event_code, u32 event_data) {
  (void)event_code;
  (void)event_data;
  CityNewspaperDialog dialog = {0};
  dialog.edition_index = city->newspaper.edition_index;
  RunPausedCityNewspaperDialog(city, &dialog);
}

void PresentCityNewspaperEdition(City *city, i32 edition) {
  if (city->game->debug_city) return;
  CityInterfaceState *ui = city->game->ui;
  CityNewspaperDialog *previous_owner = ui->newspaper_modal_owner;
  ui->newspaper_modal_owner = NULL;
  OpenCityNewspaperPage(city, NULL, edition, 0);
  ui->newspaper_modal_owner = previous_owner;
}

CityNewspaperInitialization BeginCityNewspaperOpeningPresentation(City *city, int edition) {
  CityNewspaperDialog paint = {.edition_index = edition};
  return BeginCityNewspaperDialog(city, &paint);
}

bool InitializeCityNewspaperWindow(City *city, CityNewspaperDialog *paint) {
  CityInterfaceState *ui = city->game->ui;
  CityNewspaperDialog *previous_owner = ui->newspaper_modal_owner;
  CityNewspaperDialog *previous_initializing = ui->initializing_newspaper;
  bool previous_closed = ui->newspaper_closed_during_initialization;
  ui->newspaper_modal_owner = ui->initializing_newspaper = paint;
  ui->newspaper_closed_during_initialization = false;
  InitializeCityNewspaperDialog(city, paint);
  bool ended = ui->newspaper_closed_during_initialization;
  ui->newspaper_modal_owner = previous_owner;
  ui->initializing_newspaper = previous_initializing;
  ui->newspaper_closed_during_initialization = previous_closed;
  return !ended;
}

bool ResumeCityNewspaperOpening(City *city, int token, bool resume, CityWindowCallback *callback, void *context) {
  const CityTickContinuation *work = FindCityTickContinuation(city, token);
  if (!work || work->dialog != CITY_TICK_DIALOG_NEWSPAPER_INITIALIZATION) return false;
  int edition = work->edition;
  bool ended = (work->outcome & CITY_NEWSPAPER_INITIALIZATION_ENDED) != 0;
  if (!ResumeCityTick(city, token, resume, 0)) return false;
  if (!resume || ended) {
    if (callback) callback(city, context);
    return true;
  }
  CityNewspaperDialog paint = {.edition_index = edition};
  if (CreateCityNewspaperPage(city, &paint, edition,
      MonotonicSeconds() + city->game->ui->newspaper_timer_seconds, callback, context) < 0) return false;
  ApplicationUpdateDialogPause(city);
  RefreshCityChartPanels(city);
  return true;
}

typedef struct {
  int token;
  CityWindowCallback *callback;
  void *context;
} CityNewspaperWindowCompletion;

static void CompleteCityNewspaperOpening(City *city, void *context) {
  CityNewspaperWindowCompletion opening = *(CityNewspaperWindowCompletion *)context;
  free(context);
  bool resume = !city->game->close_requested && !city->game->execution_failed;
  if (!ResumeCityNewspaperOpening(city, opening.token, resume, opening.callback, opening.context)) {
    SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    if (opening.callback) opening.callback(city, opening.context);
  }
  ApplicationUpdateDialogPause(city);
}

bool OpenCityNewspaperWithCallback(City *city, int edition, bool automatic,
    CityWindowCallback *callback, void *context) {
  if (automatic && city->game->debug_city) {
    // The UI dispatcher opens this caller. Execute the page command directly to avoid reentering it.
    SelectCityNewspaperEdition(city, edition);
    if (GetGameError(city->game)) return false;
    if (callback) callback(city, context);
    return true;
  }
  i64 token = BeginCityNewspaperOpening(city, edition);
  if (!token) return false;
  const CityTickContinuation *work = FindCityTickContinuation(city, (int)token);
  CityNewspaperWindowCompletion opening = {.token = (int)token, .callback = callback, .context = context};
  if (!work->notice)
    return ResumeCityNewspaperOpening(city, (int)token,
        !city->game->close_requested && !city->game->execution_failed, callback, context);
  CityNewspaperWindowCompletion *pending = malloc(sizeof(*pending));
  u32 window = 0;
  if (pending) {
    *pending = opening;
    int slot = CreateCityResourceAlert(city, CITY_BITMAP_NOTICE_407, CITY_NOTICE_MAYOR_PARADE);
    window = slot >= 0 ? city->game->ui->desktop.windows[slot].window_id : 0;
    if (window) slot = FocusCityCanvasWindow(&city->game->ui->desktop, window);
    if (slot >= 0 && AttachCityWindowCallback(&city->game->ui->desktop.windows[slot],
        CompleteCityNewspaperOpening, pending)) {
      ApplicationUpdateDialogPause(city);
      return true;
    }
  }
  if (window) Sc2kCanvasDesktopDismissWindow(&city->game->ui->desktop, window);
  free(pending);
  ResumeCityTick(city, (int)token, false, 0);
  SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
  return false;
}

void ScheduleCityStartupNewspaper(City *city) {
  city->game->ui->startup_newspaper_edition = city->newspaper.edition_index;
  city->game->ui->startup_newspaper_open_at = -1;
}

// Only an armed deadline limits the host wait; queued work waits for a successfully presented map frame.
int LimitCityStartupNewspaperWait(const CityInterfaceState *ui, int wait_ms) {
  if (ui->startup_newspaper_open_at > 0)
    wait_ms = IntMin(wait_ms, IntMax(0, (int)((ui->startup_newspaper_open_at - MonotonicSeconds()) * 1000) + 1));
  return wait_ms;
}

void ArmCityStartupNewspaper(CityInterfaceState *ui) {
  if (ui->startup_newspaper_open_at < 0) ui->startup_newspaper_open_at = MonotonicSeconds() + 0.5;
}

bool PollCityStartupNewspaper(City *city, double now) {
  Game *game = city->game;
  CityInterfaceState *ui = game->ui;
  if (ui->startup_newspaper_open_at > 0 && now >= ui->startup_newspaper_open_at &&
      !game->delay_blocked && !ui->dialog_pause_requested && !game->close_requested && !game->execution_failed) {
    int edition = ui->startup_newspaper_edition;
    ui->startup_newspaper_open_at = 0;
    OpenCityManualNewspaper(city, edition);
    return true;
  }
  return false;
}

void UpdateCityPeriodicNewspaper(Game *game) {
  City *city = game->city;
  CityInterfaceState *ui = game->ui;
  if (ui->newspaper_month != city->clock.month) {
    ui->newspaper_month = city->clock.month;
    if (!city->newspaper.enabled && !ui->newspaper_delivery_disabled &&
        (ui->newspaper_month == CITY_MONTH_APRIL || ui->newspaper_month == CITY_MONTH_AUGUST))
      OpenCityManualNewspaper(city, city->newspaper.edition_index);
  }
}

void OpenCityManualNewspaper(City *city, int edition) {
  city->game->ui->startup_newspaper_open_at = 0;
  const GameObservation *observer = city->game->observation;
  if (observer && observer->open_manual_newspaper) {
    observer->open_manual_newspaper(city, edition);
    return;
  }
  OpenCityNewspaperPage(city, NULL, edition, MonotonicSeconds() + city->game->ui->newspaper_timer_seconds);
}

// Direct/headless commands run the same page effects without opening a canvas window.
void ApplyCityNewspaperPageEffects(City *city, int edition) {
  CityNewspaperDialog paint = {.edition_index = edition};
  if (BeginCityNewspaperDialog(city, &paint) != CITY_NEWSPAPER_INITIALIZATION_SKIPPED)
    FinishCityNewspaperInitialization(city, edition);
  if (AreCityNewsResourcesAvailable(city)) {
    NativeNewspaperDialog *page = calloc(1, sizeof(*page));
    if (page) {
      NativeCaptureNewspaper(city, &paint, page);
      NativeReleaseNewspaperPage(page);
      free(page);
    } else SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
  }
  DestroyCityNewspaperWindow(city, &paint);
}

i64 ProcessPendingCityDisaster(City *city, bool terrain_water_allowed) {
  i64 result = AdvanceCityDisasterEffects(city, terrain_water_allowed);
  if (result == CITY_OPERATION_PENDING) return result;
  if (!result) return GetGameError(city->game) == 0;
  CityNewspaperDialog newspaper = {0};
  RunPausedCityNewspaperDialog(city, &newspaper);
  return GetGameError(city->game) == 0;
}
