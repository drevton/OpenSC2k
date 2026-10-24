#include "city/infrastructure/lines.h"
#include "city/infrastructure/spans.h"
#include "city/maps/terrain.h"
#include <stdlib.h>
#include <string.h>

#include "assets/state.h"
#include "city/economy/economy.h"
#include "city/engine/city_types.h"
#include "city/engine/commands.h"
#include "city/engine/simulation.h"
#include "city/events/emergency.h"
#include "city/events/events.h"
#include "city/infrastructure/bridges.h"
#include "city/infrastructure/highways.h"
#include "city/infrastructure/types.h"
#include "city/maps/layers.h"
#include "city/maps/map.h"
#include "city/maps/tables.h"
#include "city/maps/tile_storage.h"
#include "city/maps/trees.h"
#include "city/presentation.h"
#include "city/state.h"
#include "city/tools/tools.h"
#include "city/tools/types.h"
#include "drawing/bitmaps.h"
#include "drawing/maps/cost_labels.h"
#include "drawing/maps/map_render.h"
#include "drawing/maps/map_view.h"
#include "drawing/maps/placement.h"
#include "drawing/maps/tables.h"
#include "drawing/state.h"
#include "game/commands.h"
#include "game/errors.h"
#include "game/state.h"
#include "ui/cursor/cursor.h"
#include "ui/charts/charts.h"
#include "ui/commands.h"
#include "ui/dialogs/dialogs.h"
#include "ui/dialogs/information.h"
#include "ui/dialogs/name_editor.h"
#include "ui/input.h"
#include "ui/map.h"
#include "ui/state.h"
#include "ui/text/text.h"
#include "ui/tools/cost_labels.h"
#include "ui/tools/tools.h"
#include "ui/tools/types.h"
#include "ui/windows/windows.h"

static void InvalidateCityMapSelectionTile(City *city, int x, int y) {
  TileIndex tile = MapTileXYOrInvalid(city, x, y);
  InvalidateTile(city, tile);
}

// Invalidate an inclusive, normalized selection footprint.
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
  for (int y = first_y; y <= last_y; ++y) {
    for (int x = first_x; x <= last_x; ++x)
      InvalidateCityMapSelectionTile(city, x, y);
  }
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
void UpdateInfrastructureLinePlacementPreview(City *city, CityMapViewConfig *view, TileIndex start, TileIndex end) {
  Drawing *drawing = city->drawing;
  CityCompactHighwayPlan compact;
  bool compact_preview = PlanCityCompactHighway(city, start, end, -1, &compact);
  if (!compact_preview && GetTransportSpanCompatibilityMask(city, start) == CITY_SPAN_COMPATIBILITY_INVALID)
    return;
  PrepareCityMapPaintClip(drawing, view->view_rect);
  UpdateCityHighwayPreview(city, start, end, compact_preview ? &compact : NULL);
  ResetCityMapPaintClip(drawing);
}

// Pointer gestures belong to UI. City operations receive world locations/options;
// this boundary alone selects the main camera for the retained input adapters.

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

// The enclosing tracker controls cursor consumption through release or cancellation.
CityPoint ReadCityCommandPointer(City *city, u32 index) {
  return city->pointer_command.samples != NULL ? city->pointer_command.samples[index].point
                                               : city->pointer_command.points[index];
}

void ApplyCityCommandPointerModifiers(City *city, u32 index) {
  if (city->pointer_command.samples == NULL)
    return;
  u32 flags = city->pointer_command.samples[index].flags;
  SetCityAltKeyPressed(city->game->ui, (flags & CITY_POINTER_MODIFIER_ALT) != 0);
  SetCityShiftKeyPressed(city->game->ui, (flags & CITY_POINTER_MODIFIER_SHIFT) != 0);
  SetCityCtrlKeyPressed(city->game->ui, (flags & CITY_POINTER_MODIFIER_CONTROL) != 0);
}

static CityZoneDragInput Sc2kNextZoneDragInput(City *city) {
  if (city->zone_drag_input == NULL) {
    CityPoint point = ReadCityCommandPointer(city, city->pointer_command.index);
    ApplyCityCommandPointerModifiers(city, city->pointer_command.index);
    u32 flags;
    if (city->pointer_command.samples != NULL) {
      u32 sample = city->pointer_command.samples[city->pointer_command.index].flags;
      flags = (sample & CITY_POINTER_MODIFIER_SHIFT) != 0 ? CITY_DRAG_INPUT_SHIFT_CANCEL
                                                          : sample & CITY_POINTER_SAMPLE_RELEASE;
    } else {
      flags = city->pointer_command.index + 1 == city->pointer_command.count ? city->pointer_command.release_flags : 0;
    }
    if (flags == 0)
      ++city->pointer_command.index;
    return (CityZoneDragInput){point, flags};
  }
  return city->zone_drag_input[city->zone_drag_index++];
}

static CityZoneDragInput Sc2kNextCityMapZoneInput(Game *game) {
  City *city = game->city;
  Drawing *drawing = game->drawing;
  CityMapViewConfig *view = drawing->default_map_view;
  CityZoneDragInput input = Sc2kNextZoneDragInput(city);
  view->pointer_x = input.point.x;
  view->pointer_y = input.point.y;
  return input;
}

static void PresentCityPlacementDragCost(City *city, CityMapViewConfig *view, int unit_cost,
    int screen_x, int screen_y) {
  city->construction.cost = 25 * city->construction.terrain_surcharge_count +
      (u32)(int)unit_cost * city->construction.cell_count;
  Sc2kShowZoneDragCost(city, view, city->construction.cost, screen_x, screen_y);
}

