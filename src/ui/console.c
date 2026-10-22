#include "ui/windows/window_internal.h"
#include "ui/console.h"

#include <ctype.h>
#include <lauxlib.h>
#include <limits.h>
#include <lua.h>
#include <lualib.h>
#include <stdlib.h>

#include "assets/state.h"
#include "audio/backend.h"
#include "city/engine/cheats.h"
#include "city/maps/layers.h"
#include "drawing/fonts/fonts.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/raster/canvas_raster_internal.h"
#include "game/errors.h"
#include "game/state.h"
#include "ui/commands.h"
#include "ui/dialogs/dialogs.h"
#include "ui/sdl.h"
#include "ui/state.h"
#include "ui/windows/controls.h"
#include "ui/windows/windows.h"

// Native console, Lua bindings and persistent shortcut overrides.

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

static void ConsoleOutput(Game *game, const char *text) {
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

static Game *LuaGame(lua_State *state) { return *(Game **)lua_getextraspace(state); }

static int LuaPrint(lua_State *state) {
  char line[2048] = {0};
  size_t length = 0;
  for (int i = 1; i <= lua_gettop(state); ++i) {
    size_t size;
    const char *text = luaL_tolstring(state, i, &size);
    if (i > 1 && length + 1 < sizeof(line)) line[length++] = '\t';
    size = IntMin(size, sizeof(line) - length - 1);
    memcpy(line + length, text, size);
    length += size;
    lua_pop(state, 1);
  }
  line[length] = 0;
  ConsoleOutput(LuaGame(state), line);
  return 0;
}

static int LuaInteger(lua_State *state, int index, int fallback) {
  lua_Integer value = luaL_optinteger(state, index, fallback);
  if (value < INT_MIN || value > INT_MAX) luaL_argerror(state, index, "integer outside native command range");
  return value;
}

static int LuaCityCommand(lua_State *state) {
  Game *game = LuaGame(state);
  int id = LuaInteger(state, 1, 0);
  lua_Integer value = luaL_optinteger(state, 2, -1);
  if (value < -1 || value >= game->city->map_cell_count) return luaL_argerror(state, 2, "invalid tile index");
  int first = LuaInteger(state, 3, 0), second = LuaInteger(state, 4, 0), third = LuaInteger(state, 5, 0);
  i64 result = DoCityCommand(game->city, id, 0, value < 0 ? INVALID_TILE : (TileIndex)value, first, second, third);
  lua_pushinteger(state, result);
  lua_pushinteger(state, GetGameError(game));
  return 2;
}

static int LuaTile(lua_State *state) {
  City *city = LuaGame(state)->city;
  const char *layer = luaL_checkstring(state, 1);
  int tile = LuaInteger(state, 2, -1);
  if (tile < 0 || tile >= city->map_cell_count) return luaL_argerror(state, 2, "invalid tile index");
  int value;
  if (!strcmp(layer, "tiles")) value = city->tile[tile].kind;
  else if (!strcmp(layer, "terrain")) value = city->tile[tile].terrain;
  else if (!strcmp(layer, "flags")) value = city->tile[tile].flags;
  else if (!strcmp(layer, "zones")) value = ReadTileZoneByte(&city->tile[tile]);
  else if (!strcmp(layer, "underground")) value = city->tile[tile].underground;
  else if (!strcmp(layer, "altitude")) value = city->tile[tile].altitude;
  else if (!strcmp(layer, "overlays")) value = city->tile[tile].overlay;
  else return luaL_argerror(state, 1, "unknown map layer");
  lua_pushinteger(state, value);
  return 1;
}

static int LuaTileXY(lua_State *state) {
  City *city = LuaGame(state)->city;
  int x = LuaInteger(state, 1, -1), y = LuaInteger(state, 2, -1);
  TileIndex tile = MapTileXYOrInvalid(city, x, y);
  if (tile == INVALID_TILE) return luaL_error(state, "coordinates outside the city");
  lua_pushinteger(state, tile);
  return 1;
}

static void LuaNumber(lua_State *state, const char *name, lua_Integer value) {
  lua_pushinteger(state, value);
  lua_setfield(state, -2, name);
}

static int LuaCitySummary(lua_State *state) {
  City *city = LuaGame(state)->city;
  lua_newtable(state);
  LuaNumber(state, "width", city->map_width); LuaNumber(state, "height", city->map_height);
  LuaNumber(state, "tick", city->clock.tick); LuaNumber(state, "cash", city->indicators.cash);
  LuaNumber(state, "population", city->indicators.population); LuaNumber(state, "fixes", city->fixes);
  lua_pushstring(state, CityNameText(&city->name)); lua_setfield(state, -2, "name");
  return 1;
}

static void LuaBudget(lua_State *state, lua_Debug *debug) {
  (void)debug;
  Game *game = LuaGame(state);
  game->script_instructions -= 1000;
  if (game->script_instructions <= 0) luaL_error(state, "script instruction budget exceeded");
}

static bool InitializeCityScripting(Game *game) {
  if (game->script_state) return true;
  lua_State *state = luaL_newstate();
  if (!state) return false;
  game->script_state = state;
  *(Game **)lua_getextraspace(state) = game;
  // Deliberately expose native commands and values, never process memory or executable I/O.
  static const struct { const char *name; lua_CFunction open; } kLibraries[] = {
    {LUA_GNAME, luaopen_base}, {LUA_TABLIBNAME, luaopen_table}, {LUA_STRLIBNAME, luaopen_string},
    {LUA_MATHLIBNAME, luaopen_math}, {LUA_UTF8LIBNAME, luaopen_utf8}
  };
  for (int i = 0; i < (int)(sizeof(kLibraries) / sizeof(*kLibraries)); ++i) {
    luaL_requiref(state, kLibraries[i].name, kLibraries[i].open, 1);
    lua_pop(state, 1);
  }
  lua_pushnil(state); lua_setglobal(state, "dofile");
  lua_pushnil(state); lua_setglobal(state, "loadfile");
  lua_pushcfunction(state, LuaPrint); lua_setglobal(state, "print");
  lua_newtable(state);
  lua_pushcfunction(state, LuaCityCommand); lua_setfield(state, -2, "command");
  lua_pushcfunction(state, LuaTile); lua_setfield(state, -2, "tile");
  lua_pushcfunction(state, LuaTileXY); lua_setfield(state, -2, "tilexy");
  lua_pushcfunction(state, LuaCitySummary); lua_setfield(state, -2, "summary");
  LuaNumber(state, "SET_FIX", CITY_COMMAND_SET_FIX); LuaNumber(state, "CHEAT", CITY_COMMAND_CHEAT);
  LuaNumber(state, "ALL_INVENTIONS", CITY_COMMAND_ALL_INVENTIONS);
  LuaNumber(state, "PLACE_BUILDING", CITY_COMMAND_PLACE_BUILDING); LuaNumber(state, "ROAD", CITY_COMMAND_PLACE_ROAD);
  LuaNumber(state, "RAIL", CITY_COMMAND_PLACE_RAIL); LuaNumber(state, "POWER", CITY_COMMAND_PLACE_POWER_LINE);
  LuaNumber(state, "PIPE", CITY_COMMAND_PLACE_WATER_PIPE); LuaNumber(state, "SUBWAY", CITY_COMMAND_PLACE_SUBWAY);
  LuaNumber(state, "SPEED", CITY_COMMAND_SET_SIMULATION_SPEED);
  lua_setglobal(state, "city");
  return true;
}

void CloseCityScripting(Game *game) {
  if (game->script_state) lua_close(game->script_state);
  game->script_state = NULL;
}

static void ExecuteCityLua(Game *game, const char *code, bool file) {
  if (!game->ui->lua_enabled) { ConsoleOutput(game, "Enable Lua scripting in Game Options / Advanced."); return; }
  if (game->script_running) { ConsoleOutput(game, "The current script is still running."); return; }
  if (file && (strlen(code) < 4 || strcmp(code + strlen(code) - 4, ".lua"))) {
    ConsoleOutput(game, "Scripts must have a .lua filename."); return;
  }
  if (!InitializeCityScripting(game)) { ConsoleOutput(game, "Could not allocate Lua state."); return; }
  lua_State *state = game->script_state;
  game->script_running = true;
  game->script_instructions = 1000000;
  lua_sethook(state, LuaBudget, LUA_MASKCOUNT, 1000);
  int result = file ? luaL_loadfilex(state, code, "t") : luaL_loadbufferx(state, code, strlen(code), "console", "t");
  if (!result) result = lua_pcall(state, 0, LUA_MULTRET, 0);
  if (result) ConsoleOutput(game, lua_tostring(state, -1));
  else if (lua_gettop(state)) LuaPrint(state);
  lua_settop(state, 0);
  lua_sethook(state, NULL, 0, 0);
  game->script_running = false;
}

CityHotkey RemapCityKey(const CityInterfaceState *ui, CityHotkey key) {
  for (int i = 0; i < ui->console.binding_count; ++i)
    if (ui->console.bindings[i].key == key) return ui->console.bindings[i].action;
  return key;
}

static bool BindCityKey(Game *game, const char *key_text, const char *action_text) {
  CityHotkey key = CityHotkeyFromString(key_text), action = CityHotkeyFromString(action_text);
  if (!key || !action || key == CITY_KEY_F12 || key == CITY_KEY_ESCAPE || key == CITY_KEY_CONSOLE) return false;
  CityInterfaceState *ui = game->ui;
  int index = 0;
  while (index < ui->console.binding_count && ui->console.bindings[index].key != key) ++index;
  if (index == 64) return false;
  if (index == ui->console.binding_count) ++ui->console.binding_count;
  ui->console.bindings[index].key = key;
  ui->console.bindings[index].action = action;
  return true;
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
    ConsoleOutput(game, "help | clear | cheat CODE | inventions | bind KEY ACTION | unbind KEY | bindings | "
        "run FILE.lua | soundfont FILE.sf2");
    ConsoleOutput(game, "Key actions: B, G, J, M, Ctrl+R, Ctrl+Shift+R, R, Shift+R, Space, Ctrl+S, F8...");
    ConsoleOutput(game, "Money: cheat imacheat adds 500,000 and unlocks special facilities "
        "(Restored cheat codes must be on).");
    ConsoleOutput(game, "Lua: print(city.summary().cash); city.tile('tiles', city.tilexy(20, 30)); "
        "city.command(id, tile, a, b, c)");
    ConsoleOutput(game, "Paste: Cmd/Ctrl+V. History: Up/Down. "
        "Output: wheel, Page Up/Down, Ctrl+Home/End or scrollbar.");
  } else if (!strcmp(line, "clear")) {
    game->ui->console.output[0] = 0;
    game->ui->console.output_scroll = game->ui->console.output_wheel = 0;
  }
  else if (!strncmp(line, "cheat ", 6)) {
    for (int i = 0; i < (int)(sizeof(kCheats) / sizeof(*kCheats)); ++i)
      if (!strcmp(line + 6, kCheats[i].code)) {
        i64 result = RequestCityCheat(game, kCheats[i].cheat);
        ConsoleOutput(game, result == CITY_COMMAND_NEEDS_CONFIRMATION ? "Awaiting confirmation." :
            result ? "Cheat applied." : "Cheat rejected. Check the Restored cheat codes setting.");
        return;
      }
    ConsoleOutput(game, "Unknown cheat code.");
  } else if (!strcmp(line, "inventions")) {
    ConsoleOutput(game, DoCityCommand(game->city, CITY_COMMAND_ALL_INVENTIONS, 0, INVALID_TILE, 0, 0, 0) >= 0 ?
        "All inventions available." : "Enable Repair Add All Inventions in Advanced settings.");
  } else if (!strncmp(line, "bind ", 5)) {
    char key[48], action[48], extra;
    bool ok = sscanf(line + 5, "%47s %47s %c", key, action, &extra) == 2 && BindCityKey(game, key, action);
    if (ok) StoreGameSettings(game);
    ConsoleOutput(game, ok ? "Key binding saved." : "Could not save this binding.");
  } else if (!strncmp(line, "unbind ", 7)) {
    CityInterfaceState *ui = game->ui;
    for (int i = 0; i < ui->console.binding_count; ++i)
      if (ui->console.bindings[i].key == CityHotkeyFromString(line + 7)) {
        memmove(&ui->console.bindings[i], &ui->console.bindings[i + 1],
            (ui->console.binding_count - i - 1) * sizeof(ui->console.bindings[0]));
        --ui->console.binding_count;
        break;
      }
    StoreGameSettings(game);
    ConsoleOutput(game, "Key binding removed.");
  } else if (!strcmp(line, "bindings")) {
    for (int i = 0; i < game->ui->console.binding_count; ++i) {
      char text[104], key[48], action[48];
      CityHotkeyToString(game->ui->console.bindings[i].key, key, sizeof(key));
      CityHotkeyToString(game->ui->console.bindings[i].action, action, sizeof(action));
      snprintf(text, sizeof(text), "%s -> %s", key, action);
      ConsoleOutput(game, text);
    }
  } else if (!strncmp(line, "soundfont ", 10)) {
    bool enabled = strcmp(line + 10, "default") != 0;
    bool ok = SetCitySoundFont(game->audio, enabled ? line + 10 : NULL, enabled);
    ConsoleOutput(game, ok ? "MIDI soundfont changed." : "Could not load this MIDI soundfont.");
    if (ok) StoreGameSettings(game);
  } else if (!strncmp(line, "run ", 4)) ExecuteCityLua(game, line + 4, true);
  else ExecuteCityLua(game, line, false);
}

void OpenCityDeveloperConsole(Game *game) {
  CityInterfaceState *ui = game->ui;
  ui->developer_enabled = true;
  if (FocusCityCanvasWindowKind(&ui->desktop, CITY_CANVAS_CONSOLE) >= 0) return;
  if (!ui->console.output[0]) ConsoleOutput(game, "Native developer console. Enter help for commands; Escape closes.");
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
      ConsoleOutput(game, command);
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
