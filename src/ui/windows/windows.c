#include "game/errors.h"
#include "drawing/sprites/sprites.h"
#include "city/economy/types.h"
#include "city/engine/command_types.h"
#include "city/infrastructure/types.h"
#include "ui/windows/state.h"
#include "base/time.h"
#include "game/application_internal.h"
#include <assert.h>

#include "ui/input.h"
#include "audio/backend.h"
#include <math.h>

#include "city/economy/economy.h"
#include "city/maps/map.h"
#include "city/infrastructure/infrastructure.h"
#include "game/state.h"
#include "audio/state.h"
#include "city/state.h"
#include "drawing/state.h"
#include "assets/state.h"
#include "ui/state.h"
#include "drawing/drawing_internal.h"
#include "drawing/bitmaps.h"
#include "ui/dialogs/file_browser.h"
#include "ui/dialogs/scenario.h"


CityScrollbar ReadCityScrollbar(CityRect track, int count, int visible, int first, int minimum_thumb) {
  int height = IntMax(0, track.bottom - track.top);
  int maximum = IntMax(0, count - visible);
  int thumb = maximum ? IntClamp((i64)height * visible / count, IntMin(minimum_thumb, height), height) : height;
  int top = track.top + (maximum ? (i64)(height - thumb) * IntClamp(first, 0, maximum) / maximum : 0);
  return (CityScrollbar){track, {track.left, top, track.right, top + thumb}, maximum};
}

int CityScrollbarPosition(const CityScrollbar *scrollbar, int pointer, int grab, CityScrollbarMapping mapping) {
  int travel = scrollbar->track.bottom - scrollbar->track.top;
  if (mapping != CITY_SCROLLBAR_JUMP) travel -= scrollbar->thumb.bottom - scrollbar->thumb.top;
  if (travel <= 0) return 0;
  int offset = IntClamp(pointer - grab - scrollbar->track.top, 0, travel);
  return ((i64)offset * scrollbar->maximum + (mapping == CITY_SCROLLBAR_DRAG_NEAREST ? travel / 2 : 0)) / travel;
}

// Window policy is shared by opening, hit testing, resizing and tooltips.
static const CityWindowTraits kWindowTraits[CITY_CANVAS_WINDOW_KIND_COUNT] = {
  [0] = {.procedure = DefaultWindowProcedure},
  [CITY_CANVAS_NEWSPAPER] = {.procedure = NewspaperWindowProcedure,
      .flags = CITY_WINDOW_SINGLETON | CITY_WINDOW_DONE_BUTTON},
  [CITY_CANVAS_SIGN_LIST] = {.procedure = SignWindowProcedure, .update_categories = CITY_UPDATE_SIGNS,
      .flags = CITY_WINDOW_SINGLETON | CITY_WINDOW_RESIZABLE | CITY_WINDOW_SHADEABLE,
      .minimum_width = CITY_SIGN_LIST_WIDTH, .minimum_height = 90, .default_width = CITY_SIGN_LIST_WIDTH,
      .default_height = CITY_SIGN_LIST_HEIGHT},
  [CITY_CANVAS_SIGN_EDIT] = {.procedure = SignWindowProcedure, .flags = CITY_WINDOW_SINGLETON},
  [CITY_CANVAS_POLICIES] = {.procedure = PoliciesWindowProcedure, .flags = CITY_WINDOW_SINGLETON,
      .update_categories = CITY_UPDATE_POLICIES | CITY_UPDATE_FINANCES | CITY_UPDATE_POPULATION},
  [CITY_CANVAS_BONDS] = {.procedure = BondsWindowProcedure, .flags = CITY_WINDOW_SINGLETON | CITY_WINDOW_RESIZABLE,
      .update_categories = CITY_UPDATE_FINANCES,
      .minimum_width = CITY_BOND_WINDOW_WIDTH, .minimum_height = CITY_BOND_WINDOW_MIN_HEIGHT,
      .default_width = CITY_BOND_WINDOW_WIDTH, .default_height = CITY_BOND_WINDOW_HEIGHT},
  [CITY_CANVAS_MAP_DATA] = {.procedure = MapDataWindowProcedure,
      .update_categories = CITY_UPDATE_MAP | CITY_UPDATE_UTILITIES | CITY_UPDATE_POPULATION,
      .flags = CITY_WINDOW_SINGLETON | CITY_WINDOW_RESIZABLE | CITY_WINDOW_SHADEABLE,
      .minimum_width = CITY_MAP_DATA_MIN_WIDTH, .minimum_height = CITY_MAP_DATA_MIN_HEIGHT,
      .default_width = CITY_MAP_DATA_DEFAULT_WIDTH, .default_height = CITY_MAP_DATA_DEFAULT_HEIGHT},
  [CITY_CANVAS_LAND_USE] = {.procedure = LandUseWindowProcedure,
      .update_categories = CITY_UPDATE_MAP | CITY_UPDATE_POPULATION,
      .flags = CITY_WINDOW_SINGLETON | CITY_WINDOW_DONE_BUTTON},
  [CITY_CANVAS_JUKEBOX] = {.procedure = MusicWindowProcedure, .flags = CITY_WINDOW_SINGLETON},
  [CITY_CANVAS_MUSIC_PROGRAMME] = {.procedure = MusicWindowProcedure, .flags = CITY_WINDOW_SINGLETON},
  [CITY_CANVAS_GAME_OPTIONS] = {.procedure = OptionsWindowProcedure,
      .flags = CITY_WINDOW_SINGLETON | CITY_WINDOW_RESIZABLE, .minimum_width = GAME_OPTIONS_MIN_WIDTH,
      .minimum_height = GAME_OPTIONS_HEIGHT, .default_width = GAME_OPTIONS_WIDTH,
      .default_height = GAME_OPTIONS_HEIGHT},
  [CITY_CANVAS_HISTORY] = {.procedure = ChartsWindowProcedure, .flags = CITY_WINDOW_SINGLETON,
      .update_categories = CITY_UPDATE_HISTORY},
  [CITY_CANVAS_DEMOGRAPHICS] = {.procedure = ChartsWindowProcedure, .flags = CITY_WINDOW_SINGLETON,
      .update_categories = CITY_UPDATE_POPULATION},
  [CITY_CANVAS_NEIGHBORS] = {.procedure = ChartsWindowProcedure, .flags = CITY_WINDOW_SINGLETON,
      .update_categories = CITY_UPDATE_NEIGHBORS | CITY_UPDATE_POPULATION},
  [CITY_CANVAS_INDUSTRY] = {.procedure = ChartsWindowProcedure, .flags = CITY_WINDOW_SINGLETON,
      .update_categories = CITY_UPDATE_INDUSTRY | CITY_UPDATE_FINANCES},
  [CITY_CANVAS_SCENARIO_LIST] = {.procedure = ScenarioWindowProcedure, .flags = CITY_WINDOW_SINGLETON},
  [CITY_CANVAS_ABANDON] = {.procedure = LeaveWindowProcedure, .flags = CITY_WINDOW_SINGLETON},
  [CITY_CANVAS_EXIT] = {.procedure = LeaveWindowProcedure, .flags = CITY_WINDOW_SINGLETON},
  [CITY_CANVAS_FACILITY_INFORMATION] = {.procedure = InformationWindowProcedure, .flags = CITY_WINDOW_DONE_BUTTON,
      .update_categories = CITY_UPDATE_MAP | CITY_UPDATE_UTILITIES | CITY_UPDATE_FINANCES | CITY_UPDATE_POPULATION},
  [CITY_CANVAS_CONFIRM] = {.procedure = MessageWindowProcedure, .flags = CITY_WINDOW_DONE_BUTTON},
  [CITY_CANVAS_SAVE_QUESTION] = {.procedure = FileQuestionWindowProcedure, .flags = CITY_WINDOW_DONE_BUTTON},
  [CITY_CANVAS_FILE_QUESTION] = {.procedure = FileQuestionWindowProcedure, .flags = CITY_WINDOW_DONE_BUTTON},
  [CITY_CANVAS_FILE_SAVE] = {.procedure = FileWindowProcedure, .flags = CITY_WINDOW_RESIZABLE,
      .minimum_width = CITY_FILE_BROWSER_WIDTH, .minimum_height = 180, .default_width = CITY_FILE_BROWSER_WIDTH,
      .default_height = CITY_FILE_BROWSER_HEIGHT},
  [CITY_CANVAS_FILE_OPEN] = {.procedure = FileWindowProcedure, .flags = CITY_WINDOW_RESIZABLE,
      .minimum_width = CITY_FILE_BROWSER_WIDTH, .minimum_height = 180, .default_width = CITY_FILE_BROWSER_WIDTH,
      .default_height = CITY_FILE_BROWSER_HEIGHT},
  [CITY_CANVAS_SPAN_STYLE] = {.procedure = SpanWindowProcedure, .flags = CITY_WINDOW_RESIZABLE,
      .minimum_width = CITY_SPAN_CHOICE_WIDTH,
      .minimum_height = CITY_SPAN_LIST_TOP + CITY_SPAN_ROW_HEIGHT + CITY_SPAN_FOOTER_HEIGHT,
      .default_width = CITY_SPAN_CHOICE_WIDTH,
      .default_height = CITY_SPAN_LIST_TOP + CITY_SPAN_ROW_HEIGHT + CITY_SPAN_FOOTER_HEIGHT},
  [CITY_CANVAS_MAP_VIEWPORT] = {.procedure = MapViewportWindowProcedure,
      .flags = CITY_WINDOW_RESIZABLE | CITY_WINDOW_SHADEABLE, .minimum_width = CITY_VIEWPORT_WIDTH,
      .minimum_height = 100, .default_width = CITY_VIEWPORT_WIDTH, .default_height = CITY_VIEWPORT_HEIGHT},
  [CITY_CANVAS_TILE_INFORMATION] = {.procedure = InformationWindowProcedure},
  [CITY_CANVAS_BUDGET] = {.procedure = BudgetWindowProcedure,
      .update_categories = CITY_UPDATE_FINANCES | CITY_UPDATE_POLICIES},
  [CITY_CANVAS_BUDGET_RATE] = {.procedure = BudgetRateWindowProcedure},
  [CITY_CANVAS_NOTICE] = {.procedure = MessageWindowProcedure},
  [CITY_CANVAS_ADVISOR] = {.procedure = MessageWindowProcedure},
  [CITY_CANVAS_COMMAND_ERROR] = {.procedure = MessageWindowProcedure},
  [CITY_CANVAS_LABEL_EDIT] = {.procedure = NameEditorWindowProcedure},
  [CITY_CANVAS_STADIUM_TEAM] = {.procedure = NameEditorWindowProcedure},
  [CITY_CANVAS_TOOL_STATUS] = {.procedure = ToolStatusWindowProcedure, .flags = CITY_WINDOW_NO_KEYBOARD},
  [CITY_CANVAS_STARTUP_MENU] = {.procedure = StartupWindowProcedure},
  [CITY_CANVAS_CONSOLE] = {.procedure = ConsoleWindowProcedure},
  [CITY_CANVAS_CONSTRUCTION] = {.procedure = ConstructionWindowProcedure},
};

