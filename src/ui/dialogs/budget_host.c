#include "game/timing.h"
#include <string.h>
#include "city/economy/budget.h"
#include <stdlib.h>
#include "ui/text/text.h"

#include "assets/catalog.h"
#include "assets/resource_types.h"
#include "assets/state.h"
#include "audio/state.h"
#include "city/economy/economy.h"
#include "city/economy/types.h"
#include "city/engine/city_types.h"
#include "city/engine/command_types.h"
#include "city/engine/questions.h"
#include "city/engine/simulation.h"
#include "city/presentation.h"
#include "city/state.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/state.h"
#include "game/commands.h"
#include "game/errors.h"
#include "game/observation.h"
#include "game/state.h"
#include "ui/windows/window_internal.h"
#include "ui/charts/charts.h"
#include "ui/commands.h"
#include "ui/dialogs/budget.h"
#include "ui/dialogs/dialogs.h"
#include "ui/dialogs/messages.h"
#include "ui/input.h"
#include "ui/menus/menu.h"
#include "ui/sdl.h"
#include "ui/state.h"
#include "ui/tools/tools.h"
#include "ui/tools/types.h"
#include "ui/windows/controls.h"
#include "ui/windows/state.h"
#include "ui/windows/windows.h"

_Static_assert(CITY_BUDGET_CATEGORY_COUNT == CITY_ADVISOR_PORTRAIT_COUNT,
    "each advisor category has a portrait");

// Budget, bond and ordinance windows retain their own presentation values.
const char *CityAdvisorName(CityBudgetCategory category) {
  static const char *const kNames[CITY_BUDGET_CATEGORY_COUNT] = {
    "Property tax", "Ordinances", "Bonds", "Police", "Fire", "Health", "Education", "Transit"
  };
  return kNames[category];
}

static CityRect CityAdvisorButtonBounds(const Window *window, CityBudgetCategory category) {
  CityRect b = window->bounds;
  int width = b.right - b.left;
  int left = b.left + category * width / CITY_BUDGET_CATEGORY_COUNT;
  int right = b.left + (category + 1) * width / CITY_BUDGET_CATEGORY_COUNT;
  return (CityRect){left, b.top + 14, right, b.top + 14 + CITY_ADVISOR_BUTTON_SIZE};
}

int CityAdvisorButtonAt(const Window *window, int x, int y) {
  for (int category = 0; category < CITY_BUDGET_CATEGORY_COUNT; ++category)
    if (CanvasPointInRect(CityAdvisorButtonBounds(window, category), x, y)) return category;
  return -1;
}

i32 Sc2kCanvasDesktopOpenPolicies(CityCanvasDesktop *desktop, CityRect bounds, u32 policy_flags) {
  int left = (bounds.left + bounds.right - CITY_POLICY_WINDOW_WIDTH) / 2;
  int top = (bounds.top + bounds.bottom - CITY_POLICY_WINDOW_HEIGHT) / 2;
  return Sc2kCanvasDesktopOpen(desktop,
      (Window){.kind = CITY_CANVAS_POLICIES,
          .bounds = {left, top, left + CITY_POLICY_WINDOW_WIDTH, top + CITY_POLICY_WINDOW_HEIGHT},
          .title = "City Ordinances", .policy_flags = policy_flags});
}

static i32 Sc2kCanvasDesktopOpenBonds(CityCanvasDesktop *desktop, CityRect bounds, int issue_rate) {
  bounds.right = bounds.left + CITY_BOND_WINDOW_WIDTH;
  bounds.bottom = bounds.top + CITY_BOND_WINDOW_HEIGHT;
  return Sc2kCanvasDesktopOpen(desktop,
      (Window){.kind = CITY_CANVAS_BONDS, .bounds = bounds,
          .title = "City Bonds", .bond_issue_rate = issue_rate});
}

void UpdateCityPolicyWindows(const City *city, CityCanvasDesktop *desktop, u32 policy_flags) {
  const CityEconomyMonths *economy = &city->economy;
  const CityBudgetPolicyGroupTotals *group_totals = &city->game->ui->budget_policy_totals;
  for (i32 index = 0; index < desktop->count; ++index) {
    Window *window = &desktop->windows[index];
    if (window->kind != CITY_CANVAS_POLICIES)
      continue;
    window->policy_flags = policy_flags;
    for (i32 policy = 0; policy < CITY_ORDINANCE_COUNT; ++policy)
      window->policy_cost[policy] = CityPolicyMonthlyCost(city, policy);
    for (i32 group = 0; group < CITY_POLICY_GROUP_COUNT; ++group)
      window->policy_group_cost[group] = group_totals->group[group];
    window->policy_ytd_total = economy->sector[CITY_ECONOMY_ORDINANCES].accumulated;
    window->policy_estimated_total = economy->sector[CITY_ECONOMY_ORDINANCES].projected_annual_total;
    Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(window));
  }
}

