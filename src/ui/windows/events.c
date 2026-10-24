#include "ui/windows/window_internal.h"

#include "assets/state.h"
#include "audio/backend.h"
#include "base/time.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/state.h"
#include "game/state.h"
#include "ui/dialogs/dialogs.h"
#include "ui/input.h"
#include "ui/menus/menu.h"
#include "ui/sdl.h"
#include "ui/state.h"

// Button feedback and repeat timing belong to host input, independently of City ticks.
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

static void HandleCityButtonRepeatInput(Game *game, const CanvasSDLEvent *input) {
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
    int px, py;
    CityInterfacePixels(game, input, &px, &py);
    Window *window = FindCityWindowAtPixels(desktop, px, py);
    if (window && window->window_id == press->window_id) {
      CityPoint point = CityWindowPointFromPixels(desktop, press->window_id, px, py);
      inside = CanvasPointInRect(press->bounds, point.x - window->bounds.left, point.y - window->bounds.top);
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
  NativeAudioClick(game->audio);
}

void BeginCityButtonFeedback(Game *game, int pixel_x, int pixel_y, double now) {
  CityCanvasDesktop *desktop = &game->ui->desktop;
  game->ui->button_feedback.beeped = false;
  CancelCityButtonRepeat(game);
  RaiseCityButton(game);
  // Dropdown entries are activated by their shared selection handler, above the owning window.
  if (desktop->dropdown.items && CanvasPointInRect(CityDropdownPixelBounds(desktop), pixel_x, pixel_y)) return;
  Window *window = FindCityWindowAtPixels(desktop, pixel_x, pixel_y);
  CityPoint point = CityWindowPointFromPixels(desktop, window ? window->window_id : 0, pixel_x, pixel_y);
  CityCanvasButtonProbe hit = {.x = point.x, .y = point.y};
  // An empty clip collects control geometry without repainting or allocating a second pixel buffer.
  Canvas probe = game->drawing->framebuffer;
  probe.clip = probe.raster.clip = (CityRect){0};
  probe.button_probe = &hit;
  probe.pressed_button = NULL;
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

// Common window chrome and unclaimed Escape keys use the same policy for every window.
// Domain procedures run first and may consume or replace that behavior.
bool DefaultWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind != WINDOW_INPUT) return false;
  const CanvasSDLEvent *input = event->input;
  CityCanvasDesktop *desktop = &game->ui->desktop;
  if (input->kind == CANVAS_SDL_POINTER_DOWN && input->button == CANVAS_SDL_BUTTON_LEFT) {
    Sc2kCanvasDesktopPointerDown(desktop, event->x, event->y);
    ApplicationUpdateDialogPause(game->city);
    return true;
  }
  if (input->kind == CANVAS_SDL_KEY_DOWN && input->key == CITY_KEY_ESCAPE) {
    if (!window->sticky) Sc2kCanvasDesktopDismissWindow(desktop, window->window_id);
    ApplicationUpdateDialogPause(game->city);
    return true;
  }
  return false;
}

// A procedure may change the window stack. Reacquire its identity before default handling.
bool SendWindowEvent(Game *game, const Window *window, const WindowEvent *event) {
  u32 id = window->window_id;
  WindowProcedure *procedure = window->procedure ? window->procedure : ReadCityWindowTraits(window->kind)->procedure;
  if (procedure(game, window, event)) return true;
  if (procedure == DefaultWindowProcedure || event->kind != WINDOW_INPUT) return false;
  Window *live = FindCityCanvasWindow(&game->ui->desktop, id);
  return !live || DefaultWindowProcedure(game, live, event);
}

// Search front to back in physical pixels; callers decide whether to move focus.
Window *FindCityWindowAtPixels(CityCanvasDesktop *desktop, int x, int y) {
  for (int i = desktop->count - 1; i >= 0; --i) {
    Window *window = &desktop->windows[i];
    if (window->visible && CanvasPointInRect(Sc2kCanvasDesktopPixelBounds(desktop, window->window_id), x, y))
      return window;
  }
  return NULL;
}