const CityWindowTraits *ReadCityWindowTraits(WindowKind kind) {
  return &kWindowTraits[(unsigned)kind < CITY_CANVAS_WINDOW_KIND_COUNT ? kind : 0];
}

void ResetCityWindowSize(Window *window) {
  const CityWindowTraits *traits = ReadCityWindowTraits(window->kind);
  window->shaded = false;
  window->bounds.right = window->bounds.left + traits->default_width;
  window->bounds.bottom = window->bounds.top + traits->default_height;
}

bool CityWindowCloseAt(const Window *window, int x, int y) {
  if (CanvasCloseAt(window->bounds, x, y)) return true;
  if (window->message_buttons || !(ReadCityWindowTraits(window->kind)->flags & CITY_WINDOW_DONE_BUTTON)) return false;
  CityRect b = window->bounds;
  return CanvasPointInRect((CityRect){b.right - 51, b.bottom - 24, b.right - 10, b.bottom - 9}, x, y);
}

CityRect CityWindowResizeGrip(const Window *window) {
  const CityWindowTraits *traits = ReadCityWindowTraits(window->kind);
  if (!(traits->flags & CITY_WINDOW_RESIZABLE) ||
      ((traits->flags & CITY_WINDOW_SHADEABLE) && window->shaded) ||
      (window->kind == CITY_CANVAS_SPAN_STYLE && !window->span_styles)) return (CityRect){0};
  CityRect b = window->bounds;
  return (CityRect){b.right - 12, b.bottom - 12, b.right, b.bottom};
}

// Windows
// Drawing subsystem: windows.

CityRect CityViewportContentBounds(const Window *window) {
  CityRect b = window->bounds;
  return (CityRect){b.left + 3, b.top + CITY_VIEWPORT_TOP, b.right - 3,
      window->shaded ? b.top + CITY_VIEWPORT_TOP : b.bottom - CITY_VIEWPORT_BOTTOM};
}

CityRect CityViewportControlBounds(const Window *window, CityViewportControl control) {
  CityRect b = window->bounds;
  if (control >= CITY_VIEWPORT_SHADE) {
    int left = b.right - (CITY_VIEWPORT_STICKY - control + 1) * 11;
    return (CityRect){left, b.top, left + 11, b.top + 14};
  }
  int top = b.bottom - CITY_VIEWPORT_BOTTOM + 2;
  int middle = b.left + 88 + ((b.right - b.left - 88) >> 1);
  switch (control) {
  case CITY_VIEWPORT_ZOOM_IN: return (CityRect){b.left, top, b.left + 22, top + 22};
  case CITY_VIEWPORT_ZOOM_OUT: return (CityRect){b.left + 22, top, b.left + 44, top + 22};
  case CITY_VIEWPORT_ROTATE_CLOCKWISE: return (CityRect){b.left + 44, top, b.left + 66, top + 22};
  case CITY_VIEWPORT_ROTATE_COUNTERCLOCKWISE: return (CityRect){b.left + 66, top, b.left + 88, top + 22};
  case CITY_VIEWPORT_MAIN_CENTER: return (CityRect){b.left + 88, top, middle, top + 22};
  case CITY_VIEWPORT_CENTER: return (CityRect){middle, top, b.right, top + 22};
  default: return (CityRect){0};
  }
}

CityViewportControl CityViewportControlAt(const Window *window, int x, int y) {
  for (int control = CITY_VIEWPORT_ZOOM_IN; control <= CITY_VIEWPORT_STICKY; ++control) {
    if (window->shaded && control < CITY_VIEWPORT_SHADE) continue;
    if (CanvasPointInRect(CityViewportControlBounds(window, control), x, y)) return control;
  }
  return CITY_VIEWPORT_CONTROL_NONE;
}