void Sc2kCanvasDesktopUpdateBudget(CityCanvasDesktop *desktop, const CityBudgetPanel *budget) {
  for (i32 index = 0; index < desktop->count; ++index) {
    Window *window = &desktop->windows[index];
    if (window->kind == CITY_CANVAS_BONDS) {
      Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(window));
      int count = (int)(budget->loan / 10000);
      window->bond_scroll = IntClamp(window->bond_scroll, 0,
          IntMax(0, CityBondContentRows(count) - CityBondVisibleRows(window)));
    }
    if (window->kind != CITY_CANVAS_BUDGET) continue;
    window->budget = *budget;
    Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(window));
  }
}

static const char *const kBudgetAdviceText[CITY_ADVICE_COUNT] = {
  "No urgent advice. Enjoy it while it lasts.",
  "Issue a bond to fund expansion. The city has outgrown what the treasury can pay for.",
  "Borrowing rates are low. If we're going to issue a bond, let's do it while the bank is feeling generous.",
  "Bond payments are swallowing the budget. Raise taxes and pay some of them off before the bankers move into city "
      "hall.",
  "Cut city service spending to close the deficit. We can't keep sending the treasury bills it can't pay.",
  "Crime is low. The police can finally finish their coffee.",
  "Crime is close to the national benchmark. A disappointing way to be average.",
  "Crime is getting out of hand. Build more police stations; the burglars have already noticed the gaps.",
  "Fire coverage is good. The brigade can breathe a little easier.",
  "Fire response is adequate. The brigade is keeping up, though I'd rather not give them any surprises.",
  "We need more firefighters. The crews are stretched thin, and fires have never been good at waiting their turn.",
  "Lower taxes to encourage growth. Builders might bring us more plans if we gave them fewer reasons to complain.",
  "Raise taxes to rebuild the treasury. At the moment, counting our money doesn't take nearly long enough.",
  "Cut service spending until the treasury recovers. We can promise everyone the world after we've paid for this "
      "month.",
  "Residents want pollution controls. When people can taste the air, it's time to listen.",
  "The power plants are near capacity. Introduce energy conservation before we have to explain the shortage by "
      "candlelight.",
  "Start a neighborhood watch. Residents already notice everything; give them someone useful to tell.",
  "Start an anti-drug campaign for the city's children. The dealers have had the loudest sales pitch for long enough.",
  "Legalized gambling is attracting crime. The police would prefer fewer customers.",
  "Remove the 1% income tax to help residents.",
  "Remove the 1% sales tax to help local businesses.",
  "Hospital services are efficient and responsive. It's refreshing to find a queue in this city that actually moves.",
  "We need more hospitals. A full waiting room is a poor measure of a hospital's popularity.",
  "Introduce a public smoking ban. Residents shouldn't need to hold their breath just to enjoy a public place.",
  "Offer CPR training to residents. The ambulance can't be on every doorstep, and a trained neighbor could save a "
      "life.",
  "Free clinics would win public support, but they won't be free for the city. Check the budget before promising them.",
  "Healthcare is meeting the city's needs. Let's keep it that way; healthy residents make much better company.",
  "Education meets demand. Today's homework: keep it that way.",
  "We need more schools, with enough funding to run them. A new building doesn't teach much on its own.",
  "Build and fund more colleges. We've promised these students a future; now we need somewhere to teach them.",
  "Education cuts save money now and send a larger invoice to the future.",
  "The city has outgrown its transport network. Issue a bond and expand it. The commuters have been remarkably "
      "patient.",
  "We have more roads than we need. Remove some to cut maintenance costs; even an empty road keeps sending us bills.",
};

enum { BUDGET_QUESTION_BASE = 0x60000000, BUDGET_QUESTION_LIMIT = 0x1FFFFFFF };

int OpenCityBudgetWindow(City *city) {
  CityInterfaceState *ui = city->game->ui;
  CityBudgetPanel budget;
  Sc2kCanvasBudgetFromCity(city, &budget);
  Window window = {.kind = CITY_CANVAS_BUDGET,
      .bounds = {(kDefaultCityCanvasWidth - CITY_BUDGET_WINDOW_WIDTH) / 2,
          (kDefaultCityCanvasHeight - CITY_BUDGET_WINDOW_HEIGHT) / 2,
          (kDefaultCityCanvasWidth + CITY_BUDGET_WINDOW_WIDTH) / 2,
          (kDefaultCityCanvasHeight + CITY_BUDGET_WINDOW_HEIGHT) / 2}, .budget = budget};
  int slot = Sc2kCanvasDesktopOpen(&ui->desktop, window);
  if (slot < 0) {
    SetGameError(city->game, SC2K_ERROR_GUEST_ALLOCATION_FAILURE);
    return -1;
  }
  return slot;
}

