#include "ui/signs.h"
#include "ui/windows/window_internal.h"
#include <string.h>
#include "base/text.h"
#include "ui/map_views.h"
#include "ui/map.h"
#include "city/maps/labels.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "assets/state.h"
#include "audio/state.h"
#include "base/time.h"
#include "city/maps/terrain.h"
#include "city/maps/map.h"
#include "city/maps/map_types.h"
#include "city/maps/tile_storage.h"
#include "city/state.h"
#include "drawing/fonts/fonts.h"
#include "drawing/maps/map_view.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/raster/canvas_raster_internal.h"
#include "drawing/maps/map_labels.h"
#include "drawing/maps/map_render.h"
#include "drawing/state.h"
#include "game/commands.h"
#include "game/state.h"
#include "ui/dialogs/scenario.h"
#include "ui/dialogs/spans.h"
#include "ui/input.h"
#include "ui/sdl.h"
#include "ui/state.h"
#include "ui/text/text.h"
#include "ui/tools/types.h"
#include "ui/windows/controls.h"
#include "ui/windows/windows.h"

enum { CITY_SIGN_ROW_HEIGHT = 12, CITY_SIGN_SCROLL_WIDTH = 12 };

typedef enum CitySignControl {
  CITY_SIGN_CONTROL_NONE, CITY_SIGN_OK, CITY_SIGN_CANCEL, CITY_SIGN_DELETE, CITY_SIGN_MOVE,
  CITY_SIGN_PREVIOUS, CITY_SIGN_NEXT, CITY_SIGN_LOCATION, CITY_SIGN_TEXT, CITY_SIGN_CLEAR,
  CITY_SIGN_MATCH_CASE, CITY_SIGN_SCROLL_UP, CITY_SIGN_SCROLL_DOWN, CITY_SIGN_SCROLL_TRACK,
  CITY_SIGN_SHADE, CITY_SIGN_DEFAULT_SIZE, CITY_SIGN_STICKY,
  CITY_SIGN_COLOR_FIRST, CITY_SIGN_COLOR_LAST = CITY_SIGN_COLOR_FIRST + CITY_SIGN_COLOR_COUNT - 1
} CitySignControl;

// Grow the UI cache only after both replacement arrays are ready. Entry text survives growth; the order array
// is scratch rebuilt by SortCitySigns, so it need not be copied. Failure preserves the live arrays and flags.
static bool ReserveSignEntries(CitySignState *signs, int requested) {
  if (requested <= signs->capacity) return true;
  int capacity = IntMax(signs->capacity, 64);
  while (capacity < requested) capacity *= 2;
  CitySignEntry *entries = calloc(capacity, sizeof(*entries));
  int *order = malloc(capacity * sizeof(*order));
  if (!entries || !order) {
    free(entries);
    free(order);
    return false;
  }
  for (int slot = 0; slot < capacity; ++slot) entries[slot].tile = INVALID_TILE;
  if (signs->capacity) memcpy(entries, signs->entries, signs->capacity * sizeof(*entries));
  free(signs->entries);
  free(signs->order);
  signs->entries = entries;
  signs->order = order;
  signs->capacity = capacity;
  signs->dirty = true;
  return true;
}

// Copy a changed City sign revision into UI-owned entries. No City borrow survives this call; allocation failure
// leaves the old cache/revision available for a later retry. Changed text/tile entries dirty the list.
void RefreshCitySignEntries(City *city) {
  CitySignState *signs = &city->game->ui->signs;
  if (!signs->indexed || signs->revision == city->signs.revision ||
      !ReserveSignEntries(signs, IntMax(city->signs.used, 1))) return;
  signs->revision = city->signs.revision;
  for (int slot = 1; slot < signs->capacity; ++slot) {
    TileIndex tile = slot < city->signs.used ? city->signs.record[slot].tile : INVALID_TILE;
    const char *name = tile == INVALID_TILE ? "" :
        (const char *)ReadCityOverlayName(city, CitySignOverlay(slot))->character;
    CitySignEntry *entry = &signs->entries[slot];
    if (entry->tile == tile && !strcmp(entry->name, name)) continue;
    entry->tile = tile;
    CopyText(entry->name, sizeof(entry->name), name);
    signs->dirty = true;
  }
  if (signs->dirty) {
    Window *window = FindCityCanvasWindow(&city->game->ui->desktop, signs->list_window);
    if (window) Sc2kDirtyMarkRect(&city->game->ui->desktop.dirty, window->bounds);
  }
}

// Rebuild from sparse sign records, independently of effects and moving events on their tiles.
void RebuildCitySigns(City *city) {
  CitySignState *signs = &city->game->ui->signs;
  signs->indexed = true;
  signs->revision = city->signs.revision - 1;
  signs->dirty = true;
  RefreshCitySignEntries(city);
  if (signs->editor_window && signs->slot) {
    int slot = CitySignOverlaySlot(signs->slot);
    if (slot < city->signs.used && city->signs.record[slot].tile != INVALID_TILE)
      signs->tile = city->signs.record[slot].tile;
  }
}

// Compare text using character folding and whole digit runs, without integer parsing. Leading zeros do not break
// ties; CompareSignSlots uses the stable slot when complete names compare equally.
static int CompareSignNames(const char *a, const char *b) {
  while (*a && *b) {
    if (isdigit((u8)*a) && isdigit((u8)*b)) {
      const char *a_end = a, *b_end = b;
      while (isdigit((u8)*a_end)) ++a_end;
      while (isdigit((u8)*b_end)) ++b_end;
      while (a < a_end && *a == '0') ++a;
      while (b < b_end && *b == '0') ++b;
      int difference = (int)(a_end - a) - (int)(b_end - b);
      if (difference) return difference;
      while (a < a_end) {
        if (*a != *b) return (u8)*a - (u8)*b;
        ++a;
        ++b;
      }
    } else {
      int difference = tolower((u8)*a) - tolower((u8)*b);
      if (difference) return difference;
      ++a;
      ++b;
    }
  }
  return (u8)*a - (u8)*b;
}

static int CompareSignSlots(const CitySignState *signs, int a, int b) {
  int result = CompareSignNames(CitySignTextName(signs->entries[a].name),
      CitySignTextName(signs->entries[b].name));
  return result ? result : a - b;
}