static void DrawWindowHighlight(Canvas *canvas, const Window *window) {
  if (!window->highlighted && !window->highlight_pending) return;
  CityRect b = window->bounds;
  CanvasFill(canvas, b.left, b.top, b.right, b.top + 1, 0xFFFFFFFF);
  CanvasFill(canvas, b.left, b.bottom - 1, b.right, b.bottom, 0xFFFFFFFF);
  CanvasFill(canvas, b.left, b.top + 1, b.left + 1, b.bottom - 1, 0xFFFFFFFF);
  CanvasFill(canvas, b.right - 1, b.top + 1, b.right, b.bottom - 1, 0xFFFFFFFF);
}

const char *CityAdvisorName(CityBudgetCategory category) {
  static const char *const kNames[CITY_BUDGET_CATEGORY_COUNT] = {
    "Property tax", "Ordinances", "Bonds", "Police", "Fire", "Health", "Education", "Transit"
  };
  return kNames[category];
}

CityRect CityAdvisorButtonBounds(const Window *window, CityBudgetCategory category) {
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


static const double kButtonRepeatDelay = 0.4, kButtonRepeatInterval = 0.075;

static bool RaiseCityButton(Game *game) {
  CityButtonFeedback *press = &game->ui->button_feedback;
  if (!press->active) return false;
  Window *window = FindCityCanvasWindow(&game->ui->desktop, press->window_id);
  if (window || !press->window_id)
    Sc2kDirtyMarkRect(&game->ui->desktop.dirty, window ? CanvasWindowPaintRect(window) : press->bounds);
  press->active = false;
  return true;
}

bool PollCityButtonFeedback(Game *game, double now) {
  CityButtonFeedback *press = &game->ui->button_feedback;
  if (press->repeating) {
    CityCanvasDesktop *desktop = &game->ui->desktop;
    Window *window = FindCityCanvasWindow(desktop, press->window_id);
    // Nested dialogs, lost capture and closed windows end the original hold.
    if (!window || !window->visible || desktop->input_capture_window != press->window_id ||
        !desktop->count || desktop->windows[desktop->count - 1].window_id != press->window_id ||
        desktop->dropdown.items || desktop->menu.selected || game->delay_blocked) {
      bool dirty = press->active;
      CancelCityButtonRepeat(game);
      return dirty;
    }
    if (!press->active || now < press->repeat_at) return false;
    // Deliver one step per UI poll; stalled frames never accumulate a burst of edits.
    press->repeat_at = now + kButtonRepeatInterval;
    CanvasSDLEvent input = {.kind = CANVAS_SDL_POINTER_DOWN, .button = CANVAS_SDL_BUTTON_LEFT,
        .shift = press->shift};
    WindowEvent event = {.kind = WINDOW_INPUT, .input = &input,
        .x = window->bounds.left + (press->bounds.left + press->bounds.right) / 2,
        .y = window->bounds.top + (press->bounds.top + press->bounds.bottom) / 2};
    SendWindowEvent(game, window, &event);
    return true;
  }
  return press->active && now >= press->raise_at ? RaiseCityButton(game) : false;
}

void BeginCityButtonRepeat(Game *game, const Window *window, const WindowEvent *event) {
  CityButtonFeedback *press = &game->ui->button_feedback;
  if (!press->active || press->repeating || press->window_id != window->window_id ||
      !CanvasPointInRect(press->bounds, event->x - window->bounds.left, event->y - window->bounds.top)) return;
  press->repeating = true;
  press->shift = event->input->shift;
  press->repeat_at = MonotonicSeconds() + kButtonRepeatDelay;
}

void CancelCityButtonRepeat(Game *game) {
  CityButtonFeedback *press = &game->ui->button_feedback;
  if (!press->repeating) return;
  press->repeating = false;
  RaiseCityButton(game);
}

void HandleCityButtonRepeatInput(Game *game, const CanvasSDLEvent *input) {
  CityButtonFeedback *press = &game->ui->button_feedback;
  if (!press->repeating) return;
  if ((input->kind == CANVAS_SDL_POINTER_UP && input->button == CANVAS_SDL_BUTTON_LEFT) ||
      input->kind == CANVAS_SDL_POINTER_DOWN || input->kind == CANVAS_SDL_FOCUS_LOST ||
      input->kind == CANVAS_SDL_SCROLL) {
    CancelCityButtonRepeat(game);
    return;
  }
  press->shift = input->shift;
  if (input->kind != CANVAS_SDL_POINTER_MOVE && input->kind != CANVAS_SDL_POINTER_LEAVE) return;
  CityCanvasDesktop *desktop = &game->ui->desktop;
  bool inside = false;
  if (input->kind == CANVAS_SDL_POINTER_MOVE) {
    int px, py, x, y;
    CityInterfacePixels(game, input, &px, &py);
    Window *window = FindCityWindowAtPixels(desktop, px, py);
    if (window && window->window_id == press->window_id) {
      Sc2kCanvasDesktopPixelPointer(desktop, press->window_id, px, py, &x, &y);
      inside = CanvasPointInRect(press->bounds, x - window->bounds.left, y - window->bounds.top);
    }
  }
  if (inside == press->active) return;
  if (inside) {
    press->active = true;
    press->repeat_at = MonotonicSeconds() + kButtonRepeatDelay;
    Window *window = FindCityCanvasWindow(desktop, press->window_id);
    Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(window));
  } else RaiseCityButton(game);
}

void PlayCityButtonBeep(Game *game) {
  CityButtonFeedback *press = &game->ui->button_feedback;
  if (press->beeped) return;
  press->beeped = true;
  if (game->audio->live_open) NativeAudioClick(game->audio);
}

void BeginCityButtonFeedback(Game *game, int pixel_x, int pixel_y, double now) {
  CityCanvasDesktop *desktop = &game->ui->desktop;
  game->ui->button_feedback.beeped = false;
  CancelCityButtonRepeat(game);
  RaiseCityButton(game);
  // Dropdown entries are activated by their shared selection handler, above the owning window.
  if (desktop->dropdown.items && CanvasPointInRect(CityDropdownPixelBounds(desktop), pixel_x, pixel_y)) return;
  Window *window = NULL;
  for (int index = desktop->count - 1; index >= 0; --index) {
    Window *candidate = &desktop->windows[index];
    if (candidate->visible && CanvasPointInRect(
        Sc2kCanvasDesktopPixelBounds(desktop, candidate->window_id), pixel_x, pixel_y)) {
      window = candidate;
      break;
    }
  }
  CityCanvasButtonProbe hit = {0};
  Sc2kCanvasDesktopPixelPointer(desktop, window ? window->window_id : 0, pixel_x, pixel_y, &hit.x, &hit.y);
  // An empty clip collects control geometry without repainting or allocating a second pixel buffer.
  Canvas probe = game->drawing->framebuffer;
  probe.clip = probe.raster.clip = (CityRect){0};
  probe.button_probe = &hit;
  probe.button_feedback = NULL;
  probe.button_window_id = 0;
  probe.interface_font = &game->resources->interface_font;
  if (window) DrawCityCanvasWindow(game, &probe, window);
  else DrawCityMenu(game, &probe, desktop);
  if (!hit.found) return;
  if (window) {
    hit.bounds.left -= window->bounds.left; hit.bounds.right -= window->bounds.left;
    hit.bounds.top -= window->bounds.top; hit.bounds.bottom -= window->bounds.top;
  }
  // OpenTTD push buttons rise after seven 30 ms window ticks, including quick down/up clicks.
  game->ui->button_feedback = (CityButtonFeedback){.window_id = window ? window->window_id : 0,
      .bounds = hit.bounds, .raise_at = now + 0.210, .active = true};
  Sc2kDirtyMarkRect(&desktop->dirty, window ? CanvasWindowPaintRect(window) : hit.bounds);
  PlayCityButtonBeep(game);
}

