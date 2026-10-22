#include "game/update.h"
#include "city/infrastructure/lines.h"
#include "city/infrastructure/spans.h"
#include "city/maps/terrain.h"
#include <stdlib.h>

#include "assets/state.h"
#include "game/audio.h"
#include "audio/backend.h"
#include "audio/state.h"
#include "base/time.h"
#include "city/economy/economy.h"
#include "city/infrastructure/bridges.h"
#include "city/infrastructure/highways.h"
#include "city/infrastructure/tunnels.h"
#include "city/infrastructure/types.h"
#include "city/maps/buildings.h"
#include "city/maps/layers.h"
#include "city/maps/tile_storage.h"
#include "city/maps/trees.h"
#include "city/maps/water.h"
#include "city/presentation.h"
#include "city/state.h"
#include "city/tools/tools.h"
#include "city/tools/types.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/state.h"
#include "game/commands.h"
#include "game/state.h"
#include "ui/map_visibility.h"
#include "ui/cursor/cursor.h"
#include "ui/charts/charts.h"
#include "ui/commands.h"
#include "ui/console.h"
#include "ui/dialogs/dialogs.h"
#include "ui/dialogs/file_dialog.h"
#include "ui/dialogs/information.h"
#include "ui/dialogs/scenario.h"
#include "ui/dialogs/spans.h"
#include "ui/input.h"
#include "ui/map.h"
#include "ui/map_views.h"
#include "ui/menus/menu.h"
#include "ui/signs.h"
#include "ui/sdl.h"
#include "ui/state.h"
#include "ui/text/text.h"
#include "ui/tools/tools.h"
#include "ui/tools/types.h"
#include "ui/windows/controls.h"
#include "ui/windows/state.h"
#include "ui/windows/tooltips.h"
#include "ui/windows/windows.h"

static u32 CityPointerModifiers(const CanvasSDLEvent *event);
static void TrackCityPointerEndpoint(CityInterfaceState *ui, const CanvasSDLEvent *event, bool released);
static void TrackCityPlacementPointer(CityInterfaceState *ui, const CanvasSDLEvent *event);
static void FinishCityPointerStroke(City *city, const CanvasSDLEvent *event);
static void HandleCityCanvasContentPress(City *city, const CanvasSDLEvent *map_event);

void UpdateCityToolStatus(City *city) {
  Game *game = city->game;
  CityInterfaceState *ui = game->ui;
  FormatCityToolStatusText(game->resources, ui->status.tool_text, sizeof(ui->status.tool_text),
      ui->desktop.selected_tool);
}

static u32 CityPointerModifiers(const CanvasSDLEvent *event) {
  return (u32)event->alt * CITY_POINTER_MODIFIER_ALT |
      (u32)event->shift * CITY_POINTER_MODIFIER_SHIFT |
      (u32)event->ctrl * CITY_POINTER_MODIFIER_CONTROL;
}

static void TrackCityPointerEndpoint(CityInterfaceState *ui, const CanvasSDLEvent *event, bool released) {
  int index = ui->pointer_stroke_count ? 1 : 0;
  ui->pointer_stroke[index] = (CityPointerInput){{event->x, event->y},
      CityPointerModifiers(event) | (released ? CITY_POINTER_SAMPLE_RELEASE : 0)};
  ui->pointer_stroke_count = index + 1;
}

static bool AppendCityPlacementRectangle(City *city, CityRect rectangle, CityRect visible, int *count) {
  rectangle = IntersectCityRects(rectangle,
      (CityRect){IntMax(0, visible.left), IntMax(0, visible.top),
          IntMin(city->map_width, visible.right), IntMin(city->map_height, visible.bottom)});
  if (rectangle.left >= rectangle.right || rectangle.top >= rectangle.bottom) return true;
  int added = (rectangle.right - rectangle.left) * (rectangle.bottom - rectangle.top);
  CityCanvasDesktop *desktop = &city->game->ui->desktop;
  if (!ReserveCityPlacementCells(desktop, *count + added)) return false;
  for (int y = rectangle.top; y < rectangle.bottom; ++y) {
    TileIndex tile = MapTileXY(city, rectangle.left, y);
    for (int x = rectangle.left; x < rectangle.right; ++x)
      desktop->placement.scratch[(*count)++] = (CityCanvasPlacementCell){.tile = tile++};
  }
  return true;
}
static bool AppendCityPlacementSpan(City *city, TileIndex start, int direction, bool choose_direction,
    CityRect visible, int *count) {
  CityInfrastructureSpan span = FindCityInfrastructureSpan(city, start, direction, choose_direction);
  if (span.end == INVALID_TILE) return true;
  int x = MapTileX(city, span.start), y = MapTileY(city, span.start);
  int end_x = MapTileX(city, span.end), end_y = MapTileY(city, span.end);
  return AppendCityPlacementRectangle(city, (CityRect){IntMin(x, end_x), IntMin(y, end_y),
      IntMax(x, end_x) + 1, IntMax(y, end_y) + 1}, visible, count);
}

static bool AppendCityPlacementHighwaySpan(City *city, TileIndex start, int direction, bool choose_direction,
    CityRect visible, int *count) {
  TileIndex anchor = CityHighwayPlacementAnchor(city, start);
  if (anchor == INVALID_TILE) return true;
  CityPoint position = MapTilePt(city, anchor);
  if (!CityHighwayFootprintHasWater(city, anchor)) return true;
  CityInfrastructureSpan span = FindCityHighwaySpan(city, start, direction, choose_direction);
  if (span.end == INVALID_TILE) return true;
  int end_x = position.x + 2 * (span.length - 1) * kLineXStep[span.direction];
  int end_y = position.y + 2 * (span.length - 1) * kLineYStep[span.direction];
  return AppendCityPlacementRectangle(city, (CityRect){IntMin(position.x, end_x), IntMin(position.y, end_y),
      IntMax(position.x, end_x) + 2, IntMax(position.y, end_y) + 2}, visible, count);
}

static int CityStretchTargetHeight(City *city, int pointer_y) {
  CityInterfaceState *ui = city->game->ui;
  if (!IsMapTileValid(city, ui->pointer_terrain_tile)) return ui->pointer_terrain_height;
  int pixels = ui->pointer_terrain_pixels;
  int delta = ui->pointer_stroke[0].point.y - pointer_y;
  int maximum = ReadCityTileGeometry(city, ui->pointer_terrain_tile).land_shape == CITY_TERRAIN_FLAT ? 30 : 29;
  return IntClamp(ui->pointer_terrain_height + delta / pixels, 0, maximum);
}

static TileIndex PickCityPlacementTile(City *city, int x, int y) {
  const CityMapViewConfig *view = city->game->drawing->default_map_view;
  return city->game->ui->desktop.selected_tool == CITY_CANVAS_TOOL_LEVEL_TERRAIN ?
      PickCityMapTerrainPoint(city, view, x, y) : PickCityMapTile(city, view, x, y);
}

static bool IsLiveCityPlacementStroke(const Game *game) {
  const CityInterfaceState *ui = game->ui;
  if (game->observation || !ui->pointer_stroke_count ||
      (ui->pointer_stroke_flags & (CITY_POINTER_MODIFIER_CONTROL | CITY_POINTER_UNDERGROUND))) return false;
  int group = ui->pointer_stroke_tools & 255, variant = (ui->pointer_stroke_tools >> 8) & 255;
  return GetCityFacilityTile(group, variant) != TK_CLEAR ||
      (group == CITY_TOOL_GROUP_POWER && variant == CITY_POWER_HYDRO) ||
      (group == CITY_TOOL_GROUP_ROAD && variant == CITY_ROAD_ONRAMP) ||
      (group == CITY_TOOL_GROUP_RAIL && variant == CITY_RAIL_SUBWAY_RAIL_CONNECTION);
}