// Restore the max-heap below root in the active order prefix. Comparisons read cached entries only; this uses no
// callbacks, allocations or City state and retains slot ordering as the final tiebreaker.
static void SiftSignOrder(CitySignState *signs, int root, int count) {
  int slot = signs->order[root];
  for (int child = root * 2 + 1; child < count; child = root * 2 + 1) {
    if (child + 1 < count && CompareSignSlots(signs, signs->order[child], signs->order[child + 1]) < 0) ++child;
    if (CompareSignSlots(signs, slot, signs->order[child]) >= 0) break;
    signs->order[root] = signs->order[child];
    root = child;
  }
  signs->order[root] = slot;
}

// Rebuild the filtered order only when dirty, then sort it in place and publish the matching caption.
static void SortCitySigns(CitySignState *signs) {
  if (!signs->dirty) return;
  signs->count = 0;
  for (int slot = 1; slot < signs->capacity; ++slot) {
    const CitySignEntry *entry = &signs->entries[slot];
    const char *name = CitySignTextName(entry->name);
    if (entry->tile == INVALID_TILE || !name[0] || !TextContains(name, signs->filter.text, signs->match_case)) continue;
    signs->order[signs->count++] = slot;
  }
  for (int root = signs->count / 2 - 1; root >= 0; --root) SiftSignOrder(signs, root, signs->count);
  for (int end = signs->count - 1; end > 0; --end) {
    int first = signs->order[0];
    signs->order[0] = signs->order[end];
    signs->order[end] = first;
    SiftSignOrder(signs, 0, end);
  }
  snprintf(signs->caption, sizeof(signs->caption), "Sign List - %d Sign%s", signs->count, signs->count == 1 ? "" : "s");
  signs->dirty = false;
}

static CityRect CitySignListRows(const Window *window) {
  CityRect b = window->bounds;
  return (CityRect){b.left + 1, b.top + 15, b.right - CITY_SIGN_SCROLL_WIDTH, b.bottom - 16};
}

static int CitySignVisibleRows(const Window *window) {
  CityRect b = CitySignListRows(window);
  return IntMax(1, (b.bottom - b.top) / CITY_SIGN_ROW_HEIGHT);
}

static CityRect CitySignControlBounds(const Window *window, CitySignControl control) {
  CityRect b = window->bounds;
  if (window->kind == CITY_CANVAS_SIGN_EDIT) {
    if (control >= CITY_SIGN_OK && control <= CITY_SIGN_MOVE) {
      int index = control - CITY_SIGN_OK, width = b.right - b.left - 22;
      int left = b.left + (index * width >> 2), right = b.left + ((index + 1) * width >> 2);
      return (CityRect){left, b.bottom - 12, right, b.bottom};
    }
    if (control == CITY_SIGN_PREVIOUS || control == CITY_SIGN_NEXT) {
      int left = b.right - (control == CITY_SIGN_PREVIOUS ? 22 : 11);
      return (CityRect){left, b.bottom - 12, left + 11, b.bottom};
    }
    if (control >= CITY_SIGN_COLOR_FIRST && control <= CITY_SIGN_COLOR_LAST) {
      int color = control - CITY_SIGN_COLOR_FIRST;
      int left = b.left + 42 + (color & 15) * 13, top = b.top + 31 + (color >> 4) * 12;
      return (CityRect){left, top, left + 13, top + 12};
    }
    if (control == CITY_SIGN_LOCATION) return (CityRect){b.right - 12, b.top, b.right, b.top + 14};
    if (control == CITY_SIGN_TEXT) return (CityRect){b.left + 2, b.top + 16, b.right - 2, b.top + 28};
    if (control == CITY_SIGN_CLEAR) return (CityRect){b.right - 18, b.top + 16, b.right - 2, b.top + 28};
  } else {
    if (control >= CITY_SIGN_SHADE && control <= CITY_SIGN_STICKY) {
      int left = b.right - (CITY_SIGN_STICKY - control + 1) * 12;
      return (CityRect){left, b.top, left + 12, b.top + 14};
    }
    if (control == CITY_SIGN_TEXT) return (CityRect){b.left + 2, b.bottom - 14, b.right - 76, b.bottom - 2};
    if (control == CITY_SIGN_CLEAR) return (CityRect){b.right - 92, b.bottom - 14, b.right - 76, b.bottom - 2};
    if (control == CITY_SIGN_MATCH_CASE) return (CityRect){b.right - 74, b.bottom - 16, b.right - 12, b.bottom};
    if (control == CITY_SIGN_SCROLL_UP) return (CityRect){b.right - 12, b.top + 14, b.right, b.top + 25};
    if (control == CITY_SIGN_SCROLL_DOWN) return (CityRect){b.right - 12, b.bottom - 27, b.right, b.bottom - 16};
    if (control == CITY_SIGN_SCROLL_TRACK) return (CityRect){b.right - 12, b.top + 25, b.right, b.bottom - 27};
  }
  return (CityRect){0};
}

static CitySignControl CitySignControlAt(const Window *window, int x, int y) {
  for (int control = CITY_SIGN_COLOR_LAST; control > CITY_SIGN_CONTROL_NONE; --control) {
    if (window->shaded && control < CITY_SIGN_SHADE) continue;
    CityRect b = CitySignControlBounds(window, control);
    if (CanvasPointInRect(b, x, y)) return control;
  }
  return CITY_SIGN_CONTROL_NONE;
}

static void DrawSignClear(Canvas *canvas, CityRect b, u32 ink, bool pressed) {
  int x = b.left + 1 + pressed, y = b.top + ((b.bottom - b.top - 8) >> 1) + pressed;
  CanvasFill(canvas, x + 4, y, x + 13, y + 1, ink);
  CanvasFill(canvas, x + 4, y + 7, x + 13, y + 8, ink);
  CanvasFill(canvas, x + 12, y + 1, x + 13, y + 7, ink);
  for (int row = 1; row < 7; ++row) {
    int edge = row < 4 ? 4 - row : row - 3;
    CanvasFill(canvas, x + edge, y + row, x + edge + 1, y + row + 1, ink);
  }
  DrawCityInterfaceClose(canvas->interface_font, canvas,
      (CityRect){x + 5, y, x + 12, y + 8}, ink);
}

