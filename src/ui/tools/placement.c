#include "ui/tools/placement.h"

#include <stdlib.h>
#include <string.h>

#include "city/infrastructure/highways.h"
#include "city/infrastructure/lines.h"
#include "city/infrastructure/spans.h"
#include "city/infrastructure/tunnels.h"
#include "city/maps/map.h"
#include "city/presentation.h"
#include "city/state.h"
#include "drawing/display.h"
#include "drawing/maps/map_render.h"
#include "drawing/maps/placement.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/state.h"
#include "game/state.h"
#include "ui/cursor/cursor.h"
#include "ui/input.h"
#include "ui/map_visibility.h"
#include "ui/state.h"
#include "ui/tools/tools.h"

// Validate and normalize an inclusive selection footprint before publishing tile damage.
static void InvalidateCityMapSelectionRectangle(City *city, int first_x, int first_y,
    int last_x, int last_y) {
  if (!IsMapCoordinateValid(city, first_x, first_y) || !IsMapCoordinateValid(city, last_x, last_y))
    return;
  if (last_y < first_y) {
    int swap = first_y;
    first_y = last_y;
    last_y = swap;
  }
  if (last_x < first_x) {
    int swap = first_x;
    first_x = last_x;
    last_x = swap;
  }
  InvalidateTileRange(city, MapTileXY(city, first_x, first_y), last_x - first_x + 1, last_y - first_y + 1);
}

// Schedule erasure of the previous selection footprint.
void FlushCityMapSelectionRedraw(City *city) {
  Drawing *drawing = city->drawing;
  if (drawing->map_state.redraw_state == 0)
    return;
  CityMapSelectionBounds *bounds = &drawing->map_state.selection_bounds;
  InvalidateCityMapSelectionRectangle(city, bounds->min_x, bounds->min_y, bounds->max_x, bounds->max_y);
  drawing->map_state.redraw_state = 0;
}

// Retained drag presentation owns the temporary drawing target. City owns the counters and traversal;
// prepare the clip before publishing damage and clear it after the complete City update.
void UpdateCityHighwayDragPreview(City *city, CityMapViewConfig *view, TileIndex start, TileIndex end) {
  Drawing *drawing = city->drawing;
  CityCompactHighwayPlan compact;
  bool compact_preview = PlanCityCompactHighway(city, start, end, -1, &compact);
  if (!compact_preview && GetTransportSpanCompatibilityMask(city, start) == CITY_SPAN_COMPATIBILITY_INVALID)
    return;
  PrepareCityMapPaintClip(drawing, view->view_rect);
  UpdateCityHighwayPreview(city, start, end, compact_preview ? &compact : NULL);
  ResetCityMapPaintClip(drawing);
}