// Follow the screen path between mouse samples, including fast motion across several tiles.
// Keep the last attempted tile in UI; City still owns placement rules and payment.
static void ApplyLiveCityPlacements(City *city, const CanvasSDLEvent *event) {
  Game *game = city->game;
  CityInterfaceState *ui = game->ui;
  if (event->shift || event->ctrl) return;
  int tools = ui->pointer_stroke_tools;
  int group = tools & 255, variant = (tools >> 8) & 255;
  int building = group == CITY_TOOL_GROUP_POWER && variant == CITY_POWER_HYDRO ?
      TK_HYDRO_POWER : GetCityFacilityTile(group, variant);
  CityPoint first = ui->pointer_stroke[ui->pointer_stroke_count - 1].point;
  int dx = event->x - first.x, dy = event->y - first.y;
  int steps = IntMax(1, IntMax(abs(dx), abs(dy)));
  for (int step = 0; step <= steps; ++step) {
    int x = first.x + (i64)dx * step / steps, y = first.y + (i64)dy * step / steps;
    if (!IsCityMainMapInteractiveAt(city, x, y)) {
      ui->pointer_terrain_tile = INVALID_TILE;
      continue;
    }
    TileIndex tile = PickCityMapTile(city, game->drawing->default_map_view, x, y);
    if (tile == ui->pointer_terrain_tile) continue;
    ui->pointer_terrain_tile = tile;
    if (building != TK_CLEAR) {
      if (GetCityBuildingCost(city, tile, building).error != CITY_COMMAND_COST_OK) continue;
    } else if (!CanPlaceCityToolCell(city, tile, group, variant, 0)) continue;
    CityPlacementIntent intent = ResolveCityPlacementIntent(game, tools, tile, tile, CityPointerModifiers(event));
    u32 previous_window = ui->desktop.next_window_id;
    bool placed = RunCityMapToolAction(game, &intent);
    // A placement question keeps its normal caller and choices. End this local
    // stroke when it opens so later mouse samples cannot bypass the dialog.
    if (!placed || ui->desktop.next_window_id != previous_window) {
      ui->pointer_stroke_count = 0;
      ReleaseCityMapMouseCapture(game);
      return;
    }
  }
}

// Mouse motion resolves absolute heights from the original grab point. Only new target heights enter the shared
// command stream; returning toward the starting height submits a new edit, and release cannot repeat the last one.
static void ApplyLiveCityStretch(City *city, const CanvasSDLEvent *event) {
  CityInterfaceState *ui = city->game->ui;
  if (event->shift || event->ctrl) return;
  int height = CityStretchTargetHeight(city, event->y);
  if (height == ui->pointer_terrain_last_height) return;
  TileIndex tile = ui->pointer_terrain_tile;
  ui->pointer_terrain_last_height = height;
  int options = ui->terrain_no_seawater ? CITY_TERRAIN_NO_SEAWATER : 0;
  if (!RunCityCommand(city->game, CITY_COMMAND_STRETCH_TERRAIN_TILE, tile, height, options, 0, NULL, NULL)) {
    ui->pointer_stroke_count = 0;
    ui->pointer_terrain_stretch = false;
    ReleaseCityMapMouseCapture(city->game);
  }
}

// Level newly included strips as the selection changes. Keep only the last rectangle and pointer position;
// neither mouse motion within the same tile nor release rescans an already completed area.
static void ApplyLiveCityLevel(City *city, const CanvasSDLEvent *event) {
  Game *game = city->game;
  CityInterfaceState *ui = game->ui;
  if (event->shift || event->ctrl || !IsCityMainMapInteractiveAt(city, event->x, event->y)) return;
  CityRect previous = ui->pointer_level_bounds;
  bool applied = previous.left < previous.right && previous.top < previous.bottom;
  if (applied && event->x == ui->pointer_level_pointer.x && event->y == ui->pointer_level_pointer.y) return;
  TileIndex anchor = ui->pointer_terrain_tile;
  TileIndex end = PickCityPlacementTile(city, event->x, event->y);
  if (!IsMapTileValid(city, anchor) || !IsMapTileValid(city, end)) return;
  CityRect area = MapTileRectangle(city, anchor, end);
  ui->pointer_level_pointer = (CityPoint){event->x, event->y};
  ui->pointer_level_bounds = area;
  CityRect overlap = IntersectCityRects(area, previous);
  CityRect strips[4] = {area};
  int count = 1;
  if (applied && overlap.left < overlap.right && overlap.top < overlap.bottom) {
    strips[0] = (CityRect){area.left, area.top, area.right, overlap.top};
    strips[1] = (CityRect){area.left, overlap.bottom, area.right, area.bottom};
    strips[2] = (CityRect){area.left, overlap.top, overlap.left, overlap.bottom};
    strips[3] = (CityRect){overlap.right, overlap.top, area.right, overlap.bottom};
    count = 4;
  }
  int options = ui->terrain_no_seawater ? CITY_TERRAIN_NO_SEAWATER : 0;
  for (int i = 0; i < count; ++i) {
    CityRect strip = strips[i];
    if (strip.left >= strip.right || strip.top >= strip.bottom) continue;
    TileIndex start = MapTileXY(city, strip.left, strip.top);
    TileIndex last = MapTileXY(city, strip.right - 1, strip.bottom - 1);
    if (!RunCityLevelTerrain(game, start, last, ui->pointer_terrain_height, options)) {
      ui->pointer_stroke_count = 0;
      ui->pointer_terrain_level = false;
      ReleaseCityMapMouseCapture(game);
      return;
    }
  }
}

