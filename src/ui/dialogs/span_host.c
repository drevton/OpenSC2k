#include "city/infrastructure/bridges.h"
#include "city/engine/city_types.h"
#include "city/engine/command_types.h"
#include "city/infrastructure/types.h"
#include "ui/windows/state.h"
#include "city/presentation.h"
#include "game/observation.h"
#include "game/state.h"
#include "audio/state.h"
#include "city/state.h"
#include "drawing/state.h"
#include "assets/state.h"
#include "assets/catalog.h"
#include "ui/state.h"
#include "ui/tools/tools.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui/startup/services.h"
#include "drawing/drawing.h"
#include "ui/dialogs/span_internal.h"

#include "game/application_internal.h"
#include "city/economy/state.h"
#include "city/engine/simulation.h"
#include "city/infrastructure/infrastructure.h"
#include "game/errors.h"
#include "ui/dialogs/alerts.h"
#include "ui/input.h"
#include "ui/map.h"
#include "ui/news/newspaper.h"
#include "drawing/drawing_internal.h"
#include "ui/charts/charts.h"
#include "ui/commands.h"


// Span Host

// Answering closes the chooser. Native windows then dispatch their saved command;
// diagnostic windows resume their retained placement caller.
bool AnswerCitySpanDialog(City *city, int token, int choice) {
  CitySpanPresentation *span = &city->game->ui->span;

  NativeSpanDialog *dialog = span->active;
  if (span->desktop == NULL || dialog == NULL || dialog->token != token || dialog->answered ||
      (choice != CITY_SPAN_CANCEL && (choice < 0 || choice >= CITY_SPAN_STYLE_COUNT ||
          !(dialog->styles & (1u << choice))))) return false;
  dialog->answered = 1;
  dialog->choice = choice;
  if (dialog->drawing) ReleaseCityWindowDrawing(&dialog->drawing->base);
  dialog->drawing = NULL;
  Sc2kCanvasDesktopDismissWindow(span->desktop, dialog->window_id);
  dialog->window_id = 0;
  ApplicationUpdateDialogPause(city);
  return true;
}

void SetCitySpanPresentation(CityInterfaceState *ui, CityCanvasDesktop *desktop) {
  ui->span.desktop = desktop;
}

bool NativeSpanDialogWaiting(const CityInterfaceState *ui) {
  return ui->span.active != NULL && !ui->span.active->answered;
}

int OpenCitySpanWindow(City *city, NativeSpanDialog *dialog, int distance, int styles) {
  CitySpanPresentation *span = &city->game->ui->span;
  if (span->desktop == NULL || !styles || (styles & ~CITY_SPAN_STYLES_ALL)) {
    return -1;
  }
  dialog->parent = span->active;
  dialog->distance = distance;
  dialog->styles = styles;
  dialog->choice = CITY_SPAN_CANCEL;
  if (span->next_token == 0x7FFFFFFF) span->next_token = 0;
  dialog->token = ++span->next_token;
  snprintf(dialog->message, sizeof(dialog->message), "Choose a span for %d cells.", distance);
  i32 rows = 0;
  u32 prices[CITY_SPAN_STYLE_COUNT];
  for (i32 style = 0; style < CITY_SPAN_STYLE_COUNT; ++style) {
    rows += (styles & (1u << style)) != 0;
    prices[style] = (u32)distance * kSpanCostPerCell[style];
  }
  bool rail = styles == CITY_SPAN_STYLE_RAIL_MASK || (dialog->command.id == CITY_COMMAND_BUILD_BRIDGE &&
      (dialog->command.parameter3 & CITY_BRIDGE_COMMAND_RAIL));
  const char *title = rail ? "Select Rail Bridge" : (styles & CITY_SPAN_STYLE_ROAD_MASK) && !(styles & ~CITY_SPAN_STYLE_ROAD_MASK) ?
      "Select Road Bridge" : styles == CITY_SPAN_STYLE_RAIL_MASK ? "Select Rail Bridge" :
      styles == CITY_SPAN_STYLE_POWER_MASK ? "Select Power Span" :
      !(styles & ~CITY_SPAN_STYLE_HIGHWAY_BOTH_MASK) ? "Select Highway Bridge" : "Select Bridge";
  int height = CITY_SPAN_LIST_TOP + IntMin(rows, 4) * CITY_SPAN_ROW_HEIGHT + CITY_SPAN_FOOTER_HEIGHT;
  i32 index = Sc2kCanvasDesktopOpenSpanChoices(span->desktop,
      (CityRect){150, 40, 150 + CITY_SPAN_CHOICE_WIDTH, 40 + height}, title, dialog->message, styles, prices);
  if (index < 0) {
    if (city->game->reconstruction_error == 0)
      city->game->reconstruction_error = SC2K_ERROR_GUEST_ALLOCATION_FAILURE;
    return -1;
  }
  span->desktop->windows[index].city = city;
  span->desktop->windows[index].span_rail = rail;
  dialog->drawing = (CityBridgeWindowDrawing *)CreateCityWindowDrawing(city->game->drawing,
      CITY_WINDOW_DRAWING_BRIDGE_CHOICES);
  if (!dialog->drawing) {
    Sc2kCanvasDesktopDismissWindow(span->desktop, span->desktop->windows[index].window_id);
    SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    return -1;
  }
  dialog->window_id = span->desktop->windows[index].window_id;
  span->active = dialog;
  ApplicationUpdateDialogPause(city);
  RefreshCityChartPanels(city);
  ObserveCityPresentation(city->game, CITY_PRESENTATION_SPAN_OPEN, dialog->token, distance, styles, CITY_SPAN_CANCEL, NULL);

  return index;
}

