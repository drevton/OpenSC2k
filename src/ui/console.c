#include "ui/console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/state.h"
#include "audio/backend.h"
#include "base/text.h"
#include "city/engine/cheats.h"
#include "drawing/fonts/fonts.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/raster/canvas_raster_internal.h"
#include "game/scripting.h"
#include "game/state.h"
#include "ui/commands.h"
#include "ui/dialogs/dialogs.h"
#include "ui/input.h"
#include "ui/sdl.h"
#include "ui/state.h"
#include "ui/text/text.h"
#include "ui/windows/controls.h"
#include "ui/windows/window_internal.h"
#include "ui/windows/windows.h"

// Native console input, output, history and persistent shortcut overrides.

enum { kConsoleLineHeight = 12, kConsoleLineCapacity = 256, kConsoleScrollbarWidth = 12 };

typedef struct {
  CityRect output, text, input, up, down;
  CityScrollbar scrollbar;
  int visible, first;
} ConsoleLayout;

// Count and paint the same wrapped UTF-8 rows, without retaining a second output buffer.
static const char *NextConsoleLine(const CityInterfaceFont *font, const char *text, int width, char *line) {
  int length = 0, pixels = 0;
  while (*text && *text != '\n' && *text != '\r') {
    const char *next = text;
    ReadCityFontCharacter(&next);
    int bytes = (int)(next - text), advance = CityInterfaceTextPrefixWidth(font, text, bytes);
    if (length && (pixels + advance > width || length + bytes >= kConsoleLineCapacity)) break;
    if (line) memcpy(line + length, text, bytes);
    length += bytes;
    pixels += advance;
    text = next;
  }
  if (line) line[length] = 0;
  if (*text == '\r') {
    ++text;
    if (*text == '\n') ++text;
  } else if (*text == '\n') ++text;
  return text;
}

static ConsoleLayout ReadConsoleLayout(Game *game, CityRect b) {
  ConsoleLayout layout = {
    .output = {b.left + 5, b.top + 19, b.right - 5, b.bottom - 30},
    .text = {b.left + 9, b.top + 23, b.right - 9 - kConsoleScrollbarWidth, b.bottom - 33},
    .input = {b.left + 7, b.bottom - 26, b.right - 7, b.bottom - 7}
  };
  int rows = 0, width = layout.text.right - layout.text.left;
  const CityInterfaceFont *font = &game->resources->interface_font;
  for (const char *text = game->ui->console.output; *text; text = NextConsoleLine(font, text, width, NULL)) ++rows;
  layout.visible = IntMax(1, (layout.text.bottom - layout.text.top) / kConsoleLineHeight);
  int maximum = IntMax(0, rows - layout.visible);
  layout.first = maximum - IntClamp(game->ui->console.output_scroll, 0, maximum);
  int right = layout.output.right - 1, left = right - kConsoleScrollbarWidth + 1;
  layout.up = (CityRect){left, layout.output.top + 1, right, layout.output.top + kConsoleScrollbarWidth};
  layout.down = (CityRect){left, layout.output.bottom - kConsoleScrollbarWidth, right, layout.output.bottom - 1};
  layout.scrollbar = ReadCityScrollbar((CityRect){left, layout.up.bottom, right, layout.down.top},
      rows, layout.visible, layout.first, 8);
  return layout;
}

static void ScrollConsoleOutput(CityInterfaceState *ui, const ConsoleLayout *layout, int rows) {
  ui->console.output_scroll = IntClamp(ui->console.output_scroll + rows, 0, layout->scrollbar.maximum);
}

static void DrawConsoleScrollArrow(Canvas *canvas, CityRect b, bool up) {
  DrawCanvasButtonFrame(canvas, b, canvas_face, canvas_light, canvas_dark, true);
  int pressed = CityCanvasButtonPressed(canvas, b);
  int center = (b.left + b.right) / 2 + pressed, top = (b.top + b.bottom - 4) / 2 + pressed;
  for (int row = 0; row < 4; ++row) {
    int half = up ? row : 3 - row;
    CanvasFill(canvas, center - half, top + row, center + half + 1, top + row + 1, canvas_text);
  }
}

