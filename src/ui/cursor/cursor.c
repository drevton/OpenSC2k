#include "drawing/sprites/sources.h"
#include "ui/cursor/cursor.h"

#include "assets/cursors.h"
#include "assets/graphics.h"
#include "assets/state.h"
#include "city/maps/buildings.h"
#include "drawing/cursors.h"
#include "drawing/state.h"
#include "game/state.h"
#include "ui/menus/menu.h"
#include "ui/tools/tools.h"
#include "ui/tools/types.h"

// Cursor

_Static_assert(CITY_CANVAS_TOOL_COUNT == CITY_CURSOR_TOOL_CAPACITY,
    "UI tool cursor slots must match Drawing capacity");
_Static_assert(SC2K_CURSOR_ASSET_ICON_COUNT == CITY_CANVAS_CURSOR_ICON_COUNT,
    "Cursor crop IDs follow the data-file order");

// Upstream crops are available before the native graphics finish loading.
static const u8 kFallbackToolIcons[CITY_CANVAS_TOOL_COUNT] = {
  [CITY_CANVAS_TOOL_RESIDENTIAL] = CITY_CANVAS_CURSOR_ICON_WAREHOUSE,
  [CITY_CANVAS_TOOL_COMMERCIAL] = CITY_CANVAS_CURSOR_ICON_WAREHOUSE,
  [CITY_CANVAS_TOOL_INDUSTRIAL] = CITY_CANVAS_CURSOR_ICON_WAREHOUSE,
  [CITY_CANVAS_TOOL_DENSE_RESIDENTIAL] = CITY_CANVAS_CURSOR_ICON_WAREHOUSE,
  [CITY_CANVAS_TOOL_DENSE_COMMERCIAL] = CITY_CANVAS_CURSOR_ICON_WAREHOUSE,
  [CITY_CANVAS_TOOL_DENSE_INDUSTRIAL] = CITY_CANVAS_CURSOR_ICON_WAREHOUSE,
  [CITY_CANVAS_TOOL_POLICE] = CITY_CANVAS_CURSOR_ICON_WAREHOUSE,
  [CITY_CANVAS_TOOL_FIRE] = CITY_CANVAS_CURSOR_ICON_WAREHOUSE,
  [CITY_CANVAS_TOOL_HOSPITAL] = CITY_CANVAS_CURSOR_ICON_WAREHOUSE,
  [CITY_CANVAS_TOOL_SCHOOL] = CITY_CANVAS_CURSOR_ICON_WAREHOUSE,
  [CITY_CANVAS_TOOL_COAL] = CITY_CANVAS_CURSOR_ICON_WAREHOUSE,
  [CITY_CANVAS_TOOL_NUCLEAR] = CITY_CANVAS_CURSOR_ICON_WAREHOUSE,
  [CITY_CANVAS_TOOL_PIPES] = CITY_CANVAS_CURSOR_ICON_CELL_2_8,
  [CITY_CANVAS_TOOL_SUBWAY] = CITY_CANVAS_CURSOR_ICON_CELL_5_13,
  [CITY_CANVAS_TOOL_ROAD] = CITY_CANVAS_CURSOR_ICON_ROAD,
  [CITY_CANVAS_TOOL_RAIL] = CITY_CANVAS_CURSOR_ICON_RAIL,
  [CITY_CANVAS_TOOL_POWER_LINES] = CITY_CANVAS_CURSOR_ICON_FAST_FORWARD,
  [CITY_CANVAS_TOOL_DEMOLISH] = CITY_CANVAS_CURSOR_ICON_DEMOLISH,
  [CITY_CANVAS_TOOL_RAISE_TERRAIN] = CITY_CANVAS_CURSOR_ICON_RAISE,
  [CITY_CANVAS_TOOL_LOWER_TERRAIN] = CITY_CANVAS_CURSOR_ICON_LOWER,
  [CITY_CANVAS_TOOL_SUBWAY_STATION] = CITY_CANVAS_CURSOR_ICON_CELL_7_7,
  [CITY_CANVAS_TOOL_BUS_DEPOT] = CITY_CANVAS_CURSOR_ICON_CELL_8_6,
  [CITY_CANVAS_TOOL_RAIL_DEPOT] = CITY_CANVAS_CURSOR_ICON_CELL_7_7,
  [CITY_CANVAS_TOOL_WIND] = CITY_CANVAS_CURSOR_ICON_FAST_FORWARD,
  [CITY_CANVAS_TOOL_SOLAR] = CITY_CANVAS_CURSOR_ICON_FAST_FORWARD,
  [CITY_CANVAS_TOOL_MICROWAVE] = CITY_CANVAS_CURSOR_ICON_FAST_FORWARD,
  [CITY_CANVAS_TOOL_FUSION] = CITY_CANVAS_CURSOR_ICON_FAST_FORWARD,
  [CITY_CANVAS_TOOL_WATER_TOWER] = CITY_CANVAS_CURSOR_ICON_CELL_2_8,
  [CITY_CANVAS_TOOL_DESALINATION] = CITY_CANVAS_CURSOR_ICON_CELL_2_8,
  [CITY_CANVAS_TOOL_LIBRARY] = CITY_CANVAS_CURSOR_ICON_WAREHOUSE,
  [CITY_CANVAS_TOOL_MUSEUM] = CITY_CANVAS_CURSOR_ICON_WAREHOUSE,
  [CITY_CANVAS_TOOL_BIG_PARK] = CITY_CANVAS_CURSOR_ICON_TREE,
  [CITY_CANVAS_TOOL_ZOO] = CITY_CANVAS_CURSOR_ICON_TREE,
  [CITY_CANVAS_TOOL_PLYMOUTH_ARCOLOGY] = CITY_CANVAS_CURSOR_ICON_WAREHOUSE,
  [CITY_CANVAS_TOOL_FOREST_ARCOLOGY] = CITY_CANVAS_CURSOR_ICON_TREE,
  [CITY_CANVAS_TOOL_DARCO_ARCOLOGY] = CITY_CANVAS_CURSOR_ICON_WAREHOUSE,
  [CITY_CANVAS_TOOL_LAUNCH_ARCOLOGY] = CITY_CANVAS_CURSOR_ICON_WAREHOUSE,
  [CITY_CANVAS_TOOL_OIL] = CITY_CANVAS_CURSOR_ICON_WAREHOUSE,
  [CITY_CANVAS_TOOL_GAS] = CITY_CANVAS_CURSOR_ICON_WAREHOUSE,
  [CITY_CANVAS_TOOL_WATER_TREATMENT] = CITY_CANVAS_CURSOR_ICON_CELL_2_8,
  [CITY_CANVAS_TOOL_MAYORS_HOUSE] = CITY_CANVAS_CURSOR_ICON_WAREHOUSE,
  [CITY_CANVAS_TOOL_CITY_HALL] = CITY_CANVAS_CURSOR_ICON_WAREHOUSE,
  [CITY_CANVAS_TOOL_STATUE] = CITY_CANVAS_CURSOR_ICON_WAREHOUSE,
  [CITY_CANVAS_TOOL_LLAMA_DOME] = CITY_CANVAS_CURSOR_ICON_WAREHOUSE,
  [CITY_CANVAS_TOOL_COLLEGE] = CITY_CANVAS_CURSOR_ICON_WAREHOUSE,
  [CITY_CANVAS_TOOL_PRISON] = CITY_CANVAS_CURSOR_ICON_WAREHOUSE,
  [CITY_CANVAS_TOOL_SMALL_PARK] = CITY_CANVAS_CURSOR_ICON_TREE,
  [CITY_CANVAS_TOOL_STADIUM] = CITY_CANVAS_CURSOR_ICON_WAREHOUSE,
  [CITY_CANVAS_TOOL_WATER_PUMP] = CITY_CANVAS_CURSOR_ICON_CELL_2_8,
  [CITY_CANVAS_TOOL_TREES] = CITY_CANVAS_CURSOR_ICON_TREE,
  [CITY_CANVAS_TOOL_WATER] = CITY_CANVAS_CURSOR_ICON_WATER,
  [CITY_CANVAS_TOOL_STREAM] = CITY_CANVAS_CURSOR_ICON_WATER,
  [CITY_CANVAS_TOOL_HYDRO] = CITY_CANVAS_CURSOR_ICON_CELL_2_8,
  [CITY_CANVAS_TOOL_DISPATCH_POLICE] = CITY_CANVAS_CURSOR_ICON_CELL_4_5,
  [CITY_CANVAS_TOOL_DISPATCH_FIRE] = CITY_CANVAS_CURSOR_ICON_CELL_4_5,
  [CITY_CANVAS_TOOL_DISPATCH_MILITARY] = CITY_CANVAS_CURSOR_ICON_CELL_4_5,
  [CITY_CANVAS_TOOL_SIGN] = CITY_CANVAS_CURSOR_ICON_SIGN,
  [CITY_CANVAS_TOOL_QUERY] = CITY_CANVAS_CURSOR_ICON_QUERY,
  [CITY_CANVAS_TOOL_MARINA] = CITY_CANVAS_CURSOR_ICON_CELL_2_7,
  [CITY_CANVAS_TOOL_HIGHWAY] = CITY_CANVAS_CURSOR_ICON_ROAD,
  [CITY_CANVAS_TOOL_TUNNEL] = CITY_CANVAS_CURSOR_ICON_ROAD,
  [CITY_CANVAS_TOOL_ONRAMP] = CITY_CANVAS_CURSOR_ICON_RAIL,
  [CITY_CANVAS_TOOL_CENTER] = CITY_CANVAS_CURSOR_ICON_CELL_4_5,
  [CITY_CANVAS_TOOL_LEVEL_TERRAIN] = CITY_CANVAS_CURSOR_ICON_LEVEL,
  [CITY_CANVAS_TOOL_STRETCH_TERRAIN] = CITY_CANVAS_CURSOR_ICON_RAISE,
  [CITY_CANVAS_TOOL_DEZONE] = CITY_CANVAS_CURSOR_ICON_DEMOLISH,
  [CITY_CANVAS_TOOL_SEAPORT] = CITY_CANVAS_CURSOR_ICON_CELL_2_7,
  [CITY_CANVAS_TOOL_AIRPORT] = CITY_CANVAS_CURSOR_ICON_WAREHOUSE,
  [CITY_CANVAS_TOOL_SUBWAY_RAIL_CONNECTION] = CITY_CANVAS_CURSOR_ICON_RAIL,
  [CITY_CANVAS_TOOL_ROAD_BRIDGE] = CITY_CANVAS_CURSOR_ICON_ROAD,
  [CITY_CANVAS_TOOL_RAIL_BRIDGE] = CITY_CANVAS_CURSOR_ICON_RAIL,
};

