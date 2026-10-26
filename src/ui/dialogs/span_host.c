#include "city/infrastructure/spans.h"
#include "ui/windows/window_internal.h"
#include "ui/commands.h"
#include "game/commands.h"
#include "ui/dialogs/spans.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/state.h"
#include "city/infrastructure/bridges.h"
#include "city/state.h"
#include "drawing/bitmaps.h"
#include "drawing/fonts/fonts.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/raster/canvas_raster_internal.h"
#include "game/errors.h"
#include "game/observation.h"
#include "game/state.h"
#include "ui/dialogs/dialogs.h"
#include "ui/dialogs/span_internal.h"
#include "ui/map.h"
#include "ui/sdl.h"
#include "ui/state.h"
#include "ui/text/text.h"
#include "ui/windows/controls.h"
#include "ui/windows/windows.h"

// Bridge chooser state, layout, drawing and input.

// Chooser labels indexed by command style; the same text drives name sorting and row drawing.
static const char *const kCitySpanNames[CITY_SPAN_STYLE_COUNT] = {
  [CITY_SPAN_POWER] = "Raised power lines",
  [CITY_SPAN_RAIL] = "Rail bridge",
  [CITY_SPAN_CAUSEWAY] = "Causeway",
  [CITY_SPAN_RAISING_BRIDGE] = "Raising bridge",
  [CITY_SPAN_SUSPENSION] = "Suspension bridge",
  [CITY_SPAN_HIGHWAY] = "Highway bridge",
  [CITY_SPAN_REINFORCED_HIGHWAY] = "Reinforced highway",
  [CITY_SPAN_WOOD] = "Wooden",
  [CITY_SPAN_CONCRETE] = "Concrete",
  [CITY_SPAN_ARCH_STEEL] = "Girder, steel (arched)",
  [CITY_SPAN_SUSPENSION_CONCRETE] = "Suspension, concrete",
  [CITY_SPAN_SUSPENSION_STEEL] = "Suspension, steel",
  [CITY_SPAN_SUSPENSION_YELLOW] = "Suspension, yellow steel",
  [CITY_SPAN_CANTILEVER_STEEL] = "Cantilever, steel",
  [CITY_SPAN_CANTILEVER_BROWN] = "Cantilever, brown steel",
  [CITY_SPAN_CANTILEVER_RED] = "Cantilever, red steel",
  [CITY_SPAN_GIRDER_STEEL] = "Girder, steel",
  [CITY_SPAN_TUBULAR_STEEL] = "Tubular, steel",
  [CITY_SPAN_TUBULAR_YELLOW] = "Tubular, yellow steel",
  [CITY_SPAN_TUBULAR_SILICON] = "Tubular, silicon",
};

enum CitySpanSort { CITY_SPAN_SORT_COST, CITY_SPAN_SORT_NAME };

static i32 Sc2kCanvasDesktopOpenSpanChoices(CityCanvasDesktop *desktop, CityRect bounds,
    const char *title, const char *message, u32 styles, const u32 *prices);
static i32 CitySpanChoiceAt(const Window *window, i32 x, i32 y);

static void Sc2kCanvasDrawSpanStyle(Canvas *canvas, CityRect bounds, const char *title, const char *message) {
  static const char *const styles[CITY_ROAD_SPAN_STYLE_COUNT] = {"Basic", "Arch", "Decorated"};
  CanvasPanelFrame(canvas, bounds, title);
  CanvasBeveledBox(canvas, bounds.left + 9, bounds.top + 28, bounds.right - 9, bounds.top + 58,
      canvas_light, 0);
  CanvasTextWrapped(canvas, bounds.left + 16, bounds.top + 38, bounds.right - 16,
      bounds.top + 54, message, canvas_text);
  for (i32 style = 0; style < CITY_ROAD_SPAN_STYLE_COUNT; ++style) {
    i32 top = bounds.top + 64 + style * 30;
    DrawCanvasButtonBox(canvas, bounds.left + 24, top, bounds.right - 24, top + 24, canvas_light, 1);
    CanvasTextAt(canvas, bounds.left + 42, top + 8, styles[style], canvas_text);
  }
  DrawCanvasButtonBox(canvas, bounds.right - 68, bounds.bottom - 29, bounds.right - 12, bounds.bottom - 10,
      canvas_light, 1);
  CanvasTextAt(canvas, bounds.right - 59, bounds.bottom - 24, "Cancel", canvas_text);
}