static void DrawCityDeveloperConsole(Game *game, Canvas *canvas, const Window *window) {
  CityInterfaceState *ui = game->ui;
  ConsoleLayout layout = ReadConsoleLayout(game, window->bounds);
  CanvasPanelFrame(canvas, window->bounds, window->title);
  CanvasFill(canvas, layout.output.left, layout.output.top, layout.output.right, layout.output.bottom, 0xFF101010);
  Canvas text_canvas = *canvas;
  text_canvas.clip = IntersectCityRects(canvas->clip, layout.text);
  const char *text = ui->console.output;
  const CityInterfaceFont *font = &game->resources->interface_font;
  for (int row = 0; *text && row < layout.first + layout.visible; ++row) {
    char line[kConsoleLineCapacity];
    text = NextConsoleLine(font, text, layout.text.right - layout.text.left, line);
    if (row >= layout.first)
      DrawCityInterfaceText(font, &text_canvas, layout.text.left,
          layout.text.top + (row - layout.first) * kConsoleLineHeight, line, 0xFFE8E8D0, false);
  }
  DrawCanvasFrame(canvas, layout.scrollbar.track, canvas_dark, canvas_light, canvas_dark, false);
  DrawCanvasFrame(canvas, layout.scrollbar.thumb, canvas_face, canvas_light, canvas_dark, true);
  DrawConsoleScrollArrow(canvas, layout.up, true);
  DrawConsoleScrollArrow(canvas, layout.down, false);
  DrawCityTextBox(canvas, layout.input, ui->console.input, ui->console.caret, ui->console.scroll,
      true, ui->console.select_all, true);
}

static void RememberConsoleCommand(CityInterfaceState *ui, const char *command) {
  int count = ui->console.history_count;
  if (!*command || (count && !strcmp(command, ui->console.history[count - 1]))) return;
  char *copy = DuplicateText(command);
  if (count == CITY_CONSOLE_HISTORY_CAPACITY) {
    free(ui->console.history[0]);
    memmove(ui->console.history, ui->console.history + 1, (count - 1) * sizeof(*ui->console.history));
    --ui->console.history_count;
  }
  ui->console.history[ui->console.history_count++] = copy;
}

static void RecallConsoleCommand(CityInterfaceState *ui, int direction) {
  int cursor = IntClamp(ui->console.history_cursor + direction, 0, ui->console.history_count);
  if (cursor == ui->console.history_cursor) return;
  if (ui->console.history_cursor == ui->console.history_count)
    CopyText(ui->console.draft, sizeof(ui->console.draft), ui->console.input);
  ui->console.history_cursor = cursor;
  CopyText(ui->console.input, sizeof(ui->console.input),
      cursor == ui->console.history_count ? ui->console.draft : ui->console.history[cursor]);
  ui->console.caret = (int)strlen(ui->console.input);
  ui->console.scroll = ui->console.select_all = 0;
}

void ReleaseCityConsole(CityInterfaceState *ui) {
  for (int i = 0; i < ui->console.history_count; ++i) {
    free(ui->console.history[i]);
    ui->console.history[i] = NULL;
  }
  ui->console.history_count = ui->console.history_cursor = 0;
}

void AppendCityConsoleOutput(Game *game, const char *text) {
  char *output = game->ui->console.output;
  size_t capacity = sizeof(game->ui->console.output), length = strlen(output), added = strlen(text);
  if (added >= capacity - 2) {
    text += added - capacity + 2;
    added = capacity - 2;
  }
  while (length + added + 2 > capacity) {
    char *next = strchr(output, '\n');
    size_t removed = next ? (size_t)(next - output) + 1 : length;
    memmove(output, output + removed, length - removed + 1);
    length -= removed;
  }
  memcpy(output + length, text, added);
  output[length + added] = '\n';
  output[length + added + 1] = 0;
  fprintf(stderr, "Console: %s\n", text);
}