// Compose tool cursors from startup-loaded artwork. Before loading completes,
// GetCityCursorSprite supplies the geometric pointer without attempting resource I/O.
static void InitializeCityCursor(Game *game) {
  if (game->drawing->cursors.state == CURSOR_CACHE_ARTWORK) return;
  if (!game->resources->cursors.pixels) return;
  for (int tool = 0; tool < CITY_CANVAS_TOOL_COUNT; ++tool)
    ComposeCityCursorIcon(game->drawing->cursors.tools[tool], &game->resources->cursors,
        game->resources->cursors.pixels + SC2K_CURSOR_ASSET_ICONS_OFFSET +
        kFallbackToolIcons[tool] * SC2K_CURSOR_ASSET_ICON_PIXELS);
  // Zones have their actual R/C/I colours; stripes distinguish dense zones.
  for (i32 tool = 0; tool < CITY_CANVAS_TOOL_POLICE; ++tool) {
    const u32 color[] = {0xFF32B044u, 0xFF328BE0u, 0xFFE8C43Cu};
    ComposeCityZoneCursorIcon(game->drawing->cursors.tools[tool], &game->resources->cursors,
        color[tool % 3], tool >= CITY_CANVAS_TOOL_DENSE_RESIDENTIAL);
  }
  game->drawing->cursors.state = CURSOR_CACHE_ARTWORK;
  ++game->drawing->cursors.revision;
}