static void DrawSignTextBox(Canvas *canvas, CityRect b, const CitySignText *text, bool focused,
    u32 face, u32 light, u32 dark, bool pressed) {
  DrawCanvasFrame(canvas, b, 0xFF101010, light, dark, false);
  CityRect clear = {b.right - 16, b.top, b.right, b.bottom};
  DrawCanvasButtonFrame(canvas, clear, face, light, dark, !pressed);
  DrawSignClear(canvas, clear, text->text[0] ? 0xFF101010 : dark, pressed);
  CityRect clip = canvas->clip;
  canvas->clip = IntersectCityRects(clip, (CityRect){b.left + 1, b.top + 1, clear.left, b.bottom - 1});
  int left = b.left + 2, top = b.top + 1;
  int first = IntMin(text->cursor, text->anchor), last = IntMax(text->cursor, text->anchor);
  if (focused && first != last)
    CanvasFill(canvas, left + CityInterfaceTextPrefixWidth(canvas->interface_font, text->text, first), top,
        left + CityInterfaceTextPrefixWidth(canvas->interface_font, text->text, last), b.bottom - 1, 0xFF606060);
  DrawCityInterfaceText(canvas->interface_font, canvas, left, top, text->text, 0xFFFFFF90, false);
  if (focused) {
    int prefix = CityInterfaceTextPrefixWidth(canvas->interface_font, text->text, text->cursor);
    int next = CityTextNext(text->text, text->cursor);
    int width = text->text[text->cursor] ?
        CityInterfaceTextPrefixWidth(canvas->interface_font, text->text, next) - prefix : 6;
    CanvasFill(canvas, left + prefix, b.bottom - 3, left + prefix + IntMax(1, width),
        b.bottom - 2, 0xFFFCFCFC);
  }
  canvas->clip = clip;
}

static void DrawSignArrow(Canvas *canvas, CityRect b, int direction, int shift, u32 color) {
  int x = b.left + ((b.right - b.left) >> 1) + shift;
  int y = b.top + ((b.bottom - b.top) >> 1) + shift;
  for (int row = 0; row < 4; ++row) {
    if (direction < 2)
      CanvasFill(canvas, x - 3 + row, y + (direction ? row - 2 : 1 - row), x + 4 - row,
          y + (direction ? row - 1 : 2 - row), color);
    else
      CanvasFill(canvas, x + (direction == 2 ? 1 - row : row - 2), y - 3 + row,
          x + (direction == 2 ? 2 - row : row - 1), y + 4 - row, color);
  }
}

static CityScrollbar SignScrollbar(const CitySignState *signs, const Window *window) {
  CityRect track = CitySignControlBounds(window, CITY_SIGN_SCROLL_TRACK);
  ++track.left; ++track.top; --track.right; --track.bottom;
  return ReadCityScrollbar(track, signs->count, CitySignVisibleRows(window), signs->first, 8);
}

