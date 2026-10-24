#include <stdlib.h>

#include "assets/state.h"
#include "base/time.h"
#include "city/state.h"
#include "drawing/bitmaps.h"
#include "drawing/maps/map_view.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/raster/canvas_raster_internal.h"
#include "drawing/state.h"
#include "game/state.h"
#include "ui/map_views.h"
#include "ui/charts/charts.h"
#include "ui/dialogs/dialogs.h"
#include "ui/dialogs/file_browser.h"
#include "ui/dialogs/game_options.h"
#include "ui/dialogs/messages.h"
#include "ui/news/newspaper.h"
#include "ui/news/newspaper_capture.h"
#include "ui/state.h"
#include "ui/startup/startup.h"
#include "ui/tools/placement.h"
#include "ui/tools/types.h"
#include "ui/windows/controls.h"
#include "ui/windows/tooltips.h"
#include "ui/windows/window_internal.h"
#include "ui/windows/windows.h"

// Window stack, composition and logical dragging.

typedef struct {
  Canvas *canvas;
  const u8 *indices;
  i32 stride;
  const u32 *palette;
  const Window *window;
  const CityCanvasDesktop *desktop;
  Game *game;
} CityCanvasPaintContext;

typedef struct {
  const CityCanvasDesktop *desktop;
  const u8 *indices;
  i32 stride;
  const u32 *palette;
  Game *game;
} CanvasFramebufferPaint;

// Drawing notification boundary. UI queues window damage separately from tile
// damage; the application feeds these pixels into the shared screen grid.
bool InvalidateCityCanvasWindow(Game *game, u32 window, const CityRect *rect) {
  Drawing *drawing = game->drawing;
  // The shared framebuffer compositor repaints every requested pixel.
  if (!drawing->map_navigation_enabled || game->ui == NULL || rect == NULL) return false;
  CityCanvasDesktop *desktop = &game->ui->desktop;
  CityRect damage = *rect;
  if (window != 0) {
    const Window *target = NULL;
    for (int index = 0; index < desktop->count; ++index)
      if (desktop->windows[index].window_id == window) target = &desktop->windows[index];
    if (target == NULL) return false;
    if (!target->visible) return true;
    CityRect bounds = Sc2kCanvasDesktopPixelBounds(desktop, window);
    int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    damage = (CityRect){IntMax(damage.left, 0) + bounds.left, IntMax(damage.top, 0) + bounds.top,
        IntMin(damage.right, width) + bounds.left, IntMin(damage.bottom, height) + bounds.top};
  }
  if (drawing->framebuffer.width > 0 && drawing->framebuffer.height > 0) {
    damage = IntersectCityRects(damage, (CityRect){0, 0, drawing->framebuffer.width, drawing->framebuffer.height});
  }
  if (window == 0) {
    // Indexed map-surface paints request presentation here. Native map raster
    // damage is explicitly published by tile/layer/view operations. This does
    // not request repainting the overlapping UI windows.
    return true;
  }
  Sc2kDirtyMarkRect(&desktop->window_damage, damage);
  return true;
}

void Sc2kDirtyClear(CityDirtyRegions *dirty) {
  dirty->count = 0;
}

void Sc2kDirtyMarkRect(CityDirtyRegions *dirty, CityRect rect) {
  if (rect.left >= rect.right || rect.top >= rect.bottom)
    return;
  for (i32 i = 0; i < dirty->count;) {
    CityRect old = dirty->rects[i];
    if (rect.right < old.left || old.right < rect.left ||
        rect.bottom < old.top || old.bottom < rect.top) {
      ++i;
      continue;
    }
    CityRect joined = UnionCityRects(rect, old);
    // Keep thin frame strips separate instead of damaging the entire window inside an L shape.
    i64 area = ((i64)rect.right - rect.left) * ((i64)rect.bottom - rect.top) +
        ((i64)old.right - old.left) * ((i64)old.bottom - old.top);
    if (((i64)joined.right - joined.left) * ((i64)joined.bottom - joined.top) > area) {
      ++i;
      continue;
    }
    rect = joined;
    dirty->rects[i] = dirty->rects[--dirty->count];
    i = 0;
  }
  if (dirty->count == 64) {
    for (i32 i = 0; i < dirty->count; ++i) {
      CityRect old = dirty->rects[i];
      rect = UnionCityRects(rect, old);
    }
    dirty->count = 0;
  }
  dirty->rects[dirty->count++] = rect;
}

CityRect CanvasWindowPaintRect(const Window *window) {
  return CityNewspaperPaintBounds(window, window->bounds);
}

static i32 CanvasSubtractRect(CityRect rect, CityRect cover, CityRect *output) {
  CityRect overlap = IntersectCityRects(rect, cover);
  if (overlap.left >= overlap.right || overlap.top >= overlap.bottom) {
    output[0] = rect;
    return 1;
  }
  i32 count = 0;
  if (rect.top < overlap.top)
    output[count++] = (CityRect){rect.left, rect.top, rect.right, overlap.top};
  if (overlap.bottom < rect.bottom)
    output[count++] = (CityRect){rect.left, overlap.bottom, rect.right, rect.bottom};
  if (rect.left < overlap.left)
    output[count++] = (CityRect){rect.left, overlap.top, overlap.left, overlap.bottom};
  if (overlap.right < rect.right)
    output[count++] = (CityRect){overlap.right, overlap.top, rect.right, overlap.bottom};
  return count;
}