// Enter every paint procedure with a window-local origin, including unscaled
// drawing and input-time button probes. Raster storage stays in the shared framebuffer.
void DrawCityCanvasWindow(Game *game, Canvas *canvas, const Window *window) {
  if (!window->visible) return;
  CityRect bounds = window->bounds;
  Window local_window = *window;
  local_window.bounds = (CityRect){0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top};
  Canvas local = *canvas;
  local.raster = CanvasRasterView(canvas);
  local.raster.origin_x += bounds.left;
  local.raster.origin_y += bounds.top;
  local.width = local_window.bounds.right;
  local.height = local_window.bounds.bottom;
  local.clip = (CityRect){canvas->clip.left - bounds.left, canvas->clip.top - bounds.top,
      canvas->clip.right - bounds.left, canvas->clip.bottom - bounds.top};
  local.interface_font = game->resources ? &game->resources->interface_font : NULL;
  CityButtonFeedback feedback = game->ui->button_feedback;
  local.button_feedback = &feedback;
  local.button_window_id = window->window_id;
  CityCanvasButtonProbe probe;
  if (canvas->button_probe) {
    probe = *canvas->button_probe;
    probe.x -= bounds.left; probe.y -= bounds.top;
    local.button_probe = &probe;
  }
  SendWindowEvent(game, &local_window, &(WindowEvent){.kind = WINDOW_PAINT, .canvas = &local});
  DrawWindowHighlight(&local, &local_window);
  if (canvas->button_probe && probe.found) {
    probe.bounds.left += bounds.left; probe.bounds.right += bounds.left;
    probe.bounds.top += bounds.top; probe.bounds.bottom += bounds.top;
    *canvas->button_probe = probe;
  }
}

bool Sc2kCanvasClickWindow(Window *window, i32 x, i32 y) {
  CityRect bounds = window->bounds;
  if (!window->visible || !CanvasPointInRect(bounds, x, y))
    return false;
  if (window->kind == CITY_CANVAS_TOOL_STATUS)
    return true;
  if (CityWindowCloseAt(window, x, y))
    window->visible = 0;
  return true;
}

void ResizeCityMessageBox(CityCanvasDesktop *desktop, Window *window) {
  Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(window));
  if (window->advisor_category) return;
  CityRect bounds = CityMessageBounds(desktop, window);
  window->bounds = bounds;
  Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(window));
}

int OpenCityMessageBox(CityCanvasDesktop *desktop, Window window, u32 flags) {
  int style = flags & 0x0F, buttons = flags & 0xF0;
  assert(!(flags & ~0xFF) && style >= CITY_MESSAGE_INFORMATION && style <= CITY_MESSAGE_ERROR);
  assert(buttons >= CITY_MESSAGE_NO_BUTTONS && buttons <= CITY_MESSAGE_YES_NO_CANCEL);
  window.message_style = style;
  window.message_buttons = buttons;
  if (!window.command_error) window.bounds = CityMessageBounds(desktop, &window);
  return Sc2kCanvasDesktopOpen(desktop, window);
}

i32 Sc2kCanvasDesktopOpen(CityCanvasDesktop *desktop, Window window) {
  // Persistent panels have one instance. Command prompts and startup dialogs retain
  // separate callers, and additional map viewports are deliberately independent windows.
  if (ReadCityWindowTraits(window.kind)->flags & CITY_WINDOW_SINGLETON) {
    int existing = FocusCityCanvasWindowKind(desktop, window.kind);
    if (existing >= 0) return existing;
  }
  if (desktop->count == 16)
    return -1;
  if (!window.procedure) window.procedure = ReadCityWindowTraits(window.kind)->procedure;
  if (window.portrait || (desktop->game &&
      (window.kind == CITY_CANVAS_NEWSPAPER || window.kind == CITY_CANVAS_NEIGHBORS ||
          window.kind == CITY_CANVAS_JUKEBOX))) {
    window.picture_drawing = (CitySpriteDrawing *)CreateCityWindowDrawing(desktop->game->drawing,
        window.kind == CITY_CANVAS_NEIGHBORS ? CITY_WINDOW_DRAWING_NEIGHBORS :
        window.kind == CITY_CANVAS_JUKEBOX ? CITY_WINDOW_DRAWING_MUSIC : CITY_WINDOW_DRAWING_SPRITE);
    if (!window.picture_drawing) return -1;
  }
  window.visible = 1;
  window.window_id = ++desktop->next_window_id;
  window.update_categories |= ReadCityWindowTraits(window.kind)->update_categories;
  desktop->information_updates |= window.update_categories;
  i32 index = desktop->count;
  // Automatic status updates must not take keyboard focus from an existing editor.
  if (window.kind == CITY_CANVAS_TOOL_STATUS) {
    index = 0;
    for (i32 next = desktop->count; next > 0; --next)
      desktop->windows[next] = desktop->windows[next - 1];
    if (desktop->dragging >= 0) ++desktop->dragging;
  }
  desktop->windows[index] = window;
  ++desktop->count;
  Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(&window));
  return index;
}

i32 Sc2kCanvasDesktopOpenBudget(CityCanvasDesktop *desktop, CityRect bounds,
    const CityBudgetPanel *budget) {
  int existing = FocusCityCanvasWindowKind(desktop, CITY_CANVAS_BUDGET);
  if (existing >= 0) {
    desktop->windows[existing].budget = *budget;
    Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(&desktop->windows[existing]));
    return existing;
  }
  return Sc2kCanvasDesktopOpen(desktop,
      (Window){.kind = CITY_CANVAS_BUDGET, .bounds = bounds, .budget = *budget});
}

i32 Sc2kCanvasDesktopOpenNotice(CityCanvasDesktop *desktop, CityRect bounds,
    const char *title, const char *message, u32 flags) {
  return OpenCityMessageBox(desktop,
      (Window){.kind = CITY_CANVAS_NOTICE, .bounds = bounds, .title = title, .message = message}, flags);
}

void CloseCityCommandErrorWindows(CityCanvasDesktop *desktop) {
  for (int i = desktop->count - 1; i >= 0; --i)
    if (desktop->windows[i].kind == CITY_CANVAS_COMMAND_ERROR)
      Sc2kCanvasDesktopDismissWindow(desktop, desktop->windows[i].window_id);
  desktop->interface->command_error_close_at = 0;
}

int OpenCityCommandErrorWindow(Game *game, const char *summary, const char *detail, bool retained, TileIndex tile) {
  CityCanvasDesktop *desktop = &game->ui->desktop;
  // Retained notices keep their individual lifetime; ordinary failures share one replaceable window.
  if (!retained) CloseCityCommandErrorWindows(desktop);
  char *message = FormatCityErrorReason(detail);
  Window window = {.kind = retained ? CITY_CANVAS_NOTICE : CITY_CANVAS_COMMAND_ERROR,
      .bounds = CityCommandErrorBounds(game, summary, message), .title = summary, .message = message,
      .owned_message = message,
      .command_error = true, .city = game->city};
  int index = OpenCityMessageBox(desktop, window, CITY_MESSAGE_ERROR | CITY_MESSAGE_NO_BUTTONS);
  if (index >= 0) {
    SetCityErrorTile(game, tile);
    game->ui->error_tile_window = IsMapTileValid(game->city, tile) ? desktop->windows[index].window_id : 0;
  }
  if (index < 0) free(message);
  return index;
}