static void ExecuteCityLua(Game *game, const char *code, bool file) {
  if (!game->ui->lua_enabled) {
    AppendCityConsoleOutput(game, "Enable Lua scripting in Game Options / Advanced.");
    return;
  }
  RunCityLua(game, code, file);
}

static void ExecuteCityConsole(Game *game, const char *line) {
  static const struct { const char *code; CityCheat cheat; } kCheats[] = {
    {"fund", CITY_CHEAT_FUND}, {"cass", CITY_CHEAT_CASS}, {"imacheat", CITY_CHEAT_THE_WORKS},
    {"noah", CITY_CHEAT_NOAH}, {"moses", CITY_CHEAT_MOSES}, {"newhouse", CITY_CHEAT_FIRE_STORM},
    {"mrsoleary", CITY_CHEAT_FIRE_STORM}, {"priscilla", CITY_CHEAT_DEBUG}, {"gilmartin", CITY_CHEAT_MILITARY},
    {"joke", CITY_CHEAT_JOKE}, {"webb", CITY_CHEAT_WEBB}, {"damn", CITY_CHEAT_OOPS}, {"darn", CITY_CHEAT_OOPS},
    {"heck", CITY_CHEAT_OOPS}, {"mrsdoyle", CITY_CHEAT_REPENT}
  };
  if (!strcmp(line, "help")) {
    AppendCityConsoleOutput(game, "help | clear | cheat CODE | inventions | bind KEY ACTION | unbind KEY | bindings | "
        "run FILE.lua | soundfont FILE.sf2");
    AppendCityConsoleOutput(game, "Key actions: B, G, J, M, Ctrl+R, Ctrl+Shift+R, R, Shift+R, Space, Ctrl+S, F8...");
    AppendCityConsoleOutput(game, "Money: cheat imacheat adds 500,000 and unlocks special facilities "
        "(Restored cheat codes must be on).");
    AppendCityConsoleOutput(game, "Lua: print(city.summary().cash); city.tile('tiles', city.tilexy(20, 30)); "
        "city.command(id, tile, a, b, c)");
    AppendCityConsoleOutput(game, "Paste: Cmd/Ctrl+V. History: Up/Down. "
        "Output: wheel, Page Up/Down, Ctrl+Home/End or scrollbar.");
  } else if (!strcmp(line, "clear")) {
    game->ui->console.output[0] = 0;
    game->ui->console.output_scroll = game->ui->console.output_wheel = 0;
  }
  else if (!strncmp(line, "cheat ", 6)) {
    for (int i = 0; i < (int)(sizeof(kCheats) / sizeof(*kCheats)); ++i)
      if (!strcmp(line + 6, kCheats[i].code)) {
        i64 result = RequestCityCheat(game, kCheats[i].cheat);
        AppendCityConsoleOutput(game, result == CITY_COMMAND_NEEDS_CONFIRMATION ? "Awaiting confirmation." :
            result ? "Cheat applied." : "Cheat rejected. Check the Restored cheat codes setting.");
        return;
      }
    AppendCityConsoleOutput(game, "Unknown cheat code.");
  } else if (!strcmp(line, "inventions")) {
    AppendCityConsoleOutput(game,
        DoCityCommand(game->city, CITY_COMMAND_ALL_INVENTIONS, 0, INVALID_TILE, 0, 0, 0) >= 0 ?
        "All inventions available." : "Enable Repair Add All Inventions in Advanced settings.");
  } else if (!strncmp(line, "bind ", 5)) {
    bool ok = BindCityKey(game->ui, line + 5);
    if (ok) StoreGameSettings(game);
    AppendCityConsoleOutput(game, ok ? "Key binding saved." : "Could not save this binding.");
  } else if (!strncmp(line, "unbind ", 7)) {
    UnbindCityKey(game->ui, CityHotkeyFromString(line + 7));
    StoreGameSettings(game);
    AppendCityConsoleOutput(game, "Key binding removed.");
  } else if (!strcmp(line, "bindings")) {
    for (int i = 0; i < game->ui->console.binding_count; ++i) {
      char text[104], key[48], action[48];
      CityHotkeyToString(game->ui->console.bindings[i].key, key, sizeof(key));
      CityHotkeyToString(game->ui->console.bindings[i].action, action, sizeof(action));
      snprintf(text, sizeof(text), "%s -> %s", key, action);
      AppendCityConsoleOutput(game, text);
    }
  } else if (!strncmp(line, "soundfont ", 10)) {
    bool enabled = strcmp(line + 10, "default") != 0;
    bool ok = SetCitySoundFont(game->audio, enabled ? line + 10 : NULL, enabled);
    AppendCityConsoleOutput(game, ok ? "MIDI soundfont changed." : "Could not load this MIDI soundfont.");
    if (ok) StoreGameSettings(game);
  } else if (!strncmp(line, "run ", 4)) ExecuteCityLua(game, line + 4, true);
  else ExecuteCityLua(game, line, false);
}