// Select one pointer target or the keyboard-focused window. Pointer capture survives
// leaving its bounds; loss of host focus is delivered to all surviving windows.
bool DispatchCityWindowInput(Game *game, const CanvasSDLEvent *input) {
  HandleCityButtonRepeatInput(game, input);
  CityCanvasDesktop *desktop = &game->ui->desktop;
  WindowEvent event = {.kind = WINDOW_INPUT, .input = input};
  if (input->kind == CANVAS_SDL_FOCUS_LOST) {
    WindowIds windows = SnapshotCityWindowIds(desktop);
    for (int i = windows.count - 1; i >= 0; --i) {
      Window *window = FindCityCanvasWindow(desktop, windows.ids[i]);
      if (window) SendWindowEvent(game, window, &event);
    }
    desktop->input_capture_window = desktop->hover_window = 0;
    Sc2kCanvasDesktopPointerUp(desktop);
    return false;
  }
  bool pointer = input->kind == CANVAS_SDL_POINTER_DOWN || input->kind == CANVAS_SDL_POINTER_UP ||
      input->kind == CANVAS_SDL_POINTER_MOVE;
  u32 id = 0;
  if (pointer || input->kind == CANVAS_SDL_SCROLL) {
    int x, y;
    if (pointer) CityInterfacePixels(game, input, &x, &y);
    else {
      x = desktop->tooltip.pointer_x; y = desktop->tooltip.pointer_y;
      if (game->ui->display) Sc2kSDLFramebufferPointer(game->ui->display, &x, &y);
    }
    if (pointer && desktop->dragging != CITY_CANVAS_DRAG_NONE && input->kind != CANVAS_SDL_POINTER_DOWN) {
      if (!Sc2kCanvasDesktopMovePixelDrag(desktop, x, y)) {
        CanvasSDLEvent point = TransformCityInterfaceEvent(game, input);
        Sc2kCanvasDesktopPointerMove(desktop, point.x, point.y);
      }
      if (input->kind == CANVAS_SDL_POINTER_UP && input->button == desktop->input_capture_button) {
        Sc2kCanvasDesktopPointerUp(desktop);
        desktop->input_capture_window = 0;
      }
      return true;
    }
    if (pointer && input->kind == CANVAS_SDL_POINTER_UP && desktop->input_capture_window &&
        input->button != desktop->input_capture_button) return true;
    if (pointer) id = desktop->input_capture_window;
    if (!FindCityCanvasWindow(desktop, id)) {
      Window *hit = FindCityWindowAtPixels(desktop, x, y);
      id = hit ? hit->window_id : 0;
    }
    if (input->kind == CANVAS_SDL_POINTER_MOVE && desktop->hover_window != id) {
      Window *previous = FindCityCanvasWindow(desktop, desktop->hover_window);
      desktop->hover_window = id;
      if (previous) {
        CanvasSDLEvent leave = {.kind = CANVAS_SDL_POINTER_LEAVE};
        SendWindowEvent(game, previous, &(WindowEvent){.kind = WINDOW_INPUT, .input = &leave, .x = -1, .y = -1});
      }
    }
    if (!FindCityCanvasWindow(desktop, id)) return false;
    if (input->kind == CANVAS_SDL_POINTER_DOWN && !desktop->input_capture_window) {
      FocusCanvasWindow(desktop, id, false);
      desktop->input_capture_window = id;
      desktop->input_capture_button = input->button;
    }
    CityPoint point = CityWindowPointFromPixels(desktop, id, x, y);
    event.x = point.x; event.y = point.y;
    Window *window = FindCityCanvasWindow(desktop, id);
    SendWindowEvent(game, window, &event);
    if (input->kind == CANVAS_SDL_POINTER_DOWN) Sc2kCanvasDesktopBeginPixelDrag(desktop, x, y);
    if (input->kind == CANVAS_SDL_POINTER_UP && input->button == desktop->input_capture_button) {
      desktop->input_capture_window = 0;
      Sc2kCanvasDesktopPointerUp(desktop);
    }
    return true;
  }
  if (input->kind == CANVAS_SDL_POINTER_LEAVE) {
    id = desktop->input_capture_window ? desktop->input_capture_window : desktop->hover_window;
    desktop->hover_window = 0;
    Window *window = FindCityCanvasWindow(desktop, id);
    event.x = event.y = -1;
    return window && SendWindowEvent(game, window, &event);
  }
  for (int i = desktop->count - 1; i >= 0; --i) {
    Window *window = &desktop->windows[i];
    if (window->visible && !(ReadCityWindowTraits(window->kind)->flags & CITY_WINDOW_NO_KEYBOARD))
      return SendWindowEvent(game, window, &event);
  }
  return false;
}