i32 Sc2kCanvasDesktopOpenConfirm(CityCanvasDesktop *desktop, CityRect bounds,
    const char *title, const char *message, u32 flags) {
  return OpenCityMessageBox(desktop,
      (Window){.kind = CITY_CANVAS_CONFIRM, .bounds = bounds, .title = title, .message = message}, flags);
}

i32 Sc2kCanvasDesktopOpenSpanStyle(CityCanvasDesktop *desktop, CityRect bounds,
    const char *title, const char *message) {
  return Sc2kCanvasDesktopOpen(desktop,
      (Window){.kind = CITY_CANVAS_SPAN_STYLE, .bounds = bounds, .title = title, .message = message});
}

u32 Sc2kInfrastructureSpanPrice(int distance, int style) {
  if (style < 0 || style >= CITY_SPAN_STYLE_COUNT)
    return 0;
  return (u32)distance * kBridgeSelectionCostPerCell[style];
}

i32 Sc2kCanvasDesktopOpenSpanChoices(CityCanvasDesktop *desktop, CityRect bounds,
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

i32 CitySpanChoiceAt(const Window *window, i32 x, i32 y) {
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
  for (i32 style = 0; style < (window->span_styles != 0 ? CITY_SPAN_STYLE_COUNT : CITY_ROAD_SPAN_STYLE_COUNT); ++style) {
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

i32 Sc2kCanvasDesktopOpenNewspaper(CityCanvasDesktop *desktop, CityRect bounds,
    const char *headline, const char *article) {
  return Sc2kCanvasDesktopOpen(desktop,
      (Window){.kind = CITY_CANVAS_NEWSPAPER, .bounds = bounds, .title = headline, .message = article});
}

i32 Sc2kCanvasDesktopOpenNewspaperPage(CityCanvasDesktop *desktop, CityRect bounds,
    const CityCanvasNewspaperPage *page) {
  return Sc2kCanvasDesktopOpen(desktop,
      (Window){.kind = CITY_CANVAS_NEWSPAPER, .bounds = bounds,
          .title = "City Newspaper", .newspaper_page = *page});
}

i32 Sc2kCanvasDesktopOpenAdvisor(CityCanvasDesktop *desktop, CityRect bounds,
    const char *title, const char *message, const RunSprite *portrait, u32 flags) {
  return OpenCityMessageBox(desktop,
      (Window){.kind = CITY_CANVAS_ADVISOR, .bounds = bounds, .title = title, .message = message,
          .portrait = portrait}, flags);
}

i32 Sc2kCanvasDesktopOpenPolicies(CityCanvasDesktop *desktop, CityRect bounds, u32 policy_flags) {
  int left = (bounds.left + bounds.right - CITY_POLICY_WINDOW_WIDTH) / 2;
  int top = (bounds.top + bounds.bottom - CITY_POLICY_WINDOW_HEIGHT) / 2;
  return Sc2kCanvasDesktopOpen(desktop,
      (Window){.kind = CITY_CANVAS_POLICIES,
          .bounds = {left, top, left + CITY_POLICY_WINDOW_WIDTH, top + CITY_POLICY_WINDOW_HEIGHT},
          .title = "City Ordinances", .policy_flags = policy_flags});
}

i32 Sc2kCanvasDesktopOpenBonds(CityCanvasDesktop *desktop, CityRect bounds, int issue_rate) {
  bounds.right = bounds.left + CITY_BOND_WINDOW_WIDTH;
  bounds.bottom = bounds.top + CITY_BOND_WINDOW_HEIGHT;
  return Sc2kCanvasDesktopOpen(desktop,
      (Window){.kind = CITY_CANVAS_BONDS, .bounds = bounds,
          .title = "City Bonds", .bond_issue_rate = issue_rate});
}

i32 Sc2kCanvasDesktopOpenMapData(CityCanvasDesktop *desktop, CityRect bounds, i32 selection) {
  bounds.right = IntMax(bounds.right, bounds.left + CITY_MAP_DATA_DEFAULT_WIDTH);
  return Sc2kCanvasDesktopOpen(desktop,
      (Window){.kind = CITY_CANVAS_MAP_DATA, .bounds = bounds,
          .title = "Map Data", .data_map_selection = selection});
}

i32 Sc2kCanvasDesktopOpenLandUse(CityCanvasDesktop *desktop, CityRect bounds,
    const i16 *tile_counts) {
  Window window = {.kind = CITY_CANVAS_LAND_USE, .bounds = bounds, .title = "Land Use"};
  CalculateCityLandUse(tile_counts, window.land_use_acres, window.land_use_percent);
  return Sc2kCanvasDesktopOpen(desktop, window);
}

void Sc2kCanvasDesktopUpdateLandUse(CityCanvasDesktop *desktop, const i16 *tile_counts) {
  for (i32 index = 0; index < desktop->count; ++index) {
    Window *window = &desktop->windows[index];
    if (window->kind != CITY_CANVAS_LAND_USE)
      continue;
    u16 acres[11];
    u16 percent[11];
    CalculateCityLandUse(tile_counts, acres, percent);
    if (memcmp(acres, window->land_use_acres, sizeof(acres)) == 0 &&
        memcmp(percent, window->land_use_percent, sizeof(percent)) == 0)
      continue;
    memcpy(window->land_use_acres, acres, sizeof(acres));
    memcpy(window->land_use_percent, percent, sizeof(percent));
    Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(window));
  }
}

void Sc2kCanvasDesktopUpdateMapData(CityCanvasDesktop *desktop, i32 selection,
    const u32 *map_preview) {
  for (i32 index = 0; index < desktop->count; ++index) {
    Window *window = &desktop->windows[index];
    if (window->kind != CITY_CANVAS_MAP_DATA)
      continue;
    window->data_map_selection = selection;
    window->map_preview = map_preview;
    Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(window));
  }
}

void UpdateCityPolicyWindows(City *city, CityCanvasDesktop *desktop, u32 policy_flags) {
  const CityEconomyMonths *economy = &city->economy;
  const CityBudgetPolicyGroupTotals *group_totals = &city->game->ui->budget_policy_totals;
  for (i32 index = 0; index < desktop->count; ++index) {
    Window *window = &desktop->windows[index];
    if (window->kind != CITY_CANVAS_POLICIES)
      continue;
    window->policy_flags = policy_flags;
    for (i32 policy = 0; policy < CITY_ORDINANCE_COUNT; ++policy)
      window->policy_cost[policy] = Phase0PolicyCost(city, policy);
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
      window->bond_scroll = IntClamp(window->bond_scroll, 0, IntMax(0, CityBondContentRows(count) - CityBondVisibleRows(window)));
    }
    if (window->kind != CITY_CANVAS_BUDGET) continue;
    window->budget = *budget;
    Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(window));
  }
}

// Widgets
// Drawing subsystem: widgets.