TileIndex TrackCityPlacementDrag(Game *game, CityMapViewConfig *view, TileIndex start,
    int unit_cost, CityPlacementDragKind kind) {
  City *city = game->city;
  if (city->command_choices.endpoint_supplied) {
    TileIndex end = city->command_choices.end;
    if (!IsMapTileValid(city, start) || !IsMapTileValid(city, end)) return INVALID_TILE;
    if (kind == CITY_DRAG_ZONE) {
      city->construction.cost = CalculateCityZoneCost(city, start, end, city->construction.zone_code, unit_cost);
      return end;
    }
    return PrepareCityLineEndpoint(city, start, end, unit_cost, kind == CITY_DRAG_TRANSPORT);
  }
  Drawing *drawing = game->drawing;
  CityMapViewConfig *map_view = drawing->default_map_view;
  if (view == NULL) view = map_view;
  CityInterfaceState *ui = game->ui;
  int x = MapTileX(city, start), y = MapTileY(city, start);
  bool zone = kind == CITY_DRAG_ZONE, transport = kind == CITY_DRAG_TRANSPORT;
  TileIndex end = start;
  if (zone) {
    if (IsMapTileValid(city, start) && HasTileWater(&city->tile[start]))
      return INVALID_TILE;
  } else {
    view->pointer_command_stage = 0;
    if (transport) ui->input_loop_active = 1;
    city->construction.cost = 0;
  }
  FlushCityMapSelectionRedraw(city);
  if (transport) {
    UpdateInfrastructureLinePlacementPreview(city, view, start, start);
  } else if (!PrepareCityZonePreview(city, start, start)) {
    if (!zone) {
      ReportCityCommandResult(city, CITY_COMMAND_COST_BLOCKED);
      view->pointer_command_stage = 1;
    }
    return INVALID_TILE;
  }
  if (zone) {
    // The line preview counts its origin even when it already has this zone.
    UpdateCityZoneRectanglePreview(city, start, start, start);
    view->pointer_command_stage = 0;
    ui->input_loop_active = 1;
  }
  CityPoint screen = CityMapTileToScreen(city, map_view, (CityPoint){x, y});
  PresentCityPlacementDragCost(city, view, unit_cost, screen.x, screen.y);
  if (!zone && !transport) ui->input_loop_active = 1;
  CityZoneDragInput input;
  do {
    input = transport ? Sc2kNextCityMapZoneInput(game) : Sc2kNextZoneDragInput(city);
    if (input.flags & CITY_DRAG_INPUT_SHIFT_CANCEL) {
      CancelCityCommandFeedback(city);
      view->pointer_command_stage = 1;
      ui->input_loop_active = 0;
      view->pointer_drag_active = 0;
      ClearCityZoneCostLabel(drawing, view);
      return INVALID_TILE;
    }
    TileIndex tile = PickCityMapTile(city, map_view, input.point.x, input.point.y);
    if (IsMapTileValid(city, tile) && tile != end) {
      if (!transport) ClearCityZoneCostLabel(drawing, view);
      if (zone) UpdateCityZoneRectanglePreview(city, start, end, tile);
      else if (transport) UpdateInfrastructureLinePlacementPreview(city, view, start, end);
      else PrepareCityZonePreview(city, start, end);
      end = tile;
      if (transport) UpdateInfrastructureLinePlacementPreview(city, view, start, end);
      else if (!zone) PrepareCityZonePreview(city, start, end);
      PresentCityPlacementDragCost(city, view, unit_cost, screen.x, screen.y);
    }
  } while (!(input.flags & CITY_DRAG_INPUT_RELEASE));
  if (zone) UpdateCityZoneRectanglePreview(city, start, end, start);
  if (transport) UpdateInfrastructureLinePlacementPreview(city, view, start, end);
  else PrepareCityZonePreview(city, start, zone ? start : end);
  ClearCityZoneCostLabel(drawing, view);
  view->pointer_command_stage = 1;
  ui->input_loop_active = 0;
  return end;
}

// Original game drag loop; client inputs replace OS message polling at the host boundary.
TileIndex TrackCityZoneDrag(Game *game, TileIndex start, int unit_cost) {
  return TrackCityPlacementDrag(game, NULL, start, unit_cost, CITY_DRAG_ZONE);
}

// Track an infrastructure endpoint, updating its cost and placement footprint.
TileIndex TrackInfrastructureLinePlacement(Game *game, TileIndex start, int unit_cost) {
  return TrackCityPlacementDrag(game, NULL, start, unit_cost, CITY_DRAG_LINE);
}

// Retained pointer callers use the selected tool cost; shared commands already supply their endpoint and cost.
TileIndex TrackCitySelectedInfrastructureLine(Game *game, TileIndex start, int group) {
  int cost = kCityToolCosts[group * CITY_TOOL_VARIANT_CAPACITY + game->ui->selection.value[group]];
  return TrackInfrastructureLinePlacement(game, start, cost);
}

// Transport dragging reads the map view's pointer after the host dispatches each message.
TileIndex TrackCityMapZonePlacementDrag(Game *game, TileIndex start, int unit_cost) {
  return TrackCityPlacementDrag(game, NULL, start, unit_cost, CITY_DRAG_TRANSPORT);
}

