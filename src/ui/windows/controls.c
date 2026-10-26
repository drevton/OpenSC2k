#include "ui/windows/controls.h"

#include <string.h>

#include "drawing/fonts/fonts.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/raster/canvas_raster_internal.h"
#include "ui/menus/menu.h"
#include "ui/windows/state.h"
#include "ui/windows/windows.h"

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

// Nested logical layouts inspired by OpenTTD's NWidget containers. No global
// scale enters measurement: the compositor and input transform share it.
CityWidgetSize Sc2kCanvasWidgetMinimum(const CityWidget *widget) {
  CityWidgetSize size = {widget->min_width, widget->min_height};
  if (widget->text && !widget->count) {
    if (!size.width) {
      size.width = CanvasFontTextWidth(
          widget->text, widget->kind == CITY_WIDGET_BUTTON ? kCanvasHeadingFont : kCanvasFont);
      if (widget->kind == CITY_WIDGET_BUTTON)
        size.width += 8;
      if (widget->kind == CITY_WIDGET_TOGGLE)
        size.width += 26;
    }
    if (!size.height)
      size.height = widget->kind == CITY_WIDGET_BUTTON ? 16 : 10;
  }
  int across = 0, along = 0;
  bool row = widget->kind == CITY_WIDGET_ROW;
  for (int i = 0; i < widget->count; ++i) {
    CityWidgetSize child = Sc2kCanvasWidgetMinimum(&widget->children[i]);
    int length = row ? child.width : child.height;
    int breadth = row ? child.height : child.width;
    along += length;
    across = IntMax(across, breadth);
  }
  if (widget->count) {
    along += (widget->count - 1) * widget->gap;
    int width = (row ? along : across) + widget->padding * 2;
    int height = (row ? across : along) + widget->padding * 2;
    size.width = IntMax(size.width, width);
    size.height = IntMax(size.height, height);
  }
  return size;
}

static bool CanvasWidgetAssign(const CityWidget *widget, CityRect bounds, CityWidgetLayout *layout,
    int capacity, int *count) {
  if (*count >= capacity)
    return false;
  layout[(*count)++] = (CityWidgetLayout){widget, bounds};
  CityRect inner = {bounds.left + widget->padding, bounds.top + widget->padding,
      bounds.right - widget->padding, bounds.bottom - widget->padding};
  bool row = widget->kind == CITY_WIDGET_ROW;
  int available = row ? inner.right - inner.left : inner.bottom - inner.top;
  int total = widget->count > 0 ? (widget->count - 1) * widget->gap : 0, total_weight = 0;
  for (int i = 0; i < widget->count; ++i) {
    CityWidgetSize size = Sc2kCanvasWidgetMinimum(&widget->children[i]);
    total += row ? size.width : size.height;
    total_weight += widget->children[i].fill;
  }
  int extra = available > total ? available - total : 0;
  int position = row ? inner.left : inner.top, used_weight = 0, used_extra = 0;
  for (int i = 0; i < widget->count; ++i) {
    const CityWidget *child = &widget->children[i];
    CityWidgetSize size = Sc2kCanvasWidgetMinimum(child);
    int length = row ? size.width : size.height;
    used_weight += child->fill;
    int cumulative_extra = total_weight ? (int)((i64)extra * used_weight / total_weight) : 0;
    length += cumulative_extra - used_extra;
    used_extra = cumulative_extra;
    CityRect child_bounds = row ? (CityRect){position, inner.top, position + length, inner.bottom} :
        (CityRect){inner.left, position, inner.right, position + length};
    if (!CanvasWidgetAssign(child, child_bounds, layout, capacity, count))
      return false;
    position += length + widget->gap;
  }
  return true;
}

i32 Sc2kCanvasWidgetLayout(const CityWidget *widget, CityRect bounds, CityWidgetLayout *layout,
    i32 capacity) {
  if (!widget || !layout || capacity <= 0)
    return -1;
  CityWidgetSize minimum = Sc2kCanvasWidgetMinimum(widget);
  if (bounds.right - bounds.left < minimum.width || bounds.bottom - bounds.top < minimum.height)
    return -1;
  int count = 0;
  return CanvasWidgetAssign(widget, bounds, layout, capacity, &count) ? count : -1;
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
  for (int i = count - 1; i >= 0; --i) {
    const CityWidget *widget = layout[i].widget;
    if (widget->id > 0 &&
        (widget->kind == CITY_WIDGET_BUTTON || widget->kind == CITY_WIDGET_SLIDER ||
         widget->kind == CITY_WIDGET_TOGGLE) &&
        CanvasPointInRect(layout[i].bounds, x, y))
      return widget->id;
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

static CityRect CityDropdownBounds(const CityCanvasDesktop *desktop) {
  const CityDropdown *d = &desktop->dropdown;
  const Window *owner = DropdownOwner(desktop);
  if (!d->items || !owner) return (CityRect){0};
  int height = d->count * CITY_MENU_ROW_HEIGHT + 2;
  CityRect b = {owner->bounds.left + d->anchor.left, owner->bounds.top + d->anchor.bottom,
      owner->bounds.left + d->anchor.right, owner->bounds.top + d->anchor.bottom + height};
  if (d->button_menu) {
    int width = 0;
    for (int i = 0; i < d->count; ++i)
      width = IntMax(width, CityInterfaceTextWidth(desktop->interface_font, d->items[i].text) + 8);
    width = IntMin(width, desktop->canvas_width);
    b.left = IntClamp(b.right - width, 0, desktop->canvas_width - width);
    b.right = b.left + width;
  }
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

void SetCityDropdownMenu(CityCanvasDesktop *desktop, u32 disabled) {
  DirtyCityDropdown(desktop);
  desktop->dropdown.button_menu = true;
  desktop->dropdown.disabled = disabled;
  DirtyCityDropdown(desktop);
}

static bool CityDropdownItemEnabled(const CityDropdown *dropdown, int item) {
  return item >= 0 && item < dropdown->count && dropdown->items[item].kind != CITY_DROPDOWN_SEPARATOR &&
      !(item < 32 && (dropdown->disabled & (1u << item)));
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
  if (!d->button_menu)
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
      DrawCanvasChoiceMarker(&local, row, d->items[i].kind == CITY_DROPDOWN_RADIO,
          selected ? colors->light : 0xFF000000, selected ? 0xFF000000 : colors->light);
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