// Nested logical layouts inspired by OpenTTD's NWidget containers. No global
// scale enters measurement: the compositor and input transform share it.
CityWidgetSize Sc2kCanvasWidgetMinimum(const CityWidget *w) {
  CityWidgetSize size = {w->min_width, w->min_height};
  if (w->text && !w->count) {
    if (!size.width) {
      size.width = CanvasFontTextWidth(
          w->text, w->kind == CITY_WIDGET_BUTTON ? kCanvasHeadingFont : kCanvasFont);
      if (w->kind == CITY_WIDGET_BUTTON)
        size.width += 8;
      if (w->kind == CITY_WIDGET_TOGGLE)
        size.width += 26;
    }
    if (!size.height)
      size.height = w->kind == CITY_WIDGET_BUTTON ? 16 : 10;
  }
  i32 across = 0, along = 0;
  for (i32 i = 0; i < w->count; ++i) {
    CityWidgetSize child = Sc2kCanvasWidgetMinimum(&w->children[i]);
    i32 a = w->kind == CITY_WIDGET_ROW ? child.width : child.height;
    i32 c = w->kind == CITY_WIDGET_ROW ? child.height : child.width;
    along += a;
    across = IntMax(across, c);
  }
  if (w->count) {
    along += (w->count - 1) * w->gap;
    i32 width = (w->kind == CITY_WIDGET_ROW ? along : across) + w->padding * 2;
    i32 height = (w->kind == CITY_WIDGET_ROW ? across : along) + w->padding * 2;
    size.width = IntMax(size.width, width);
    size.height = IntMax(size.height, height);
  }
  return size;
}

static bool CanvasWidgetAssign(const CityWidget *w, CityRect b, CityWidgetLayout *out,
                                i32 capacity, i32 *count) {
  if (*count >= capacity)
    return false;
  out[(*count)++] = (CityWidgetLayout){w, b};
  CityRect inner = {b.left + w->padding, b.top + w->padding, b.right - w->padding,
                    b.bottom - w->padding};
  i32 row = w->kind == CITY_WIDGET_ROW;
  i32 available = (row ? inner.right - inner.left : inner.bottom - inner.top);
  i32 total = w->count > 0 ? (w->count - 1) * w->gap : 0, weight = 0;
  for (i32 i = 0; i < w->count; ++i) {
    CityWidgetSize size = Sc2kCanvasWidgetMinimum(&w->children[i]);
    total += row ? size.width : size.height;
    weight += w->children[i].fill;
  }
  i32 extra = available > total ? available - total : 0;
  i32 position = row ? inner.left : inner.top, consumed = 0, distributed = 0;
  for (i32 i = 0; i < w->count; ++i) {
    const CityWidget *child = &w->children[i];
    CityWidgetSize size = Sc2kCanvasWidgetMinimum(child);
    i32 length = row ? size.width : size.height;
    consumed += child->fill;
    i32 share = weight ? (i32)((i64)extra * consumed / weight) : 0;
    length += share - distributed;
    distributed = share;
    CityRect bounds = row ? (CityRect){position, inner.top, position + length, inner.bottom}
                          : (CityRect){inner.left, position, inner.right, position + length};
    if (!CanvasWidgetAssign(child, bounds, out, capacity, count))
      return false;
    position += length + w->gap;
  }
  return true;
}

i32 Sc2kCanvasWidgetLayout(const CityWidget *w, CityRect b, CityWidgetLayout *out,
                              i32 capacity) {
  if (!w || !out || capacity <= 0)
    return -1;
  CityWidgetSize minimum = Sc2kCanvasWidgetMinimum(w);
  if (b.right - b.left < minimum.width || b.bottom - b.top < minimum.height)
    return -1;
  i32 count = 0;
  return CanvasWidgetAssign(w, b, out, capacity, &count) ? count : -1;
}

const char *CityWidgetTooltipAt(const CityWidgetLayout *layout, int count, int x, int y) {
  for (int i = count - 1; i >= 0; --i) {
    const CityWidget *widget = layout[i].widget;
    if (!CanvasPointInRect(layout[i].bounds, x, y)) continue;
    if (widget->tooltip) return widget->tooltip;
    if (widget->kind == CITY_WIDGET_BUTTON || widget->kind == CITY_WIDGET_TOGGLE ||
        widget->kind == CITY_WIDGET_LABEL) return widget->text;
  }
  return NULL;
}

i32 Sc2kCanvasWidgetHit(const CityWidgetLayout *layout, i32 count, i32 x, i32 y) {
  for (i32 i = count - 1; i >= 0; --i) {
    const CityWidget *w = layout[i].widget;
    if (w->id > 0 &&
        (w->kind == CITY_WIDGET_BUTTON || w->kind == CITY_WIDGET_SLIDER ||
         w->kind == CITY_WIDGET_TOGGLE) &&
        CanvasPointInRect(layout[i].bounds, x, y))
      return w->id;
  }
  return 0;
}

// Shared dropdowns. The controller owns capture and screen damage;
// callers only supply immutable choices and apply a returned selection.
const CityDropdownColors kCityDropdownGoldColors = {
  .face = 0xFFFCB030, .light = 0xFFFFDC80, .dark = 0xFFD88C00, .pressed_face = 0xFFFCC868,
  .text = 0xFF101010,
  .disabled_face = 0xFF97835B, .disabled_light = 0xFFD3BB93, .disabled_dark = 0xFF674F2B,
  .disabled_text = 0xFF674F2B, .disabled_item_text = 0xFF636562,
  .selected_face = 0xFF101010, .selected_text = 0xFFFFFFFF
};

void DrawCityDropdownControl(Canvas *canvas, const CityDropdownColors *colors, CityRect b,
    const char *text, bool pressed, bool disabled) {
  CityRect arrow = {b.right - 12, b.top, b.right, b.bottom};
  u32 face = disabled ? colors->disabled_face : colors->face;
  u32 light = disabled ? colors->disabled_light : colors->light;
  u32 dark = disabled ? colors->disabled_dark : colors->dark;
  u32 ink = disabled ? colors->disabled_text : colors->text;
  pressed = pressed && !disabled;
  if (disabled) {
    DrawCanvasFrame(canvas, b, face, light, dark, true);
    DrawCanvasFrame(canvas, arrow, face, light, dark, true);
  } else {
    pressed |= DrawCanvasButtonFrame(canvas, b, face, light, dark, true);
    pressed |= DrawCanvasButtonFrame(canvas, arrow, pressed ? colors->pressed_face : face, light, dark, !pressed);
  }
  Canvas local = *canvas;
  local.clip = IntersectCityRects(local.clip, (CityRect){b.left + 2, b.top + 1, arrow.left - 1, b.bottom - 1});
  char shortened[256];
  int available = arrow.left - b.left - 4;
  if (CityInterfaceTextWidth(local.interface_font, text) > available) {
    int limit = available - CityInterfaceTextWidth(local.interface_font, "...");
    const char *end = text;
    while (*end) {
      const char *next = end;
      ReadCityFontCharacter(&next);
      size_t bytes = next - text;
      if (bytes > sizeof(shortened) - 4) break;
      if (CityInterfaceTextPrefixWidth(local.interface_font, text, (int)bytes) > limit) break;
      end = next;
    }
    size_t length = end - text;
    memcpy(shortened, text, length);
    memcpy(shortened + length, "...", 4);
    text = shortened;
  }
  DrawCityInterfaceTextCentered(local.interface_font, &local,
      (CityRect){b.left + 2, b.top, b.left + 2 + CityInterfaceTextWidth(local.interface_font, text), b.bottom},
      text, ink, false, 10);
  int x = (arrow.left + arrow.right) / 2, y = (b.top + b.bottom) / 2 - 1 + pressed;
  for (int row = 0; row < 4; ++row) CanvasFill(canvas, x - 4 + row, y + row, x + 4 - row, y + row + 1, ink);
}