void OpenCityDeveloperConsole(Game *game) {
  CityInterfaceState *ui = game->ui;
  ui->developer_enabled = true;
  if (FocusCityCanvasWindowKind(&ui->desktop, CITY_CANVAS_CONSOLE) >= 0) return;
  if (!ui->console.output[0])
    AppendCityConsoleOutput(game, "Native developer console. Enter help for commands; Escape closes.");
  int width = IntMin(520, ui->desktop.canvas_width), height = IntMin(270, ui->desktop.canvas_height - 18);
  Sc2kCanvasDesktopOpen(&ui->desktop, (Window){.kind = CITY_CANVAS_CONSOLE,
      .bounds = {0, 18, width, 18 + height}, .title = "Developer Console"});
  StoreGameSettings(game);
  ApplicationUpdateDialogPause(game->city);
}

bool HandleCityDeveloperShortcut(Game *game, const CanvasSDLEvent *event) {
  CityInterfaceState *ui = game->ui;
  if (event->kind == CANVAS_SDL_KEY_DOWN && event->key == CITY_KEY_CONSOLE) {
    if (ui->desktop.count && ui->desktop.windows[ui->desktop.count - 1].kind == CITY_CANVAS_CONSOLE) {
      Sc2kCanvasDesktopDismissWindow(&ui->desktop, ui->desktop.windows[ui->desktop.count - 1].window_id);
      ApplicationUpdateDialogPause(game->city);
    } else OpenCityDeveloperConsole(game);
    return true;
  }
  if (event->kind == CANVAS_SDL_KEY_DOWN && event->key == (CITY_HOTKEY_CTRL | CITY_KEY_F12) && ui->developer_enabled) {
    OpenCityDeveloperConsole(game);
    return true;
  }
  return false;
}