const u32 *GetCityCursorSprite(Game *game, int tool) {
  InitializeCityCursor(game);
  if (!game->resources->cursors.pixels) return PrepareCityFallbackCursor(&game->drawing->cursors);
  return tool >= 0 && tool < CITY_CANVAS_TOOL_COUNT ? game->drawing->cursors.tools[tool] :
      game->resources->cursors.pixels + SC2K_CURSOR_ASSET_DEFAULT_OFFSET;
}

// UI selects the tool image and records a Drawing preparation failure on Game.
void DrawCityCursor(Game *game, Canvas *canvas, i32 tool, i32 x, i32 y, i32 scale) {
  if (!canvas || !canvas->pixels || scale < 100 || scale > 500) return;
  if (!DrawCityCursorSprite(canvas, GetCityCursorSprite(game, tool), x, y, scale))
    game->execution_failed = true;
}

void PrepareCityToolCursors(Game *game, const u32 palette[256]) {
  InitializeCityCursor(game);
  if (!game->resources->cursors.pixels || !palette) return;
  // A later allocation failure may leave earlier badges updated. Invalidate host copies before those writes.
  ++game->drawing->cursors.revision;

  for (i32 tool = 0; tool < CITY_CANVAS_TOOL_COUNT; ++tool) {
    u32 icon[40 * 40];
    TileKind kind = CityToolBuildingKind(tool);
    if (kind != TK_CLEAR && DecodeCityBuildingCursorIcon(game->city, kind, palette, icon)) {
      ComposeCityCursorIcon(game->drawing->cursors.tools[tool], &game->resources->cursors, icon);
      continue;
    }
    const CityInterfaceIcons *toolbar = &game->resources->main_toolbar_icons;
    if (!toolbar->data.data) continue;
    int atlas = tool == CITY_CANVAS_TOOL_STRETCH_TERRAIN ? CITY_TOOLBAR_STRETCH_TERRAIN_ICON :
        CITY_TOPBAR_ICON_COUNT + (tool == CITY_CANVAS_TOOL_STREAM ? CITY_CANVAS_TOOL_WATER : tool);
    InterfaceSpriteSource source = GetInterfaceSprite(toolbar, atlas, 1);
    // Wide bridge cells retain their 2:1 silhouette inside the cursor badge.
    int height = CityToolbarIconColumns(atlas) == 2 ? 20 : 40;
    if (!DecodeCityInterfaceCursorIcon(source, height, icon)) { game->execution_failed = true; return; }
    ComposeCityCursorIcon(game->drawing->cursors.tools[tool], &game->resources->cursors, icon);
  }
}