void OpenCityBudget(City *city) {
  if (!city->game->observation && city->settings.timer_mode == CITY_TIMER_STOPPED) return;
  if (FocusCityCanvasWindowKind(&city->game->ui->desktop, CITY_CANVAS_BUDGET) >= 0) return;
  if (city->game->observation && city->game->observation->open_manual_budget) {
    city->game->observation->open_manual_budget(city);
    return;
  }
  CityInterfaceState *ui = city->game->ui;
  if (ui->main_window_iconic) return;
  int slot = OpenCityBudgetWindow(city);
  if (slot < 0) return;
  ApplicationBudgetBegin(city, ui->desktop.windows[slot].window_id, true);
  ApplicationUpdateDialogPause(city);
  RefreshCityChartPanels(city);
}

// Open the manual budget through the installed observation adapter or native UI. Requires live City/UI owners; this
// routes presentation and does not itself perform annual settlement.
void ShowBudgetDialogManually(City *city) {
  const GameObservation *observer = city->game->observation;
  if (observer && observer->show_manual_budget) observer->show_manual_budget(city);
  else OpenCityBudget(city);
}

// Prepare the annual budget once, then retain its paused caller until the dialog returns.
void ShowAnnualBudgetDialog(City *city) {
  if (!PrepareCityAnnualBudget(city)) return;
  CityCanvasBudgetRequest request = {CITY_CANVAS_BUDGET_DIALOG_REQUEST};
  RunPausedCityModalDialog(city, &request);
  SetCityCursor(city, CITY_CURSOR_ARROW, CITY_CURSOR_SELECT);
}

// Observed callers retain the original budget continuation in City; native window controls use UI state.
static const CityBudgetState *ReadCityBudgetWindowState(const City *city) {
  return city->game->observation ? &city->budget : &city->game->ui->budget_window;
}

const CityBudgetState *FindCityBudgetWindowState(const City *city, u32 window_id) {
  const CityBudgetState *budget = ReadCityBudgetWindowState(city);
  return window_id && window_id <= INT32_MAX && (u32)budget->id == window_id ? budget : NULL;
}

void ApplicationBudgetBegin(City *city, u32 window, bool manual) {
  bool opened;
  if (city->game->observation) opened = BeginCityBudget(city, (i32)window, manual);
  else {
    CityBudgetState *budget = &city->game->ui->budget_window;
    opened = !budget->id;
    if (opened) *budget = (CityBudgetState){.id = (i32)window, .manual = manual,
        .helper_stage = city->indicators.year_flag && city->clock.month == CITY_MONTH_JANUARY ?
            0 : CITY_BUDGET_HELPER_DISABLED, .timer_running = true, .timer_revision = 1};
  }
  if (!opened) {
    city->game->execution_failed = 1;
    return;
  }
  ApplicationBudgetSync(city);
}

void CloseCityManualBudget(City *city) {
  const CityBudgetState *budget = ReadCityBudgetWindowState(city);
  if (!budget->id || !budget->manual) return;
  u32 window_id = budget->id;
  Sc2kCanvasDesktopDismissWindow(&city->game->ui->desktop, window_id);
  ApplicationBudgetEnd(city, window_id);
  ApplicationUpdateDialogPause(city);
}

static void ApplyCityBudgetWindowRate(City *city, u32 window, int row, int rate) {
  Game *game = city->game;
  int kind = CityBudgetRateKindForRow(row);
  if (kind < 0) return;
  rate = IntClamp(rate, 0, CityBudgetRateMaximum(row));
  if (!RunCityCommand(game, CITY_COMMAND_SET_BUDGET_RATE, INVALID_TILE, kind, rate, 0, NULL, NULL)) return;
  if (game->observation) RestartCityBudgetHelperAfterRateEdit(city, (i32)window);
  else {
    CityBudgetState *budget = &game->ui->budget_window;
    if ((u32)budget->id == window && budget->helper_stage != CITY_BUDGET_HELPER_DISABLED) {
      budget->helper_stage = 0;
      budget->timer_running = true;
      ++budget->timer_revision;
    }
  }
  ApplicationBudgetSync(city);
}