static void DrawCitySignWindow(Game *game, Canvas *canvas, const Window *window) {
  CitySignState *signs = &game->ui->signs;
  bool editor = window->kind == CITY_CANVAS_SIGN_EDIT;
  u32 face = editor ? canvas_face : 0xFF97855B;
  u32 light = editor ? canvas_light : 0xFFC7B78B;
  u32 dark = editor ? canvas_dark : 0xFF67532B;
  CityRect b = window->bounds;
  SortCitySigns(signs);
  DrawCanvasFrame(canvas, (CityRect){b.left, b.top + 14, b.right, b.bottom}, face, light, dark, true);
  DrawCityWindowCaption(canvas, b, editor ? "Edit sign" : signs->caption, face, light, dark, editor ? 12 : 36);
  if (!editor && !window->shaded) {
    int rows = CitySignVisibleRows(window);
    signs->first = IntMin(signs->first, IntMax(0, signs->count - rows));
    CityRect list = CitySignListRows(window), clip = canvas->clip;
    canvas->clip = IntersectCityRects(clip, list);
    if (!signs->count)
      DrawCityInterfaceText(canvas->interface_font, canvas, list.left + 2, list.top, "None", 0xFFFFFF90, true);
    for (int row = 0; row < rows && signs->first + row < signs->count; ++row) {
      const CitySignEntry *entry = &signs->entries[signs->order[signs->first + row]];
      int y = list.top + row * CITY_SIGN_ROW_HEIGHT;
      CitySignColors colors = CitySignColorStyle(CitySignTextColor(entry->name));
      CanvasFill(canvas, list.left + 3, y + 2, list.left + 12, y + 10, colors.dark);
      CanvasFill(canvas, list.left + 1, y + 4, list.left + 14, y + 8, colors.dark);
      CanvasFill(canvas, list.left + 3, y + 3, list.left + 12, y + 9, colors.face);
      CanvasFill(canvas, list.left + 2, y + 4, list.left + 13, y + 8, colors.face);
      CanvasFill(canvas, list.left + 4, y + 3, list.left + 11, y + 4, colors.light);
      DrawCityInterfaceText(canvas->interface_font, canvas, list.left + 21, y,
          CitySignTextName(entry->name), 0xFFFFFF90, true);
    }
    canvas->clip = clip;
  }
  if (editor) {
    bool full = strlen(signs->name.text) >= CITY_OVERLAY_NAME_MAX_LENGTH;
    DrawCityInterfaceText(canvas->interface_font, canvas, b.left + 2, b.top + 32, "Color", 0xFF101010, false);
    if (full) DrawCityInterfaceText(canvas->interface_font, canvas, b.left + 2, b.top + 44,
        "Name full", 0xFF636363, false);
    for (int color = 0; color < CITY_SIGN_COLOR_COUNT; ++color) {
      CityRect swatch = CitySignControlBounds(window, CITY_SIGN_COLOR_FIRST + color);
      CitySignColors colors = CitySignColorStyle(color);
      bool selected = signs->color == color;
      bool disabled = full && color;
      if (disabled) DrawCanvasFrame(canvas, swatch, face, light, dark, !selected);
      else DrawCanvasButtonFrame(canvas, swatch, colors.face, colors.light, colors.dark, !selected);
      if (selected) {
        CityRect border = {swatch.left + 2, swatch.top + 2, swatch.right - 2, swatch.bottom - 2};
        DrawCanvasFrame(canvas, border, colors.face, colors.ink, colors.ink, false);
      }
      if (!color) DrawCityInterfaceText(canvas->interface_font, canvas, swatch.left + 3, swatch.top + 1,
          "-", colors.ink, false);
    }
  }
  for (int control = CITY_SIGN_OK; control <= CITY_SIGN_STICKY; ++control) {
    CityRect rect = CitySignControlBounds(window, control);
    if (rect.left >= rect.right || (window->shaded && control < CITY_SIGN_SHADE)) continue;
    bool pressed = (signs->pressed_window == window->window_id && signs->pressed == control) ||
        (control == CITY_SIGN_MATCH_CASE && signs->match_case) || (control == CITY_SIGN_MOVE && signs->moving) ||
        (control == CITY_SIGN_SHADE && window->shaded) || (control == CITY_SIGN_STICKY && window->sticky);
    if (control == CITY_SIGN_TEXT) {
      bool focused = game->ui->desktop.windows[game->ui->desktop.count - 1].window_id == window->window_id &&
          (editor || signs->filter_focus);
      DrawSignTextBox(canvas, rect, editor ? &signs->name : &signs->filter, focused, face, light, dark,
          signs->pressed_window == window->window_id && signs->pressed == CITY_SIGN_CLEAR);
      continue;
    }
    if (control == CITY_SIGN_CLEAR) continue;
    DrawCanvasButtonFrame(canvas, rect, face, light, dark, !pressed);
    static const char *const kButtons[] = {"OK", "Cancel", "Delete", "Move"};
    if (control <= CITY_SIGN_MOVE || control == CITY_SIGN_MATCH_CASE) {
      CityRect label = {rect.left + pressed, rect.top + pressed, rect.right + pressed, rect.bottom + pressed};
      DrawCityInterfaceTextCentered(canvas->interface_font, canvas, label,
          control == CITY_SIGN_MATCH_CASE ? "Match case" : kButtons[control - CITY_SIGN_OK], 0xFF101010, false, 10);
    } else if (control == CITY_SIGN_PREVIOUS || control == CITY_SIGN_NEXT) {
      DrawSignArrow(canvas, rect, control == CITY_SIGN_PREVIOUS ? 2 : 3, pressed, 0xFF101010);
    } else if (control == CITY_SIGN_SCROLL_UP || control == CITY_SIGN_SCROLL_DOWN) {
      DrawSignArrow(canvas, rect, control == CITY_SIGN_SCROLL_UP ? 0 : 1, pressed, 0xFF101010);
    } else if (control == CITY_SIGN_SCROLL_TRACK) {
      DrawCanvasFrame(canvas, SignScrollbar(signs, window).thumb, face, light, dark, true);
    } else if (control == CITY_SIGN_LOCATION) {
      int x = rect.left + 6 + pressed, y = rect.top + 6 + pressed;
      for (int row = -3; row <= 3; ++row)
        CanvasFill(canvas, x - 3, y + row, x + 4, y + row + 1, 0xFF101010);
      CanvasFill(canvas, x - 1, y - 1, x + 2, y + 2, face);
      for (int row = 0; row < 4; ++row)
        CanvasFill(canvas, x - 3 + row, y + 4 + row, x + 4 - row, y + 5 + row, 0xFF101010);
    } else if (control >= CITY_SIGN_SHADE && control <= CITY_SIGN_STICKY) {
      DrawCityWindowControlIcon(canvas, rect, control == CITY_SIGN_SHADE ? CITY_WINDOW_ICON_SHADE :
          control == CITY_SIGN_DEFAULT_SIZE ? CITY_WINDOW_ICON_DEFAULT_SIZE : CITY_WINDOW_ICON_STICKY, pressed);
    }
  }
  if (!editor && !window->shaded) {
    CityRect grip = {b.right - 12, b.bottom - 16, b.right, b.bottom};
    DrawCanvasFrame(canvas, grip, face, light, dark, true);
    for (int offset = 3; offset <= 9; offset += 3)
      DrawCanvasLine(canvas, b.right - offset, b.bottom - 3, b.right - 3, b.bottom - offset, 0xFF101010, 1);
  }
}

static void DirtySignWindow(CityInterfaceState *ui, u32 id) {
  Window *window = FindCityCanvasWindow(&ui->desktop, id);
  if (window) Sc2kDirtyMarkRect(&ui->desktop.dirty, window->bounds);
}

static void SetSignMoving(CityInterfaceState *ui, bool moving) {
  CitySignState *signs = &ui->signs;
  if (signs->moving == moving) return;
  if (moving) {
    signs->previous_tool = ui->desktop.selected_tool;
    ui->desktop.selected_tool = CITY_CANVAS_TOOL_SIGN;
  } else if (ui->desktop.selected_tool == CITY_CANVAS_TOOL_SIGN) {
    ui->desktop.selected_tool = signs->previous_tool;
  }
  signs->moving = moving;
  DirtySignWindow(ui, signs->editor_window);
}

static void AssignSignText(CitySignText *text, const char *name) {
  CopyText(text->text, sizeof(text->text), name);
  text->cursor = text->anchor = strlen(text->text);
}

void OpenCitySignList(City *city) {
  CityInterfaceState *ui = city->game->ui;
  CitySignState *signs = &ui->signs;
  if (!signs->indexed) RebuildCitySigns(city);
  else RefreshCitySignEntries(city);
  int index = FocusCityCanvasWindowKind(&ui->desktop, CITY_CANVAS_SIGN_LIST);
  if (index < 0) {
    int left = IntMax(0, (ui->desktop.canvas_width - CITY_SIGN_LIST_WIDTH) >> 1);
    int top = IntMax(18, (ui->desktop.canvas_height - CITY_SIGN_LIST_HEIGHT) >> 1);
    index = Sc2kCanvasDesktopOpen(&ui->desktop, (Window){.kind = CITY_CANVAS_SIGN_LIST,
        .bounds = {left, top, left + CITY_SIGN_LIST_WIDTH, top + CITY_SIGN_LIST_HEIGHT}, .title = signs->caption});
  }
  if (index >= 0) signs->list_window = ui->desktop.windows[index].window_id;
  signs->filter_focus = true;
  signs->dirty = true;
  DirtySignWindow(ui, signs->list_window);
}