void BeginCityMapPointerInput(Game *game, CityPoint point) {
  CityMapViewConfig *view = game->drawing->default_map_view;
  view->pointer_x = point.x;
  view->pointer_y = point.y;
  view->pointer_drag_active = 1;
}

u32 BeginCityVolcanoPresentation(Game *game, TileIndex location) {
  Drawing *drawing = game->drawing;
  CityMapViewConfig *view = drawing->default_map_view;
  u32 label = drawing->map_state.label_display;
  drawing->map_state.label_display = 0;
  City *city = game->city;
  CenterCityMapViewAtTile(city, NULL, MapTileX(city, location), MapTileY(city, location));
  if (view->zoom == CITY_MAP_ZOOM_SMALL) ZoomCityMapViewIn(city, view);
  return label;
}

CityPlacementIntent ResolveCityPlacementIntent(Game *game, int tools, TileIndex start, TileIndex end, u32 options) {
  const CityInterfaceState *ui = game->ui;
  int group = tools & 255;
  int variant = group == CITY_TOOL_GROUP_DEMOLISH ? (u32)tools >> 16 : (tools >> 8) & 255;
  bool control = (options & CITY_POINTER_MODIFIER_CONTROL) != 0;
  CityPlacementIntent intent = {.start = start, .end = end, .options = options, .group = group, .variant = variant,
      .bulldoze_options = (options & CITY_POINTER_MODIFIER_ALT ? CITY_BULLDOZER_ALT : 0) |
          (options & CITY_POINTER_UNDERGROUND ? CITY_BULLDOZER_UNDERGROUND : 0),
      .tree_options = ui->desktop.menu.tree_species | ui->desktop.menu.tree_climate << CITY_TREE_CLIMATE_SHIFT |
          (options & CITY_POINTER_MODIFIER_SHIFT ? CITY_TREE_REMOVE : 0),
      .terrain_options = (ui->terrain_point_edit ? CITY_TERRAIN_POINT_EDIT : 0) |
          (ui->terrain_no_seawater ? CITY_TERRAIN_NO_SEAWATER : 0),
      .highway_facing = ui->placement_rotation_set ? ui->placement_rotation : -1,
      .facility_facing = (ui->placement_rotation + game->city->geography.rotation) & 3};
  intent.clear = control || (group == CITY_TOOL_GROUP_DEMOLISH && variant == CITY_DEMOLISH_CLEAR);
  intent.dezone = !control && group == CITY_TOOL_GROUP_DEMOLISH && variant == CITY_DEMOLISH_DEZONE;
  intent.trees = !control && group == CITY_TOOL_GROUP_TREE_WATER && variant == CITY_TREE_WATER_TREES;
  intent.canal = !control && group == CITY_TOOL_GROUP_TREE_WATER && variant == CITY_TREE_WATER_WATER;
  intent.bridge = (group == CITY_TOOL_GROUP_ROAD && variant == CITY_ROAD_BRIDGE) ||
      (group == CITY_TOOL_GROUP_RAIL && variant == CITY_RAIL_BRIDGE);
  intent.highway = group == CITY_TOOL_GROUP_ROAD && variant == CITY_ROAD_HIGHWAY;
  intent.surface_span = (group == CITY_TOOL_GROUP_ROAD && variant == CITY_ROAD_ROAD) ||
      (group == CITY_TOOL_GROUP_RAIL && variant == CITY_RAIL_RAIL) ||
      (group == CITY_TOOL_GROUP_POWER && variant == CITY_POWER_LINES);
  intent.span_tool = intent.surface_span || intent.highway;
  intent.line = intent.bridge || intent.span_tool ||
      (group == CITY_TOOL_GROUP_RAIL && variant == CITY_RAIL_SUBWAY) ||
      (group == CITY_TOOL_GROUP_WATER && variant == CITY_WATER_PIPES);
  intent.endpoint_drag = intent.span_tool ||
      (group == CITY_TOOL_GROUP_WATER && variant == CITY_WATER_PIPES) ||
      (group == CITY_TOOL_GROUP_RAIL && variant == CITY_RAIL_SUBWAY) ||
      (group >= CITY_TOOL_GROUP_PORT && group <= CITY_TOOL_GROUP_INDUSTRIAL) || intent.dezone;
  intent.point = !control && group == CITY_TOOL_GROUP_DEMOLISH &&
      (variant == CITY_DEMOLISH_LEVEL || (ui->terrain_point_edit &&
          (variant == CITY_DEMOLISH_RAISE || variant == CITY_DEMOLISH_LOWER)));
  intent.size = control || (tools >> 16) ? 1 : group < CITY_TOOL_FOOTPRINT_GROUP_COUNT &&
      variant < CITY_TOOL_VARIANT_CAPACITY ? kCityToolFootprintSizes[group * CITY_TOOL_VARIANT_CAPACITY + variant] : 0;
  return intent;
}