void UpdateCityPlacementPreview(City *city) {
  CityInterfaceState *ui = city->game->ui;
  Drawing *drawing = city->game->drawing;
  int count = 0;
  CityPlacementIntent intent = {0};
  if (ui->desktop.menu.selected || ui->desktop.menu.held || !ui->placement_hover_valid || ui->desktop.dragging != CITY_CANVAS_DRAG_NONE ||
      ui->options_slider || ui->jukebox_slider || ui->placement_hover_shift || ui->pan_button ||
      !IsCityMainMapInteractiveAt(city, ui->placement_hover.x, ui->placement_hover.y)) goto done;
  int tools = Sc2kCanvasMapPointerTools(ui->desktop.selected_tool, 0);
  if (tools < 0) goto done;
  TileIndex tile = ui->placement_hover_ctrl ? PickCityMapTile(city, drawing->default_map_view,
      ui->placement_hover.x, ui->placement_hover.y) :
      PickCityPlacementTile(city, ui->placement_hover.x, ui->placement_hover.y);
  if (ui->pointer_stroke_count && ui->pointer_terrain_stretch && !ui->placement_hover_ctrl)
    tile = ui->pointer_terrain_tile;
  if (ui->pointer_stroke_count && ui->pointer_terrain_level && !ui->placement_hover_ctrl &&
      ui->pointer_level_bounds.left < ui->pointer_level_bounds.right) {
    // Live elevation changes can move a different tile beneath the same screen point.
    // Draw the rectangle actually applied by the gesture rather than picking it again.
    CityPoint anchor = MapTilePt(city, ui->pointer_terrain_tile);
    CityRect area = ui->pointer_level_bounds;
    tile = MapTileXY(city, anchor.x == area.left ? area.right - 1 : area.left,
        anchor.y == area.top ? area.bottom - 1 : area.top);
  }
  if (!IsMapTileValid(city, tile)) goto done;
  CityPoint first = ui->pointer_stroke_count ? ui->pointer_stroke[0].point : ui->placement_hover;
  TileIndex start = PickCityMapTile(city, drawing->default_map_view, first.x, first.y);
  u32 options = (ui->placement_hover_ctrl ? CITY_POINTER_MODIFIER_CONTROL : 0) |
      (IsAltKeyPressed(ui) ? CITY_POINTER_MODIFIER_ALT : 0) |
      (drawing->map_state.events_suppressed ? CITY_POINTER_UNDERGROUND : 0);
  intent = ResolveCityPlacementIntent(city->game, tools, start, tile, options);
  int group = intent.group, variant = intent.variant;
  if (group > CITY_TOOL_GROUP_SIGN || group == CITY_TOOL_GROUP_DISPATCH) goto done;
  int x = MapTileX(city, tile), y = MapTileY(city, tile);
  int magnify = drawing->default_map_view->magnified_large_zoom ? 2 : 1;
  CityRect visible = VisibleCityMapTileBounds(city, drawing->map_state.origin_x, drawing->map_state.origin_y,
      drawing->default_map_view->zoom, CityMapViewRotation(drawing->default_map_view),
      (CityRect){0, 0, drawing->map_width / magnify, drawing->map_height / magnify});
  bool stretch = ui->desktop.selected_tool == CITY_CANVAS_TOOL_STRETCH_TERRAIN && !ui->placement_hover_ctrl;
  bool level = group == CITY_TOOL_GROUP_DEMOLISH && variant == CITY_DEMOLISH_LEVEL && !ui->placement_hover_ctrl;
  if (ui->desktop.selected_tool == CITY_CANVAS_TOOL_STREAM && !ui->placement_hover_ctrl) {
    if (!AppendCityPlacementRectangle(city, (CityRect){x, y, x + 1, y + 1}, visible, &count)) count = 0;
    goto done;
  }
  if (stretch || level) {
    TileIndex anchor = ui->pointer_stroke_count ? ui->pointer_terrain_tile : tile;
    if (!IsMapTileValid(city, anchor)) goto done;
    CityPoint origin = MapTilePt(city, anchor);
    CityRect area = stretch ? (CityRect){origin.x, origin.y, origin.x + 1, origin.y + 1} :
        MapTileRectangle(city, anchor, tile);
    if (!AppendCityPlacementRectangle(city, area, visible, &count)) count = 0;
    goto done;
  }
  if (group == CITY_TOOL_GROUP_DEMOLISH &&
      (variant == CITY_DEMOLISH_RAISE || variant == CITY_DEMOLISH_LOWER) && !ui->placement_hover_ctrl) {
    TileIndex anchor = ui->pointer_stroke_count ? ui->pointer_terrain_tile : tile;
    if (!IsMapTileValid(city, anchor)) goto done;
    if (!AppendCityPlacementRectangle(city, MapTileRectangle(city, anchor, tile), visible, &count)) count = 0;
    if (anchor != tile) intent.point = false;
    goto done;
  }
  if (intent.clear || intent.dezone) {
    if (!IsMapTileValid(city, start)) goto done;
    int sx = MapTileX(city, start), sy = MapTileY(city, start);
    if (!AppendCityPlacementRectangle(city, (CityRect){IntMin(x, sx), IntMin(y, sy),
        IntMax(x, sx) + 1, IntMax(y, sy) + 1}, visible, &count)) count = 0;
    goto done;
  }
  // Bridge spans have a variable footprint and no entry in the fixed-size table.
  if (!ui->placement_hover_ctrl && intent.bridge) {
    if (IsMapTileValid(city, start)) {
      int sx = MapTileX(city, start), sy = MapTileY(city, start);
      int ex = MapTileX(city, tile), ey = MapTileY(city, tile);
      if (abs(ex - sx) >= abs(ey - sy)) ey = sy; else ex = sx;
      if (!AppendCityPlacementRectangle(city, (CityRect){IntMin(sx, ex), IntMin(sy, ey),
          IntMax(sx, ex) + 1, IntMax(sy, ey) + 1}, visible, &count)) count = 0;
    }
    goto done;
  }
  int size = intent.size;
  if (size < 1 || size > 4) goto done;
  bool highway = intent.highway;
  if (highway && !ui->placement_hover_ctrl) {
    CityCompactHighwayPlan compact_preview;
    if (PlanCityCompactHighway(city, start, tile,
        intent.highway_facing, &compact_preview)) {
      const CityRect areas[] = {compact_preview.land, compact_preview.water};
      for (int part = 0; part < 2; ++part)
        if (!AppendCityPlacementRectangle(city, areas[part], visible, &count)) { count = 0; break; }
      if (!count && compact_preview.cost < 0) {
        int sx = MapTileX(city, compact_preview.start), sy = MapTileY(city, compact_preview.start);
        int width = compact_preview.shape || compact_preview.axis ? 2 : 1;
        int height = compact_preview.shape || !compact_preview.axis ? 2 : 1;
        if (!AppendCityPlacementRectangle(city, (CityRect){sx, sy, sx + width, sy + height}, visible, &count)) count = 0;
      }
      goto done;
    }
  }
  bool surface_span = intent.surface_span;
  if (ui->pointer_stroke_count && !ui->placement_hover_ctrl && intent.line) {
    if (!IsMapTileValid(city, start)) goto done;
    int next_x = MapTileX(city, start), next_y = MapTileY(city, start);
    if (highway) {
      if (!AppendCityPlacementHighwaySpan(city, start, city->geography.rotation, true, visible, &count)) count = 0;
      if (count) goto done;
    }
    if (surface_span && IsCityInfrastructureSpanWater(city, start)) {
      if (!AppendCityPlacementSpan(city, start, city->geography.rotation, true, visible, &count)) count = 0;
      if (!count && !AppendCityPlacementRectangle(city,
          (CityRect){next_x, next_y, next_x + 1, next_y + 1}, visible, &count)) count = 0;
      goto done;
    }
    // Reuse construction's terrain/crossing rules with private working state.
    // The algorithms only read the borrowed map layers; hovering never changes City.
    CityConstructionState preview = {0};
    preview.stage = group == CITY_TOOL_GROUP_WATER ? CITY_PLACEMENT_PIPE :
        group == CITY_TOOL_GROUP_POWER ? CITY_PLACEMENT_POWER :
        group == CITY_TOOL_GROUP_RAIL ? (variant == CITY_RAIL_SUBWAY ? CITY_PLACEMENT_SUBWAY : CITY_PLACEMENT_RAIL) :
        CITY_PLACEMENT_ROAD;
    bool prepared;
    if (highway) {
      TileIndex anchor = CityHighwayPlacementAnchor(city, MapTileXY(city, next_x, next_y));
      prepared = anchor != INVALID_TILE;
      if (prepared) {
        next_x = MapTileX(city, anchor); next_y = MapTileY(city, anchor);
        PrepareCityHighwayLine(city, &preview, next_x, next_y, x, y);
      }
    } else {
      prepared = PrepareCityLine(city, &preview, next_x, next_y, x, y);
    }
    if (prepared) {
      int width = highway ? 2 : 1;
      bool has_new_cells = false;
      do {
        for (int dy = 0; dy < width; ++dy)
          for (int dx = 0; dx < width; ++dx) {
            TileIndex cell = MapTileXY(city, next_x + dx, next_y + dy);
            has_new_cells |= highway ? !CityHighwayCellAlreadyBuilt(city, cell) :
                !CityLineCellAlreadyBuilt(city, cell, preview.stage);
          }
        if (!AppendCityPlacementRectangle(city,
            (CityRect){next_x, next_y, next_x + width, next_y + width}, visible, &count)) {
          count = 0;
          goto done;
        }
      } while (highway ? AdvanceCityHighwayLine(city, &preview, &next_x, &next_y) :
          AdvanceCityLine(city, &preview, &next_x, &next_y));
      bool span_added = false;
      if ((surface_span || highway) && (has_new_cells || (surface_span))) {
        int before_span = count;
        int follow_x = next_x + width * kLineXStep[preview.direction];
        int follow_y = next_y + width * kLineYStep[preview.direction];
        TileIndex follow = MapTileXYOrInvalid(city, follow_x, follow_y);
        if (follow != INVALID_TILE) {
          if (highway) {
            if (!AppendCityPlacementHighwaySpan(city, follow, preview.direction, false, visible, &count)) count = 0;
          } else if (IsCityInfrastructureSpanWater(city, follow) &&
              !AppendCityPlacementSpan(city, follow, preview.direction, false, visible, &count))
            count = 0;
        }
        span_added = count > before_span;
      }
      if (!span_added && (next_x != (highway ? preview.endpoint_x : x) || next_y != (highway ? preview.endpoint_y : y))) {
        // Keep the first rejected step visible instead of silently shortening the preview.
        int step = highway ? 2 : 1;
        int dx = x - next_x, dy = y - next_y;
        if (abs(dx) >= abs(dy)) next_x += dx < 0 ? -step : step;
        else next_y += dy < 0 ? -step : step;
        if (!AppendCityPlacementRectangle(city,
            (CityRect){next_x, next_y, next_x + width, next_y + width}, visible, &count)) count = 0;
      }
      goto done;
    }
    if (!AppendCityPlacementRectangle(city,
        (CityRect){next_x, next_y, next_x + 1, next_y + 1}, visible, &count)) count = 0;
    goto done;
  }
  if (!ui->pointer_stroke_count && !ui->placement_hover_ctrl) {
    if (surface_span && IsCityInfrastructureSpanWater(city, tile)) {
      if (!AppendCityPlacementSpan(city, tile, city->geography.rotation, true, visible, &count)) count = 0;
      if (count) goto done;
    }
    if (highway) {
      if (!AppendCityPlacementHighwaySpan(city, tile, city->geography.rotation, true, visible, &count)) count = 0;
      if (count) goto done;
    }
    if (group == CITY_TOOL_GROUP_ROAD && variant == CITY_ROAD_TUNNEL) {
      CityInfrastructureSpan tunnel = FindCityRoadTunnel(city, tile);
      if (tunnel.end != INVALID_TILE) {
        int end_x = MapTileX(city, tunnel.end), end_y = MapTileY(city, tunnel.end);
        if (!AppendCityPlacementRectangle(city, (CityRect){IntMin(x, end_x), IntMin(y, end_y),
            IntMax(x, end_x) + 1, IntMax(y, end_y) + 1}, visible, &count)) count = 0;
        goto done;
      }
    }
  }
  int first_x = x, first_y = y, last_x, last_y;
  if (!ui->placement_hover_ctrl && !(tools >> 16) && highway) {
    TileIndex anchor = CityHighwayPlacementAnchor(city, MapTileXY(city, x, y));
    if (anchor == INVALID_TILE) goto done;
    first_x = MapTileX(city, anchor); first_y = MapTileY(city, anchor);
    size = 2;
  } else if (size > 2) {
    --first_x; --first_y;
  }
  last_x = first_x + size; last_y = first_y + size;
  if (ui->pointer_stroke_count && !ui->placement_hover_ctrl &&
      (intent.trees || intent.canal || (group >= CITY_TOOL_GROUP_PORT && group <= CITY_TOOL_GROUP_INDUSTRIAL))) {
    if (!IsMapTileValid(city, start)) goto done;
    int sx = MapTileX(city, start), sy = MapTileY(city, start);
    first_x = IntMin(x, sx); last_x = IntMax(x, sx) + 1;
    first_y = IntMin(y, sy); last_y = IntMax(y, sy) + 1;
  }
  // Show the in-map footprint even when the command will reject an edge placement.
  if (!AppendCityPlacementRectangle(city, (CityRect){first_x, first_y, last_x, last_y}, visible, &count)) count = 0;
done:
  ui->placement_dirty |= SetCityCanvasPlacementCells(city, &ui->desktop,
      ui->desktop.placement.scratch, count, intent.point);
}

