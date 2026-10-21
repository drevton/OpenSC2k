#include "ui/windows/state.h"
#include "drawing/drawing_internal.h"

// Drawing subsystem: window pixels.

static const CityRect *CanvasPixelLogicalBounds(const CityCanvasDesktop *desktop, u32 id) {
  for (i32 i = 0; i < desktop->count; ++i)
    if (desktop->windows[i].window_id == id) return &desktop->windows[i].bounds;
  return NULL;
}

static CanvasPixelPosition *FindCanvasPixelPosition(CityCanvasDesktop *desktop, u32 id,
                                                  CityRect bounds) {
  if (!desktop->pixel_state.scale) return NULL;
  // Explicit positioning owns subpixel anchors; read-only queries derive movement without changing them.
  CanvasPixelPosition *available = NULL;
  for (i32 i = 0; i < 16; ++i) {
    CanvasPixelPosition *p = &desktop->pixel_state.positions[i];
    if (p->id == id) {
      p->x += CanvasScaleCeil(bounds.left, desktop->pixel_state.scale, 100) -
          CanvasScaleCeil(p->logical_x, desktop->pixel_state.scale, 100);
      p->y += CanvasScaleCeil(bounds.top, desktop->pixel_state.scale, 100) -
          CanvasScaleCeil(p->logical_y, desktop->pixel_state.scale, 100);
      p->logical_x = bounds.left; p->logical_y = bounds.top;
      return p;
    }
    if (!p->id || !CanvasPixelLogicalBounds(desktop, p->id)) available = p;
  }
  if (!available) return NULL;
  *available = (CanvasPixelPosition){id, bounds.left, bounds.top,
      CanvasScaleCeil(bounds.left, desktop->pixel_state.scale, 100),
      CanvasScaleCeil(bounds.top, desktop->pixel_state.scale, 100)};
  return available;
}

void Sc2kCanvasDesktopPixelMetrics(CityCanvasDesktop *desktop, i32 scale, i32 map_scale,
                                     i32 width, i32 height) {
  if (!desktop || scale < 100 || scale > 500 || map_scale < 1 || map_scale > 16 ||
      width < 1 || height < 1) return;
  if (!desktop->pixel_state.scale) {
    memset(&desktop->pixel_state, 0, sizeof(desktop->pixel_state));
  } else if (desktop->pixel_state.scale != scale || desktop->pixel_state.map_scale != map_scale) {
    // Preserve the sub-unit placement when logical bounds are resized by the
    // host. Normal moves/recentering still follow their new logical bounds.
    for (i32 i = 0; i < 16; ++i) {
      CanvasPixelPosition *p = &desktop->pixel_state.positions[i];
      const CityRect *b = p->id ? CanvasPixelLogicalBounds(desktop, p->id) : NULL;
      if (!b) { p->id = 0; continue; }
      i32 dx = p->x - CanvasScaleCeil(p->logical_x, desktop->pixel_state.scale, 100);
      i32 dy = p->y - CanvasScaleCeil(p->logical_y, desktop->pixel_state.scale, 100);
      p->x = CanvasScaleCeil(b->left, scale, 100) + dx;
      p->y = CanvasScaleCeil(b->top, scale, 100) + dy;
      p->logical_x = b->left; p->logical_y = b->top;
    }
    desktop->pixel_state.capture = 0;
  }
  desktop->pixel_state.scale = scale; desktop->pixel_state.map_scale = map_scale;
  desktop->pixel_state.width = width; desktop->pixel_state.height = height;
}

CityRect Sc2kCanvasDesktopPixelBounds(const CityCanvasDesktop *desktop, u32 id) {
  const CityRect *logical = CanvasPixelLogicalBounds(desktop, id);
  if (!logical) return (CityRect){0};
  if (!desktop->pixel_state.scale) return *logical;
  int scale = desktop->pixel_state.scale;
  int x = CanvasScaleCeil(logical->left, scale, 100), y = CanvasScaleCeil(logical->top, scale, 100);
  for (int i = 0; i < 16; ++i) {
    const CanvasPixelPosition *p = &desktop->pixel_state.positions[i];
    if (p->id != id) continue;
    x += p->x - CanvasScaleCeil(p->logical_x, scale, 100);
    y += p->y - CanvasScaleCeil(p->logical_y, scale, 100);
    break;
  }
  return (CityRect){x, y, x + CanvasScaleCeil(logical->right - logical->left, scale, 100),
      y + CanvasScaleCeil(logical->bottom - logical->top, scale, 100)};
}