static int ReadCitySpanList(const Window *window, u8 order[CITY_SPAN_STYLE_COUNT]) {
  int count = 0;
  for (int style = 0; style < CITY_SPAN_STYLE_COUNT; ++style) {
    if (!(window->span_styles & (1 << style))) continue;
    int position = count++;
    while (position > 0) {
      int previous = order[position - 1];
      int compare = window->span_sort == CITY_SPAN_SORT_NAME ? strcmp(kCitySpanNames[style], kCitySpanNames[previous]) :
          window->span_prices[style] < window->span_prices[previous] ? -1 :
          window->span_prices[style] > window->span_prices[previous];
      if (window->span_descending ? compare <= 0 : compare >= 0) break;
      order[position] = previous;
      --position;
    }
    order[position] = style;
  }
  return count;
}

static int CitySpanVisibleRows(const Window *window) {
  return IntMax(1, (window->bounds.bottom - window->bounds.top - CITY_SPAN_LIST_TOP -
      CITY_SPAN_FOOTER_HEIGHT) / CITY_SPAN_ROW_HEIGHT);
}

// Nonnegative controls are visible row numbers; negative values are header/scroll controls.
static CityRect CitySpanControlBounds(const Window *window, int control) {
  CityRect b = window->bounds;
  if (control >= 0) {
    int top = b.top + CITY_SPAN_LIST_TOP + control * CITY_SPAN_ROW_HEIGHT;
    return (CityRect){b.left + 1, top, b.right - 12, top + CITY_SPAN_ROW_HEIGHT};
  }
  switch (control) {
  case CITY_CANVAS_SPAN_HIT_DIRECTION: return (CityRect){b.left + 1, b.top + 14, b.left + 65, b.top + 26};
  case CITY_CANVAS_SPAN_HIT_SORT: return (CityRect){b.left + 65, b.top + 14, b.right - 12, b.top + 26};
  case CITY_CANVAS_SPAN_HIT_UP: return (CityRect){b.right - 12, b.top + 14, b.right, b.top + 26};
  case CITY_CANVAS_SPAN_HIT_DOWN: return (CityRect){b.right - 12, b.bottom - 26, b.right, b.bottom - 14};
  case CITY_CANVAS_SPAN_HIT_TRACK: return (CityRect){b.right - 12, b.top + 26, b.right, b.bottom - 26};
  case CITY_CANVAS_SPAN_HIT_RESTORE: return (CityRect){b.right - 11, b.top, b.right, b.top + 14};
  default: return (CityRect){0};
  }
}

static CityRect CitySpanScrollThumb(const Window *window) {
  CityRect track = CitySpanControlBounds(window, CITY_CANVAS_SPAN_HIT_TRACK);
  u8 order[CITY_SPAN_STYLE_COUNT];
  int count = ReadCitySpanList(window, order), visible = CitySpanVisibleRows(window);
  CityRect thumb = ReadCityScrollbar(track, count, visible, window->span_scroll, 8).thumb;
  if (count > visible) { ++thumb.left; --thumb.right; }
  return thumb;
}

static void DrawBridgeArrow(Canvas *canvas, CityRect b, bool up, u32 ink) {
  int x = (b.left + b.right) / 2, y = (b.top + b.bottom) / 2;
  for (int row = 0; row < 4; ++row) {
    int top = y + (up ? 1 - row : row - 2);
    CanvasFill(canvas, x - 4 + row, top, x + 4 - row, top + 1, ink);
  }
}