void Sc2kCanvasSetWindowSnapRadius(CityCanvasDesktop *desktop, i32 radius) {
  desktop->snap_radius = radius >= 0 && radius <= 32 ? radius : 0;
}

void Sc2kCanvasSetUIScale(CityCanvasDesktop *desktop, i32 numerator, i32 denominator) {
  if (numerator > 0 && numerator <= 500 && denominator > 0 && denominator <= 1600) {
    desktop->ui_scale_numerator = numerator;
    desktop->ui_scale_denominator = denominator;
  }
}

CityRect Sc2kCanvasUIBounds(const CityCanvasDesktop *desktop, CityRect b) {
  return (CityRect){CanvasScaleFloor(b.left, desktop->ui_scale_numerator, desktop->ui_scale_denominator),
                    CanvasScaleFloor(b.top, desktop->ui_scale_numerator, desktop->ui_scale_denominator),
                    CanvasScaleCeil(b.right, desktop->ui_scale_numerator, desktop->ui_scale_denominator),
                    CanvasScaleCeil(b.bottom, desktop->ui_scale_numerator, desktop->ui_scale_denominator)};
}

// Split around opaque windows before dispatching paints, including the map behind them.
// No fixed fragment array: every visible piece is visited without truncation.
static void CanvasVisitVisibleScaled(const CityCanvasDesktop *desktop, CityRect area,
                                        i32 first_window, CityCanvasVisiblePaint paint,
                                        void *context, i32 numerator, i32 denominator) {
  if (area.left >= area.right || area.top >= area.bottom)
    return;
  for (int overlay = 0; overlay < CITY_MENU_OVERLAY_COUNT; ++overlay) {
    CityRect cover = CityMenuBounds(desktop, overlay);
    if (desktop->pixel_state.scale) {
      cover = CityMenuPixelBounds(desktop, overlay);
      cover = CanvasScaleInnerRect(cover, 1, desktop->pixel_state.map_scale);
    } else cover = CanvasScaleInnerRect(cover, numerator, denominator);
    CityRect overlap = IntersectCityRects(area, cover);
    if (overlap.left >= overlap.right || overlap.top >= overlap.bottom) continue;
    CityRect pieces[4];
    int count = CanvasSubtractRect(area, cover, pieces);
    for (int piece = 0; piece < count; ++piece)
      CanvasVisitVisibleScaled(desktop, pieces[piece], first_window, paint, context, numerator, denominator);
    return;
  }
  for (i32 index = first_window; index < desktop->count; ++index) {
    const Window *window = &desktop->windows[index];
    if (!window->visible)
      continue;
    if (CityNewspaperOpening(window)) continue;
    CityRect cover = desktop->pixel_state.scale != 0 ?
        CanvasPixelMapCover(desktop, window->window_id, window->bounds) :
        CanvasScaleInnerRect(window->bounds, numerator, denominator);
    CityRect overlap = IntersectCityRects(area, cover);
    if (overlap.left >= overlap.right || overlap.top >= overlap.bottom)
      continue;
    CityRect pieces[4];
    i32 count = CanvasSubtractRect(area, cover, pieces);
    for (i32 piece = 0; piece < count; ++piece)
      CanvasVisitVisibleScaled(desktop, pieces[piece], index + 1, paint, context, numerator,
                                  denominator);
    return;
  }
  paint(area, context);
}

static void CanvasVisitVisible(const CityCanvasDesktop *desktop, CityRect area, i32 first_window,
                                 CityCanvasVisiblePaint paint, void *context) {
  CanvasVisitVisibleScaled(desktop, area, first_window, paint, context, 1, 1);
}

void Sc2kCanvasDesktopVisitMap(const CityCanvasDesktop *desktop, CityRect area,
    CityCanvasVisiblePaint paint, void *context) {
  CanvasVisitVisibleScaled(desktop, area, 0, paint, context, desktop->ui_scale_numerator,
      desktop->ui_scale_denominator);
}

static CityRect TooltipPixelBounds(const CityCanvasDesktop *desktop) {
  if (!desktop->tooltip.visible) return (CityRect){0};
  CityRect b = desktop->tooltip.bounds;
  int scale = desktop->pixel_state.scale;
  return (CityRect){CanvasScaleCeil(b.left, scale, 100), CanvasScaleCeil(b.top, scale, 100),
      CanvasScaleCeil(b.right, scale, 100), CanvasScaleCeil(b.bottom, scale, 100)};
}