CityRect CityCanvasDirtyPixelBounds(const CityCanvasDesktop *desktop, CityRect logical) {
  // Whole-window requests use the actual pixel anchor, including sub-unit
  // positioning. The newspaper supplies its own transformed paint bounds.
  for (int index = 0; index < desktop->count; ++index) {
    const Window *window = &desktop->windows[index];
    if (!window->visible) continue;
    CityRect paint = CanvasWindowPaintRect(window);
    if (logical.left == paint.left && logical.top == paint.top &&
        logical.right == paint.right && logical.bottom == paint.bottom)
      return CityNewspaperPaintBounds(window, Sc2kCanvasDesktopPixelBounds(desktop, window->window_id));
  }
  int scale = desktop->pixel_state.scale ? desktop->pixel_state.scale : 100;
  return CanvasScaleCover(logical, scale, 100);
}

void PositionCityWindowPixels(CityCanvasDesktop *desktop, u32 id, CityPoint position) {
  Window *window = FindCityCanvasWindow(desktop, id);
  CityRect *bounds = window ? &window->bounds : NULL;
  if (!bounds) return;
  int scale = desktop->pixel_state.scale ? desktop->pixel_state.scale : 100;
  CanvasPixelPosition *p = FindCanvasPixelPosition(desktop, id, *bounds);
  int width = bounds->right - bounds->left, height = bounds->bottom - bounds->top;
  Sc2kDirtyMarkRect(&desktop->window_damage, Sc2kCanvasDesktopPixelBounds(desktop, id));
  Sc2kDirtyMarkRect(&desktop->dirty, *bounds);
  bounds->left = CanvasScaleFloor(position.x, 100, scale);
  bounds->top = CanvasScaleFloor(position.y, 100, scale);
  bounds->right = bounds->left + width;
  bounds->bottom = bounds->top + height;
  if (p) *p = (CanvasPixelPosition){id, bounds->left, bounds->top, position.x, position.y};
  Sc2kDirtyMarkRect(&desktop->window_damage, Sc2kCanvasDesktopPixelBounds(desktop, id));
  Sc2kDirtyMarkRect(&desktop->dirty, *bounds);
}

CityRect CanvasPixelMapCover(const CityCanvasDesktop *desktop, u32 id, CityRect logical) {
  if (!desktop->pixel_state.scale)
    return CanvasScaleCover(logical, desktop->ui_scale_numerator, desktop->ui_scale_denominator);
  CityRect b = Sc2kCanvasDesktopPixelBounds(desktop, id);
  i32 scale = desktop->pixel_state.map_scale;
  return (CityRect){CanvasScaleCeil(b.left, 1, scale), CanvasScaleCeil(b.top, 1, scale),
      CanvasScaleFloor(b.right, 1, scale), CanvasScaleFloor(b.bottom, 1, scale)};
}

void Sc2kCanvasDesktopPixelPointer(const CityCanvasDesktop *desktop, u32 capture,
                                      i32 x, i32 y, i32 *logical_x, i32 *logical_y) {
  if (!desktop->pixel_state.scale) { *logical_x = x; *logical_y = y; return; }
  if (!capture) {
    for (i32 i = desktop->count - 1; i >= 0; --i)
      if (desktop->windows[i].visible && CanvasPointInRect(
          Sc2kCanvasDesktopPixelBounds(desktop, desktop->windows[i].window_id), x, y)) {
        capture = desktop->windows[i].window_id; break;
      }

  }
  const CityRect *bounds = capture ? CanvasPixelLogicalBounds(desktop, capture) : NULL;
  if (bounds) {
    CityRect pixels = Sc2kCanvasDesktopPixelBounds(desktop, capture);
    *logical_x = bounds->left + CanvasScaleFloor(x - pixels.left, 100, desktop->pixel_state.scale);
    *logical_y = bounds->top + CanvasScaleFloor(y - pixels.top, 100, desktop->pixel_state.scale);
  } else {
    *logical_x = CanvasScaleFloor(x, 100, desktop->pixel_state.scale);
    *logical_y = CanvasScaleFloor(y, 100, desktop->pixel_state.scale);
  }
}