static void OpenCitySignEditor(City *city, TileIndex tile) {
  CityInterfaceState *ui = city->game->ui;
  CitySignState *signs = &ui->signs;
  if (!CanEditCityMapSign(city, tile)) return;
  if (!signs->indexed) RebuildCitySigns(city);
  else RefreshCitySignEntries(city);
  signs->tile = tile;
  signs->slot = ReadCitySignOverlay(city, tile);
  SetSignMoving(ui, false);
  const char *name = signs->slot ? (const char *)CityOverlayNameAt(city, signs->slot)->character : "";
  signs->color = CitySignTextColor(name);
  AssignSignText(&signs->name, CitySignTextName(name));
  int index = FocusCityCanvasWindowKind(&ui->desktop, CITY_CANVAS_SIGN_EDIT);
  if (index < 0) {
    int left = IntMax(0, (ui->desktop.canvas_width - CITY_SIGN_EDITOR_WIDTH) >> 1);
    int top = IntMax(18, (ui->desktop.canvas_height - CITY_SIGN_EDITOR_HEIGHT) >> 1);
    index = Sc2kCanvasDesktopOpen(&ui->desktop, (Window){.kind = CITY_CANVAS_SIGN_EDIT,
        .bounds = {left, top, left + CITY_SIGN_EDITOR_WIDTH, top + CITY_SIGN_EDITOR_HEIGHT}, .title = "Edit sign"});
  }
  if (index >= 0) signs->editor_window = ui->desktop.windows[index].window_id;
  DirtySignWindow(ui, signs->editor_window);
}

static void CloseSignEditor(City *city) {
  CitySignState *signs = &city->game->ui->signs;
  Sc2kCanvasDesktopDismissWindow(&city->game->ui->desktop, signs->editor_window);
  signs->editor_window = 0;
  SetSignMoving(city->game->ui, false);
}

void ResetCitySignEditor(City *city) {
  if (city->game->ui->signs.editor_window) CloseSignEditor(city);
}

static bool RenameSign(City *city, TileIndex tile, int slot, const char *name, int color) {
  char encoded[CITY_OVERLAY_NAME_BYTES];
  int length = strlen(name), prefix = length && color;
  if (length + prefix > CITY_OVERLAY_NAME_MAX_LENGTH) return false;
  if (prefix) encoded[0] = color;
  memcpy(encoded + prefix, name, length + 1);
  return ExecuteRecordedCityCommandWithPayload(city->game, (CityCommand){CITY_COMMAND_RENAME_SIGN, (int)tile, slot, 0},
      NULL, 0, (const u8 *)encoded, length + prefix) >= 0;
}

static void CenterOnSign(City *city, TileIndex tile) {
  CenterCityMapViewAtTile(city, NULL, MapTileX(city, tile), MapTileY(city, tile));
}

static void ScrollSigns(CityInterfaceState *ui, const Window *window, int delta) {
  CitySignState *signs = &ui->signs;
  SortCitySigns(signs);
  signs->first = IntClamp(signs->first + delta, 0, signs->count - CitySignVisibleRows(window));
  DirtySignWindow(ui, window->window_id);
}

static void ApplySignControl(City *city, Window *window, CitySignControl control) {
  CityInterfaceState *ui = city->game->ui;
  CitySignState *signs = &ui->signs;
  // Navigation can focus/reorder windows. Keep the originating identity instead of borrowing its array slot.
  u32 window_id = window->window_id;
  if (control == CITY_SIGN_CANCEL) { CloseSignEditor(city); return; }
  if (control == CITY_SIGN_OK || control == CITY_SIGN_DELETE) {
    if (RenameSign(city, signs->tile, signs->slot, control == CITY_SIGN_DELETE ? "" : signs->name.text, signs->color))
      CloseSignEditor(city);
    return;
  }
  if (control >= CITY_SIGN_COLOR_FIRST && control <= CITY_SIGN_COLOR_LAST) {
    signs->color = control - CITY_SIGN_COLOR_FIRST;
    DirtySignWindow(ui, window_id);
    return;
  }
  switch (control) {
  case CITY_SIGN_MOVE: SetSignMoving(ui, !signs->moving); break;
  case CITY_SIGN_LOCATION: CenterOnSign(city, signs->tile); break;
  case CITY_SIGN_PREVIOUS:
  case CITY_SIGN_NEXT: {
    // Editor navigation is alphabetical across all signs, independently of the list filter.
    int current = signs->slot ? CitySignOverlaySlot(signs->slot) : 0;
    int selected = 0, wrap = 0;
    bool next = control == CITY_SIGN_NEXT;
    const char *current_name = current < signs->capacity ? signs->entries[current].name : "";
    for (int slot = 1; slot < signs->capacity; ++slot) {
      if (signs->entries[slot].tile == INVALID_TILE) continue;
      const char *name = CitySignTextName(signs->entries[slot].name);
      int relative = CompareSignNames(name, CitySignTextName(current_name));
      if (!relative) relative = slot - current;
      if (!wrap || (next ? CompareSignSlots(signs, slot, wrap) < 0 : CompareSignSlots(signs, slot, wrap) > 0)) wrap = slot;
      if (slot == current || (next ? relative <= 0 : relative >= 0)) continue;
      if (!selected || (next ? CompareSignSlots(signs, slot, selected) < 0 : CompareSignSlots(signs, slot, selected) > 0)) selected = slot;
    }
    if (!selected) selected = wrap;
    if (selected) {
      TileIndex tile = signs->entries[selected].tile;
      OpenCitySignEditor(city, tile);
      CenterOnSign(city, tile);
    }
    break;
  }
  case CITY_SIGN_CLEAR:
    AssignSignText(window->kind == CITY_CANVAS_SIGN_EDIT ? &signs->name : &signs->filter, "");
    signs->dirty = true;
    signs->first = 0;
    break;
  case CITY_SIGN_MATCH_CASE: signs->match_case = !signs->match_case; signs->dirty = true; break;
  case CITY_SIGN_SCROLL_UP: ScrollSigns(ui, window, -1); break;
  case CITY_SIGN_SCROLL_DOWN: ScrollSigns(ui, window, 1); break;
  case CITY_SIGN_SHADE:
    DirtySignWindow(ui, window_id);
    if (window->shaded) window->bounds.bottom = window->bounds.top + window->restore_height;
    else {
      window->restore_height = window->bounds.bottom - window->bounds.top;
      window->bounds.bottom = window->bounds.top + 14;
    }
    window->shaded = !window->shaded;
    break;
  case CITY_SIGN_DEFAULT_SIZE:
    DirtySignWindow(ui, window_id);
    window->shaded = false;
    ResetCityWindowSize(window);
    break;
  case CITY_SIGN_STICKY: window->sticky = !window->sticky; break;
  default: break;
  }
  DirtySignWindow(ui, window_id);
}