// Input coordinates and selected tools are local. Only resolved locations/options cross the City boundary.
const u8 kCityToolVariantCounts[CITY_TOOL_GROUP_COUNT] = {
  [CITY_TOOL_GROUP_DEMOLISH] = 5,
  [CITY_TOOL_GROUP_TREE_WATER] = 2,
  [CITY_TOOL_GROUP_DISPATCH] = 3,
  [CITY_TOOL_GROUP_POWER] = 11,
  [CITY_TOOL_GROUP_WATER] = 5,
  [CITY_TOOL_GROUP_REWARDS] = 9,
  [CITY_TOOL_GROUP_ROAD] = 6,
  [CITY_TOOL_GROUP_RAIL] = 6,
  [CITY_TOOL_GROUP_PORT] = 2,
  [CITY_TOOL_GROUP_RESIDENTIAL] = 2,
  [CITY_TOOL_GROUP_COMMERCIAL] = 2,
  [CITY_TOOL_GROUP_INDUSTRIAL] = 2,
  [CITY_TOOL_GROUP_EDUCATION] = 4,
  [CITY_TOOL_GROUP_CIVIC] = 4,
  [CITY_TOOL_GROUP_RECREATION] = 5,
  [CITY_TOOL_GROUP_SIGN] = 1,
  [CITY_TOOL_GROUP_QUERY] = 1,
  [CITY_TOOL_GROUP_CENTER] = 1,
};

// Each tile command is independently priced and admitted against live City state. Lower first, then raise, so
// slope propagation never moves an already leveled tile away from the target. Stop on the first failed edit.
bool RunCityLevelTerrain(Game *game, TileIndex start, TileIndex end, int height, int options) {
  City *city = game->city;
  if (!IsMapTileValid(city, start) || !IsMapTileValid(city, end)) return false;
  CityRect bounds = MapTileRectangle(city, start, end);
  for (int pass = 0; pass < 2; ++pass)
    for (int y = bounds.top; y < bounds.bottom; ++y)
      for (int x = bounds.left; x < bounds.right; ++x) {
        TileIndex tile = MapTileXY(city, x, y);
        CityTileGeometry geometry = ReadCityTileGeometry(city, tile);
        bool lower = geometry.land_height >= height &&
            (geometry.land_height > height || geometry.land_shape != CITY_TERRAIN_FLAT);
        if (pass == 0 ? !lower : geometry.land_height >= height) continue;
        if (!RunCityCommand(game, CITY_COMMAND_LEVEL_TERRAIN_TILE, tile, height, options, 0, NULL, NULL))
          return false;
      }
  return true;
}