int CityTextBoxScroll(const CityInterfaceFont *font, CityRect b, const char *text, int caret, int scroll) {
  int prefix = CityInterfaceTextPrefixWidth(font, text, caret);
  int available = IntMax(1, b.right - b.left - 4);
  scroll = IntClamp(scroll, 0, CityInterfaceTextWidth(font, text) + 8 - available);
  if (prefix < scroll) scroll = prefix;
  if (prefix + 8 > scroll + available) scroll = prefix + 8 - available;
  return IntMax(0, scroll);
}

int CityTextBoxCaretAt(const CityInterfaceFont *font, CityRect b, const char *text, int caret, int *scroll, int x) {
  *scroll = CityTextBoxScroll(font, b, text, caret, *scroll);
  return CityInterfaceTextCaretAt(font, text, x - (b.left + 2 - *scroll));
}

void DrawCityTextBox(Canvas *canvas, CityRect b, const char *text, int caret, int scroll,
    bool focused, bool selected, bool caret_visible) {
  DrawCanvasFrame(canvas, b, 0xFF101010, canvas_light, canvas_dark, false);
  Canvas local = *canvas;
  local.clip = IntersectCityRects(local.clip, (CityRect){b.left + 2, b.top + 1, b.right - 2, b.bottom - 1});
  int x = b.left + 2 - CityTextBoxScroll(local.interface_font, b, text, caret, scroll);
  int y = b.top + (b.bottom - b.top - 10) / 2;
  if (focused && selected)
    CanvasFill(&local, x, y, x + CityInterfaceTextWidth(local.interface_font, text), y + 10, 0xFF606060);
  DrawCityInterfaceText(local.interface_font, &local, x, y, text, 0xFFFFFF80, false);
  if (focused && !selected && caret_visible) {
    caret = IntClamp(caret, 0, strlen(text));
    int prefix = CityInterfaceTextPrefixWidth(local.interface_font, text, caret);
    const char *next = text + caret;
    if (*next) ReadCityFontCharacter(&next);
    int width = text[caret] ?
        CityInterfaceTextPrefixWidth(local.interface_font, text, next - text) - prefix : 6;
    CanvasFill(&local, x + prefix, y + 9, x + prefix + IntMax(1, width), y + 10, 0xFFFFFFFF);
  }
}

static const Window *DropdownOwner(const CityCanvasDesktop *desktop) {
  for (int i = 0; i < desktop->count; ++i)
    if (desktop->windows[i].visible && desktop->windows[i].window_id == desktop->dropdown.window_id)
      return &desktop->windows[i];
  return NULL;
}

CityRect CityDropdownBounds(const CityCanvasDesktop *desktop) {
  const CityDropdown *d = &desktop->dropdown;
  const Window *owner = DropdownOwner(desktop);
  if (!d->items || !owner) return (CityRect){0};
  int height = d->count * CITY_MENU_ROW_HEIGHT + 2;
  CityRect b = {owner->bounds.left + d->anchor.left, owner->bounds.top + d->anchor.bottom,
      owner->bounds.left + d->anchor.right, owner->bounds.top + d->anchor.bottom + height};
  if (b.bottom > desktop->canvas_height) {
    b.top = owner->bounds.top + d->anchor.top - height;
    b.bottom = b.top + height;
  }
  return b;
}

CityRect CityDropdownPixelBounds(const CityCanvasDesktop *desktop) {
  const Window *owner = DropdownOwner(desktop);
  if (!owner || !desktop->dropdown.items) return (CityRect){0};
  CityRect b = CityDropdownBounds(desktop);
  CityRect pixels = Sc2kCanvasDesktopPixelBounds(desktop, owner->window_id);
  int scale = desktop->pixel_state.scale ? desktop->pixel_state.scale : 100;
  return (CityRect){pixels.left + CanvasScaleCeil(b.left - owner->bounds.left, scale, 100),
      pixels.top + CanvasScaleCeil(b.top - owner->bounds.top, scale, 100),
      pixels.left + CanvasScaleCeil(b.right - owner->bounds.left, scale, 100),
      pixels.top + CanvasScaleCeil(b.bottom - owner->bounds.top, scale, 100)};
}

static void DirtyCityDropdown(CityCanvasDesktop *desktop) {
  Sc2kDirtyMarkRect(&desktop->window_damage, CityDropdownPixelBounds(desktop));
  const Window *owner = DropdownOwner(desktop);
  if (owner) {
    CityRect b = desktop->dropdown.anchor;
    b.left += owner->bounds.left; b.right += owner->bounds.left;
    b.top += owner->bounds.top; b.bottom += owner->bounds.top;
    Sc2kDirtyMarkRect(&desktop->dirty, b);
  }
}

void CloseCityDropdown(CityCanvasDesktop *desktop) {
  if (!desktop->dropdown.items) return;
  DirtyCityDropdown(desktop);
  desktop->dropdown = (CityDropdown){0};
}

void OpenCityDropdown(CityCanvasDesktop *desktop, u32 window_id, int control, CityRect anchor,
    const CityDropdownItem *items, int count, int selected, const CityDropdownColors *colors) {
  CloseCityDropdown(desktop);
  desktop->dropdown = (CityDropdown){.items = items, .colors = colors, .anchor = anchor, .window_id = window_id,
      .control = control, .count = count, .selected = selected, .hovered = -1};
  DirtyCityDropdown(desktop);
}

void SetCityDropdownChecks(CityCanvasDesktop *desktop, u32 checked, u32 disabled, const char *summary) {
  desktop->dropdown.checked = checked;
  desktop->dropdown.disabled = disabled;
  desktop->dropdown.summary = summary;
  DirtyCityDropdown(desktop);
}

static bool CityDropdownItemEnabled(const CityDropdown *dropdown, int item) {
  return item >= 0 && item < dropdown->count && dropdown->items[item].kind != CITY_DROPDOWN_SEPARATOR &&
      !(dropdown->summary && (dropdown->disabled & (1u << item)));
}

static void DrawCityDropdownMarker(Canvas *canvas, const CityDropdownColors *colors, CityRect row,
    bool circle, bool highlighted) {
  CanvasRaster raster = CanvasRasterView(canvas);
  float scale = (float)raster.numerator / raster.denominator;
  float center_x = (row.left + 5.5f + raster.origin_x) * scale + raster.offset_x;
  float center_y = ((row.top + row.bottom) * 0.5f + raster.origin_y) * scale + raster.offset_y;
  float radius = 3.75f * scale, inner_radius = 3 * scale;
  CityRect clip = CanvasRasterRect(canvas, (CityRect){row.left + 1, row.top, row.left + 10, row.bottom});
  u32 fill = highlighted ? colors->light : 0xFF000000;
  u32 rim = highlighted ? 0xFF000000 : colors->light;
  // Rasterize at final display resolution. Only the edge pixels blend, so
  // circles stay round at fractional and high UI scales without a bitmap.
  for (int y = clip.top; y < clip.bottom; ++y) for (int x = clip.left; x < clip.right; ++x) {
    float dx = fabsf(x + 0.5f - center_x), dy = fabsf(y + 0.5f - center_y);
    float distance = circle ? sqrtf(dx * dx + dy * dy) : fmaxf(dx, dy);
    int outer = IntMax(0, IntMin(255, (int)((radius + 0.5f - distance) * 255)));
    if (!outer) continue;
    int inner = IntMax(0, IntMin(255, (int)((inner_radius + 0.5f - distance) * 255)));
    size_t index = (size_t)y * canvas->stride + x;
    u32 background = canvas->pixels[index], color = 0xFF000000;
    for (int shift = 0; shift <= 16; shift += 8)
      color |= (((fill >> shift & 255) * inner + (rim >> shift & 255) * (outer - inner) +
          (background >> shift & 255) * (255 - outer) + 127) / 255) << shift;
    canvas->pixels[index] = color;
    if (canvas->palette_indices && (!canvas->palette_rows || canvas->palette_rows[y]))
      canvas->palette_indices[(size_t)y * canvas->index_stride + x] = 0;
  }
}