// Map caches and UI paint use physical fragments, including partially exposed
// pixels at arbitrary origins.
static void VisitCanvasPixelPieces(const CityCanvasDesktop *desktop, CityRect area, int first,
    CityCanvasVisiblePaint paint, void *context, bool repaint) {
  if (area.left >= area.right || area.top >= area.bottom) return;
  int menus = desktop->count;
  for (int index = first; index < menus + CITY_MENU_OVERLAY_COUNT + 2; ++index) {
    CityRect cover;
    if (index == menus + CITY_MENU_OVERLAY_COUNT + 1) {
      cover = CityDropdownPixelBounds(desktop);
    } else if (index == menus + CITY_MENU_OVERLAY_COUNT) {
      cover = TooltipPixelBounds(desktop);
    } else if (index >= menus) {
      int overlay = index - menus;
      // Toolbar/status controls are cheap to composite after a map repaint.
      // Scroll caches and window paints still exclude their opaque pixels.
      if (repaint && overlay != 1) continue;
      cover = CityMenuPixelBounds(desktop, overlay);
    } else {
      if (!desktop->windows[index].visible) continue;
      if (CityNewspaperOpening(&desktop->windows[index])) continue;
      cover = Sc2kCanvasDesktopPixelBounds(desktop, desktop->windows[index].window_id);
    }
    CityRect overlap = IntersectCityRects(area, cover);
    if (overlap.left >= overlap.right || overlap.top >= overlap.bottom) continue;
    CityRect pieces[4];
    int count = CanvasSubtractRect(area, cover, pieces);
    for (int i = 0; i < count; ++i)
      VisitCanvasPixelPieces(desktop, pieces[i], index + 1, paint, context, repaint);
    return;
  }
  paint(area, context);
}
static void VisitCityMapPixels(const CityCanvasDesktop *desktop, u32 window_id, CityRect area,
    CityCanvasVisiblePaint paint, void *context, bool repaint) {
  if (!window_id) { VisitCanvasPixelPieces(desktop, area, 0, paint, context, repaint); return; }
  for (int index = 0; index < desktop->count; ++index) {
    const Window *window = &desktop->windows[index];
    if (window->window_id != window_id || !window->visible || window->shaded) continue;
    CityRect b = Sc2kCanvasDesktopPixelBounds(desktop, window_id);
    int scale = desktop->pixel_state.scale ? desktop->pixel_state.scale : 100;
    CityRect content = {b.left + (3 * scale + 99) / 100, b.top + (17 * scale + 99) / 100,
        b.right - (3 * scale + 99) / 100,
        b.bottom - ((window->viewport_pixels ? 3 : CITY_VIEWPORT_BOTTOM) * scale + 99) / 100};
    VisitCanvasPixelPieces(desktop, IntersectCityRects(area, content), index + 1, paint, context, repaint);
    return;
  }
}

void VisitCanvasMapPixels(const CityCanvasDesktop *desktop, u32 window_id, CityRect area,
    CityCanvasVisiblePaint paint, void *context) {
  VisitCityMapPixels(desktop, window_id, area, paint, context, false);
}

void VisitCanvasMapRepaintPixels(const CityCanvasDesktop *desktop, u32 window_id, CityRect area,
    CityCanvasVisiblePaint paint, void *context) {
  VisitCityMapPixels(desktop, window_id, area, paint, context, true);
}

typedef struct {
  const CityCanvasDesktop *desktop;
  CityCanvasVisiblePaint paint;
  void *context;
  int origin_x, origin_y, scale;
} ViewportVisibility;

static void VisitViewportPieces(CityRect area, int first, const ViewportVisibility *view) {
  if (area.left >= area.right || area.top >= area.bottom) return;
  for (int index = first; index < view->desktop->count + CITY_MENU_OVERLAY_COUNT; ++index) {
    CityRect pixels;
    if (index >= view->desktop->count) {
      pixels = CityMenuPixelBounds(view->desktop, index - view->desktop->count);
    } else {
      if (!view->desktop->windows[index].visible) continue;
      if (CityNewspaperOpening(&view->desktop->windows[index])) continue;
      pixels = Sc2kCanvasDesktopPixelBounds(view->desktop, view->desktop->windows[index].window_id);
    }
    CityRect cover = {CanvasScaleCeil(pixels.left - view->origin_x, 100, view->scale),
        CanvasScaleCeil(pixels.top - view->origin_y, 100, view->scale),
        CanvasScaleFloor(pixels.right - view->origin_x, 100, view->scale),
        CanvasScaleFloor(pixels.bottom - view->origin_y, 100, view->scale)};
    CityRect overlap = IntersectCityRects(area, cover);
    if (overlap.left >= overlap.right || overlap.top >= overlap.bottom) continue;
    CityRect pieces[4];
    int count = CanvasSubtractRect(area, cover, pieces);
    for (int piece = 0; piece < count; ++piece)
      VisitViewportPieces(pieces[piece], index + 1, view);
    return;
  }
  view->paint(area, view->context);
}

void Sc2kCanvasDesktopVisitViewport(const CityCanvasDesktop *desktop, u32 window_id, CityRect area,
    CityCanvasVisiblePaint paint, void *context) {
  for (int index = 0; index < desktop->count; ++index) {
    const Window *window = &desktop->windows[index];
    if (window->window_id != window_id || !window->visible || window->shaded) continue;
    CityRect pixels = Sc2kCanvasDesktopPixelBounds(desktop, window_id);
    int ui_scale = desktop->pixel_state.scale != 0 ? desktop->pixel_state.scale : 100;
    int scale = IntMax(1, desktop->pixel_state.map_scale) * 100;
    ViewportVisibility view = {desktop, paint, context, pixels.left + CanvasScaleCeil(3, ui_scale, 100),
        pixels.top + CanvasScaleCeil(17, ui_scale, 100), scale};
    if (!window->viewport_pixels) {
      CityRect content = CityViewportContentBounds(window);
      int width = CanvasScaleCeil(content.right - content.left, ui_scale, scale);
      int height = CanvasScaleCeil(content.bottom - content.top, ui_scale, scale);
      area = IntersectCityRects(area, (CityRect){0, 0, width, height});
    }
    if (desktop->pixel_state.scale != 0) {
      CityRect visible = {CanvasScaleFloor(-view.origin_x, 100, scale),
          CanvasScaleFloor(-view.origin_y, 100, scale),
          CanvasScaleCeil(desktop->pixel_state.width - view.origin_x, 100, scale),
          CanvasScaleCeil(desktop->pixel_state.height - view.origin_y, 100, scale)};
      area = IntersectCityRects(area, visible);
    }
    VisitViewportPieces(area, index + 1, &view);
    return;
  }
}