typedef struct {
  int zoom, scale, x, y, origin_x, origin_y;
  u32 window;
  int rotation;
} SignCamera;

static SignCamera SignMapCamera(City *city, CityExtraMapView *view) {
  Drawing *drawing = city->drawing;
  if (!view) return (SignCamera){drawing->default_map_view->zoom,
      drawing->map_pixel_scale * (drawing->default_map_view->magnified_large_zoom ? 200 : 100),
      drawing->scroll_pixels.x + drawing->screen_shake_offset * drawing->map_pixel_scale,
      drawing->scroll_pixels.y, drawing->map_state.origin_x, drawing->map_state.origin_y,
      0, CityMapViewRotation(drawing->default_map_view)};
  CityRect b = Sc2kCanvasDesktopPixelBounds(&city->game->ui->desktop, view->window_id);
  int scale = drawing->options.scale;
  return (SignCamera){IntMin(view->zoom, CITY_MAP_ZOOM_LARGE),
      drawing->map_pixel_scale * (view->zoom == CITY_CANVAS_MAP_ZOOM_MAGNIFIED ? 200 : 100),
      b.left + (3 * scale + 99) / 100 + view->scroll_pixels.x,
      b.top + (CITY_VIEWPORT_TOP * scale + 99) / 100 + view->scroll_pixels.y,
      view->origin_x, view->origin_y, view->window_id, view->rotation};
}

static int SignSourceCoordinate(int pixel, int scale) {
  int product = pixel * 100;
  return product / scale - (product % scale < 0);
}

static TileIndex PickSignTile(City *city, SignCamera camera, int x, int y) {
  x = SignSourceCoordinate(x - camera.x, camera.scale);
  y = SignSourceCoordinate(y - camera.y, camera.scale);
  return PickCityMapCameraPoint(city,
      (CityMapCamera){.origin_x = camera.origin_x, .origin_y = camera.origin_y,
          .zoom = camera.zoom, .rotation = camera.rotation}, (CityPoint){x, y});
}

static TileIndex PickSignLabel(City *city, SignCamera camera, int pixel_x, int pixel_y) {
  if (!city->drawing->map_state.label_display) return INVALID_TILE;
  int x = SignSourceCoordinate(pixel_x - camera.x, camera.scale);
  int y = SignSourceCoordinate(pixel_y - camera.y, camera.scale);
  CityRect tiles = VisibleCityMapTileBounds(city,
      (CityMapCamera){.origin_x = camera.origin_x, .origin_y = camera.origin_y,
          .zoom = camera.zoom, .rotation = camera.rotation},
      (CityRect){x, y, x + 1, y + 1});
  TileIndex result = INVALID_TILE;
  int order = -1, last_y = -1, scale = 1 << camera.zoom;
  for (int row = tiles.top; row < tiles.bottom; ++row)
    for (int column = tiles.left; column < tiles.right; ++column) {
      TileIndex tile = MapTileXY(city, column, row);
      int slot = ReadCitySignOverlay(city, tile);
      CityPoint point = CityMapCameraPoint(city, camera.rotation, column, row);
      if (!IsCitySignOverlay(slot) || point.x + point.y < order ||
          (point.x + point.y == order && point.y <= last_y)) continue;
      int sx = camera.origin_x + (point.y - point.x) * 4 * scale;
      const TileInfo *tp = &city->tile[tile];
      int height = city->drawing->map_state.water_enabled ? ReadCitySurfaceHeight(tp) : ReadCityLandHeight(tp);
      int sy = camera.origin_y + (point.x + point.y) * 2 * scale - height * CityMapHeightStep(camera.zoom);
      CityPoint anchor = {camera.x + (sx + 4 * scale) * camera.scale / 100,
          camera.y + (sy - 2 * scale) * camera.scale / 100};
      CityRect rect = CitySignPixelBounds(&city->game->resources->interface_font, city->drawing->options.scale,
          anchor, (const char *)ReadCityOverlayName(city, slot)->character);
      if (CanvasPointInRect(rect, pixel_x, pixel_y)) {
        result = tile;
        order = point.x + point.y;
        last_y = point.y;
      }
    }
  return result;
}

static void SignPixelVisible(CityRect rect, void *context) {
  *(bool *)context = rect.left < rect.right && rect.top < rect.bottom;
}

static void MoveSignScrollThumb(CityInterfaceState *ui, Window *window, int y) {
  CitySignState *signs = &ui->signs;
  CityScrollbar scrollbar = SignScrollbar(signs, window);
  signs->first = CityScrollbarPosition(&scrollbar, y, signs->scroll_grab, CITY_SCROLLBAR_DRAG);
  DirtySignWindow(ui, window->window_id);
}