// Update the tool footprint at the view's saved pointer position.
void UpdateCityMapToolPreview(Game *game) {
  City *city = game->city;
  Drawing *drawing = game->drawing;
  CityMapViewConfig *view = drawing->default_map_view;
  CityInterfaceState *ui = game->ui;
  UpdateCityMapCursor(game);
  if (ui->pointer_mode != CITY_MAP_POINTER_HOVER) {
    if (drawing->map_state.redraw_state != 0)
      FlushCityMapSelectionRedraw(city);
    return;
  }
  if (city->disasters.monster_count != 0)
    return;
  int size = city->settings.timer_mode != CITY_TIMER_STOPPED ?
      kCityToolFootprintSizes[CITY_TOOL_VARIANT_CAPACITY * ui->tool_group +
          (i16)ui->selection.value[ui->tool_group]] :
      ui->terrain_tool != CITY_TERRAIN_TOOL_CENTER;
  int align_even = city->settings.timer_mode != CITY_TIMER_STOPPED &&
      ui->tool_group == CITY_TOOL_GROUP_ROAD &&
      ui->selection.value[CITY_TOOL_GROUP_ROAD] == CITY_ROAD_HIGHWAY;
  if (size == 0)
    return;
  int half = size >> 1;
  int third = size / 3;
  int pointer_x = view->pointer_x;
  int pointer_y = view->pointer_y;
  if (view->magnified_large_zoom != 0) {
    pointer_x >>= 1;
    pointer_y >>= 1;
  }
  if (pointer_x < view->view_rect.left || pointer_x >= view->view_rect.right ||
      pointer_y < view->view_rect.top || pointer_y >= view->view_rect.bottom) {
    FlushCityMapSelectionRedraw(city);
    return;
  }
  TileIndex tile = PickCityMapTile(city, view, view->pointer_x, view->pointer_y);
  if (!IsMapTileValid(city, tile)) {
    FlushCityMapSelectionRedraw(city);
    return;
  }
  int y = MapTileY(city, tile);
  int x = MapTileX(city, tile);
  if (align_even) {
    y &= ~1;
    x &= ~1;
  }
  if (y < third || x < third || y > city->map_height - half || x > city->map_width - half) {
    FlushCityMapSelectionRedraw(city);
    return;
  }
  if (y == drawing->map_state.selection_redraw_state && x == drawing->map_state.preview_x)
    return;
  CityMapSelectionBounds *bounds = &drawing->map_state.selection_bounds;
  if (drawing->map_state.redraw_state != 0)
    InvalidateCityMapSelectionRectangle(city, bounds->min_x, bounds->min_y, bounds->max_x, bounds->max_y);
  drawing->map_state.selection_redraw_state = y;
  drawing->map_state.preview_x = x;
  bounds->min_y = y - third;
  bounds->min_x = x - third;
  bounds->max_y = y + half;
  bounds->max_x = x + half;
  InvalidateCityMapSelectionRectangle(city, bounds->min_x, bounds->min_y, bounds->max_x, bounds->max_y);
  drawing->map_state.redraw_state = 1;
}

// The tile renderer chooses this marker's layer; later tiles still occlude it in painter order.
void DrawCityMapPlacementTile(const City *city, TileIndex tile) {
  Drawing *drawing = city->drawing;
  if (!drawing->paint_placement || drawing->sprite_damage_bounds) return;
  const CityCanvasPlacementSelection *placement = &city->game->ui->desktop.placement;
  int first = 0, end = placement->count;
  while (first < end) {
    int middle = first + (end - first) / 2;
    if (placement->cells[middle].tile < tile) first = middle + 1;
    else end = middle;
  }
  bool selected = first < placement->count && placement->cells[first].tile == tile;
  if (city->game->ui->error_tile_window && city->game->ui->error_tile == tile && (!selected || placement->point))
    DrawCityMapPlacementMarker(city, tile, false, true);
  if (selected)
    DrawCityMapPlacementMarker(city, tile, placement->point, false);
}

// Grow UI's paired arrays, preserving the committed selection and the populated scratch prefix.
// Failure leaves both arrays intact; unused scratch capacity has no contents to preserve.
static bool ReserveCityPlacementCells(CityCanvasPlacementSelection *placement, int scratch_count, int required_count) {
  if (required_count <= placement->capacity) return true;
  int capacity = IntMax(required_count, IntMax(64, placement->capacity * 2));
  CityCanvasPlacementCell *cells = malloc((size_t)capacity * 2 * sizeof(*cells));
  if (!cells) return false;
  if (placement->count)
    memcpy(cells, placement->cells, (size_t)placement->count * sizeof(*cells));
  if (scratch_count)
    memcpy(cells + capacity, placement->scratch, (size_t)scratch_count * sizeof(*cells));
  free(placement->cells);
  placement->cells = cells;
  placement->scratch = cells + capacity;
  placement->capacity = capacity;
  return true;
}