static void OpenCityBudgetRateEditor(City *city, u32 parent, int row) {
  static const char *const kTitles[CITY_BUDGET_ROW_COUNT] = {
    "Residential property tax", "Commercial property tax", "Industrial property tax", NULL, NULL,
    "Police funding", "Fire funding", "Health funding", "Education funding", "Transit funding"
  };
  CityCanvasDesktop *desktop = &city->game->ui->desktop;
  Window *budget = FindCityCanvasWindow(desktop, parent);
  if (!budget || CityBudgetRateKindForRow(row) < 0) return;
  if (FocusCityCanvasWindowKind(desktop, CITY_CANVAS_BUDGET_RATE) >= 0) return;
  int left = (budget->bounds.left + budget->bounds.right - 236) / 2;
  int top = (budget->bounds.top + budget->bounds.bottom - 88) / 2;
  Window query = {.kind = CITY_CANVAS_BUDGET_RATE, .bounds = {left, top, left + 236, top + 88},
      .title = kTitles[row], .budget_rate_parent = parent, .budget_rate_row = row, .text_select_all = 1};
  snprintf(query.budget_rate_input, sizeof(query.budget_rate_input), "%d", budget->budget.rate[row]);
  query.text_caret = strlen(query.budget_rate_input);
  Sc2kCanvasDesktopOpen(desktop, query);
}

static void CompleteBudgetRateEditor(City *city, Window *query, bool accept) {
  CityCanvasDesktop *desktop = &city->game->ui->desktop;
  if (accept && !query->budget_rate_input[0]) return;
  int row = query->budget_rate_row;
  int rate = atoi(query->budget_rate_input);
  u32 parent = query->budget_rate_parent;
  u32 id = query->window_id;
  Sc2kCanvasDesktopDismissWindow(desktop, id);
  Window *budget = FindCityCanvasWindow(desktop, parent);
  if (accept && budget && budget->kind == CITY_CANVAS_BUDGET) ApplyCityBudgetWindowRate(city, parent, row, rate);
  ApplicationUpdateDialogPause(city);
}

static Window *FindCityAdvisorQuestionWindow(City *city, int token) {
  CityCanvasDesktop *desktop = &city->game->ui->desktop;
  for (int index = 0; index < desktop->count; ++index)
    if (desktop->windows[index].advisor_question && desktop->windows[index].advisor_question->token == token)
      return &desktop->windows[index];
  return NULL;
}

void ApplicationBudgetEnd(City *city, u32 window) {
  CityCanvasDesktop *desktop = &city->game->ui->desktop;
  const CityBudgetState *budget = FindCityBudgetWindowState(city, window);
  bool manual = budget && budget->manual;
  // A process unwind cancels live questions explicitly, from child to parent.
  for (int remaining = CITY_PENDING_QUESTION_CAPACITY; remaining > 0; --remaining) {
    budget = FindCityBudgetWindowState(city, window);
    if (!budget || !budget->advisor_question) break;
    int token = budget->advisor_question;
    const CityQuestion *question = FindCityQuestion(city, token);
    if (!question || question->kind != CITY_QUESTION_BUDGET_ADVISOR ||
        question->context.budget_advisor.budget_session != (i32)window) {
      city->game->execution_failed = 1;
      break;
    }
    Window *advisor = FindCityAdvisorQuestionWindow(city, token);
    u32 advisor_window = advisor ? advisor->window_id : 0;
    if (ExecuteRecordedCityCommand(city->game, CITY_COMMAND_REPLY_TO_QUESTION, token, CITY_DIALOG_CANCEL, 0) < 0) break;
    if (advisor_window) Sc2kCanvasDesktopDismissWindow(desktop, advisor_window);
  }
  if (city->game->observation) {
    if (!EndCityBudget(city, (i32)window)) city->game->execution_failed = true;
  } else city->game->ui->budget_window = (CityBudgetState){0};
  if (manual) RefreshCityChartPanels(city);
}