bool RunCityMapToolAction(Game *game, const CityPlacementIntent *intent) {
  TileIndex start = intent->start, end = intent->end;
  int group = intent->group, variant = intent->variant;
  u32 options = intent->options;
  City *city = game->city;
  if (!IsMapTileValid(city, start) || !IsMapTileValid(city, end)) return false;
  if ((options & CITY_POINTER_MODIFIER_SHIFT) || group == CITY_TOOL_GROUP_QUERY) {
    PresentCityTileInformationDialog(city, end);
    return true;
  }
  if (group == CITY_TOOL_GROUP_CENTER) {
    CenterCityMapViewAtTile(city, NULL, MapTileX(city, end), MapTileY(city, end));
    return true;
  }
  if (options & CITY_POINTER_MODIFIER_CONTROL) { group = CITY_TOOL_GROUP_DEMOLISH; variant = CITY_DEMOLISH_CLEAR; }
  if (group == CITY_TOOL_GROUP_DEMOLISH) {
    if (variant == CITY_DEMOLISH_CLEAR) {
      return RunCityCommand(game, CITY_COMMAND_BULLDOZE_RECT, start, end, intent->bulldoze_options, 0, NULL, NULL);
    }
    if (variant == CITY_DEMOLISH_DEZONE)
      return RunCityCommand(game, CITY_COMMAND_PLACE_ZONE_RECT, start, ZONE_NONE, end, 0, NULL, NULL);
    if (!game->observation && variant == CITY_DEMOLISH_LEVEL)
      return RunCityLevelTerrain(game, start, end, ReadCityTileGeometry(city, start).land_height,
          intent->terrain_options & CITY_TERRAIN_NO_SEAWATER);
    int terrain = variant == CITY_DEMOLISH_RAISE ? variant :
        (variant == CITY_DEMOLISH_LOWER ? CITY_TERRAIN_COMMAND_CHARGE :
            ReadCityLandHeight(&city->tile[start])) |
        variant << CITY_TERRAIN_COMMAND_TOOL_SHIFT;
    CityCommandId id = variant == CITY_DEMOLISH_RAISE ? CITY_COMMAND_RAISE_TERRAIN_TILE :
        variant == CITY_DEMOLISH_LOWER ? CITY_COMMAND_LOWER_TERRAIN_TILE : CITY_COMMAND_STEP_TERRAIN_HEIGHT;
    return RunCityCommand(game, id, end, terrain,
        intent->terrain_options, 0, NULL, NULL);
  }
  if (group == CITY_TOOL_GROUP_TREE_WATER) {
    bool trees = variant == CITY_TREE_WATER_TREES;
    return RunCityCommand(game, trees ? CITY_COMMAND_PLANT_TREES_RECT : CITY_COMMAND_BUILD_CANALS_RECT,
        start, end, trees ? intent->tree_options : 0, 0, NULL, NULL);
  }
  if (group == CITY_TOOL_GROUP_DISPATCH) {
    static const u8 kServices[] = {CITY_MAP_EVENT_POLICE, CITY_MAP_EVENT_FIRE, CITY_MAP_EVENT_MILITARY};
    return variant >= 0 && variant < 3 && RunCityCommand(game, CITY_COMMAND_DISPATCH_CITY_SERVICE,
        end, kServices[variant], 0, 0, NULL, NULL);
  }
  if (group >= CITY_TOOL_GROUP_PORT && group <= CITY_TOOL_GROUP_INDUSTRIAL) {
    int zone = CityToolZoneCode(group, variant);
    return RunCityCommand(game, CITY_COMMAND_PLACE_ZONE_RECT, start, zone, end, 0, NULL, NULL);
  }
  if (group == CITY_TOOL_GROUP_SIGN) {
    PresentCityLabelEditor(city, end, false);
    return true;
  }
  int span = (options & CITY_POINTER_EXPLICIT_ANSWERS) ?
      ((options & CITY_POINTER_SPAN_CHOICE_MASK) >> CITY_POINTER_SPAN_CHOICE_SHIFT) - 1 : CITY_SPAN_CANCEL;
  bool border = options & CITY_POINTER_ACCEPT_BORDER;
  if (group == CITY_TOOL_GROUP_ROAD) {
    if (variant == CITY_ROAD_ROAD) {
      int style = EncodeCityRoadSpanChoice(span);
      return RunCityCommand(game, CITY_COMMAND_PLACE_ROAD, start, end, style << 8 | (border ? CITY_LINE_ACCEPT_BORDER : 0), 0, NULL, NULL);
    }
    if (variant == CITY_ROAD_HIGHWAY) {
      int style = span == CITY_SPAN_CANCEL ? CITY_ROAD_SPAN_CANCEL : span;
      return RunCityCommand(game, CITY_COMMAND_PLACE_TRANSPORT_LINE, start, end,
          style | border << 8 | (intent->highway_facing + 1) << CITY_HIGHWAY_FACING_SHIFT, 0, NULL, NULL);
    }
    if (variant == CITY_ROAD_TUNNEL)
      return RunCityCommand(game, CITY_COMMAND_PLACE_ROAD_TERRAIN_SPAN, start,
          border ? CITY_TUNNEL_CONFIRMED : 0, 0, 0, NULL, NULL);
    if (variant == CITY_ROAD_ONRAMP)
      return RunCityCommand(game, CITY_COMMAND_PLACE_HIGHWAY_ONRAMP, start, 0, 0, 0, NULL, NULL);
  }
  if (group == CITY_TOOL_GROUP_RAIL) {
    if (variant == CITY_RAIL_RAIL)
      return RunCityCommand(game, CITY_COMMAND_PLACE_RAIL, start, end,
          (border ? CITY_LINE_ACCEPT_BORDER : 0) | (span == CITY_SPAN_CANCEL ? CITY_LINE_CANCEL_SPAN : 0), 0, NULL, NULL);
    if (variant == CITY_RAIL_SUBWAY)
      return RunCityCommand(game, CITY_COMMAND_PLACE_SUBWAY, start, end, 0, 0, NULL, NULL);
    if (variant == CITY_RAIL_SUBWAY_RAIL_CONNECTION)
      return RunCityCommand(game, CITY_COMMAND_PLACE_SUBWAY_RAIL_CONNECTION, start, 0, 0, 0, NULL, NULL);
  }
  if (group == CITY_TOOL_GROUP_POWER && variant == CITY_POWER_HYDRO)
    return RunCityCommand(game, CITY_COMMAND_PLACE_BUILDING, end, TK_HYDRO_POWER, -1, 0, NULL, NULL);
  if (group == CITY_TOOL_GROUP_POWER && variant == CITY_POWER_LINES)
    return RunCityCommand(game, CITY_COMMAND_PLACE_POWER_LINE, start, end,
        span == CITY_SPAN_CANCEL ? CITY_LINE_CANCEL_SPAN : 0, 0, NULL, NULL);
  if (group == CITY_TOOL_GROUP_WATER && variant == CITY_WATER_PIPES)
    return RunCityCommand(game, CITY_COMMAND_PLACE_WATER_PIPE, start, end, variant, 0, NULL, NULL);
  int building = GetCityFacilityTile(group, variant);
  int facing = IsTileKindBuilding(building) ? intent->facility_facing : -1;
  return RunCityCommand(game, CITY_COMMAND_PLACE_BUILDING, end, building, facing, 0, NULL, NULL);
}