void DrawCityDropdown(const CityCanvasDesktop *desktop, Canvas *canvas) {
  const Window *owner = DropdownOwner(desktop);
  const CityDropdown *d = &desktop->dropdown;
  if (!owner || !d->items) return;
  const CityDropdownColors *colors = d->colors;
  CityRect b = CityDropdownBounds(desktop);
  Canvas local = *canvas;
  if (desktop->pixel_state.scale && local.raster.pixels == local.pixels) {
    CityRect pixels = Sc2kCanvasDesktopPixelBounds(desktop, owner->window_id);
    local.raster.origin_x = -owner->bounds.left; local.raster.origin_y = -owner->bounds.top;
    local.raster.offset_x = pixels.left; local.raster.offset_y = pixels.top;
  }
  CityRect anchor = d->anchor;
  anchor.left += owner->bounds.left; anchor.right += owner->bounds.left;
  anchor.top += owner->bounds.top; anchor.bottom += owner->bounds.top;
  DrawCityDropdownControl(&local, colors, anchor, d->summary ? d->summary : d->items[d->selected].text, true, false);
  DrawCanvasFrame(&local, b, colors->face, colors->light, colors->dark, true);
  local.clip = IntersectCityRects(local.clip, (CityRect){b.left + 1, b.top + 1, b.right - 1, b.bottom - 1});
  CityRect clip = local.clip;
  for (int i = 0; i < d->count; ++i) {
    CityRect row = {b.left + 1, b.top + 1 + i * CITY_MENU_ROW_HEIGHT, b.right - 1,
        b.top + 1 + (i + 1) * CITY_MENU_ROW_HEIGHT};
    local.clip = IntersectCityRects(clip, row);
    if (d->items[i].kind == CITY_DROPDOWN_SEPARATOR) {
      int y = (row.top + row.bottom) / 2;
      CanvasFill(&local, row.left + 2, y, row.right - 2, y + 1, colors->dark);
      CanvasFill(&local, row.left + 2, y + 1, row.right - 2, y + 2, colors->light);
      continue;
    }
    bool disabled = !CityDropdownItemEnabled(d, i);
    bool selected = i == d->hovered && !disabled;
    if (selected) CanvasFill(&local, row.left, row.top, row.right, row.bottom, colors->selected_face);
    if ((d->items[i].kind == CITY_DROPDOWN_RADIO || d->items[i].kind == CITY_DROPDOWN_CHECKBOX) &&
        (d->checked & (1u << i)))
      DrawCityDropdownMarker(&local, colors, row, d->items[i].kind == CITY_DROPDOWN_RADIO, selected);
    CityRect text_bounds = row;
    text_bounds.left += d->summary ? 13 : 1;
    DrawCityInterfaceTextInRow(local.interface_font, &local, text_bounds,
        d->items[i].text, disabled ? colors->disabled_item_text : selected ? colors->selected_text : colors->text);
  }
}

static void SelectCityDropdown(CityCanvasDesktop *desktop, CityDropdownSelection *selection) {
  CityDropdown *d = &desktop->dropdown;
  if (CityDropdownItemEnabled(d, d->hovered))
    *selection = (CityDropdownSelection){d->window_id, d->control, d->items[d->hovered].value};
  if (d->summary) DirtyCityDropdown(desktop);
  else CloseCityDropdown(desktop);
}

static CityRect DropdownAnchorPixelBounds(const CityCanvasDesktop *desktop) {
  const Window *owner = DropdownOwner(desktop);
  if (!owner) return (CityRect){0};
  CityRect b = Sc2kCanvasDesktopPixelBounds(desktop, owner->window_id);
  CityRect anchor = desktop->dropdown.anchor;
  int scale = desktop->pixel_state.scale ? desktop->pixel_state.scale : 100;
  return (CityRect){b.left + CanvasScaleCeil(anchor.left, scale, 100),
      b.top + CanvasScaleCeil(anchor.top, scale, 100),
      b.left + CanvasScaleCeil(anchor.right, scale, 100),
      b.top + CanvasScaleCeil(anchor.bottom, scale, 100)};
}

bool HandleCityDropdownPointer(CityCanvasDesktop *desktop, CityDropdownPointer kind, int x, int y,
    CityDropdownSelection *selection) {
  CityDropdown *d = &desktop->dropdown;
  if (!d->items) return false;
  CityRect b = CityDropdownPixelBounds(desktop);
  int scale = desktop->pixel_state.scale ? desktop->pixel_state.scale : 100;
  int offset = (y - b.top) * 100 / scale - 1;
  int row = CanvasPointInRect(b, x, y) && offset >= 0 ? offset / CITY_MENU_ROW_HEIGHT : -1;
  if (row >= d->count) row = -1;
  if (kind == CITY_DROPDOWN_MOVE) {
    CityRect anchor = DropdownAnchorPixelBounds(desktop);
    int margin = CanvasScaleCeil(CITY_DROPDOWN_DISMISS_MARGIN, scale, 100);
    if (!CanvasPointInRect(CityDropdownCaptureBounds(b, anchor, margin), x, y)) {
      CloseCityDropdown(desktop);
      return true;
    }
    int hovered = CityDropdownItemEnabled(d, row) ? row : -1;
    if (hovered != d->hovered) {
      d->hovered = hovered;
      Sc2kDirtyMarkRect(&desktop->window_damage, b);
    }
  } else if (kind == CITY_DROPDOWN_DOWN) {
    if (row < 0) CloseCityDropdown(desktop);
  } else if (row >= 0) {
    d->hovered = row;
    SelectCityDropdown(desktop, selection);
  } else {
    CityRect anchor = DropdownAnchorPixelBounds(desktop);
    if (!CanvasPointInRect(anchor, x, y)) CloseCityDropdown(desktop);
  }
  return true;
}

bool HandleCityDropdownKey(CityCanvasDesktop *desktop, CityHotkey key, CityDropdownSelection *selection) {
  CityDropdown *d = &desktop->dropdown;
  if (!d->items) return false;
  if (key == CITY_KEY_ESCAPE || key == CITY_KEY_TAB) CloseCityDropdown(desktop);
  else if (key == CITY_KEY_RETURN || key == CITY_KEY_SPACE) {
    if (d->hovered < 0) d->hovered = d->selected;
    SelectCityDropdown(desktop, selection);
  } else if (key == CITY_KEY_UP || key == CITY_KEY_DOWN || key == CITY_KEY_HOME || key == CITY_KEY_END) {
    int direction = key == CITY_KEY_UP || key == CITY_KEY_END ? -1 : 1;
    int start = key == CITY_KEY_HOME ? 0 : key == CITY_KEY_END ? d->count - 1 :
        (d->hovered < 0 ? d->selected : d->hovered) + direction;
    for (int item = start; item >= 0 && item < d->count; item += direction) {
      if (!CityDropdownItemEnabled(d, item)) continue;
      d->hovered = item;
      break;
    }
    Sc2kDirtyMarkRect(&desktop->window_damage, CityDropdownPixelBounds(desktop));
  }
  return true;
}