static u32 OpenCityAdvisorWindow(City *city, CityBudgetCategory category, const char *message) {
  static const char *const kAdvisorTitles[CITY_BUDGET_CATEGORY_COUNT] = {
    "City Advisors – Property tax", "City Advisors – Ordinances", "City Advisors – Bonds",
    "City Advisors – Police", "City Advisors – Fire", "City Advisors – Health",
    "City Advisors – Education", "City Advisors – Transit"
  };
  CityCanvasDesktop *desktop = &city->game->ui->desktop;
  Window *advisor = NULL;
  for (int index = 0; index < desktop->count; ++index) {
    if (desktop->windows[index].advisor_category) {
      advisor = &desktop->windows[index];
      if (advisor->advisor_question && advisor->advisor_question->opening) return advisor->window_id;
      break;
    }
  }
  const RunSprite *portrait = ReadCityPicture(city->game->resources, CITY_ADVISOR_PORTRAIT_FIRST + category);
  if (advisor) {
    advisor->advisor_category = category + 1;
    advisor->title = kAdvisorTitles[category];
    advisor->portrait = portrait;
    advisor->message = message;
    ResizeCityMessageBox(desktop, advisor);
    u32 id = advisor->window_id;
    if (advisor != &desktop->windows[desktop->count - 1]) FocusCityCanvasWindow(desktop, id);
    return id;
  }
  Window window = {.kind = CITY_CANVAS_ADVISOR, .procedure = AdvisorWindowProcedure,
      .title = kAdvisorTitles[category], .message = message,
      .portrait = portrait,
      .advisor_category = category + 1, .message_style = CITY_MESSAGE_INFORMATION,
      .message_buttons = CITY_MESSAGE_NO_BUTTONS};
  window.bounds = CityMessageBounds(desktop, &window);
  int window_height = window.bounds.bottom - window.bounds.top;
  // Every advisor shares one geometry, including advice selected after funding changes.
  for (int advice = 0; advice < CITY_ADVICE_COUNT; ++advice) {
    window.message = kBudgetAdviceText[advice];
    CityRect bounds = CityMessageBounds(desktop, &window);
    window_height = IntMax(window_height, bounds.bottom - bounds.top);
  }
  window.message = message;
  window.bounds.top = IntMax(0, window.bounds.top -
      (window_height - (window.bounds.bottom - window.bounds.top)) / 2);
  window.bounds.bottom = window.bounds.top + window_height;
  int slot = Sc2kCanvasDesktopOpen(desktop, window);
  return slot < 0 ? 0 : desktop->windows[slot].window_id;
}

static void ApplicationBudgetAdvisor(City *city, u32 window, CityBudgetCategory category) {
  if (!city->game->observation) {
    OpenCityAdvisor(city, category);
    return;
  }
  CityInterfaceState *ui = city->game->ui;
  if (category < 0 || category >= CITY_BUDGET_CATEGORY_COUNT) return;
  for (int index = 0; index < ui->desktop.count; ++index) {
    Window *advisor = &ui->desktop.windows[index];
    if (!advisor->advisor_category || !advisor->advisor_question) continue;
    // Selecting another category keeps the existing budget caller suspended until closure.
    if (!advisor->advisor_question->opening) OpenCityAdvisor(city, category);
    return;
  }
  if (ui->budget_question_sequence == BUDGET_QUESTION_LIMIT) {
    city->game->execution_failed = 1;
    return;
  }
  int token = BUDGET_QUESTION_BASE + ++ui->budget_question_sequence;
  // Reserve the window before the command can pump nested callers. It is not replyable yet.
  CityAdvisorWindowState *reply = calloc(1, sizeof(*reply));
  if (!reply) {
    city->game->execution_failed = 1;
    return;
  }
  reply->token = token;
  reply->opening = true;
  u32 advisor_window = OpenCityAdvisorWindow(city, category, "");
  if (!advisor_window) {
    free(reply);
    city->game->execution_failed = 1;
    return;
  }
  Window *advisor = FindCityCanvasWindow(&city->game->ui->desktop, advisor_window);
  advisor->advisor_question = reply;
  bool accepted = QueueCityBudgetAdvisorQuestion(city, token, (i32)window, category);
  reply->opening = false;
  if (!accepted) {
    Sc2kCanvasDesktopDismissWindow(&ui->desktop, advisor_window);
    ApplicationUpdateDialogPause(city);
    return;
  }
  // Stacking or closing another window may have moved this one.
  advisor = FindCityCanvasWindow(&city->game->ui->desktop, advisor_window);
  if (!advisor) {
    ApplicationUpdateDialogPause(city);
    return;
  }
  const CityQuestion *question = FindCityQuestion(city, token);
  advisor->message = kBudgetAdviceText[question->context.budget_advisor.advice];
  ResizeCityMessageBox(&ui->desktop, advisor);
  ApplicationUpdateDialogPause(city);
}

// Menu advice has no budget caller or helper timer to suspend and resume.
void OpenCityAdvisor(City *city, CityBudgetCategory category) {
  if (category < 0 || category >= CITY_BUDGET_CATEGORY_COUNT) return;
  CityBudgetAdvice advice = SelectCityBudgetAdvisorMessage(city, category);
  if (!OpenCityAdvisorWindow(city, category,
      kBudgetAdviceText[advice])) {
    city->game->execution_failed = 1;
    return;
  }
  ApplicationUpdateDialogPause(city);
}