bool HandleCityMapSignInput(City *city, CityExtraMapView *view, const CanvasSDLEvent *event) {
  CityInterfaceState *ui = city->game->ui;
  CitySignState *signs = &ui->signs;
  if (signs->moving && ui->desktop.selected_tool != CITY_CANVAS_TOOL_SIGN) SetSignMoving(ui, false);
  if (event->kind == CANVAS_SDL_POINTER_DOWN && event->button == CANVAS_SDL_BUTTON_RIGHT && signs->moving) {
    SetSignMoving(ui, false);
    return true;
  }
  if (event->kind != CANVAS_SDL_POINTER_DOWN || event->button != CANVAS_SDL_BUTTON_LEFT) return false;
  CityPoint pixels = CityInterfacePointerPixels(city->game, event);
  if (NativeSpanDialogWaiting(ui) || CityConnectionQuestionWaiting(ui) || CityMissionNoticeWaiting(ui) ||
      ui->startup_waiting || NativeScenarioDialogWaiting(ui)) return false;
  SignCamera camera = SignMapCamera(city, view);
  bool visible = false;
  VisitCanvasMapPixels(&ui->desktop, camera.window, (CityRect){pixels.x, pixels.y, pixels.x + 1, pixels.y + 1},
      SignPixelVisible, &visible);
  if (!visible) return false;
  if (signs->moving && FindCityCanvasWindow(&ui->desktop, signs->editor_window)) {
    TileIndex destination = PickSignTile(city, camera, pixels.x, pixels.y);
    if (!IsMapTileValid(city, destination)) return true;
    if (!signs->slot) {
      if (ReadCitySignOverlay(city, destination) != CITY_OVERLAY_NONE) return true;
      signs->tile = destination;
    }
    if (ReadCitySignOverlay(city, destination) != CITY_OVERLAY_NONE && destination != signs->tile) {
      ExecuteRecordedCityCommand(city->game, CITY_COMMAND_MOVE_SIGN, (int)signs->tile, (int)destination, signs->slot);
      return true;
    }
    if (!RenameSign(city, signs->tile, signs->slot, signs->name.text, signs->color)) return true;
    signs->slot = ReadCitySignOverlay(city, signs->tile);
    if (!signs->slot || ExecuteRecordedCityCommand(city->game, CITY_COMMAND_MOVE_SIGN,
        (int)signs->tile, (int)destination, signs->slot) >= 0) CloseSignEditor(city);
    return true;
  }
  TileIndex tile = PickSignLabel(city, camera, pixels.x, pixels.y);
  if (tile != INVALID_TILE) {
    if (event->ctrl) RenameSign(city, tile, ReadCitySignOverlay(city, tile), "", 0);
    else OpenCitySignEditor(city, tile);
    return true;
  }
  if (ui->desktop.selected_tool == CITY_CANVAS_TOOL_SIGN && !event->ctrl && !event->shift) {
    tile = PickSignTile(city, camera, pixels.x, pixels.y);
    if (IsMapTileValid(city, tile)) {
      int slot = ReadCitySignOverlay(city, tile);
      if (!slot) {
        if (RenameSign(city, tile, 0, "Sign", 0)) {
          OpenCitySignEditor(city, tile);
          signs->name.anchor = 0;
        }
      } else if (IsCitySignOverlay(slot)) {
        OpenCitySignEditor(city, tile);
      } else {
        RenameSign(city, tile, 0, "Sign", 0);
      }
    }
    return true;
  }
  return false;
}

bool PollCitySigns(City *city) {
  CityInterfaceState *ui = city->game->ui;
  CitySignState *signs = &ui->signs;
  if (signs->editor_window && !FindCityCanvasWindow(&ui->desktop, signs->editor_window)) {
    signs->editor_window = 0;
    SetSignMoving(ui, false);
  }
  if (signs->editor_window && signs->slot &&
      (!IsMapTileValid(city, signs->tile) || ReadCitySignOverlay(city, signs->tile) != signs->slot))
    CloseSignEditor(city);
  if (signs->list_window && !FindCityCanvasWindow(&ui->desktop, signs->list_window)) signs->list_window = 0;
  if (!signs->editor_window && !signs->list_window) signs->indexed = false;
  if (!signs->pressed_window) return false;
  if (FindCityCanvasWindow(&ui->desktop, signs->pressed_window) && (!signs->released ||
      (u32)((u32)(u64)(MonotonicSeconds() * 1000) - signs->release_time) < 150)) return false;
  DirtySignWindow(ui, signs->pressed_window);
  signs->pressed_window = 0;
  signs->pressed = CITY_SIGN_CONTROL_NONE;
  return true;
}