static void TrackCityPlacementPointer(CityInterfaceState *ui, const CanvasSDLEvent *event) {
  SetCityMapCursorPosition(ui, event->x, event->y);
  ui->placement_hover = (CityPoint){event->x, event->y};
  ui->placement_hover_shift = event->shift;
  ui->placement_hover_ctrl = event->ctrl;
  ui->placement_hover_valid = 1;
}

static void FinishCityPointerStroke(City *city, const CanvasSDLEvent *event) {
  Game *game = city->game;
  CityInterfaceState *ui = game->ui;
  if (!ui->pointer_stroke_count)
    return;
  CanvasSDLEvent release = *event;
  if (!ui->pointer_terrain_stretch && !IsCityMainMapInteractiveAt(city, release.x, release.y)) {
    release.x = ui->pointer_stroke[ui->pointer_stroke_count - 1].point.x;
    release.y = ui->pointer_stroke[ui->pointer_stroke_count - 1].point.y;
  }
  if (IsLiveCityPlacementStroke(game)) {
    ApplyLiveCityPlacements(city, &release);
    bool captured = ui->pointer_stroke_count != 0;
    ui->pointer_stroke_count = 0;
    if (captured) ReleaseCityMapMouseCapture(game);
    return;
  }
  TrackCityPointerEndpoint(ui, &release, true);
  if (!game->observation && (ui->pointer_terrain_stretch || ui->pointer_terrain_level)) {
    if (ui->pointer_terrain_stretch) ApplyLiveCityStretch(city, &release);
    else ApplyLiveCityLevel(city, &release);
    bool captured = ui->pointer_stroke_count != 0;
    ui->pointer_stroke_count = 0;
    ui->pointer_terrain_stretch = ui->pointer_terrain_level = false;
    if (captured) ReleaseCityMapMouseCapture(game);
    return;
  }
  u32 count = ui->pointer_stroke_count;
  int pointer_flags = ui->pointer_stroke_flags;
  int tools = ui->pointer_stroke_tools;
  CityPoint first = ui->pointer_stroke[0].point;
  TileIndex start = PickCityMapTile(city, game->drawing->default_map_view, first.x, first.y);
  TileIndex end = PickCityMapTile(city, game->drawing->default_map_view, release.x, release.y);
  CityPlacementIntent intent = ResolveCityPlacementIntent(game, tools, start, end,
      CityPointerModifiers(&release) | (pointer_flags & CITY_POINTER_UNDERGROUND));
  int group = intent.group, variant = intent.variant;
  if (intent.clear) {
    int options = intent.bulldoze_options;
    ui->pointer_stroke_count = 0;
    if (!release.shift && IsMapTileValid(city, start) && IsMapTileValid(city, end))
      RunCityCommand(game, CITY_COMMAND_BULLDOZE_RECT, start, end, options, 0, NULL, NULL);
    ReleaseCityMapMouseCapture(game);
    return;
  }
  if ((tools & 255) == CITY_TOOL_GROUP_TREE_WATER) {
    int options = intent.tree_options;
    ui->pointer_stroke_count = 0;
    bool trees = ((tools >> 8) & 255) == CITY_TREE_WATER_TREES;
    if ((trees || !release.shift) && IsMapTileValid(city, start) && IsMapTileValid(city, end))
      RunCityCommand(game, trees ? CITY_COMMAND_PLANT_TREES_RECT : CITY_COMMAND_BUILD_CANALS_RECT,
          start, end, trees ? options : 0, 0, NULL, NULL);
    ReleaseCityMapMouseCapture(game);
    return;
  }
  if (!game->observation && !release.shift && !release.ctrl &&
      (tools & 255) == CITY_TOOL_GROUP_ROAD && ((tools >> 8) & 255) == CITY_ROAD_TUNNEL &&
      !(ui->pointer_stroke_flags & CITY_POINTER_UNDERGROUND)) {
    TileIndex tile = start;
    ui->pointer_stroke_count = 0;
    ReleaseCityMapMouseCapture(game);
    if (IsMapTileValid(city, tile)) PresentCityTunnelConfirmation(city, tile);
    return;
  }
  if (!game->observation && !release.shift && !release.ctrl &&
      !(ui->pointer_stroke_flags & CITY_POINTER_UNDERGROUND) &&
      IsTileKindBuilding(GetCityFacilityTile(group, variant))) {
    TileIndex tile = end;
    ui->pointer_stroke_count = 0;
    ReleaseCityMapMouseCapture(game);
    if (IsMapTileValid(city, tile)) {
      int orientation = intent.facility_facing;
      RunCityCommand(game, CITY_COMMAND_PLACE_BUILDING, tile,
          GetCityFacilityTile(group, variant), orientation, 0, NULL, NULL);
    }
    return;
  }
  if (!game->observation && group == CITY_TOOL_GROUP_ROAD && variant == CITY_ROAD_HIGHWAY &&
      !release.shift && !release.ctrl && !(ui->pointer_stroke_flags & CITY_POINTER_UNDERGROUND)) {
    int orientation = intent.highway_facing;
    CityCompactHighwayPlan plan;
    if (PlanCityCompactHighway(city, start, end, orientation, &plan)) {
      ui->pointer_stroke_count = 0;
      ReleaseCityMapMouseCapture(game);
      CityCommand command = EncodeCityCommand(city, CITY_COMMAND_PLACE_TRANSPORT_LINE, start, end,
          CITY_ROAD_SPAN_CANCEL | (orientation + 1) << CITY_HIGHWAY_FACING_SHIFT, 0);
      if (plan.cost >= 0 && plan.span.end != INVALID_TILE) {
        if (!OpenCitySpanCommand(city, command, plan.span.length, plan.span.styles)) game->execution_failed = 1;
        return;
      }
      RunCityCommand(game, CITY_COMMAND_PLACE_TRANSPORT_LINE, start, end,
          CITY_ROAD_SPAN_CANCEL | (orientation + 1) << CITY_HIGHWAY_FACING_SHIFT, 0, NULL, NULL);
      return;
    }
  }
  if (!game->observation && intent.bridge) {
    bool underground = (ui->pointer_stroke_flags & CITY_POINTER_UNDERGROUND) != 0;
    ui->pointer_stroke_count = 0;
    ReleaseCityMapMouseCapture(game);
    if (release.shift || release.ctrl || underground) return;
    bool rail = group == CITY_TOOL_GROUP_RAIL;
    CityInfrastructureSpan span = FindCityBridgeSpan(city, start, end, rail);
    CityCommand queued = EncodeCityCommand(city, CITY_COMMAND_BUILD_BRIDGE, start, end,
        rail ? CITY_BRIDGE_COMMAND_RAIL | CITY_SPAN_WOOD : CITY_SPAN_WOOD, 0);
    if (span.end != INVALID_TILE) {
      if (!OpenCitySpanCommand(city, queued, span.length, span.styles)) game->execution_failed = 1;
    } else {
      RunCityCommand(game, CITY_COMMAND_BUILD_BRIDGE, start, end,
          rail ? CITY_BRIDGE_COMMAND_RAIL | CITY_SPAN_WOOD : CITY_SPAN_WOOD, 0, NULL, NULL);
    }
    return;
  }
  if (!game->observation && group == CITY_TOOL_GROUP_DEMOLISH && variant == CITY_DEMOLISH_LEVEL) {
    int height = ui->pointer_terrain_height;
    TileIndex anchor = ui->pointer_terrain_tile;
    ui->pointer_stroke_count = 0;
    ReleaseCityMapMouseCapture(game);
    if (release.shift || !IsMapTileValid(city, anchor)) return;
    int options = intent.terrain_options & CITY_TERRAIN_NO_SEAWATER;
    if (IsMapTileValid(city, end)) RunCityLevelTerrain(game, anchor, end, height, options);
    return;
  }
  if (!game->observation && group == CITY_TOOL_GROUP_DEMOLISH &&
      (tools >> 16) != CITY_DEMOLISH_DEZONE) {
    ui->pointer_stroke_count = 0;
    ReleaseCityMapMouseCapture(game);
    start = ui->pointer_terrain_tile;
    if (!release.shift && IsMapTileValid(city, start) && IsMapTileValid(city, end)) {
      int tool = tools >> 16;
      CityCommandId id = tool == CITY_DEMOLISH_RAISE ? CITY_COMMAND_RAISE_TERRAIN_TILE :
          tool == CITY_DEMOLISH_LOWER ? CITY_COMMAND_LOWER_TERRAIN_TILE : CITY_COMMAND_STEP_TERRAIN_HEIGHT;
      int options = tool == CITY_DEMOLISH_RAISE ? tool :
          (tool == CITY_DEMOLISH_LOWER ? CITY_TERRAIN_COMMAND_CHARGE :
              ReadCityLandHeight(&city->tile[start])) |
          tool << CITY_TERRAIN_COMMAND_TOOL_SHIFT;
      CityRect bounds = MapTileRectangle(city, tool == CITY_DEMOLISH_RAISE ||
          tool == CITY_DEMOLISH_LOWER ? start : end, end);
      for (int y = bounds.top; y < bounds.bottom; ++y)
        for (int x = bounds.left; x < bounds.right; ++x)
          if (!RunCityCommand(game, id, MapTileXY(city, x, y), options,
              intent.terrain_options, 0, NULL, NULL)) return;
    }
    return;
  }
  if (!game->observation && intent.span_tool && !release.shift && !release.ctrl &&
      !(ui->pointer_stroke_flags & CITY_POINTER_UNDERGROUND)) {
    CityInfrastructureSpan span = GetCityConstructionSpan(city, start, end, group, variant);
    CityCommandId id = group == CITY_TOOL_GROUP_ROAD ?
        (variant == CITY_ROAD_HIGHWAY ? CITY_COMMAND_PLACE_TRANSPORT_LINE : CITY_COMMAND_PLACE_ROAD) :
        group == CITY_TOOL_GROUP_RAIL ? CITY_COMMAND_PLACE_RAIL : CITY_COMMAND_PLACE_POWER_LINE;
    CityCommand queued = EncodeCityCommand(city, id, start, end,
        id == CITY_COMMAND_PLACE_TRANSPORT_LINE ? CITY_ROAD_SPAN_CANCEL : variant, 0);
    if (span.end != INVALID_TILE) {
      int styles = CityInfrastructureSpanStyles(span) &
          (group == CITY_TOOL_GROUP_POWER ? CITY_SPAN_STYLE_POWER_MASK :
           group == CITY_TOOL_GROUP_RAIL ? CITY_SPAN_STYLE_RAIL_MASK :
           variant == CITY_ROAD_HIGHWAY ? CITY_SPAN_STYLES_ALL : CITY_SPAN_STYLE_ROAD_MASK);
      if (styles) {
        ui->pointer_stroke_count = 0;
        ReleaseCityMapMouseCapture(game);
        if (!OpenCitySpanCommand(city, queued, span.length, styles)) game->execution_failed = 1;
        return;
      }
    }
    int connection_cost = GetCityLineConnectionCost(city, start, end, group == CITY_TOOL_GROUP_RAIL ? CITY_INFRASTRUCTURE_RAIL :
        variant == CITY_ROAD_HIGHWAY ? CITY_INFRASTRUCTURE_HIGHWAY : CITY_INFRASTRUCTURE_ROAD);
    if (connection_cost) {
      ui->pointer_stroke_count = 0;
      ReleaseCityMapMouseCapture(game);
      PresentCityNeighborConfirmation(city, queued, connection_cost);
      return;
    }
    if (group == CITY_TOOL_GROUP_ROAD && variant == CITY_ROAD_HIGHWAY) {
      // The endpoint command selects straight or diagonal placement. Pointer
      // replay selects compact mode before it has seen the diagonal endpoint.
      ui->pointer_stroke_count = 0;
      ReleaseCityMapMouseCapture(game);
      RunCityCommand(game, CITY_COMMAND_PLACE_TRANSPORT_LINE, start, end,
          CITY_ROAD_SPAN_CANCEL, 0, NULL, NULL);
      return;
    }
  }
  // A span chooser pumps window mouse-up events while this original placement
  // is still on the stack. Retire the frontend gesture before dispatching it.
  if (!game->observation && !intent.endpoint_drag) {
    ui->pointer_stroke[0] = ui->pointer_stroke[count - 1];
    count = 1;
  }
  ui->pointer_stroke_count = 0;
  if (!RunCityPointerGesture(game, ui->pointer_stroke, count, pointer_flags, tools))
    game->execution_failed = 1;
  if (ui->tool_group == CITY_TOOL_GROUP_CENTER)
    ui->desktop.selected_tool = CITY_CANVAS_TOOL_NONE;
  ui->pointer_stroke_count = 0;
  ReleaseCityMapMouseCapture(game);
}

