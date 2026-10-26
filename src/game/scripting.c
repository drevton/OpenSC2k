#include "game/scripting.h"

#include <lauxlib.h>
#include <limits.h>
#include <lua.h>
#include <lualib.h>
#include <string.h>

#include "city/engine/city_name.h"
#include "city/engine/commands.h"
#include "city/state.h"
#include "game/errors.h"
#include "game/state.h"
#include "ui/console.h"

// Game owns the retained interpreter; UI supplies input and presents its output.

static Game *LuaGame(lua_State *state) { return *(Game **)lua_getextraspace(state); }

// Keep each converted string on the Lua stack until copied; its size may exceed the native int range.
static int LuaPrint(lua_State *state) {
  char line[2048] = {0};
  size_t length = 0;
  for (int i = 1; i <= lua_gettop(state); ++i) {
    size_t size;
    const char *text = luaL_tolstring(state, i, &size);
    if (i > 1 && length + 1 < sizeof(line)) line[length++] = '\t';
    size_t available = sizeof(line) - length - 1;
    if (size > available) size = available;
    memcpy(line + length, text, size);
    length += size;
    lua_pop(state, 1);
  }
  line[length] = 0;
  AppendCityConsoleOutput(LuaGame(state), line);
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
  const City *city = LuaGame(state)->city;
  const char *layer = luaL_checkstring(state, 1);
  // Unsigned admission rejects negative Lua integers as out-of-map indexes.
  TileIndex tile = LuaInteger(state, 2, -1);
  if (tile >= city->map_cell_count) return luaL_argerror(state, 2, "invalid tile index");
  const TileInfo *tp = &city->tile[tile];
  int value;
  if (!strcmp(layer, "tiles")) value = tp->kind;
  else if (!strcmp(layer, "terrain")) value = tp->terrain;
  else if (!strcmp(layer, "flags")) value = tp->flags;
  else if (!strcmp(layer, "zones")) value = ReadTileZoneByte(tp);
  else if (!strcmp(layer, "underground")) value = tp->underground;
  else if (!strcmp(layer, "altitude")) value = tp->altitude;
  else if (!strcmp(layer, "overlays")) value = tp->overlay;
  else return luaL_argerror(state, 1, "unknown map layer");
  lua_pushinteger(state, value);
  return 1;
}

static int LuaTileXY(lua_State *state) {
  const City *city = LuaGame(state)->city;
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
  const City *city = LuaGame(state)->city;
  lua_newtable(state);
  LuaNumber(state, "width", city->map_width);
  LuaNumber(state, "height", city->map_height);
  LuaNumber(state, "tick", city->clock.tick);
  LuaNumber(state, "cash", city->indicators.cash);
  LuaNumber(state, "population", city->indicators.population);
  LuaNumber(state, "fixes", city->fixes);
  lua_pushstring(state, CityNameText(&city->name));
  lua_setfield(state, -2, "name");
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
  static const struct {
    const char *name;
    lua_CFunction open;
  } kLibraries[] = {
    {LUA_GNAME, luaopen_base}, {LUA_TABLIBNAME, luaopen_table}, {LUA_STRLIBNAME, luaopen_string},
    {LUA_MATHLIBNAME, luaopen_math}, {LUA_UTF8LIBNAME, luaopen_utf8}
  };
  for (int i = 0; i < (int)(sizeof(kLibraries) / sizeof(*kLibraries)); ++i) {
    luaL_requiref(state, kLibraries[i].name, kLibraries[i].open, 1);
    lua_pop(state, 1);
  }
  lua_pushnil(state);
  lua_setglobal(state, "dofile");
  lua_pushnil(state);
  lua_setglobal(state, "loadfile");
  lua_pushcfunction(state, LuaPrint);
  lua_setglobal(state, "print");
  lua_newtable(state);
  lua_pushcfunction(state, LuaCityCommand);
  lua_setfield(state, -2, "command");
  lua_pushcfunction(state, LuaTile);
  lua_setfield(state, -2, "tile");
  lua_pushcfunction(state, LuaTileXY);
  lua_setfield(state, -2, "tilexy");
  lua_pushcfunction(state, LuaCitySummary);
  lua_setfield(state, -2, "summary");
  LuaNumber(state, "SET_FIX", CITY_COMMAND_SET_FIX);
  LuaNumber(state, "CHEAT", CITY_COMMAND_CHEAT);
  LuaNumber(state, "ALL_INVENTIONS", CITY_COMMAND_ALL_INVENTIONS);
  LuaNumber(state, "PLACE_BUILDING", CITY_COMMAND_PLACE_BUILDING);
  LuaNumber(state, "ROAD", CITY_COMMAND_PLACE_ROAD);
  LuaNumber(state, "RAIL", CITY_COMMAND_PLACE_RAIL);
  LuaNumber(state, "POWER", CITY_COMMAND_PLACE_POWER_LINE);
  LuaNumber(state, "PIPE", CITY_COMMAND_PLACE_WATER_PIPE);
  LuaNumber(state, "SUBWAY", CITY_COMMAND_PLACE_SUBWAY);
  LuaNumber(state, "SPEED", CITY_COMMAND_SET_SIMULATION_SPEED);
  lua_setglobal(state, "city");
  return true;
}

void CloseCityScripting(Game *game) {
  if (game->script_state) lua_close(game->script_state);
  game->script_state = NULL;
}

void RunCityLua(Game *game, const char *code, bool file) {
  if (game->script_running) {
    AppendCityConsoleOutput(game, "The current script is still running.");
    return;
  }
  size_t length = strlen(code);
  if (file && (length < 4 || strcmp(code + length - 4, ".lua"))) {
    AppendCityConsoleOutput(game, "Scripts must have a .lua filename.");
    return;
  }
  if (!InitializeCityScripting(game)) {
    AppendCityConsoleOutput(game, "Could not allocate Lua state.");
    return;
  }
  lua_State *state = game->script_state;
  game->script_running = true;
  game->script_instructions = 1000000;
  lua_sethook(state, LuaBudget, LUA_MASKCOUNT, 1000);
  int result = file ? luaL_loadfilex(state, code, "t") : luaL_loadbufferx(state, code, length, "console", "t");
  if (!result) result = lua_pcall(state, 0, LUA_MULTRET, 0);
  if (!result && lua_gettop(state)) {
    // Result conversion can invoke __tostring or exhaust the still-active instruction budget.
    // Give the printer the original results as arguments inside a second protected call.
    int count = lua_gettop(state);
    if (lua_checkstack(state, 1)) {
      lua_pushcfunction(state, LuaPrint);
      lua_insert(state, 1);
      result = lua_pcall(state, count, 0, 0);
    } else AppendCityConsoleOutput(game, "Could not allocate Lua result stack.");
  }
  if (result) {
    const char *error = lua_tostring(state, -1);
    AppendCityConsoleOutput(game, error ? error : "Lua script failed with a non-text error.");
  }
  lua_settop(state, 0);
  lua_sethook(state, NULL, 0, 0);
  game->script_running = false;
}