// Children already satisfy the heap order. Move this parent down until it is at least as large as both children.
static void SiftCityPlacementCells(CityCanvasPlacementCell *cells, int root, int count) {
  CityCanvasPlacementCell value = cells[root];
  for (int child = 2 * root + 1; child < count; child = 2 * root + 1) {
    if (child + 1 < count && cells[child].tile < cells[child + 1].tile) ++child;
    if (value.tile >= cells[child].tile) break;
    cells[root] = cells[child];
    root = child;
  }
  cells[root] = value;
}

// Placement records contain only tile keys. A max heap produces ascending order without a comparator callback,
// recursion or extra storage; duplicate keys remain in the selection.
static void SortCityPlacementCells(CityCanvasPlacementCell *cells, int count) {
  for (int root = count / 2 - 1; root >= 0; --root) SiftCityPlacementCells(cells, root, count);
  for (int end = count - 1; end > 0; --end) {
    CityCanvasPlacementCell value = cells[0];
    cells[0] = cells[end];
    cells[end] = value;
    SiftCityPlacementCells(cells, 0, end);
  }
}

// Commit the prefix prepared by AppendCityPlacementRectangle, without allocating or borrowing another array.
// Returns whether the visible selection changed; frame preparation publishes its old/new damage once.
static bool CommitCityPlacementPreview(const City *city, int count, bool point) {
  CityCanvasPlacementSelection *placement = &city->game->ui->desktop.placement;
  CityCanvasPlacementCell *cells = placement->scratch;
  if ((u32)count > city->map_cell_count) return false;
  const Drawing *drawing = city->drawing;
  CityRect bounds = {0};
  bool ordered = true;
  for (int i = 0; i < count; ++i) {
    if (!IsMapTileValid(city, cells[i].tile)) return false;
    if (i && cells[i - 1].tile > cells[i].tile) ordered = false;
    CityRect cell = CityMapPlacementBounds(city, cells[i].tile, point);
    if (!i) bounds = cell;
    else bounds = UnionCityRects(bounds, cell);
  }
  // Sorted cells allow bounded tile rendering to use binary lookup. Keep duplicate keys.
  if (!ordered) SortCityPlacementCells(cells, count);
  bool same_cells = count == placement->count;
  for (int i = 0; same_cells && i < count; ++i)
    same_cells = cells[i].tile == placement->cells[i].tile;
  const CityMapViewConfig *view = count ? drawing->default_map_view : NULL;
  if (same_cells && (!count ||
      (placement->point == point &&
       !memcmp(&bounds, &placement->bounds, sizeof(bounds)) &&
       placement->origin_x == drawing->map_state.origin_x &&
       placement->origin_y == drawing->map_state.origin_y &&
       placement->zoom == view->zoom &&
       placement->magnified == view->magnified_large_zoom &&
       placement->underground == drawing->map_state.events_suppressed))) return false;
  placement->count = count;
  placement->bounds = bounds;
  placement->point = point;
  if (count) {
    memcpy(placement->cells, cells, (size_t)count * sizeof(*cells));
    placement->origin_x = drawing->map_state.origin_x;
    placement->origin_y = drawing->map_state.origin_y;
    placement->zoom = view->zoom;
    placement->magnified = view->magnified_large_zoom;
    placement->underground = drawing->map_state.events_suppressed;
  }
  return true;
}

static bool AppendCityPlacementRectangle(const City *city, CityRect rectangle, CityRect visible, int *count) {
  rectangle = IntersectCityRects(rectangle,
      (CityRect){IntMax(0, visible.left), IntMax(0, visible.top),
          IntMin(city->map_width, visible.right), IntMin(city->map_height, visible.bottom)});
  if (rectangle.left >= rectangle.right || rectangle.top >= rectangle.bottom) return true;
  int added = (rectangle.right - rectangle.left) * (rectangle.bottom - rectangle.top);
  CityCanvasDesktop *desktop = &city->game->ui->desktop;
  if (!ReserveCityPlacementCells(&desktop->placement, *count, *count + added)) return false;
  for (int y = rectangle.top; y < rectangle.bottom; ++y) {
    TileIndex tile = MapTileXY(city, rectangle.left, y);
    for (int x = rectangle.left; x < rectangle.right; ++x)
      desktop->placement.scratch[(*count)++] = (CityCanvasPlacementCell){.tile = tile++};
  }
  return true;
}