static void HandleCityCanvasContentPress(City *city, const CanvasSDLEvent *map_event) {
  Game *game = city->game;
  CityInterfaceState *ui = game->ui;
  const CanvasSDLEvent *event = map_event;
  if (NativeSpanDialogWaiting(ui) || CityConnectionQuestionWaiting(ui) || CityMissionNoticeWaiting(ui)) return;
  if (!IsCityMainMapInteractiveAt(city, event->x, event->y) ||
      (ui->desktop.selected_tool < 0 && !event->shift && !event->ctrl))
    return;
  int tools = ui->desktop.selected_tool < 0 ? (int)(Sc2kCurrentCityMapPointerTools(ui)) :
      Sc2kCanvasMapPointerTools(ui->desktop.selected_tool, 0);
  if ((tools & 255) != CITY_TOOL_GROUP_DEMOLISH)
    tools = (int)((u32)(tools & 65535) | ((u32)ui->selection.value[0] << 16));
  if (event->shift || (!event->ctrl &&
      (ui->desktop.selected_tool == CITY_CANVAS_TOOL_SIGN ||
       ui->desktop.selected_tool == CITY_CANVAS_TOOL_QUERY))) {
    if (!game->observation && (event->shift || ui->desktop.selected_tool == CITY_CANVAS_TOOL_QUERY)) {
      CityMapViewConfig *view = game->drawing->default_map_view;
      TileIndex tile = PickCityMapTile(city, view, event->x, event->y);
      view->pointer_drag_active = 0;
      view->pointer_command_stage = 0;
      if (IsMapTileValid(city, tile)) PresentCityTileInformationDialog(city, tile);
      return;
    }
    int flags = CITY_MOUSE_LEFT_BUTTON | (int)CityPointerModifiers(event) |
        (game->drawing->map_state.events_suppressed != 0 ? CITY_POINTER_UNDERGROUND : 0) |
        (event->shift ? CITY_MOUSE_SHIFT : 0) | (event->ctrl ? CITY_MOUSE_CONTROL : 0);
    CityPointerInput input = {{event->x, event->y}, CITY_POINTER_SAMPLE_RELEASE | CityPointerModifiers(event)};
    ui->pointer_query_capture = true;
    if (!RunCityPointerGesture(game, &input, 1, flags, tools))
      game->execution_failed = 1;
    return;
  }
  if (!game->observation && !event->ctrl && ui->desktop.selected_tool == CITY_CANVAS_TOOL_STREAM) {
    TileIndex tile = PickCityMapTile(city, game->drawing->default_map_view, event->x, event->y);
    if (IsMapTileValid(city, tile))
      RunCityCommand(game, CITY_COMMAND_PLACE_STREAM, tile, CITY_STREAM_DEFAULT_LENGTH, 0, 0, NULL, NULL);
    return;
  }
  ui->pointer_stroke_count = 0;
  ui->pointer_stroke_tools = tools;
  ui->pointer_stroke_flags = CITY_MOUSE_LEFT_BUTTON | (int)CityPointerModifiers(event) |
      (game->drawing->map_state.events_suppressed != 0 ? CITY_POINTER_UNDERGROUND : 0) |
      (event->shift ? CITY_MOUSE_SHIFT : 0) | (event->ctrl ? CITY_MOUSE_CONTROL : 0);
  ui->pointer_terrain_tile = event->ctrl ?
      PickCityMapTile(city, game->drawing->default_map_view, event->x, event->y) :
      PickCityPlacementTile(city, event->x, event->y);
  ui->pointer_terrain_height = IsMapTileValid(city, ui->pointer_terrain_tile) ?
      ui->desktop.selected_tool == CITY_CANVAS_TOOL_LEVEL_TERRAIN ?
          ReadCityMapTerrainPointHeight(city, ui->pointer_terrain_tile) :
          ReadCityTileGeometry(city, ui->pointer_terrain_tile).land_height : 0;
  CityMapViewConfig *view = game->drawing->default_map_view;
  ui->pointer_terrain_pixels = CityMapHeightStep(view->zoom) * (view->magnified_large_zoom ? 2 : 1);
  ui->pointer_terrain_last_height = ui->pointer_terrain_height;
  ui->pointer_terrain_stretch = !event->ctrl && ui->desktop.selected_tool == CITY_CANVAS_TOOL_STRETCH_TERRAIN;
  ui->pointer_terrain_level = !event->ctrl && ui->desktop.selected_tool == CITY_CANVAS_TOOL_LEVEL_TERRAIN;
  ui->pointer_level_bounds = (CityRect){0};
  TrackCityPointerEndpoint(ui, event, false);
  if (!game->observation && ui->pointer_terrain_level) ApplyLiveCityLevel(city, event);
  if (IsLiveCityPlacementStroke(game)) {
    ui->pointer_terrain_tile = INVALID_TILE;
    ApplyLiveCityPlacements(city, event);
  }
}