void Sc2kCanvasDesktopInit(CityCanvasDesktop *desktop) {
  desktop->game = NULL;
  desktop->input_capture_window = desktop->hover_window = 0;
  desktop->input_capture_button = 0;
  desktop->menu = (CityMenuState){0};
  desktop->interface_font = NULL;
  desktop->pixel_state = (CityCanvasWindowPixelState){0};
  desktop->ui_scale_numerator = desktop->ui_scale_denominator = 1;
  desktop->snap_radius = 0;
  desktop->count = 0;
  desktop->next_window_id = 0;
  desktop->dragging = CITY_CANVAS_DRAG_NONE;
  desktop->drag_x = 0;
  desktop->drag_y = 0;
  desktop->selected_tool = CITY_CANVAS_TOOL_NONE;
  desktop->canvas_width = 0;
  desktop->canvas_height = 0;
  Sc2kDirtyClear(&desktop->dirty);
  Sc2kDirtyClear(&desktop->window_damage);
  memset(&desktop->placement, 0, sizeof(desktop->placement));
  desktop->map_zoom = CITY_CANVAS_MAP_ZOOM_MEDIUM;
  desktop->dropdown = (CityDropdown){0};
  desktop->tooltip = (CityTooltip){0};
}

i32 Sc2kCanvasDesktopOpen(CityCanvasDesktop *desktop, Window window) {
  const CityWindowTraits *traits = ReadCityWindowTraits(window.kind);
  // Persistent panels have one instance. Command prompts and startup dialogs retain
  // separate callers, and additional map viewports are deliberately independent windows.
  if (traits->flags & CITY_WINDOW_SINGLETON) {
    int existing = FocusCityCanvasWindowKind(desktop, window.kind);
    if (existing >= 0) return existing;
  }
  if (desktop->count == CITY_CANVAS_WINDOW_CAPACITY)
    return -1;
  if (!window.procedure) window.procedure = traits->procedure;
  if (window.portrait || (desktop->game && traits->picture_images)) {
    int images = IntMax(1, traits->picture_images);
    window.picture_drawing = (CitySpriteDrawing *)CreateCityWindowDrawing(desktop->game->drawing,
        CITY_WINDOW_DRAWING_SPRITE, images);
    if (!window.picture_drawing) return -1;
  }
  window.visible = 1;
  window.window_id = ++desktop->next_window_id;
  window.update_categories |= traits->update_categories;
  desktop->information_updates |= window.update_categories;
  i32 index = desktop->count;
  // Windows without keyboard focus join the bottom without disturbing an existing editor.
  if (traits->flags & CITY_WINDOW_NO_KEYBOARD) {
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

bool AttachCityWindowCallback(Window *window, CityWindowCallback *callback, void *context) {
  if (!callback) return false;
  CityWindowCompletion *completion = malloc(sizeof(*completion));
  if (!completion) return false;
  *completion = (CityWindowCompletion){window->completion, callback, context};
  window->completion = completion;
  return true;
}

// The record has left the stack. Its owned data stays alive through WINDOW_DESTROY.
static void ReleaseClosedCityWindow(CityCanvasDesktop *desktop, Window *window) {
  if (desktop->game && desktop->interface->button_feedback.window_id == window->window_id)
    CancelCityButtonRepeat(desktop->game);
  if (desktop->input_capture_window == window->window_id) desktop->input_capture_window = 0;
  if (desktop->hover_window == window->window_id) desktop->hover_window = 0;
  if (desktop->game) SendWindowEvent(desktop->game, window, &(WindowEvent){.kind = WINDOW_DESTROY});
  while (window->completion) {
    CityWindowCompletion *completion = window->completion;
    window->completion = completion->next;
    completion->next = desktop->interface->closed_windows;
    desktop->interface->closed_windows = completion;
  }
  if (window->command_error && window->city && window->city->game->ui->error_tile_window == window->window_id) {
    SetCityErrorTile(window->city->game, INVALID_TILE);
    window->city->game->ui->error_tile_window = 0;
  }
  if (window->picture_drawing) ReleaseCityWindowDrawing(&window->picture_drawing->base);
  window->picture_drawing = NULL;
  free(window->owned_message);
  window->owned_message = NULL;
  if (window->newspaper) {
    NativeReleaseNewspaperPage(window->newspaper);
    free(window->newspaper);
    window->newspaper = NULL;
  }
  if (window->confirmation && window->confirmation->window_id == window->window_id)
    window->confirmation->window_id = 0;
  if (window->advisor_question) {
    CityInterfaceState *ui = desktop->interface;
    window->advisor_question->next = ui->closed_advisor_questions;
    ui->closed_advisor_questions = window->advisor_question;
  }
}

void CompleteCityWindowCallbacks(City *city) {
  if (city->game->command_depth) return;
  CityInterfaceState *ui = city->game->ui;
  while (ui->closed_windows) {
    CityWindowCompletion *closed = ui->closed_windows, *pending = NULL;
    ui->closed_windows = NULL;
    // Resume in dismissal order, including windows closed together by replacement.
    while (closed) {
      CityWindowCompletion *next = closed->next;
      closed->next = pending;
      pending = closed;
      closed = next;
    }
    while (pending) {
      CityWindowCompletion *next = pending->next;
      CityWindowCallback *callback = pending->callback;
      void *context = pending->context;
      free(pending);
      callback(city, context);
      pending = next;
    }
  }
}

bool Sc2kCanvasDesktopDismissTop(CityCanvasDesktop *desktop) {
  if (!desktop->count) return false;
  return Sc2kCanvasDesktopDismissWindow(desktop, desktop->windows[desktop->count - 1].window_id);
}

bool Sc2kCanvasDesktopDismissWindow(CityCanvasDesktop *desktop, u32 window_id) {
  if (desktop->dropdown.window_id == window_id) CloseCityDropdown(desktop);
  for (int index = 0; index < desktop->count; ++index) {
    if (desktop->windows[index].window_id != window_id) continue;
    Window closing = desktop->windows[index];
    CityRect paint = CanvasWindowPaintRect(&closing);
    Sc2kDirtyMarkRect(&desktop->window_damage, CityCanvasDirtyPixelBounds(desktop, paint));
    Sc2kDirtyMarkRect(&desktop->dirty, paint);
    for (int next = index; next < desktop->count - 1; ++next)
      desktop->windows[next] = desktop->windows[next + 1];
    --desktop->count;
    desktop->dragging = CITY_CANVAS_DRAG_NONE;
    // The procedure may open or close other windows. No stack entry is borrowed across it.
    ReleaseClosedCityWindow(desktop, &closing);
    return true;
  }
  return false;
}

static void DirtyWindowHighlight(CityCanvasDesktop *desktop, const Window *window) {
  CityRect b = Sc2kCanvasDesktopPixelBounds(desktop, window->window_id);
  int thickness = CanvasScaleCeil(1, desktop->pixel_state.scale ? desktop->pixel_state.scale : 100, 100);
  CityRect edges[] = {{b.left, b.top, b.right, b.top + thickness},
      {b.left, b.bottom - thickness, b.right, b.bottom},
      {b.left, b.top + thickness, b.left + thickness, b.bottom - thickness},
      {b.right - thickness, b.top + thickness, b.right, b.bottom - thickness}};
  for (int i = 0; i < 4; ++i) Sc2kDirtyMarkRect(&desktop->window_damage, edges[i]);
}

bool PollCityWindowHighlights(CityCanvasDesktop *desktop, u32 milliseconds) {
  // OpenTTD keeps the white border for three 30 ms window ticks.
  enum { WINDOW_HIGHLIGHT_MILLISECONDS = 90 };
  bool dirty = false;
  for (int i = 0; i < desktop->count; ++i) {
    Window *window = &desktop->windows[i];
    if (window->highlight_pending) {
      window->highlight_pending = false;
      window->highlighted = true;
      window->highlighted_at = milliseconds;
    } else if (window->highlighted &&
        (u32)(milliseconds - window->highlighted_at) >= WINDOW_HIGHLIGHT_MILLISECONDS) {
      window->highlighted = false;
    } else continue;
    DirtyWindowHighlight(desktop, window);
    dirty = true;
  }
  return dirty;
}

WindowIds SnapshotCityWindowIds(const CityCanvasDesktop *desktop) {
  WindowIds windows = {.count = desktop->count};
  for (int index = 0; index < windows.count; ++index) windows.ids[index] = desktop->windows[index].window_id;
  return windows;
}

Window *FindCityCanvasWindow(CityCanvasDesktop *desktop, u32 id) {
  for (int index = 0; index < desktop->count; ++index)
    if (desktop->windows[index].window_id == id) return &desktop->windows[index];
  return NULL;
}

int FocusCanvasWindow(CityCanvasDesktop *desktop, u32 id, bool highlight) {
  for (int index = desktop->count - 1; index >= 0; --index) {
    Window window = desktop->windows[index];
    if (!window.visible || window.window_id != id) continue;
    if (highlight) window.highlight_pending = true;
    else {
      if (window.highlighted || window.highlight_pending) DirtyWindowHighlight(desktop, &window);
      window.highlighted = window.highlight_pending = false;
    }
    if (index != desktop->count - 1) {
      for (int next = index; next < desktop->count - 1; ++next)
        desktop->windows[next] = desktop->windows[next + 1];
      desktop->windows[desktop->count - 1] = window;
      if (desktop->dragging == index) desktop->dragging = desktop->count - 1;
      else if (desktop->dragging > index) --desktop->dragging;
      Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(&window));
    } else desktop->windows[index] = window;
    return desktop->count - 1;
  }
  return -1;
}

int FocusCityCanvasWindow(CityCanvasDesktop *desktop, u32 id) {
  return FocusCanvasWindow(desktop, id, true);
}

int FocusCityCanvasWindowKind(CityCanvasDesktop *desktop, WindowKind kind) {
  for (int i = desktop->count - 1; i >= 0; --i)
    if (desktop->windows[i].visible && desktop->windows[i].kind == kind)
      return FocusCityCanvasWindow(desktop, desktop->windows[i].window_id);
  return -1;
}

static u32 FocusCanvasAt(CityCanvasDesktop *desktop, i32 x, i32 y) {
  for (int index = desktop->count - 1; index >= 0; --index) {
    const Window *window = &desktop->windows[index];
    if (!window->visible || !CanvasPointInRect(window->bounds, x, y)) continue;
    u32 id = window->window_id;
    FocusCanvasWindow(desktop, id, false);
    return id;
  }
  return 0;
}

bool Sc2kCanvasDesktopPointerDown(CityCanvasDesktop *desktop, i32 x, i32 y) {
  desktop->resizing = false;
  desktop->dragging = CITY_CANVAS_DRAG_NONE;
  if (FocusCanvasAt(desktop, x, y) != 0) {
    Window window = desktop->windows[desktop->count - 1];
    CityRect bounds = window.bounds;
    if (CityWindowCloseAt(&window, x, y)) {
      Sc2kCanvasDesktopDismissTop(desktop);
      return true;
    }
    if (CanvasPointInRect(CityWindowResizeGrip(&window), x, y)) {
      desktop->resizing = true;
      desktop->dragging = desktop->count - 1;
      desktop->drag_x = bounds.right - x;
      desktop->drag_y = bounds.bottom - y;
      return true;
    }
    if (y >= bounds.top && y < bounds.top + 14) {
      desktop->dragging = desktop->count - 1;
      desktop->drag_x = x - bounds.left;
      desktop->drag_y = y - bounds.top;
    }
    return true;
  }
  return false;
}

void CanvasSnapCandidate(i32 target, i32 original, i32 *snapped, i32 *radius) {
  i64 distance = (i64)target - original;
  if (distance < 0)
    distance = -distance;
  if (distance <= *radius) {
    *snapped = target;
    *radius = (i32)distance;
  }
}

void CanvasSnapToRect(CityRect other, i32 width, i32 height, i32 x, i32 y, i32 *left,
                                i32 *top, i32 *horizontal, i32 *vertical) {
  if (y + height >= other.top && y <= other.bottom) {
    CanvasSnapCandidate(other.right, x, left, horizontal);
    CanvasSnapCandidate(other.left - width, x, left, horizontal);
    CanvasSnapCandidate(other.left, x, left, horizontal);
    CanvasSnapCandidate(other.right - width, x, left, horizontal);
  }
  if (x + width >= other.left && x <= other.right) {
    CanvasSnapCandidate(other.bottom, y, top, vertical);
    CanvasSnapCandidate(other.top - height, y, top, vertical);
    CanvasSnapCandidate(other.top, y, top, vertical);
    CanvasSnapCandidate(other.bottom - height, y, top, vertical);
  }
}

bool Sc2kCanvasDesktopPointerMove(CityCanvasDesktop *desktop, i32 x, i32 y) {
  if (desktop->dragging == CITY_CANVAS_DRAG_NONE)
    return false;

  CityRect *bounds = &desktop->windows[desktop->dragging].bounds;
  if (desktop->resizing) {
    Sc2kDirtyMarkRect(&desktop->window_damage, CityCanvasDirtyPixelBounds(desktop, *bounds));
    Sc2kDirtyMarkRect(&desktop->dirty, *bounds);
    const CityWindowTraits *traits = ReadCityWindowTraits(desktop->windows[desktop->dragging].kind);
    int minimum_width = traits->minimum_width, minimum_height = traits->minimum_height;
    bounds->right = bounds->left + IntClamp(x + desktop->drag_x - bounds->left, minimum_width, 16384);
    bounds->bottom = bounds->top + IntClamp(y + desktop->drag_y - bounds->top, minimum_height, 16384);
    Sc2kDirtyMarkRect(&desktop->window_damage, CityCanvasDirtyPixelBounds(desktop, *bounds));
    Sc2kDirtyMarkRect(&desktop->dirty, *bounds);
    return true;
  }
  i64 proposed_left = (i64)x - desktop->drag_x;
  i64 proposed_top = (i64)y - desktop->drag_y;
  i32 width = bounds->right - bounds->left;
  i32 height = bounds->bottom - bounds->top;
  // Keep a fixed 32-pixel strip visible, plus a grabbable part of the title bar.
  i32 visible_width = IntMin(width, 32);
  i32 visible_height = IntMin(height, 32);
  visible_width = IntMin(visible_width, desktop->canvas_width);
  visible_height = IntMin(visible_height, desktop->canvas_height);
  i32 min_left = visible_width - width;
  i32 max_left = desktop->canvas_width - visible_width;
  i32 min_top = CityMenuBounds(desktop, 0).bottom;
  i32 max_top = IntMax(min_top, CityMenuBounds(desktop, 2).top - visible_height);
  i32 left = proposed_left < min_left ? min_left : proposed_left > max_left ? max_left : (i32)proposed_left;
  i32 top = proposed_top < min_top ? min_top : proposed_top > max_top ? max_top : (i32)proposed_top;
  if (desktop->snap_radius) {
    i32 original_x = left, original_y = top;
    i32 horizontal = desktop->snap_radius, vertical = desktop->snap_radius;
    CanvasSnapCandidate(0, original_x, &left, &horizontal);
    CanvasSnapCandidate(desktop->canvas_width - width, original_x, &left, &horizontal);
    CanvasSnapCandidate(min_top, original_y, &top, &vertical);
    CanvasSnapCandidate(CityMenuBounds(desktop, 2).top - height, original_y, &top, &vertical);
    for (i32 index = 0; index < desktop->count; ++index)
      if (index != desktop->dragging && desktop->windows[index].visible)
        CanvasSnapToRect(desktop->windows[index].bounds, width, height, original_x, original_y,
                            &left, &top, &horizontal, &vertical);
    left = IntMax(left, min_left);
    left = IntMin(left, max_left);
    top = IntMax(top, min_top);
    top = IntMin(top, max_top);
  }
  if (left == bounds->left && top == bounds->top)
    return true;
  Sc2kDirtyMarkRect(&desktop->dirty, *bounds);
  *bounds = (CityRect){left, top, left + width, top + height};
  Sc2kDirtyMarkRect(&desktop->dirty, *bounds);
  return true;
}

void Sc2kCanvasDesktopPointerUp(CityCanvasDesktop *desktop) {
  desktop->resizing = false;

  desktop->dragging = CITY_CANVAS_DRAG_NONE;
}

static void CanvasPaintVisibleMap(CityRect area, void *context) {
  CityCanvasPaintContext *paint = context;
  Canvas *canvas = paint->canvas;
  canvas->clip = area;
  if (paint->desktop->map_zoom == CITY_CANVAS_MAP_ZOOM_MAGNIFIED) {
    // Decode directly from the existing indexed map cache. Each source pixel
    // becomes a 2x2 map block, rasterized into the shared physical framebuffer.
    for (i32 y = area.top / 2; y < (area.bottom + 1) / 2; ++y)
      for (i32 x = area.left / 2; x < (area.right + 1) / 2; ++x)
        CanvasFill(canvas, x * 2, y * 2, x * 2 + 2, y * 2 + 2,
            paint->palette[paint->indices[(size_t)y * paint->stride + x]]);
  } else
    Sc2kCanvasBlitIndexed(canvas, paint->indices, paint->stride, paint->palette, area);
}

static void CanvasPaintVisibleWindow(CityRect area, void *context) {
  CityCanvasPaintContext *paint = context;
  paint->canvas->clip = area;
  DrawCityCanvasWindow(paint->game, paint->canvas, paint->window);
}

void DrawCityDesktop(Game *game, CityCanvasDesktop *desktop, Canvas *canvas,
    const u8 *map_indices, i32 map_stride, const u32 *palette) {
  desktop->canvas_width = canvas->width;
  desktop->canvas_height = canvas->height;
  CityRect original_clip = canvas->clip;
  CityCanvasPaintContext paint = {canvas, map_indices, map_stride, palette, NULL, desktop, game};
  for (i32 index = 0; index < desktop->dirty.count; ++index) {
    CityRect dirty = IntersectCityRects(desktop->dirty.rects[index], original_clip);
    Sc2kCanvasDesktopVisitMap(desktop, dirty, CanvasPaintVisibleMap, &paint);
    for (i32 window = 0; window < desktop->count; ++window) {
      const Window *current = &desktop->windows[window];
      if (!current->visible)
        continue;
      paint.window = current;
      CityRect area = IntersectCityRects(dirty, CanvasWindowPaintRect(current));
      CanvasVisitVisible(desktop, area, window + 1, CanvasPaintVisibleWindow, &paint);
    }

    canvas->clip = dirty;
    DrawCityMenu(game, canvas, desktop);
    DrawCityDropdown(desktop, canvas);
    DrawCityTooltip(desktop, canvas);
  }
  canvas->clip = original_clip;
  Sc2kDirtyClear(&desktop->dirty);
}

static void DrawCityDesktopMap(Game *game, const CityCanvasDesktop *desktop, Canvas *canvas,
    const u8 *map_indices, i32 map_stride, const u32 *palette) {
  CityRect clip = canvas->clip;
  CityCanvasPaintContext paint = {canvas, map_indices, map_stride, palette, NULL, desktop, game};
  Sc2kCanvasDesktopVisitMap(desktop, clip, CanvasPaintVisibleMap, &paint);
  canvas->clip = clip;
}

typedef struct {
  Canvas *canvas;
  const Window *window;
  CanvasRaster raster;
  CityRect pixels;
  Game *game;
} WindowPixelPaint;

static void PaintWindowPixels(CityRect area, void *context) {
  WindowPixelPaint *paint = context;
  const Window *window = paint->window;
  Canvas local = *paint->canvas;
  local.clip = (CityRect){CanvasScaleFloor(area.left - paint->pixels.left,
      paint->raster.denominator, paint->raster.numerator), CanvasScaleFloor(area.top - paint->pixels.top,
      paint->raster.denominator, paint->raster.numerator), CanvasScaleCeil(area.right - paint->pixels.left,
      paint->raster.denominator, paint->raster.numerator), CanvasScaleCeil(area.bottom - paint->pixels.top,
      paint->raster.denominator, paint->raster.numerator)};
  local.raster = paint->raster;
  local.clip.left += window->bounds.left; local.clip.right += window->bounds.left;
  local.clip.top += window->bounds.top; local.clip.bottom += window->bounds.top;
  local.raster.origin_x = -window->bounds.left;
  local.raster.origin_y = -window->bounds.top;
  local.raster.offset_x = paint->pixels.left;
  local.raster.offset_y = paint->pixels.top;
  local.raster.clip = IntersectCityRects(area, paint->raster.clip);
  DrawCityCanvasWindow(paint->game, &local, window);
}

void DrawCityDesktopUI(Game *game, const CityCanvasDesktop *desktop, Canvas *canvas) {
  Canvas ui_canvas = *canvas;
  ui_canvas.interface_font = &game->resources->interface_font;
  ui_canvas.unscaled_bevels = !game->drawing->options.scale_bevels;
  ui_canvas.scalable_font = !game->drawing->options.sprite_font;
  ui_canvas.font_antialias = game->drawing->options.font_antialias;
  canvas = &ui_canvas;
  if (!desktop->pixel_state.scale || canvas->raster.pixels != canvas->pixels) {
    for (i32 i = 0; i < desktop->count; ++i) DrawCityCanvasWindow(game, canvas, &desktop->windows[i]);
    DrawCityMenu(game, canvas, desktop);
    DrawCityDropdown(desktop, canvas);
    DrawCityTooltip(desktop, canvas);
    return;
  }
  CanvasRaster saved = canvas->raster;
  for (i32 i = 0; i < desktop->count; ++i) {
    const Window *window = &desktop->windows[i];
    if (!window->visible) continue;
    CityRect pixels = Sc2kCanvasDesktopPixelBounds(desktop, window->window_id);
    WindowPixelPaint paint = {canvas, window, saved, pixels, game};
    CityRect painted = CityNewspaperPaintBounds(window, pixels);
    VisitCanvasPixelPieces(desktop, IntersectCityRects(painted, saved.clip), i + 1,
        PaintWindowPixels, &paint, false);
  }

  DrawCityMenu(game, canvas, desktop);
  DrawCityDropdown(desktop, canvas);
  DrawCityTooltip(desktop, canvas);
}

static void CanvasFramebufferMap(Canvas *canvas, void *context) {
  CanvasFramebufferPaint *paint = context;
  DrawCityDesktopMap(paint->game, paint->desktop, canvas, paint->indices, paint->stride, paint->palette);
}

static void CanvasFramebufferWindows(Canvas *canvas, void *context) {
  CanvasFramebufferPaint *paint = context;
  DrawCityDesktopUI(paint->game, paint->desktop, canvas);
}

void DrawCityDesktopFramebuffer(Game *game, const CityCanvasDesktop *desktop, Canvas *framebuffer,
    const u8 *map_indices, i32 map_stride, const u32 *palette, i32 map_scale, i32 ui_scale) {
  CanvasFramebufferPaint paint = {desktop, map_indices, map_stride, palette, game};
  PaintScaledCanvas(framebuffer, map_scale, 1, CanvasFramebufferMap, &paint);
  PaintScaledCanvas(framebuffer, ui_scale, 100, CanvasFramebufferWindows, &paint);
}

static void PaintWindowsScaled(Canvas *canvas, void *context) {
  City *city = context;
  DrawCityDesktopUI(city->game, &city->game->ui->desktop, canvas);
}

static void CompositeCityScreenDamage(CityRect rect, void *context) {
  City *city = context;
  Drawing *drawing = city->drawing;
  drawing->framebuffer.clip = rect;
  MarkScreenDirty(&drawing->upload_damage, rect);
  PaintScaledCanvas(&drawing->framebuffer, drawing->options.scale, 100, PaintWindowsScaled, city);
  drawing->framebuffer.clip = (CityRect){0, 0, drawing->framebuffer.width, drawing->framebuffer.height};
}

static void PaintInactiveCityScreen(CityRect rect, void *context) {
  City *city = context;
  Drawing *drawing = city->drawing;
  Canvas canvas = drawing->framebuffer;
  canvas.clip = rect;
  DrawCityStartupBackdrop(city->game, &canvas, rect);
  CompositeCityScreenDamage(rect, context);
}

static void FinishCityDesktopPaint(City *city) {
  Drawing *drawing = city->drawing;
  CityInterfaceState *ui = city->game->ui;
  if (drawing->show_redraw_rects) drawing->world_damage = drawing->screen_damage;
  else ClearScreenDamage(&drawing->world_damage);
  ClearScreenDamage(&drawing->screen_damage);
  Sc2kDirtyClear(&ui->desktop.dirty);
  Sc2kDirtyClear(&ui->desktop.window_damage);
  ui->placement_dirty = false;
}

void ComposeCityStartupFrame(City *city) {
  Drawing *drawing = city->drawing;
  CityCanvasDesktop *desktop = &city->game->ui->desktop;
  if (drawing->main_cache.valid)
    MarkScreenDirty(&drawing->screen_damage, drawing->framebuffer.clip);
  drawing->main_cache.valid = false;
  drawing->map_target.canvas.clip = (CityRect){0};
  drawing->placement_painted_bounds = (CityRect){0};
  desktop->placement.count = 0;
  for (int index = 0; index < desktop->dirty.count; ++index) {
    MarkScreenDirty(&drawing->screen_damage, CityCanvasDirtyPixelBounds(desktop, desktop->dirty.rects[index]));
  }
  for (int index = 0; index < desktop->window_damage.count; ++index)
    MarkScreenDirty(&drawing->screen_damage, desktop->window_damage.rects[index]);
  VisitScreenDamage(&drawing->screen_damage, PaintInactiveCityScreen, city);
  FinishCityDesktopPaint(city);
}

void ComposeCityDesktopFrame(City *city) {
  Drawing *drawing = city->drawing;
  CityInterfaceState *ui = city->game->ui;
  Sc2kCanvasSetMapZoom(&ui->desktop, drawing->default_map_view->zoom +
      (drawing->default_map_view->magnified_large_zoom != 0));
  ClearScreenDamage(&drawing->world_damage);
  for (int index = 0; index < ui->desktop.dirty.count; ++index) {
    CityRect rect = CityCanvasDirtyPixelBounds(&ui->desktop, ui->desktop.dirty.rects[index]);
    MarkScreenDirty(&drawing->screen_damage, rect);
    MarkScreenDirty(&drawing->world_damage, rect);
  }
  for (int index = 0; index < ui->desktop.window_damage.count; ++index) {
    MarkScreenDirty(&drawing->screen_damage, ui->desktop.window_damage.rects[index]);
    MarkScreenDirty(&drawing->world_damage, ui->desktop.window_damage.rects[index]);
  }
  VisitScreenDamage(&drawing->world_damage, CompositeCityScreenDamage, city);
  drawing->framebuffer.clip = (CityRect){0, 0, drawing->framebuffer.width, drawing->framebuffer.height};
  drawing->placement_painted_bounds = CityPlacementPixelBounds(city);
  FinishCityDesktopPaint(city);
}