static void CompleteCitySpanCommand(City *city, void *context) {
  NativeSpanDialog *dialog = context;
  CitySpanPresentation *span = &city->game->ui->span;
  NativeSpanDialog **scope = &span->active;
  while (*scope && *scope != dialog) scope = &(*scope)->parent;
  if (*scope) *scope = dialog->parent;
  CityCommand command = dialog->command;
  int choice = dialog->choice;
  if (dialog->drawing) ReleaseCityWindowDrawing(&dialog->drawing->base);
  free(dialog);
  ApplicationUpdateDialogPause(city);
  if (choice == CITY_SPAN_CANCEL || city->game->close_requested || city->game->execution_failed) return;
  if (command.id == CITY_COMMAND_PLACE_ROAD)
    command.parameter3 = (command.parameter3 & ~65280) | EncodeCityRoadSpanChoice(choice) << 8;
  if (command.id == CITY_COMMAND_BUILD_BRIDGE)
    command.parameter3 = (command.parameter3 & CITY_BRIDGE_COMMAND_RAIL) | choice;
  if (command.id == CITY_COMMAND_PLACE_TRANSPORT_LINE)
    command.parameter3 = (command.parameter3 & ~255) | choice;
  if (CheckCityConstructionCommand(city, command) >= 0)
    ExecuteRecordedCityCommandWithInput(city->game, command, NULL, 0);
}

bool OpenCitySpanCommand(City *city, CityCommand command, int distance, int styles) {
  NativeSpanDialog *dialog = calloc(1, sizeof(*dialog));
  if (!dialog) {
    SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    return false;
  }
  dialog->command = command;
  int opened = OpenCitySpanWindow(city, dialog, distance, styles);
  if (opened < 0) {
    free(dialog);
    return false;
  }
  Window *window = &city->game->ui->desktop.windows[opened];
  if (!AttachCityWindowCallback(window, CompleteCitySpanCommand, dialog)) {
    Sc2kCanvasDesktopDismissWindow(&city->game->ui->desktop, dialog->window_id);
    city->game->ui->span.active = dialog->parent;
    if (dialog->drawing) ReleaseCityWindowDrawing(&dialog->drawing->base);
    free(dialog);
    SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    return false;
  }
  return true;
}

void CancelCitySpanCommands(City *city) {
  for (NativeSpanDialog *dialog = city->game->ui->span.active; dialog; dialog = dialog->parent)
    if (dialog->command.id) dialog->choice = CITY_SPAN_CANCEL;
}

static void DirtySpanList(CitySpanPresentation *span, Window *window) {
  Sc2kDirtyMarkRect(&span->desktop->dirty, window->bounds);
}

static bool ScrollCitySpanDialog(City *city, Window *window, int delta) {
  CitySpanPresentation *span = &city->game->ui->span;
  u8 order[CITY_SPAN_STYLE_COUNT];
  int count = ReadCitySpanList(window, order), visible = CitySpanVisibleRows(window);
  int next = IntClamp(window->span_scroll + delta, 0, count - visible);
  if (window->span_scroll != next) {
    window->span_scroll = next;
    DirtySpanList(span, window);
  }
  return true;
}

static bool TrackCitySpanScrollbar(City *city, Window *window, NativeSpanDialog *dialog, int y, bool held) {
  CitySpanPresentation *span = &city->game->ui->span;
  if (!dialog->scroll_dragging) return false;
  if (!held) { dialog->scroll_dragging = false; return true; }
  CityRect track = CitySpanControlBounds(window, CITY_CANVAS_SPAN_HIT_TRACK);
  u8 order[CITY_SPAN_STYLE_COUNT];
  CityScrollbar scrollbar = ReadCityScrollbar(track, ReadCitySpanList(window, order),
      CitySpanVisibleRows(window), window->span_scroll, 8);
  int next = CityScrollbarPosition(&scrollbar, y, dialog->scroll_drag_offset, CITY_SCROLLBAR_DRAG_NEAREST);
  if (window->span_scroll != next) {
    window->span_scroll = next;
    DirtySpanList(span, window);
  }
  return true;
}