void BeginCityCanvasButtonFeedback(Game *game, const CanvasSDLEvent *event) {
  int x, y;
  CityInterfacePixels(game, event, &x, &y);
  BeginCityButtonFeedback(game, x, y, MonotonicSeconds());
}

void HandleCityCanvasPointerDown(City *city, const CanvasSDLEvent *event) {
  Game *game = city->game;
  CityInterfaceState *ui = game->ui;
  if (HandleCityMainMenuPointer(game, event)) return;
  RecordGameInput(game);
  TrackCityPlacementPointer(ui, event);
  if (DispatchCityWindowInput(game, event)) return;
  if (HandleCityMapSignInput(city, NULL, event) || HandleCityMainMapPan(city, event)) return;
  if (IsCityMainMapInteractiveAt(city, event->x, event->y)) ui->map_view_focus_window = 0;
  HandleCityCanvasContentPress(city, event);
  UpdateCityPlacementPreview(city);
}

void HandleCityCanvasPointerMove(City *city, const CanvasSDLEvent *event) {
  Game *game = city->game;
  CityInterfaceState *ui = game->ui;
  if (IsLiveCityPlacementStroke(game)) {
    TrackCityPlacementPointer(ui, event);
    ApplyLiveCityPlacements(city, event);
    if (ui->pointer_stroke_count) TrackCityPointerEndpoint(ui, event, false);
    UpdateCityPlacementPreview(city);
    return;
  }
  if (!game->observation && ui->pointer_stroke_count &&
      (ui->pointer_terrain_stretch || ui->pointer_terrain_level)) {
    TrackCityPlacementPointer(ui, event);
    if (ui->pointer_terrain_stretch || IsCityMainMapInteractiveAt(city, event->x, event->y))
      TrackCityPointerEndpoint(ui, event, false);
    if (ui->pointer_terrain_stretch) ApplyLiveCityStretch(city, event);
    else ApplyLiveCityLevel(city, event);
    UpdateCityPlacementPreview(city);
    return;
  }
  if (HandleCityMainMenuPointer(game, event)) return;
  TrackCityPlacementPointer(ui, event);
  UpdateCityPlacementPreview(city);
  if (ui->pointer_stroke_count) {
    if (ui->pointer_terrain_stretch || IsCityMainMapInteractiveAt(city, event->x, event->y))
      TrackCityPointerEndpoint(ui, event, false);
    return;
  }
  if (ui->pan_button && !ui->panning_view) {
    HandleCityMainMapPan(city, event);
    return;
  }
  if (DispatchCityWindowInput(game, event)) return;
  HandleCityMainMapPan(city, event);
}

void HandleCityCanvasPointerUp(City *city, const CanvasSDLEvent *event) {
  Game *game = city->game;
  CityInterfaceState *ui = game->ui;
  bool live_placement = IsLiveCityPlacementStroke(game) || (!game->observation && ui->pointer_stroke_count &&
      (ui->pointer_terrain_stretch || ui->pointer_terrain_level));
  if (!live_placement && HandleCityMainMenuPointer(game, event)) {
    ui->desktop.input_capture_window = 0;
    Sc2kCanvasDesktopPointerUp(&ui->desktop);
    return;
  }
  TrackCityPlacementPointer(ui, event);
  if (ui->pointer_stroke_count || ui->pointer_query_capture) {
    FinishCityPointerStroke(city, event);
    if (ui->pointer_query_capture) {
      ui->pointer_query_capture = false;
      ReleaseCityMapMouseCapture(game);
    }
  } else if (ui->pan_button && !ui->panning_view) HandleCityMainMapPan(city, event);
  else if (!DispatchCityWindowInput(game, event)) HandleCityMainMapPan(city, event);
  UpdateCityPlacementPreview(city);
}

static void ZoomCityCanvasAt(City *city, int x, int y, int steps) {
  if (!steps) return;
  Game *game = city->game;
  Window *window = FindCityWindowAtPixels(&game->ui->desktop, x, y);
  if (window) {
    WindowEvent event = {.kind = WINDOW_ZOOM, .zoom_steps = steps};
    Sc2kCanvasDesktopPixelPointer(&game->ui->desktop, window->window_id, x, y, &event.x, &event.y);
    SendWindowEvent(game, window, &event);
    return;
  }
  Drawing *drawing = game->drawing;
  if (!IsCityMainMapInteractiveAt(city, x / drawing->map_pixel_scale, y / drawing->map_pixel_scale)) return;
  int zoom = drawing->default_map_view->zoom + drawing->default_map_view->magnified_large_zoom;
  int target = IntClamp(zoom + steps, CITY_CANVAS_MAP_ZOOM_SMALL,
      CityMapZoomLimit(drawing->map_pixel_scale * 100));
  int button = steps > 0 ? CITY_TOOLBAR_BUTTON_ZOOM_IN : CITY_TOOLBAR_BUTTON_ZOOM_OUT;
  for (int count = abs(target - zoom); count > 0; --count)
    SelectCityToolWithModifiers(game, button, 0, 0);
  UpdateCityPlacementPreview(city);
}