// Read-only translation into native tool-group/variant bits; City mutations use resolved commands.
int Sc2kCanvasMapPointerTools(int tool, int road_variant) {
  if (tool == CITY_CANVAS_TOOL_ROAD)
    return road_variant >= CITY_ROAD_ROAD && road_variant <= CITY_ROAD_BUS_DEPOT
        ? CITY_TOOL_GROUP_ROAD | (road_variant << 8)
        : -1;
  // Add one so omitted tools stay invalid while group zero remains selectable.
  static const i32 kPointerTools[CITY_CANVAS_TOOL_COUNT] = {
    [CITY_CANVAS_TOOL_RESIDENTIAL] = (CITY_TOOL_GROUP_RESIDENTIAL | CITY_ZONE_LIGHT << 8) + 1,
    [CITY_CANVAS_TOOL_COMMERCIAL] = (CITY_TOOL_GROUP_COMMERCIAL | CITY_ZONE_LIGHT << 8) + 1,
    [CITY_CANVAS_TOOL_INDUSTRIAL] = (CITY_TOOL_GROUP_INDUSTRIAL | CITY_ZONE_LIGHT << 8) + 1,
    [CITY_CANVAS_TOOL_DENSE_RESIDENTIAL] = (CITY_TOOL_GROUP_RESIDENTIAL | CITY_ZONE_DENSE << 8) + 1,
    [CITY_CANVAS_TOOL_DENSE_COMMERCIAL] = (CITY_TOOL_GROUP_COMMERCIAL | CITY_ZONE_DENSE << 8) + 1,
    [CITY_CANVAS_TOOL_DENSE_INDUSTRIAL] = (CITY_TOOL_GROUP_INDUSTRIAL | CITY_ZONE_DENSE << 8) + 1,
    [CITY_CANVAS_TOOL_POLICE] = (CITY_TOOL_GROUP_CIVIC | CITY_CIVIC_POLICE << 8) + 1,
    [CITY_CANVAS_TOOL_FIRE] = (CITY_TOOL_GROUP_CIVIC | CITY_CIVIC_FIRE << 8) + 1,
    [CITY_CANVAS_TOOL_HOSPITAL] = (CITY_TOOL_GROUP_CIVIC | CITY_CIVIC_HOSPITAL << 8) + 1,
    [CITY_CANVAS_TOOL_SCHOOL] = (CITY_TOOL_GROUP_EDUCATION | CITY_EDUCATION_SCHOOL << 8) + 1,
    [CITY_CANVAS_TOOL_COAL] = (CITY_TOOL_GROUP_POWER | CITY_POWER_COAL << 8) + 1,
    [CITY_CANVAS_TOOL_NUCLEAR] = (CITY_TOOL_GROUP_POWER | CITY_POWER_NUCLEAR << 8) + 1,
    [CITY_CANVAS_TOOL_PIPES] = (CITY_TOOL_GROUP_WATER | CITY_WATER_PIPES << 8) + 1,
    [CITY_CANVAS_TOOL_SUBWAY] = (CITY_TOOL_GROUP_RAIL | CITY_RAIL_SUBWAY << 8) + 1,
    [CITY_CANVAS_TOOL_ROAD] = (CITY_TOOL_GROUP_ROAD | CITY_ROAD_ROAD << 8) + 1,
    [CITY_CANVAS_TOOL_RAIL] = (CITY_TOOL_GROUP_RAIL | CITY_RAIL_RAIL << 8) + 1,
    [CITY_CANVAS_TOOL_POWER_LINES] = (CITY_TOOL_GROUP_POWER | CITY_POWER_LINES << 8) + 1,
    [CITY_CANVAS_TOOL_DEMOLISH] = (CITY_TOOL_GROUP_DEMOLISH | CITY_DEMOLISH_CLEAR << 8) + 1,
    [CITY_CANVAS_TOOL_RAISE_TERRAIN] = (CITY_DEMOLISH_RAISE << 8 | CITY_DEMOLISH_RAISE << 16) + 1,
    [CITY_CANVAS_TOOL_LOWER_TERRAIN] = (CITY_DEMOLISH_LOWER << 8 | CITY_DEMOLISH_LOWER << 16) + 1,
    [CITY_CANVAS_TOOL_SUBWAY_STATION] = (CITY_TOOL_GROUP_RAIL | CITY_RAIL_SUBWAY_STATION << 8) + 1,
    [CITY_CANVAS_TOOL_BUS_DEPOT] = (CITY_TOOL_GROUP_ROAD | CITY_ROAD_BUS_DEPOT << 8) + 1,
    [CITY_CANVAS_TOOL_RAIL_DEPOT] = (CITY_TOOL_GROUP_RAIL | CITY_RAIL_DEPOT << 8) + 1,
    [CITY_CANVAS_TOOL_WIND] = (CITY_TOOL_GROUP_POWER | CITY_POWER_WIND << 8) + 1,
    [CITY_CANVAS_TOOL_SOLAR] = (CITY_TOOL_GROUP_POWER | CITY_POWER_SOLAR << 8) + 1,
    [CITY_CANVAS_TOOL_MICROWAVE] = (CITY_TOOL_GROUP_POWER | CITY_POWER_MICROWAVE << 8) + 1,
    [CITY_CANVAS_TOOL_FUSION] = (CITY_TOOL_GROUP_POWER | CITY_POWER_FUSION << 8) + 1,
    [CITY_CANVAS_TOOL_WATER_TOWER] = (CITY_TOOL_GROUP_WATER | CITY_WATER_TOWER << 8) + 1,
    [CITY_CANVAS_TOOL_DESALINATION] = (CITY_TOOL_GROUP_WATER | CITY_WATER_DESALINATION << 8) + 1,
    [CITY_CANVAS_TOOL_LIBRARY] = (CITY_TOOL_GROUP_EDUCATION | CITY_EDUCATION_LIBRARY << 8) + 1,
    [CITY_CANVAS_TOOL_MUSEUM] = (CITY_TOOL_GROUP_EDUCATION | CITY_EDUCATION_MUSEUM << 8) + 1,
    [CITY_CANVAS_TOOL_BIG_PARK] = (CITY_TOOL_GROUP_RECREATION | CITY_RECREATION_BIG_PARK << 8) + 1,
    [CITY_CANVAS_TOOL_ZOO] = (CITY_TOOL_GROUP_RECREATION | CITY_RECREATION_ZOO << 8) + 1,
    [CITY_CANVAS_TOOL_PLYMOUTH_ARCOLOGY] = (CITY_TOOL_GROUP_REWARDS | CITY_REWARD_PLYMOUTH_ARCOLOGY << 8) + 1,
    [CITY_CANVAS_TOOL_FOREST_ARCOLOGY] = (CITY_TOOL_GROUP_REWARDS | CITY_REWARD_FOREST_ARCOLOGY << 8) + 1,
    [CITY_CANVAS_TOOL_DARCO_ARCOLOGY] = (CITY_TOOL_GROUP_REWARDS | CITY_REWARD_DARCO_ARCOLOGY << 8) + 1,
    [CITY_CANVAS_TOOL_LAUNCH_ARCOLOGY] = (CITY_TOOL_GROUP_REWARDS | CITY_REWARD_LAUNCH_ARCOLOGY << 8) + 1,
    [CITY_CANVAS_TOOL_OIL] = (CITY_TOOL_GROUP_POWER | CITY_POWER_OIL << 8) + 1,
    [CITY_CANVAS_TOOL_GAS] = (CITY_TOOL_GROUP_POWER | CITY_POWER_GAS << 8) + 1,
    [CITY_CANVAS_TOOL_WATER_TREATMENT] = (CITY_TOOL_GROUP_WATER | CITY_WATER_TREATMENT << 8) + 1,
    [CITY_CANVAS_TOOL_MAYORS_HOUSE] = (CITY_TOOL_GROUP_REWARDS | CITY_REWARD_MAYORS_HOUSE << 8) + 1,
    [CITY_CANVAS_TOOL_CITY_HALL] = (CITY_TOOL_GROUP_REWARDS | CITY_REWARD_CITY_HALL << 8) + 1,
    [CITY_CANVAS_TOOL_STATUE] = (CITY_TOOL_GROUP_REWARDS | CITY_REWARD_STATUE << 8) + 1,
    [CITY_CANVAS_TOOL_LLAMA_DOME] = (CITY_TOOL_GROUP_REWARDS | CITY_REWARD_LLAMA_DOME << 8) + 1,
    [CITY_CANVAS_TOOL_COLLEGE] = (CITY_TOOL_GROUP_EDUCATION | CITY_EDUCATION_COLLEGE << 8) + 1,
    [CITY_CANVAS_TOOL_PRISON] = (CITY_TOOL_GROUP_CIVIC | CITY_CIVIC_PRISON << 8) + 1,
    [CITY_CANVAS_TOOL_SMALL_PARK] = (CITY_TOOL_GROUP_RECREATION | CITY_RECREATION_SMALL_PARK << 8) + 1,
    [CITY_CANVAS_TOOL_STADIUM] = (CITY_TOOL_GROUP_RECREATION | CITY_RECREATION_STADIUM << 8) + 1,
    [CITY_CANVAS_TOOL_WATER_PUMP] = (CITY_TOOL_GROUP_WATER | CITY_WATER_PUMP << 8) + 1,
    [CITY_CANVAS_TOOL_TREES] = (CITY_TOOL_GROUP_TREE_WATER | CITY_TREE_WATER_TREES << 8) + 1,
    [CITY_CANVAS_TOOL_WATER] = (CITY_TOOL_GROUP_TREE_WATER | CITY_TREE_WATER_WATER << 8) + 1,
    [CITY_CANVAS_TOOL_HYDRO] = (CITY_TOOL_GROUP_POWER | CITY_POWER_HYDRO << 8) + 1,
    [CITY_CANVAS_TOOL_DISPATCH_POLICE] = (CITY_TOOL_GROUP_DISPATCH | CITY_DISPATCH_POLICE << 8) + 1,
    [CITY_CANVAS_TOOL_DISPATCH_FIRE] = (CITY_TOOL_GROUP_DISPATCH | CITY_DISPATCH_FIRE << 8) + 1,
    [CITY_CANVAS_TOOL_DISPATCH_MILITARY] = (CITY_TOOL_GROUP_DISPATCH | CITY_DISPATCH_MILITARY << 8) + 1,
    [CITY_CANVAS_TOOL_SIGN] = CITY_TOOL_GROUP_SIGN + 1,
    [CITY_CANVAS_TOOL_QUERY] = CITY_TOOL_GROUP_QUERY + 1,
    [CITY_CANVAS_TOOL_MARINA] = (CITY_TOOL_GROUP_RECREATION | CITY_RECREATION_MARINA << 8) + 1,
    [CITY_CANVAS_TOOL_HIGHWAY] = (CITY_TOOL_GROUP_ROAD | CITY_ROAD_HIGHWAY << 8) + 1,
    [CITY_CANVAS_TOOL_TUNNEL] = (CITY_TOOL_GROUP_ROAD | CITY_ROAD_TUNNEL << 8) + 1,
    [CITY_CANVAS_TOOL_ONRAMP] = (CITY_TOOL_GROUP_ROAD | CITY_ROAD_ONRAMP << 8) + 1,
    [CITY_CANVAS_TOOL_CENTER] = CITY_TOOL_GROUP_CENTER + 1,
    [CITY_CANVAS_TOOL_LEVEL_TERRAIN] = (CITY_DEMOLISH_LEVEL << 8 | CITY_DEMOLISH_LEVEL << 16) + 1,
    [CITY_CANVAS_TOOL_DEZONE] = (CITY_DEMOLISH_DEZONE << 8 | CITY_DEMOLISH_DEZONE << 16) + 1,
    [CITY_CANVAS_TOOL_SEAPORT] = (CITY_TOOL_GROUP_PORT | CITY_PORT_SEAPORT << 8) + 1,
    [CITY_CANVAS_TOOL_AIRPORT] = (CITY_TOOL_GROUP_PORT | CITY_PORT_AIRPORT << 8) + 1,
    [CITY_CANVAS_TOOL_SUBWAY_RAIL_CONNECTION] = (CITY_TOOL_GROUP_RAIL | CITY_RAIL_SUBWAY_RAIL_CONNECTION << 8) + 1,
    [CITY_CANVAS_TOOL_ROAD_BRIDGE] = (CITY_TOOL_GROUP_ROAD | CITY_ROAD_BRIDGE << 8) + 1,
    [CITY_CANVAS_TOOL_RAIL_BRIDGE] = (CITY_TOOL_GROUP_RAIL | CITY_RAIL_BRIDGE << 8) + 1,
    [CITY_CANVAS_TOOL_STRETCH_TERRAIN] = (CITY_DEMOLISH_RAISE << 8 | CITY_DEMOLISH_RAISE << 16) + 1,
    [CITY_CANVAS_TOOL_STREAM] = (CITY_TOOL_GROUP_TREE_WATER | CITY_TREE_WATER_WATER << 8) + 1,
  };
  return tool >= 0 && tool < CITY_CANVAS_TOOL_COUNT ? kPointerTools[tool] - 1 : -1;
}