// Handles this window's painting and input; paint coordinates are local to the window.
bool SignWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    Canvas *canvas = event->canvas;
    DrawCitySignWindow(game, canvas, window);
    return true;
  }
  if (event->kind != WINDOW_INPUT) return false;
  const CanvasSDLEvent *input = event->input;
  City *city = game->city;
  CityInterfaceState *ui = game->ui;
  CitySignState *signs = &ui->signs;
  Window *target = FindCityCanvasWindow(&ui->desktop, window->window_id);
  if (input->kind == CANVAS_SDL_KEY_DOWN) {
    bool editor = target->kind == CITY_CANVAS_SIGN_EDIT;
    CityHotkey key = input->key & CITY_HOTKEY_KEY_MASK;
    if (key == CITY_KEY_ESCAPE) {
      if (editor && signs->moving) SetSignMoving(ui, false);
      else if (editor) CloseSignEditor(city);
      else if (!target->sticky) Sc2kCanvasDesktopDismissWindow(&ui->desktop, target->window_id);
    } else if (editor && key == CITY_KEY_RETURN) ApplySignControl(city, target, CITY_SIGN_OK);
    else if (editor && input->ctrl && (key == CITY_KEY_LEFT || key == CITY_KEY_RIGHT))
      ApplySignControl(city, target, key == CITY_KEY_LEFT ? CITY_SIGN_PREVIOUS : CITY_SIGN_NEXT);
    else if (!editor && (key == CITY_KEY_UP || key == CITY_KEY_DOWN ||
        key == CITY_KEY_PAGE_UP || key == CITY_KEY_PAGE_DOWN)) {
      int delta = key == CITY_KEY_UP || key == CITY_KEY_PAGE_UP ? -1 : 1;
      bool page = key == CITY_KEY_PAGE_UP || key == CITY_KEY_PAGE_DOWN;
      ScrollSigns(ui, target, delta * (page ? CitySignVisibleRows(target) : 1));
    } else {
      CitySignText *text = editor ? &signs->name : &signs->filter;
      if (!editor && (key == 'F' || key == CITY_KEY_RETURN)) {
        signs->filter_focus = true;
        if (!input->shift) text->anchor = text->cursor;
      } else {
        int caret = text->cursor, anchor = text->anchor;
        CityHotkey edit_key = key | (input->shift ? CITY_HOTKEY_SHIFT : 0);
        if (input->ctrl && key == 'A') edit_key = CITY_HOTKEY_CTRL | 'A';
        if (!HandleCityTextSelectionKey(text->text, &caret, &anchor, edit_key))
          return true; // Text input owns typing; game shortcuts stay suppressed.
        text->cursor = caret;
        text->anchor = anchor;
        if (key == CITY_KEY_BACKSPACE || key == CITY_KEY_DELETE) {
          signs->dirty = true;
          signs->first = 0;
        }
      }
    }
    DirtySignWindow(ui, editor ? signs->editor_window : signs->list_window);
    return true;
  }
  if (input->kind == CANVAS_SDL_TEXT_INPUT) {
    bool editor = target->kind == CITY_CANVAS_SIGN_EDIT;
    if (!editor && (target->kind != CITY_CANVAS_SIGN_LIST || !ui->signs.filter_focus || target->shaded)) return false;
    CitySignText *text = editor ? &ui->signs.name : &ui->signs.filter;
    int caret = text->cursor, anchor = text->anchor;
    DeleteCityTextSelection(text->text, &caret, &anchor);
    int limit = CITY_OVERLAY_NAME_MAX_LENGTH - (editor && ui->signs.color != 0);
    InsertCityTextSelection(text->text, limit + 1, &caret, &anchor, input->text, false);
    text->cursor = caret;
    text->anchor = anchor;
    ui->signs.dirty = true;
    ui->signs.first = 0;
    DirtySignWindow(ui, target->window_id);
    return true;
  }
  if (input->kind == CANVAS_SDL_SCROLL) {
    if (window->kind != CITY_CANVAS_SIGN_LIST || window->shaded) return false;
    ScrollSigns(ui, window, input->y > 0 ? -1 : input->y < 0 ? 1 : 0);
    return true;
  }
  if (input->kind == CANVAS_SDL_FOCUS_LOST || input->kind == CANVAS_SDL_POINTER_UP) {
    if (signs->pressed_window == window->window_id && !signs->released) {
      signs->released = true;
      signs->release_time = (u32)(u64)(MonotonicSeconds() * 1000);
    }
    signs->scroll_drag = false;
    return true;
  }
  if (input->kind == CANVAS_SDL_POINTER_MOVE && signs->scroll_drag) {
    MoveSignScrollThumb(ui, target, event->y);
    return true;
  }
  if (input->kind != CANVAS_SDL_POINTER_DOWN || input->button != CANVAS_SDL_BUTTON_LEFT) return false;
  CityPoint point = {event->x, event->y};
  u32 id = window->window_id;
  {
    CitySignControl control = CitySignControlAt(target, point.x, point.y);
    if (control == CITY_SIGN_TEXT) {
      signs->filter_focus = true;
      CitySignText *text = target->kind == CITY_CANVAS_SIGN_EDIT ? &signs->name : &signs->filter;
      CityRect field = CitySignControlBounds(target, CITY_SIGN_TEXT);
      int position = CityInterfaceTextCaretAt(&city->game->resources->interface_font,
          text->text, point.x - field.left - 2);
      text->cursor = position;
      if (!input->shift) text->anchor = position;
      DirtySignWindow(ui, id);
      return true;
    }
    if (control) {
      if (control > CITY_SIGN_COLOR_FIRST && control <= CITY_SIGN_COLOR_LAST &&
          strlen(signs->name.text) >= CITY_OVERLAY_NAME_MAX_LENGTH) return true;
      if (control == CITY_SIGN_CLEAR && !(target->kind == CITY_CANVAS_SIGN_EDIT ?
          signs->name.text[0] : signs->filter.text[0])) return true;
      signs->pressed_window = id;
      signs->pressed = control;
      signs->released = false;
      PlayCityButtonBeep(city->game);
      if (control == CITY_SIGN_SCROLL_TRACK) {
        SortCitySigns(signs);
        CityScrollbar scrollbar = SignScrollbar(signs, window);
        int rows = CitySignVisibleRows(window);
        if (point.y < scrollbar.thumb.top) ScrollSigns(ui, target, -rows);
        else if (point.y >= scrollbar.thumb.bottom) ScrollSigns(ui, target, rows);
        else {
          signs->scroll_drag = true;
          signs->scroll_grab = point.y - scrollbar.thumb.top;
        }
      } else ApplySignControl(city, target, control);
      return true;
    }
    if (target->kind == CITY_CANVAS_SIGN_LIST && !target->shaded &&
        point.y >= target->bounds.top + 15 && point.y < target->bounds.bottom - 16) {
      SortCitySigns(signs);
      signs->filter_focus = false;
      int row = (point.y - target->bounds.top - 15) / CITY_SIGN_ROW_HEIGHT + signs->first;
      if (row < signs->count) {
        TileIndex tile = signs->entries[signs->order[row]].tile;
        CenterOnSign(city, tile);
        if (input->ctrl) OpenCitySignEditor(city, tile);
      }
      DirtySignWindow(ui, id);
      return true;
    }
    return false; // Caption dragging, closing and resizing use the shared desktop.
  }
}

// Return borrowed control help for a sign window, or NULL for generic title-bar help.
const char *CitySignTooltipAt(Game *game, const Window *window, int x, int y) {
  if ((window->kind == CITY_CANVAS_SIGN_EDIT || window->kind == CITY_CANVAS_SIGN_LIST) &&
      y < window->bounds.top + 14) {
    switch (CitySignControlAt(window, x, y)) {
    case CITY_SIGN_LOCATION: return "Center the main view on this sign.";
    case CITY_SIGN_SHADE: return window->shaded ? "Expand this window." : "Shade this window to its title bar.";
    case CITY_SIGN_DEFAULT_SIZE: return "Restore the default window size.";
    case CITY_SIGN_STICKY: return window->sticky ?
        "Unpin this window so Escape can close it." : "Pin this window to keep it open when pressing Escape.";
    default: break;
    }
    return NULL;
  }
  switch (window->kind) {
  case CITY_CANVAS_SIGN_EDIT: {
    CitySignControl control = CitySignControlAt(window, x, y);
    if (control >= CITY_SIGN_COLOR_FIRST && control <= CITY_SIGN_COLOR_LAST) {
      if (control == CITY_SIGN_COLOR_FIRST) return "Default green. Leaves more room for the sign name.";
      if (strlen(game->ui->signs.name.text) >= CITY_OVERLAY_NAME_MAX_LENGTH)
        return "Shorten the sign's name before choosing a color.";
      return "Choose the sign's color. Press OK to apply it, or Cancel to keep the current color.";
    }
    if (control == CITY_SIGN_TEXT) return "Click to place the caret, or move it with Left and Right.";
    if (control == CITY_SIGN_MOVE) return "Choose a new map location for this sign.";
    if (control == CITY_SIGN_DELETE) return "Delete this sign.";
    return "Edit this sign's text and color, then confirm or cancel the changes.";
  }
  case CITY_CANVAS_SIGN_LIST: return "Find signs by name. Click a sign to center the map; Ctrl-click to edit it.";
  default: return NULL;
  }
}