static void ReleaseClosedCityAdvisorQuestions(City *city) {
  CityAdvisorWindowState **link = &city->game->ui->closed_advisor_questions;
  while (*link) {
    CityAdvisorWindowState *reply = *link;
    if (reply->opening || FindCityQuestion(city, reply->token)) {
      link = &reply->next;
    } else {
      *link = reply->next;
      free(reply);
    }
  }
}

void ReleaseCityBudgetPresentation(CityInterfaceState *ui) {
  for (int index = 0; index < ui->desktop.count; ++index) {
    free(ui->desktop.windows[index].advisor_question);
    ui->desktop.windows[index].advisor_question = NULL;
  }
  while (ui->closed_advisor_questions) {
    CityAdvisorWindowState *reply = ui->closed_advisor_questions;
    ui->closed_advisor_questions = reply->next;
    free(reply);
  }
}

void ApplicationBudgetSync(City *city) {
  CityCanvasDesktop *desktop = &city->game->ui->desktop;
  // Closed windows leave the real City questions alive until their cancellation is accepted.
  for (int remaining = CITY_PENDING_QUESTION_CAPACITY; remaining > 0; --remaining) {
    int token = ReadCityBudgetWindowState(city)->advisor_question;
    ReleaseClosedCityAdvisorQuestions(city);
    if (!token) break;
    CityAdvisorWindowState *closed = city->game->ui->closed_advisor_questions;
    while (closed && closed->token != token) closed = closed->next;
    if (!closed || closed->opening) break;
    if (ExecuteRecordedCityCommand(city->game, CITY_COMMAND_REPLY_TO_QUESTION, token, CITY_DIALOG_CANCEL, 0) < 0) break;
  }
  ReleaseClosedCityAdvisorQuestions(city);
  const CityBudgetState *budget = ReadCityBudgetWindowState(city);
  if (!budget->id) return;
  Window *window = FindCityCanvasWindow(&city->game->ui->desktop, budget->id);
  if (!window) {
    if (budget->manual) ApplicationBudgetEnd(city, budget->id);
    return;
  }
  if (budget->timer_revision != window->budget_helper_revision) {
    window->budget_helper_revision = budget->timer_revision;
    window->budget_helper_deadline =
        ReadGameTime(city->game, GAME_TIME_REALTIME) + CITY_BUDGET_HELPER_INTERVAL_MS / 1000.0;
  }
  if (budget->close_requested) {
    Sc2kCanvasDesktopDismissWindow(desktop, window->window_id);
    if (budget->manual) ApplicationBudgetEnd(city, budget->id);
    return;
  }
}

void ApplicationBudgetPoll(City *city) {
  const CityBudgetState *budget = ReadCityBudgetWindowState(city);
  Window *window = FindCityCanvasWindow(&city->game->ui->desktop, budget->id);
  double now = ReadGameTime(city->game, GAME_TIME_REALTIME);
  if (window && budget->timer_running &&
      PollGameInterval(now, CITY_BUDGET_HELPER_INTERVAL_MS / 1000.0, &window->budget_helper_deadline)) {
    AdvanceCityBudgetHelper(city, budget->id);
  }
  ApplicationBudgetSync(city);
}

void AdvanceCityBudgetHelper(City *city, u32 window_id) {
  if (city->game->observation) {
    DeliverCityBudgetHelperTimer(city, (i32)window_id, CITY_BUDGET_HELPER_TIMER);
    return;
  }
  CityBudgetState *budget = &city->game->ui->budget_window;
  if ((u32)budget->id != window_id) return;
  if (budget->helper_stage == CITY_BUDGET_HELPER_LAST_STAGE) budget->close_requested = true;
  else if (budget->helper_stage != CITY_BUDGET_HELPER_DISABLED) ++budget->helper_stage;
}