int SelectInfrastructureSpan(City *city, int distance, int enabled_styles) {
  const GameObservation *observer = city->game->observation;
  if (observer && observer->select_span) return observer->select_span(city, distance, enabled_styles);
  int choice = city->command_choices.supplied ? city->command_choices.span : CITY_SPAN_CANCEL;
  return choice >= 0 && choice < CITY_SPAN_STYLE_COUNT && (enabled_styles & (1 << choice)) ? choice : CITY_SPAN_CANCEL;
}

// Connection Host

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

// Mission Host

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

// Dialog Host
// Native canvas dialog presentation.

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

i32 QueryCityOverlayNameDialog(City *city, int slot, CityNameCommandInput *state) {
  CityCanvasNameRequest request = {CITY_CANVAS_NAME_DIALOG_REQUEST, slot, state};
  i32 answer = RunPausedCityModalDialog(city, &request);
  return answer;
}

void ShowRankedCityEventModal(City *city, u32 event_code, u32 event_data) {
  PresentRankedCityEvent(city, event_code, event_data);
}

void ShowCityNewspaper(City *city, i32 edition_index) {
  PresentCityNewspaperEdition(city, edition_index);
}

void SetCityCanvasPaletteDialogState(City *city, bool paused) {
  SetCityPaletteDialogState(city, paused);
}

static i32 RunNewspaperHost(City *city, void *dialog) {
  const GameObservation *observer = city->game->observation;
  if (observer && observer->choose_newspaper) return observer->choose_newspaper(city, dialog);
  return CITY_DIALOG_UNANSWERED;
}

i32 RunPausedCityNewspaperDialog(City *city, CityNewspaperDialog *dialog) {
  return RunPausedCityAction(city, RunNewspaperHost, dialog);
}

i32 ChooseCityCanvasDialog(City *city, void *dialog) {
  const GameObservation *observer = city->game->observation;
  if (observer && observer->choose_canvas_dialog) return observer->choose_canvas_dialog(city, dialog);
  return CITY_DIALOG_UNANSWERED;
}