static void CanvasDrawSpanChoices(Canvas *canvas, const Window *window) {
  _Static_assert(CITY_BRIDGE_CHOICE_ICON_COUNT == CITY_BRIDGE_BITMAP_COUNT, "Bridge cache covers every style");
  NativeSpanDialog *dialog = window->city->game->ui->span.active;
  while (dialog->window_id != window->window_id) dialog = dialog->parent;
  CityBridgeWindowDrawing *pixels = dialog->drawing;
  const u32 face = 0xFF687B48, light = 0xFF9BB56C, dark = 0xFF3D4F29, ink = 0xFF101010;
  CityRect b = window->bounds;
  DrawCanvasFrame(canvas, b, face, light, dark, true);
  DrawCityWindowCaption(canvas, b, window->title, face, light, dark, 11);
  CityRect restore = CitySpanControlBounds(window, CITY_CANVAS_SPAN_HIT_RESTORE);
  DrawCanvasButtonFrame(canvas, restore, face, light, dark, true);
  DrawCityWindowControlIcon(canvas, restore, CITY_WINDOW_ICON_DEFAULT_SIZE, false);
  CityRect direction = CitySpanControlBounds(window, CITY_CANVAS_SPAN_HIT_DIRECTION);
  CityRect sort = CitySpanControlBounds(window, CITY_CANVAS_SPAN_HIT_SORT);
  DrawCanvasButtonFrame(canvas, direction, face, light, dark, true);
  DrawCanvasButtonFrame(canvas, sort, face, light, dark, true);
  DrawCityInterfaceText(canvas->interface_font, canvas, direction.left + 3, direction.top + 1, "Sort by", ink, false);
  DrawBridgeArrow(canvas, (CityRect){direction.right - 13, direction.top, direction.right, direction.bottom},
      window->span_descending, ink);
  DrawCityInterfaceText(canvas->interface_font, canvas, sort.left + 3, sort.top + 1,
      window->span_sort == CITY_SPAN_SORT_NAME ? "Name" : "Cost", ink, false);
  DrawBridgeArrow(canvas, (CityRect){sort.right - 12, sort.top, sort.right, sort.bottom}, false, ink);
  u8 order[CITY_SPAN_STYLE_COUNT];
  int count = ReadCitySpanList(window, order), visible = CitySpanVisibleRows(window);
  int scroll = IntMin(window->span_scroll, IntMax(0, count - visible));
  CityRect clip = canvas->clip;
  canvas->clip = IntersectCityRects(clip, (CityRect){b.left + 1, b.top + CITY_SPAN_LIST_TOP,
      b.right - 12, b.bottom - CITY_SPAN_FOOTER_HEIGHT});
  for (int row = 0; row < IntMin(visible, count - scroll); ++row) {
    int style = order[scroll + row];
    CityRect rect = CitySpanControlBounds(window, row);
    DrawCanvasButtonFrame(canvas, rect, style == window->span_selected ? 0xFF7B8D53 : face, light, dark, true);
    const CityInterfaceIcons *icons = window->city ? &window->city->game->resources->bridge_choice_icons : NULL;
    int icon = style + (window->span_rail && style >= CITY_ROAD_BRIDGE_STYLE_FIRST ?
        CITY_ROAD_BRIDGE_STYLE_COUNT : 0);
    if (icons && !DrawCityInterfaceIcon(canvas,
        &pixels->icons[icon], icons, icon,
        (CityRect){rect.left + 2, rect.top, rect.left + 48, rect.bottom}))
      window->city->game->execution_failed = true;
    DrawCityInterfaceText(canvas->interface_font, canvas, rect.left + 51, rect.top + 1,
        kCitySpanNames[style], 0xFFFFD800, true);
    char price[CITY_CURRENCY_TEXT_CAPACITY];
    if (window->city) FormatCityCurrency(window->city->game, price, window->span_prices[style]);
    else snprintf(price, sizeof(price), "$%u", window->span_prices[style]);
    DrawCityInterfaceText(canvas->interface_font, canvas, rect.left + 51, rect.top + 12,
        price, 0xFFFCFCFC, true);
  }
  canvas->clip = clip;
  CityRect track = CitySpanControlBounds(window, CITY_CANVAS_SPAN_HIT_TRACK);
  DrawCanvasFrame(canvas, track, dark, light, dark, false);
  if (count > visible)
    DrawCanvasFrame(canvas, CitySpanScrollThumb(window), face, light, dark, true);
  else DrawCanvasFrame(canvas, track, face, light, dark, false);
  for (int control = CITY_CANVAS_SPAN_HIT_UP; control >= CITY_CANVAS_SPAN_HIT_DOWN; --control) {
    CityRect rect = CitySpanControlBounds(window, control);
    bool up = control == CITY_CANVAS_SPAN_HIT_UP;
    bool disabled = up ? scroll == 0 : scroll >= count - visible;
    DrawCanvasButtonFrame(canvas, rect, face, light, dark, true);
    DrawBridgeArrow(canvas, rect, up, disabled ? dark : ink);
  }
  CityRect grip = {b.right - 12, b.bottom - 12, b.right, b.bottom};
  DrawCanvasFrame(canvas, grip, face, light, dark, true);
  for (int line = 0; line < 3; ++line)
    for (int pixel = 0; pixel < 3 + line * 2; ++pixel)
      CanvasFill(canvas, b.right - 3 - pixel, b.bottom - 3 - line * 2 + pixel,
          b.right - 2 - pixel, b.bottom - 2 - line * 2 + pixel, dark);
}

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