// Handles this window's painting and input; paint coordinates are local to the window.
bool BudgetWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    Canvas *canvas = event->canvas;
    Sc2kCanvasDrawBudgetPanel(canvas, window->bounds, &window->budget);
    return true;
  }
  if (event->kind != WINDOW_INPUT) return false;
  const CanvasSDLEvent *input = event->input;
  if (input->kind != CANVAS_SDL_POINTER_DOWN || input->button != CANVAS_SDL_BUTTON_LEFT) return false;
  City *city = game->city;
  CityCanvasDesktop *desktop = &game->ui->desktop;
  int x = event->x, y = event->y;
  const CityBudgetState *session = FindCityBudget(city, (i32)window->window_id);
  int step = Sc2kCanvasBudgetStepAt(window, x, y);
  int advice = Sc2kCanvasBudgetAdviceAt(window, x, y);
  if (session && session->advisor_question && step < 0 && advice < 0) {
    // Pending advisor callers retain their budget; exposed rate/advisor controls remain usable.
    Sc2kCanvasDesktopPointerDown(desktop, x, y);
    ApplicationUpdateDialogPause(city);
    return true;
  }
  int bond_action = CanvasPointInRect(
      CityBudgetControlBounds(window->bounds, CITY_BUDGET_CONTROL_BORROW, 0), x, y) ? CITY_BOND_ACTION_ISSUE :
      CanvasPointInRect(CityBudgetControlBounds(window->bounds, CITY_BUDGET_CONTROL_REPAY, 0), x, y) ?
          CITY_BOND_ACTION_REDEEM : CITY_BOND_ACTION_NONE;
  int rate_row = Sc2kCanvasBudgetRateAt(window, x, y);
  if (bond_action != CITY_BOND_ACTION_NONE) {
    if (bond_action == CITY_BOND_ACTION_ISSUE || bond_action == CITY_BOND_ACTION_REDEEM)
      PresentCityBondConfirmation(city, bond_action == CITY_BOND_ACTION_ISSUE ? CITY_COMMAND_ISSUE_BOND :
          CITY_COMMAND_REDEEM_BOND, city->fixes & (1ull << CITY_FIX_BOND_RATING) ? CityBondInterestRate(city) : 5);
  } else if (step >= 0) {
    BeginCityButtonRepeat(game, window, event);
    int row = step / 2;
    int increment = row <= CITY_BUDGET_ROW_INDUSTRIAL || input->shift ? 1 : 5;
    int delta = (step & 1 ? 1 : -1) * increment;
    ApplyCityBudgetWindowRate(city, window->window_id, row, window->budget.rate[row] + delta);
  } else if (rate_row >= 0) OpenCityBudgetRateEditor(city, window->window_id, rate_row);
  else if (advice >= 0) ApplicationBudgetAdvisor(city, window->window_id, advice);
  else if (CanvasPointInRect(CityBudgetControlBounds(window->bounds, CITY_BUDGET_CONTROL_OPEN_ORDINANCES, 0), x, y)) {
    InitializeCityOrdinancesDialog(city);
    Sc2kCanvasDesktopOpenPolicies(desktop, (CityRect){70, 18, 530, 472}, city->unlocks.ordinance_flags);
  } else if (CanvasPointInRect(CityBudgetControlBounds(window->bounds, CITY_BUDGET_CONTROL_SHOW_BONDS, 0), x, y))
    Sc2kCanvasDesktopOpenBonds(desktop, (CityRect){120, 70, 480, 274},
        city->fixes & (1ull << CITY_FIX_BOND_RATING) ? CityBondInterestRate(city) : 5);
  else return false;
  ApplicationUpdateDialogPause(city);
  return true;
}

// Handles this window's painting and input; paint coordinates are local to the window.
bool BudgetRateWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    DrawCityBudgetRateEditor(event->canvas, window);
    return true;
  }
  if (event->kind != WINDOW_INPUT) return false;
  City *city = game->city;
  Window *query = FindCityCanvasWindow(&game->ui->desktop, window->window_id);
  const CanvasSDLEvent *input = event->input;
  if (input->kind == CANVAS_SDL_POINTER_DOWN && input->button == CANVAS_SDL_BUTTON_LEFT) {
    int x = event->x, y = event->y;
    CityCanvasDesktop *desktop = &city->game->ui->desktop;
    for (int control = CITY_BUDGET_QUERY_INPUT; control <= CITY_BUDGET_QUERY_OK; ++control) {
      CityRect bounds = CityBudgetQueryBounds(query->bounds, control);
      if (!CanvasPointInRect(bounds, x, y)) continue;
      if (control == CITY_BUDGET_QUERY_OK || control == CITY_BUDGET_QUERY_CANCEL) {
        CompleteBudgetRateEditor(city, query, control == CITY_BUDGET_QUERY_OK);
        return true;
      }
      if (control == CITY_BUDGET_QUERY_INPUT) {
        query->text_caret = CityTextBoxCaretAt(&city->game->resources->interface_font,
            bounds, query->budget_rate_input, query->text_caret, &query->text_scroll, x);
        query->text_select_all = 0;
      } else {
        if (control == CITY_BUDGET_QUERY_CLEAR) query->budget_rate_input[0] = 0;
        else snprintf(query->budget_rate_input, sizeof(query->budget_rate_input), "%d",
            query->budget_rate_row <= CITY_BUDGET_ROW_INDUSTRIAL ? 7 : 100);
        query->text_caret = strlen(query->budget_rate_input);
        query->text_select_all = 1;
      }
      Sc2kDirtyMarkRect(&desktop->dirty, query->bounds);
      return true;
    }
    Sc2kCanvasDesktopPointerDown(desktop, x, y);
    return true;
  }
  if (input->kind == CANVAS_SDL_KEY_DOWN) {
    CityHotkey key = input->key;
    if (key == CITY_KEY_RETURN || key == CITY_KEY_ESCAPE) CompleteBudgetRateEditor(city, query, key == CITY_KEY_RETURN);
    else if (HandleCityTextEditKey(query->budget_rate_input, &query->text_caret, &query->text_select_all, key))
      Sc2kDirtyMarkRect(&city->game->ui->desktop.dirty, query->bounds);
    return true;
  }
  if (input->kind == CANVAS_SDL_TEXT_INPUT) {
    const char *text = input->text;
    InsertCityTextInput(query->budget_rate_input, sizeof(query->budget_rate_input),
        &query->text_caret, &query->text_select_all, text, true);
    Sc2kDirtyMarkRect(&city->game->ui->desktop.dirty, query->bounds);
    return true;
  }
  return false;
}