// Resolve only building/facility tools. Line, dispatch and terrain tools have separate cursor/toolbar artwork.
TileKind CityToolBuildingKind(int tool) {
  if (tool == CITY_CANVAS_TOOL_HYDRO) return TK_HYDRO_POWER;
  int selection = Sc2kCanvasMapPointerTools(tool, 0);
  if (selection < 0) return TK_CLEAR;
  return GetCityFacilityTile(selection & 255, (selection >> 8) & 255);
}

u32 Sc2kCurrentCityMapPointerTools(CityInterfaceState *ui) {
  int group = (ui->tool_group);
  u32 variant = group >= CITY_TOOL_GROUP_DEMOLISH && group < CITY_TOOL_GROUP_BUILD_COUNT
      ? ui->selection.value[group]
      : CITY_TOOL_VARIANT_DEFAULT;
  return (u16)group | (variant << 8) | ((u32)(ui->selection.value[0]) << 16);
}

CityZoneDragInput Sc2kZoneDragEndpointInput(Game *game, TileIndex tile) {
  City *city = game->city;
  Drawing *drawing = game->drawing;
  CityMapViewConfig *view = drawing->default_map_view;
  int x = MapTileX(city, tile), y = MapTileY(city, tile);
  CityPoint screen = CityMapTileToScreen(city, view, (CityPoint){x, y});
  u32 zoom = view->zoom;
  int scale = view->magnified_large_zoom != 0 ? 2 : 1;
  screen.x += (4 << zoom) * scale;
  screen.y += (2 << zoom) * scale;
  // Projection anchors can pick a neighboring tile. Confirm the endpoint before releasing the drag.
  static const i8 kEndpointXOffsets[] = {0, -2, 2, -4, 4, -8, 8};
  for (int dy = 0; dy >= -32 * scale; dy -= 2 * scale) {
    for (int index = 0; index < sizeof(kEndpointXOffsets) / sizeof(kEndpointXOffsets[0]); ++index) {
      int point_x = screen.x + kEndpointXOffsets[index] * scale;
      int point_y = screen.y + dy;
      if (PickCityMapTile(city, view, point_x, point_y) == MapTileXY(city, x, y))
        return (CityZoneDragInput){{point_x, point_y}, CITY_DRAG_INPUT_RELEASE};
    }
  }
  // Cancel instead of placing at a different endpoint when no nearby point picks this tile.
  return (CityZoneDragInput){{screen.x, screen.y}, CITY_DRAG_INPUT_SHIFT_CANCEL};
}