u32 Sc2kCanvasDesktopFocusPixels(CityCanvasDesktop *desktop, i32 x, i32 y) {
  for (i32 i = desktop->count - 1; i >= 0; --i) {
    Window window = desktop->windows[i];
    if (!window.visible || !CanvasPointInRect(
        Sc2kCanvasDesktopPixelBounds(desktop, window.window_id), x, y)) continue;
    for (i32 next = i; next < desktop->count - 1; ++next) desktop->windows[next] = desktop->windows[next + 1];
    desktop->windows[desktop->count - 1] = window;
    Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(&window));
    return window.window_id;
  }
  return 0;
}

void Sc2kCanvasDesktopBeginPixelDrag(CityCanvasDesktop *desktop, i32 x, i32 y) {
  desktop->pixel_state.capture = 0;
  if (!desktop->pixel_state.scale || desktop->dragging == CITY_CANVAS_DRAG_NONE) return;
  u32 id = desktop->windows[desktop->dragging].window_id;
  CityRect b = Sc2kCanvasDesktopPixelBounds(desktop, id);
  desktop->pixel_state.capture = id;
  desktop->pixel_state.grab_x = x - b.left; desktop->pixel_state.grab_y = y - b.top;
}

bool Sc2kCanvasDesktopMovePixelDrag(CityCanvasDesktop *desktop, i32 x, i32 y) {
  if (!desktop->pixel_state.scale || !desktop->pixel_state.capture || desktop->dragging == CITY_CANVAS_DRAG_NONE) return false;
  if (desktop->resizing) {
    int logical_x, logical_y;
    Sc2kCanvasDesktopPixelPointer(desktop, desktop->pixel_state.capture, x, y, &logical_x, &logical_y);
    return Sc2kCanvasDesktopPointerMove(desktop, logical_x, logical_y);
  }
  u32 id = desktop->pixel_state.capture;
  Window *window = FindCityCanvasWindow(desktop, id);
  CityRect *bounds = window ? &window->bounds : NULL;
  if (!bounds) return false;
  CityRect pixels = Sc2kCanvasDesktopPixelBounds(desktop, id);
  i32 width = pixels.right - pixels.left, height = pixels.bottom - pixels.top;
  i32 strip = CanvasScaleCeil(32, desktop->pixel_state.scale, 100);
  i32 visible_width = IntMin(width, strip);
  i32 visible_height = IntMin(height, strip);
  visible_width = IntMin(visible_width, desktop->pixel_state.width);
  visible_height = IntMin(visible_height, desktop->pixel_state.height);
  i32 min_left = visible_width - width, max_left = desktop->pixel_state.width - visible_width;
  i32 min_top = CityMenuPixelBounds(desktop, 0).bottom;
  i32 max_top = IntMax(min_top, CityMenuPixelBounds(desktop, 2).top - visible_height);
  i64 proposed_x = (i64)x - desktop->pixel_state.grab_x, proposed_y = (i64)y - desktop->pixel_state.grab_y;
  i32 left = proposed_x < min_left ? min_left : proposed_x > max_left ? max_left : (i32)proposed_x;
  i32 top = proposed_y < min_top ? min_top : proposed_y > max_top ? max_top : (i32)proposed_y;
  if (desktop->snap_radius) {
    i32 original_x = left, original_y = top;
    i32 horizontal = CanvasScaleCeil(desktop->snap_radius, desktop->pixel_state.scale, 100), vertical = horizontal;
    CanvasSnapCandidate(0, original_x, &left, &horizontal);
    CanvasSnapCandidate(desktop->pixel_state.width - width, original_x, &left, &horizontal);
    CanvasSnapCandidate(min_top, original_y, &top, &vertical);
    CanvasSnapCandidate(CityMenuPixelBounds(desktop, 2).top - height, original_y, &top, &vertical);
    for (i32 i = 0; i < desktop->count; ++i)
      if (desktop->windows[i].visible && desktop->windows[i].window_id != id)
        CanvasSnapToRect(Sc2kCanvasDesktopPixelBounds(desktop, desktop->windows[i].window_id),
            width, height, original_x, original_y, &left, &top, &horizontal, &vertical);
    left = IntMax(left, min_left);
    left = IntMin(left, max_left);
    top = IntMax(top, min_top);
    top = IntMin(top, max_top);
  }
  PositionCityWindowPixels(desktop, id, (CityPoint){left, top});
  return true;
}