// Retain the original game prefix and shared modal pause lifetime.
// Canvas dialogs retain the original pause lifetime and City effects.
void ShowAnnualBudgetDialog(City *city) {
  if (!PrepareCityAnnualBudget(city)) return;
  CityCanvasBudgetRequest request = {CITY_CANVAS_BUDGET_DIALOG_REQUEST};
  RunPausedCityModalDialog(city, &request);
  SetCityCursor(city, CITY_CURSOR_ARROW, 0);
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

bool HasCityDialogPause(const CityInterfaceState *ui, int token) {
  if (token <= 0) return false;
  for (int index = 0; index < CITY_DIALOG_PAUSE_TOKEN_CAPACITY; ++index)
    if (ui->dialog_pause_tokens[index] == token) return true;
  return false;
}

// Handles this window's painting and input; paint coordinates are local to the window.
bool SpanWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    Canvas *canvas = event->canvas;
    if (window->span_styles)
      CanvasDrawSpanChoices(canvas, window);
    else Sc2kCanvasDrawSpanStyle(canvas, window->bounds, window->title, window->message);
    return true;
  }
  Window *target = FindCityCanvasWindow(&game->ui->desktop, window->window_id);
  if (event->kind == WINDOW_DROPDOWN) {
    target->span_sort = event->selection->value;
    target->span_scroll = 0;
    DirtySpanList(&game->ui->span, target);
    return true;
  }
  if (event->kind != WINDOW_INPUT) return false;
  NativeSpanDialog *dialog = game->ui->span.active;
  while (dialog && dialog->window_id != window->window_id) dialog = dialog->parent;
  if (!dialog || dialog->answered) return false;
  const CanvasSDLEvent *input = event->input;
  switch (input->kind) {
  case CANVAS_SDL_POINTER_DOWN: {
    if (input->button != CANVAS_SDL_BUTTON_LEFT) return false;
    int x = event->x, y = event->y;
    City *city = game->city;
    CitySpanPresentation *span = &city->game->ui->span;
    i32 choice = CitySpanChoiceAt(target, x, y);
    if (choice == CITY_CANVAS_SPAN_HIT_NONE) return false; // Caption/background stays draggable.
    if (choice >= 0 || choice == CITY_CANVAS_SPAN_HIT_CANCEL)
      AnswerCitySpanDialog(city, dialog->token, choice == CITY_CANVAS_SPAN_HIT_CANCEL ? CITY_SPAN_CANCEL : choice);
    else if (choice == CITY_CANVAS_SPAN_HIT_SORT) {
      static const CityDropdownItem kSortItems[] = {
        {"Cost", CITY_SPAN_SORT_COST, CITY_DROPDOWN_PLAIN},
        {"Name", CITY_SPAN_SORT_NAME, CITY_DROPDOWN_PLAIN},
      };
      CityRect anchor = CitySpanControlBounds(target, choice);
      anchor.left -= target->bounds.left; anchor.right -= target->bounds.left;
      anchor.top -= target->bounds.top; anchor.bottom -= target->bounds.top;
      OpenCityDropdown(span->desktop, target->window_id, choice, anchor, kSortItems, 2, target->span_sort,
          &kCityDropdownGoldColors);
    } else if (choice == CITY_CANVAS_SPAN_HIT_DIRECTION) {
      target->span_descending = !target->span_descending;
      target->span_scroll = 0;
      DirtySpanList(span, target);
    } else if (choice == CITY_CANVAS_SPAN_HIT_UP || choice == CITY_CANVAS_SPAN_HIT_DOWN)
      ScrollCitySpanDialog(city, target, choice == CITY_CANVAS_SPAN_HIT_UP ? -1 : 1);
    else if (choice == CITY_CANVAS_SPAN_HIT_TRACK) {
      CityRect track = CitySpanControlBounds(target, choice);
      u8 order[CITY_SPAN_STYLE_COUNT];
      int count = ReadCitySpanList(target, order), visible = CitySpanVisibleRows(target);
      if (count > visible && track.bottom > track.top) {
        CityRect thumb = CitySpanScrollThumb(target);
        dialog->scroll_drag_offset = y >= thumb.top && y < thumb.bottom ?
            y - thumb.top : (thumb.bottom - thumb.top) >> 1;
        dialog->scroll_dragging = true;
        TrackCitySpanScrollbar(city, target, dialog, y, true);
      }
    } else if (choice == CITY_CANVAS_SPAN_HIT_RESTORE) {
      u8 order[CITY_SPAN_STYLE_COUNT];
      int count = ReadCitySpanList(target, order);
      DirtySpanList(span, target);
      target->bounds.right = target->bounds.left + CITY_SPAN_CHOICE_WIDTH;
      target->bounds.bottom = target->bounds.top + CITY_SPAN_LIST_TOP +
          IntMin(count, 4) * CITY_SPAN_ROW_HEIGHT + CITY_SPAN_FOOTER_HEIGHT;
      target->span_scroll = 0;
      DirtySpanList(span, target);
    }
    return true;
  }
  case CANVAS_SDL_POINTER_MOVE: return TrackCitySpanScrollbar(game->city, target, dialog, event->y, true);
  case CANVAS_SDL_POINTER_UP:
  case CANVAS_SDL_FOCUS_LOST: return TrackCitySpanScrollbar(game->city, target, dialog, 0, false);
  case CANVAS_SDL_KEY_DOWN: {
    City *city = game->city;
    CityHotkey key = input->key;
    CitySpanPresentation *span = &city->game->ui->span;
    if (key == CITY_KEY_ESCAPE) AnswerCitySpanDialog(city, dialog->token, CITY_SPAN_CANCEL);
    else if (key == CITY_KEY_RETURN) AnswerCitySpanDialog(city, dialog->token, target->span_selected);
    else if (key == CITY_KEY_UP || key == CITY_KEY_DOWN || key == CITY_KEY_HOME || key == CITY_KEY_END) {
      u8 order[CITY_SPAN_STYLE_COUNT];
      int count = ReadCitySpanList(target, order), position = 0;
      while (position < count && order[position] != target->span_selected) ++position;
      if (key == CITY_KEY_HOME) position = 0;
      else if (key == CITY_KEY_END) position = count - 1;
      else position = IntClamp(position + (key == CITY_KEY_DOWN ? 1 : -1), 0, count - 1);
      target->span_selected = order[position];
      int visible = CitySpanVisibleRows(target);
      target->span_scroll = IntClamp(target->span_scroll, 0, count - visible);
      if (position < target->span_scroll) target->span_scroll = position;
      if (position >= target->span_scroll + visible) target->span_scroll = position - visible + 1;
      DirtySpanList(span, target);
    }
    return true; // A focused chooser consumes map shortcut keys.
  }
  case CANVAS_SDL_SCROLL: return ScrollCitySpanDialog(game->city, target, input->y > 0 ? -1 : input->y < 0 ? 1 : 0);
  }
  return false;
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