static void PinchCityCanvas(City *city, const CanvasSDLEvent *event) {
  Game *game = city->game;
  CityInterfaceState *ui = game->ui;
  if (event->kind != CANVAS_SDL_ZOOM) {
    ui->pinch_remainder = 0;
    return;
  }
  if (ui->desktop.dropdown.items || ui->desktop.menu.selected || ui->desktop.menu.held ||
      ui->pointer_stroke_count || ui->pan_button || ui->desktop.dragging != CITY_CANVAS_DRAG_NONE ||
      ui->startup_waiting || NativeScenarioDialogWaiting(ui)) {
    ui->pinch_remainder = 0;
    return;
  }
  // Accumulate small trackpad deltas; a quarter-scale pinch advances one zoom level.
  ui->pinch_remainder += event->y;
  int steps = ui->pinch_remainder / 16384;
  ui->pinch_remainder %= 16384;
  if (!steps) return;
  int x, y;
  CityInterfacePixels(game, event, &x, &y);
  ZoomCityCanvasAt(city, x, y, steps);
}

void ScrollCityCanvas(City *city, const CanvasSDLEvent *event) {
  Game *game = city->game;
  CityInterfaceState *ui = game->ui;
  if (ui->desktop.menu.selected || ui->desktop.menu.held) return;
  int pointer_x = ui->desktop.tooltip.pointer_x, pointer_y = ui->desktop.tooltip.pointer_y;
  if (ui->display)
    Sc2kSDLFramebufferPointer(ui->display, &pointer_x, &pointer_y);
  if (DispatchCityWindowInput(game, event)) return;
  if (ui->pointer_stroke_count || ui->pan_button || ui->desktop.dragging != CITY_CANVAS_DRAG_NONE)
    return;
  // UI lists above follow natural scrolling. Normalize only map input to keep
  // its established panning and wheel-zoom direction in every viewport.
  CanvasSDLEvent map_scroll = *event;
  if (map_scroll.scroll_flipped) {
    map_scroll.x = -map_scroll.x;
    map_scroll.y = -map_scroll.y;
  }
  if (ui->scrollwheel_mode == CITY_SCROLLWHEEL_OFF) return;
  if (ui->scrollwheel_mode == CITY_SCROLLWHEEL_ZOOM || event->ctrl) {
    ui->wheel_zoom_remainder += map_scroll.y;
    int steps = ui->wheel_zoom_remainder / 1024;
    ui->wheel_zoom_remainder %= 1024;
    ZoomCityCanvasAt(city, pointer_x, pointer_y, steps);
    return;
  }
  PanCityMapWithScroll(city, NULL, event);
}

static bool HandleCityRedrawDebugKey(Game *game, CityHotkey key) {
  if (!game->debug_city) return false;
  Drawing *drawing = game->drawing;
  if (key == CITY_KEY_F6) {
    drawing->main_cache.valid = false;
    drawing->show_render_stats = true;
  } else if (key == CITY_KEY_F7) drawing->disable_repaint_coalescing = !drawing->disable_repaint_coalescing;
  else if (key == CITY_KEY_F8) drawing->show_redraw_rects = !drawing->show_redraw_rects;
  else if (key == CITY_KEY_F9) drawing->show_render_stats = !drawing->show_render_stats;
  else return false;
  return true;
}

static void HandleGameKey(City *city, CityHotkey key) {
  Game *game = city->game;
  CityInterfaceState *ui = game->ui;
  ui->button_feedback.beeped = false;
  Drawing *drawing = game->drawing;
  if (ui->desktop.menu.selected || key == CITY_KEY_F10) {
    if (!HandleCityToolbarShortcut(game, key)) ExecuteCityMenuItem(game, CityMenuKey(&ui->desktop, key));
    return;
  }
  if (key == CITY_KEY_ESCAPE && !CountCityCanvasWindows(ui)) {
    ui->exit_cancel_enabled = true;
    HandleCityExitCommand(city);
    return;
  }
  if (!CountCityCanvasWindows(ui) && HandleCityPlacementRotationKey(game, key)) return;
  if (HandleCityToolbarShortcut(game, key)) return;
  // Global actions are dispatched from the menu definitions above; remaining keys navigate the map.
  if (key == CITY_KEY_LEFT)
    OffsetCityMapPixels(city, NULL, 40 * drawing->map_pixel_scale, 0);
  else if (key == CITY_KEY_RIGHT)
    OffsetCityMapPixels(city, NULL, -40 * drawing->map_pixel_scale, 0);
  else if (key == CITY_KEY_UP)
    OffsetCityMapPixels(city, NULL, 0, 20 * drawing->map_pixel_scale);
  else if (key == CITY_KEY_DOWN)
    OffsetCityMapPixels(city, NULL, 0, -20 * drawing->map_pixel_scale);
}