// Build and bind the caller without running pause or observation hooks. The returned slot is borrowed only
// until presentation; native callers attach completion before publishing this scope.
int CreateCitySpanWindow(City *city, NativeSpanDialog *dialog, int distance, int styles) {
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
    SetGameError(city->game, SC2K_ERROR_GUEST_ALLOCATION_FAILURE);
    return -1;
  }
  span->desktop->windows[index].city = city;
  span->desktop->windows[index].span_rail = rail;
  dialog->drawing = (CityBridgeWindowDrawing *)CreateCityWindowDrawing(city->drawing,
      CITY_WINDOW_DRAWING_BRIDGE_CHOICES, 0);
  if (!dialog->drawing) {
    Sc2kCanvasDesktopDismissWindow(span->desktop, span->desktop->windows[index].window_id);
    SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    return -1;
  }
  dialog->window_id = span->desktop->windows[index].window_id;
  span->active = dialog;
  return index;
}

// Pass opening values, not a borrowed dialog: hooks can close the window and complete/free the caller.
void PresentCitySpanWindow(City *city, int token, int distance, int styles) {
  ApplicationUpdateDialogPause(city);
  RefreshCityChartPanels(city);
  ObserveCityPresentation(city->game, CITY_PRESENTATION_SPAN_OPEN, token, distance, styles, CITY_SPAN_CANCEL, NULL);
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
  int opened = CreateCitySpanWindow(city, dialog, distance, styles);
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
  PresentCitySpanWindow(city, dialog->token, distance, styles);
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

// Handles this window's painting and input; paint coordinates are local to the window.
bool SpanWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    Canvas *canvas = event->canvas;
    if (window->span_styles)
      CanvasDrawSpanChoices(canvas, window);
    else Sc2kCanvasDrawSpanStyle(canvas, window->bounds, window->title, window->message);
    return true;
  }
  if (event->kind != WINDOW_INPUT && event->kind != WINDOW_DROPDOWN) return false;
  Window *target = FindCityCanvasWindow(&game->ui->desktop, window->window_id);
  if (event->kind == WINDOW_DROPDOWN) {
    target->span_sort = event->selection->value;
    target->span_scroll = 0;
    DirtySpanList(&game->ui->span, target);
    return true;
  }
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

// Return the desktop array index, or -1 when the styles/prices are invalid or opening fails.
static i32 Sc2kCanvasDesktopOpenSpanChoices(CityCanvasDesktop *desktop, CityRect bounds,
    const char *title, const char *message, u32 styles, const u32 *prices) {
  if (styles == 0 || (styles & ~CITY_SPAN_STYLES_ALL) != 0 || prices == NULL)
    return -1;
  Window window = {.kind = CITY_CANVAS_SPAN_STYLE, .bounds = bounds,
      .title = title, .message = message, .span_styles = styles};
  memcpy(window.span_prices, prices, sizeof(window.span_prices));
  for (int style = 0; style < CITY_SPAN_STYLE_COUNT; ++style)
    if (styles & (1 << style)) { window.span_selected = style; break; }
  return Sc2kCanvasDesktopOpen(desktop, window);
}

// Return a style index or a CityCanvasSpanHit sentinel.
static i32 CitySpanChoiceAt(const Window *window, i32 x, i32 y) {
  CityRect bounds = window->bounds;
  if (window->span_styles) {
    if (CanvasCloseAt(bounds, x, y)) return CITY_CANVAS_SPAN_HIT_CANCEL;
    for (int control = CITY_CANVAS_SPAN_HIT_SORT; control >= CITY_CANVAS_SPAN_HIT_RESTORE; --control)
      if (CanvasPointInRect(CitySpanControlBounds(window, control), x, y)) return control;
    u8 order[CITY_SPAN_STYLE_COUNT];
    int count = ReadCitySpanList(window, order), visible = CitySpanVisibleRows(window);
    int scroll = IntMin(window->span_scroll, IntMax(0, count - visible));
    for (int row = 0; row < IntMin(visible, count - scroll); ++row)
      if (CanvasPointInRect(CitySpanControlBounds(window, row), x, y)) return order[scroll + row];
    return CITY_CANVAS_SPAN_HIT_NONE;
  }
  i32 row = 0;
  for (i32 style = 0;
      style < (window->span_styles != 0 ? CITY_SPAN_STYLE_COUNT : CITY_ROAD_SPAN_STYLE_COUNT); ++style) {
    if (window->span_styles != 0 && (window->span_styles & (1u << style)) == 0)
      continue;
    i32 top = bounds.top + 64 + row++ * 30;
    if (x >= bounds.left + 24 && x < bounds.right - 24 && y >= top && y < top + 24)
      return style;
  }
  if ((x >= bounds.right - 68 && x < bounds.right - 12 &&
       y >= bounds.bottom - 29 && y < bounds.bottom - 10) ||
      (CanvasCloseAt(bounds, x, y)))
    return CITY_CANVAS_SPAN_HIT_CANCEL;
  return CITY_CANVAS_SPAN_HIT_NONE;
}