bool SelectCityToolWithModifiers(Game *game, int group, int variant, u32 modifiers) {
  ClearGameError(game);
  CityInterfaceState *ui = game->ui;
  SetCityAltKeyPressed(ui, (modifiers & CITY_POINTER_MODIFIER_ALT) != 0);
  SetCityShiftKeyPressed(ui, (modifiers & CITY_POINTER_MODIFIER_SHIFT) != 0);
  SetCityCtrlKeyPressed(ui, (modifiers & CITY_POINTER_MODIFIER_CONTROL) != 0);
  SelectCityToolGroupAndMode(game, group, variant);
  if (game->city->settings.timer_mode == CITY_TIMER_STOPPED) {
    ui->terrain_tool = group == CITY_TOOL_GROUP_TREE_WATER ?
        (variant == CITY_TREE_WATER_WATER ? CITY_TERRAIN_TOOL_WATER : CITY_TERRAIN_TOOL_TREE) :
        group == CITY_TOOL_GROUP_DEMOLISH ?
        (variant == CITY_DEMOLISH_RAISE ? CITY_TERRAIN_TOOL_RAISE :
         variant == CITY_DEMOLISH_LOWER ? CITY_TERRAIN_TOOL_LOWER :
         variant == CITY_DEMOLISH_LEVEL ? CITY_TERRAIN_TOOL_LEVEL : CITY_TERRAIN_TOOL_NONE) :
        CITY_TERRAIN_TOOL_CENTER;
  }
  return !GetGameError(game);
}

bool RunCityPointerGesture(Game *game, const CityPointerInput *input, u32 count, int options, int tools) {
  if (!count) return false;
  CityPoint first = input[0].point, last = input[count - 1].point;
  TileIndex start = PickCityMapTile(game->city, game->drawing->default_map_view, first.x, first.y);
  TileIndex end = PickCityMapTile(game->city, game->drawing->default_map_view, last.x, last.y);
  CityPlacementIntent intent = ResolveCityPlacementIntent(game, tools, start, end, options);
  ClearGameError(game);
  RunCityMapToolAction(game, &intent);
  return !GetGameError(game);
}