static void DispatchCityCanvasInput(City *city, CanvasSDLEvent *event) {
  Game *game = city->game;
  CityInterfaceState *ui = game->ui;
  if (event->kind == CANVAS_SDL_KEY_DOWN) {
    ui->button_feedback.beeped = false;
    RecordGameInput(game);
  }
  if (event->kind == CANVAS_SDL_POINTER_DOWN && event->button == CANVAS_SDL_BUTTON_LEFT)
    BeginCityCanvasButtonFeedback(game, event);
  if (event->kind == CANVAS_SDL_KEY_DOWN || event->kind == CANVAS_SDL_KEY_UP ||
      event->kind == CANVAS_SDL_POINTER_DOWN || event->kind == CANVAS_SDL_POINTER_UP ||
      event->kind == CANVAS_SDL_POINTER_MOVE)
    SetCityShortcutHints(game, event->alt != 0);
  else if (event->kind == CANVAS_SDL_FOCUS_LOST)
    SetCityShortcutHints(game, false);
  if (event->kind == CANVAS_SDL_KEY_DOWN && HandleCityRedrawDebugKey(game, event->key)) return;
  if (HandleCityDeveloperShortcut(game, event)) return;
  CityTooltip *tooltip = &ui->desktop.tooltip;
  if (event->kind == CANVAS_SDL_POINTER_MOVE) {
    int x, y;
    CityInterfacePixels(game, event, &x, &y);
    MoveCityTooltipPointer(game, x, y, MonotonicSeconds());
  } else if (event->kind != CANVAS_SDL_NONE) {
    HideCityTooltip(game);
    if (event->kind == CANVAS_SDL_POINTER_DOWN && event->button < 8)
      tooltip->buttons |= 1 << event->button;
    if (event->kind == CANVAS_SDL_POINTER_UP && event->button < 8)
      tooltip->buttons &= ~(1 << event->button);
    if (event->kind == CANVAS_SDL_FOCUS_LOST || event->kind == CANVAS_SDL_POINTER_LEAVE)
      tooltip->buttons = 0;
  }
  if (event->kind == CANVAS_SDL_ZOOM_BEGIN || event->kind == CANVAS_SDL_ZOOM ||
      event->kind == CANVAS_SDL_ZOOM_END) {
    PinchCityCanvas(city, event);
    UpdateCityToolCursor(city);
    return;
  }
  if (ui->desktop.dropdown.items) {
    CityDropdownSelection selection = {0};
    bool handled = false;
    if (event->kind == CANVAS_SDL_POINTER_MOVE || event->kind == CANVAS_SDL_POINTER_DOWN ||
        event->kind == CANVAS_SDL_POINTER_UP) {
      int x, y;
      CityInterfacePixels(game, event, &x, &y);
      CityDropdownPointer kind = event->kind == CANVAS_SDL_POINTER_MOVE ? CITY_DROPDOWN_MOVE :
          event->kind == CANVAS_SDL_POINTER_DOWN ? CITY_DROPDOWN_DOWN : CITY_DROPDOWN_UP;
      if (event->kind != CANVAS_SDL_POINTER_MOVE && event->button != CANVAS_SDL_BUTTON_LEFT) {
        CloseCityDropdown(&ui->desktop);
        handled = true;
      } else handled = HandleCityDropdownPointer(&ui->desktop, kind, x, y, &selection);
    } else if (event->kind == CANVAS_SDL_KEY_DOWN) {
      handled = HandleCityDropdownKey(&ui->desktop, event->key, &selection);
    } else if (event->kind == CANVAS_SDL_SCROLL) {
      handled = HandleCityDropdownKey(&ui->desktop, event->y > 0 ? CITY_KEY_UP : CITY_KEY_DOWN, &selection);
    } else if (event->kind == CANVAS_SDL_FOCUS_LOST || event->kind == CANVAS_SDL_POINTER_LEAVE)
      CloseCityDropdown(&ui->desktop);
    if (selection.window_id) PlayCityButtonBeep(game);
    Window *owner = FindCityCanvasWindow(&ui->desktop, selection.window_id);
    if (owner) SendWindowEvent(game, owner, &(WindowEvent){.kind = WINDOW_DROPDOWN, .selection = &selection});
    if (handled) {
      if (event->kind == CANVAS_SDL_POINTER_UP) ui->desktop.input_capture_window = 0;
      return;
    }
  }
  if (event->kind == CANVAS_SDL_KEY_DOWN && ui->pointer_stroke_count &&
      HandleCityPlacementRotationKey(game, event->key)) return;
  if (event->kind == CANVAS_SDL_POINTER_DOWN && event->button == CANVAS_SDL_BUTTON_MIDDLE &&
      ui->middle_center && !CountCityCanvasWindows(ui) && IsCityMainMapInteractiveAt(city, event->x, event->y)) {
    TileIndex tile = PickCityMapTile(city, game->drawing->default_map_view, event->x, event->y);
    if (IsMapTileValid(city, tile)) CenterCityMapViewAtTile(city, NULL, MapTileX(city, tile), MapTileY(city, tile));
    return;
  }
  if (event->kind == CANVAS_SDL_POINTER_DOWN &&
      (event->button == CANVAS_SDL_BUTTON_RIGHT || event->button == CANVAS_SDL_BUTTON_MIDDLE)) {
    RecordGameInput(game);
  }
  if (event->kind == CANVAS_SDL_POINTER_DOWN && event->button != CANVAS_SDL_BUTTON_LEFT) {
    if (DispatchCityWindowInput(game, event) || HandleCityMapSignInput(city, NULL, event) ||
        HandleCityMainMapPan(city, event)) {
      UpdateCityPlacementPreview(city);
      UpdateCityToolCursor(city);
      return;
    }
  }
  if (event->kind == CANVAS_SDL_POINTER_UP && event->button != CANVAS_SDL_BUTTON_LEFT) {
    if (ui->pan_button && !ui->panning_view) HandleCityMainMapPan(city, event);
    else if (!DispatchCityWindowInput(game, event)) HandleCityMainMapPan(city, event);
    UpdateCityToolCursor(city);
    return;
  }
  if (event->kind == CANVAS_SDL_POINTER_LEAVE || event->kind == CANVAS_SDL_FOCUS_LOST)
    SetCityMapCursorPosition(ui, -1, -1);
  if (event->kind == CANVAS_SDL_KEY_DOWN || event->kind == CANVAS_SDL_KEY_UP) {
    ui->placement_hover_shift = event->shift;
    ui->placement_hover_ctrl = event->ctrl & 1;
  }
  if (event->kind == CANVAS_SDL_POINTER_LEAVE) {
    DispatchCityWindowInput(game, event);
    HandleCityMainMapPan(city, event);
    CloseCityMenu(&ui->desktop);
    ui->placement_hover_valid = 0;
    UpdateCityPlacementPreview(city);
    return;
  }
  if (event->kind == CANVAS_SDL_CLOSE) {
    ui->exit_cancel_enabled = true;
    HandleCityExitCommand(city);
  } else if (event->kind == CANVAS_SDL_KEY_DOWN && !ui->pointer_stroke_count) {
    CityHotkey key = CountCityCanvasWindows(ui) == 0 ? RemapCityKey(ui, event->key) : event->key;
    RecordGameInput(game);
    CanvasSDLEvent key_event = *event;
    key_event.key = key;
    if (ui->desktop.menu.selected || key == CITY_KEY_F10) HandleGameKey(city, key);
    else if (!DispatchCityWindowInput(game, &key_event)) HandleGameKey(city, key);
  }
  else if (event->kind == CANVAS_SDL_KEY_UP)
    DispatchCityWindowInput(game, event);
  else if (event->kind == CANVAS_SDL_TEXT_INPUT && !ui->pointer_stroke_count)
    DispatchCityWindowInput(game, event);
  else if (event->kind == CANVAS_SDL_POINTER_DOWN && event->button == CANVAS_SDL_BUTTON_LEFT)
    HandleCityCanvasPointerDown(city, event);
  else if (event->kind == CANVAS_SDL_POINTER_MOVE)
    HandleCityCanvasPointerMove(city, event);
  else if (event->kind == CANVAS_SDL_POINTER_UP && event->button == CANVAS_SDL_BUTTON_LEFT)
    HandleCityCanvasPointerUp(city, event);
  else if (event->kind == CANVAS_SDL_SCROLL)
    ScrollCityCanvas(city, event);
  else if (event->kind == CANVAS_SDL_FOCUS_LOST) {
    SetCityAudioFocus(game->audio, false);
    ui->pinch_remainder = 0;
    ui->wheel_zoom_remainder = 0;
    CanvasSDLEvent release = *event;
    release.kind = CANVAS_SDL_POINTER_UP;
    HandleCityMapSignInput(city, NULL, &release);
    release.button = ui->pan_button ? ui->pan_button : CANVAS_SDL_BUTTON_LEFT;
    HandleCityMainMapPan(city, &release);
    CloseCityMenu(&ui->desktop);
    ui->desktop.menu.held = false;
    DispatchCityWindowInput(game, event);
    ui->placement_hover_valid = 0;
    UpdateCityPlacementPreview(city);
    if (ui->pointer_stroke_count) {
      CityPointerInput last = ui->pointer_stroke[ui->pointer_stroke_count - 1];
      event->x = last.point.x;
      event->y = last.point.y;
      event->alt = !!(last.flags & CITY_POINTER_MODIFIER_ALT);
      event->shift = !!(last.flags & CITY_POINTER_MODIFIER_SHIFT);
      event->ctrl = !!(last.flags & CITY_POINTER_MODIFIER_CONTROL);
      FinishCityPointerStroke(city, event);
    }
    Sc2kCanvasDesktopPointerUp(&ui->desktop);
    HandleApplicationActivation(game, 0);
  } else if (event->kind == CANVAS_SDL_FOCUS_GAINED) {
    SetCityAudioFocus(game->audio, true);
    HandleApplicationActivation(game, 1);
  }
  UpdateCityToolCursor(city);
}

// Each real input owns its command feedback scope, including input pumped by a retained caller.
void DispatchCityCanvasEvent(City *city, CanvasSDLEvent *event) {
  CityInterfaceState *ui = city->game->ui;
  bool previous_enabled = ui->command_errors_enabled;
  u32 previous_depth = ui->command_error_depth;
  ui->command_errors_enabled = event->kind == CANVAS_SDL_KEY_DOWN || event->kind == CANVAS_SDL_TEXT_INPUT ||
      event->kind == CANVAS_SDL_POINTER_DOWN || event->kind == CANVAS_SDL_POINTER_UP ||
      event->kind == CANVAS_SDL_POINTER_MOVE || event->kind == CANVAS_SDL_SCROLL;
  ui->command_error_depth = city->game->command_depth;
  DispatchCityCanvasInput(city, event);
  CompleteCityWindowCallbacks(city);
  CompleteCityScenarioSelection(city);
  CompleteCityCreationWindow(city);
  CompleteCityFileDialogs(city);
  ui->command_error_depth = previous_depth;
  ui->command_errors_enabled = previous_enabled;
}

void CityInterfacePixels(Game *game, const CanvasSDLEvent *event, int *x, int *y) {
  Drawing *drawing = game->drawing;
  *x = event->x * drawing->map_pixel_scale; *y = event->y * drawing->map_pixel_scale;
  if (game->ui->display) Sc2kSDLFramebufferPointer(game->ui->display, x, y);
}

CanvasSDLEvent TransformCityInterfaceEvent(Game *game, const CanvasSDLEvent *event) {
  CityInterfaceState *ui = game->ui;
  CanvasSDLEvent transformed = *event;
  int x, y;
  CityInterfacePixels(game, event, &x, &y);
  u32 capture = ui->options_slider ? ui->options_slider_window : ui->jukebox_slider ?
      ui->jukebox_slider_window :
      ui->jukebox_pressed_window;
  Sc2kCanvasDesktopPixelPointer(&ui->desktop, capture, x, y, &transformed.x, &transformed.y);
  return transformed;
}