// Handles this window's painting and input; paint coordinates are local to the window.
bool ConsoleWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    Canvas *canvas = event->canvas;
    DrawCityDeveloperConsole(game, canvas, window);
    return true;
  }
  CityInterfaceState *ui = game->ui;
  if (event->kind == WINDOW_DESTROY) {
    ui->console.output_dragging = false;
    return true;
  }
  if (event->kind != WINDOW_INPUT) return false;
  const CanvasSDLEvent *input = event->input;
  u32 id = window->window_id;
  ConsoleLayout layout = ReadConsoleLayout(game, window->bounds);
  if (input->kind == CANVAS_SDL_TEXT_INPUT) {
    InsertCityTextInput(ui->console.input, sizeof(ui->console.input), &ui->console.caret, &ui->console.select_all,
        input->text, false);
    ui->console.history_cursor = ui->console.history_count;
  } else if (input->kind == CANVAS_SDL_KEY_DOWN) {
    CityHotkey key = input->key, navigation = key & ~CITY_HOTKEY_SHIFT;
    if (key == CITY_KEY_ESCAPE) {
      Sc2kCanvasDesktopDismissWindow(&ui->desktop, id);
      ApplicationUpdateDialogPause(game->city);
      return true;
    }
    if (key == (CITY_HOTKEY_CTRL | 'V') || key == (CITY_HOTKEY_SHIFT | CITY_KEY_INSERT)) {
      Sc2kSDLPasteText(ui->console.input, sizeof(ui->console.input), &ui->console.caret, &ui->console.select_all);
      ui->console.history_cursor = ui->console.history_count;
    } else if (key == CITY_KEY_RETURN) {
      char command[sizeof(ui->console.input)];
      CopyText(command, sizeof(command), ui->console.input);
      RememberConsoleCommand(ui, command);
      ui->console.history_cursor = ui->console.history_count;
      ui->console.draft[0] = 0;
      ui->console.input[0] = 0; ui->console.caret = ui->console.scroll = ui->console.select_all = 0;
      ui->console.output_scroll = ui->console.output_wheel = 0;
      AppendCityConsoleOutput(game, command);
      ExecuteCityConsole(game, command);
    } else if (key == CITY_KEY_UP || key == CITY_KEY_DOWN) RecallConsoleCommand(ui, key == CITY_KEY_UP ? -1 : 1);
    else if (navigation == CITY_KEY_PAGE_UP || navigation == CITY_KEY_PAGE_DOWN)
      ScrollConsoleOutput(ui, &layout, (navigation == CITY_KEY_PAGE_UP ? 1 : -1) * IntMax(1, layout.visible - 1));
    else if (key == (CITY_HOTKEY_CTRL | CITY_KEY_HOME)) ui->console.output_scroll = layout.scrollbar.maximum;
    else if (key == (CITY_HOTKEY_CTRL | CITY_KEY_END)) ui->console.output_scroll = 0;
    else if (HandleCityTextEditKey(ui->console.input, &ui->console.caret, &ui->console.select_all, key) &&
        (key == CITY_KEY_BACKSPACE || key == CITY_KEY_DELETE)) ui->console.history_cursor = ui->console.history_count;
  } else if (input->kind == CANVAS_SDL_SCROLL) {
    ui->console.output_wheel += input->y;
    ScrollConsoleOutput(ui, &layout, ui->console.output_wheel / 1024 * 3);
    ui->console.output_wheel %= 1024;
  } else if (input->kind == CANVAS_SDL_POINTER_DOWN && input->button == CANVAS_SDL_BUTTON_LEFT) {
    int x = event->x, y = event->y;
    if (CanvasPointInRect(layout.up, x, y)) ScrollConsoleOutput(ui, &layout, 1);
    else if (CanvasPointInRect(layout.down, x, y)) ScrollConsoleOutput(ui, &layout, -1);
    else if (CanvasPointInRect(layout.scrollbar.track, x, y)) {
      if (y < layout.scrollbar.thumb.top) ScrollConsoleOutput(ui, &layout, layout.visible);
      else if (y >= layout.scrollbar.thumb.bottom) ScrollConsoleOutput(ui, &layout, -layout.visible);
      else {
        ui->console.output_dragging = true;
        ui->console.output_scroll_grab = y - layout.scrollbar.thumb.top;
      }
    } else if (CanvasPointInRect(layout.input, x, y)) {
      ui->console.caret = CityTextBoxCaretAt(&game->resources->interface_font, layout.input, ui->console.input,
          ui->console.caret, &ui->console.scroll, x);
      ui->console.select_all = 0;
    } else return false;
  } else if (input->kind == CANVAS_SDL_POINTER_MOVE && ui->console.output_dragging) {
    int first = CityScrollbarPosition(&layout.scrollbar, event->y, ui->console.output_scroll_grab,
        CITY_SCROLLBAR_DRAG_NEAREST);
    ui->console.output_scroll = layout.scrollbar.maximum - first;
  } else if ((input->kind == CANVAS_SDL_POINTER_UP && input->button == CANVAS_SDL_BUTTON_LEFT) ||
      input->kind == CANVAS_SDL_FOCUS_LOST) {
    bool dragging = ui->console.output_dragging;
    ui->console.output_dragging = false;
    return dragging;
  } else return false;
  // Commands may open or close nested windows; reacquire by identity after execution.
  for (int i = 0; i < ui->desktop.count; ++i)
    if (ui->desktop.windows[i].window_id == id) Sc2kDirtyMarkRect(&ui->desktop.dirty, ui->desktop.windows[i].bounds);
  return true;
}