// Handles this window's painting and input; paint coordinates are local to the window.
bool PoliciesWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    Canvas *canvas = event->canvas;
    Sc2kCanvasDrawPolicies(game, canvas, window);
    return true;
  }
  if (event->kind != WINDOW_INPUT) return false;
  const CanvasSDLEvent *input = event->input;
  if (input->kind == CANVAS_SDL_POINTER_DOWN && input->button == CANVAS_SDL_BUTTON_LEFT) {
    int policy = Sc2kCanvasPolicyAt(window, event->x, event->y);
    if (policy < 0) return false;
    RunCityCommand(game, CITY_COMMAND_TOGGLE_CITY_POLICY, INVALID_TILE, policy, 0, 0, NULL, NULL);
    ApplicationUpdateDialogPause(game->city);
    return true;
  }
  return false;
}

// Advisor controls and retained replies belong to this window. Opening replies
// remain blocked until their caller has finished establishing the question.
bool AdvisorWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    Canvas *canvas = event->canvas;
    DrawCityMessageBox(canvas, window);
    if (window->advisor_category) {
      static const u8 kAdvisorIcons[CITY_BUDGET_CATEGORY_COUNT] = {
        CITY_TOPBAR_BUDGET, CITY_TOPBAR_ORDINANCES, CITY_TOPBAR_REPORTS,
        CITY_TOPBAR_ICON_COUNT + CITY_CANVAS_TOOL_POLICE,
        CITY_TOPBAR_ICON_COUNT + CITY_CANVAS_TOOL_FIRE,
        CITY_TOPBAR_ICON_COUNT + CITY_CANVAS_TOOL_HOSPITAL,
        CITY_TOPBAR_ICON_COUNT + CITY_CANVAS_TOOL_SCHOOL,
        CITY_TOPBAR_ROADS
      };
      for (int category = 0; category < CITY_BUDGET_CATEGORY_COUNT; ++category) {
        CityRect b = CityAdvisorButtonBounds(window, category);
        bool selected = window->advisor_category == category + 1;
        DrawCanvasButtonFrame(canvas, b, selected ? 0xFFE8B810 : 0xFF7E9696, 0xFFB4CCCA, 0xFF4D6565, !selected);
        DrawCityToolbarIcon(game, canvas, b, kAdvisorIcons[category], false, false);
      }
    }
    return true;
  }
  if (event->kind != WINDOW_INPUT || !window->advisor_category) return false;
  const CanvasSDLEvent *input = event->input;
  if (window->advisor_question && window->advisor_question->opening) return true;
  if (input->kind == CANVAS_SDL_KEY_DOWN) {
    if (input->key == CITY_KEY_RETURN || input->key == CITY_KEY_ESCAPE) {
      u32 id = window->window_id;
      int token = window->advisor_question ? window->advisor_question->token : 0;
      if (!token || ExecuteRecordedCityCommand(game, CITY_COMMAND_REPLY_TO_QUESTION, token,
          input->key == CITY_KEY_RETURN ? CITY_DIALOG_OK : CITY_DIALOG_CANCEL, 0) >= 0) {
        Sc2kCanvasDesktopDismissWindow(&game->ui->desktop, id);
        ApplicationUpdateDialogPause(game->city);
      }
    }
    return true;
  }
  if (input->kind == CANVAS_SDL_POINTER_DOWN && input->button == CANVAS_SDL_BUTTON_LEFT) {
    int category = CityAdvisorButtonAt(window, event->x, event->y);
    if (category >= 0) {
      OpenCityAdvisor(game->city, category);
      ApplicationUpdateDialogPause(game->city);
      return true;
    }
  }
  return false;
}