static bool AppendCityPlacementSpan(const City *city, TileIndex start, int direction, bool choose_direction,
    CityRect visible, int *count) {
  CityInfrastructureSpan span = FindCityInfrastructureSpan(city, start, direction, choose_direction);
  if (span.end == INVALID_TILE) return true;
  int x = MapTileX(city, span.start), y = MapTileY(city, span.start);
  int end_x = MapTileX(city, span.end), end_y = MapTileY(city, span.end);
  return AppendCityPlacementRectangle(city, (CityRect){IntMin(x, end_x), IntMin(y, end_y),
      IntMax(x, end_x) + 1, IntMax(y, end_y) + 1}, visible, count);
}

static bool AppendCityPlacementHighwaySpan(const City *city, TileIndex start, int direction, bool choose_direction,
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

TileIndex PickCityPlacementTile(const City *city, int x, int y) {
  const CityMapViewConfig *view = city->drawing->default_map_view;
  return city->game->ui->desktop.selected_tool == CITY_CANVAS_TOOL_LEVEL_TERRAIN ?
      PickCityMapTerrainPoint(city, view, x, y) : PickCityMapTile(city, view, x, y);
}

void UpdateCityPlacementPreview(const City *city) {
  CityInterfaceState *ui = city->game->ui;
  Drawing *drawing = city->drawing;
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
  CityMapCamera camera = {.origin_x = drawing->map_state.origin_x, .origin_y = drawing->map_state.origin_y,
      .zoom = drawing->default_map_view->zoom, .rotation = CityMapViewRotation(drawing->default_map_view)};
  CityRect visible = VisibleCityMapTileBounds(city, camera,
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
  ui->placement_dirty |= CommitCityPlacementPreview(city, count, intent.point);
}

CityRect CityPlacementPixelBounds(const City *city) {
  const Drawing *drawing = city->drawing;
  const CityCanvasPlacementSelection *placement = &city->game->ui->desktop.placement;
  CityRect bounds = placement->bounds;
  int scale = drawing->map_pixel_scale;
  return placement->count ?
      (CityRect){bounds.left * scale + drawing->scroll_pixels.x, bounds.top * scale + drawing->scroll_pixels.y,
          bounds.right * scale + drawing->scroll_pixels.x, bounds.bottom * scale + drawing->scroll_pixels.y} :
      (CityRect){0};
}

void PublishCityPlacementDamage(const City *city) {
  Drawing *drawing = city->drawing;
  CityInterfaceState *ui = city->game->ui;
  // Ignore intermediate hover positions that never reached the framebuffer.
  // Only the last painted preview and the final pointer position need damage.
  CityRect previous = drawing->placement_painted_bounds;
  if (ui->placement_dirty) {
    int scale = drawing->map_pixel_scale;
    CityRect current = CityPlacementPixelBounds(city);
    MarkCityPlacementDamage(drawing, previous, current);
    if (drawing->main_cache.valid && previous.left < previous.right && previous.top < previous.bottom) {
      int magnification = drawing->default_map_view->magnified_large_zoom ? 2 : 1;
      int dx = (drawing->map_state.origin_x - drawing->main_cache.origin_x) * scale * magnification;
      int dy = (drawing->map_state.origin_y - drawing->main_cache.origin_y) * scale * magnification;
      dx += drawing->target_x - drawing->main_cache.target_x;
      dy += drawing->target_y - drawing->main_cache.target_y;
      if (dx || dy) MarkCityPlacementDamage(drawing,
          (CityRect){previous.left + dx, previous.top + dy, previous.right + dx, previous.bottom + dy}, current);
    }
  }
}
