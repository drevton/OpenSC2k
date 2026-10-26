#include "ui/tools/tools.h"

#include "city/engine/city_types.h"
#include "city/engine/commands.h"
#include "city/events/types.h"
#include "city/infrastructure/bridges.h"
#include "city/infrastructure/lines.h"
#include "city/maps/terrain.h"
#include "city/maps/trees.h"
#include "city/presentation.h"
#include "city/state.h"
#include "city/tools/tools.h"
#include "city/tools/types.h"
#include "drawing/maps/cost_labels.h"
#include "drawing/maps/map_camera.h"
#include "drawing/maps/map_view.h"
#include "drawing/state.h"
#include "game/commands.h"
#include "game/errors.h"
#include "game/state.h"
#include "ui/commands.h"
#include "ui/dialogs/information.h"
#include "ui/dialogs/name_editor.h"
#include "ui/input.h"
#include "ui/map.h"
#include "ui/state.h"
#include "ui/tools/cost_labels.h"
#include "ui/tools/placement.h"

// Pointer gestures belong to UI. City operations receive world locations/options;
// this boundary alone selects the main camera for the retained input adapters.

// The enclosing tracker controls cursor consumption through release or cancellation.
CityPoint ReadCityCommandPointer(const CityPointerCommandState *pointer, u32 index) {
  return pointer->samples != NULL ? pointer->samples[index].point : pointer->points[index];
}

void ApplyCityCommandPointerModifiers(CityInterfaceState *ui, const CityPointerCommandState *pointer, u32 index) {
  if (pointer->samples == NULL)
    return;
  u32 flags = pointer->samples[index].flags;
  SetCityAltKeyPressed(ui, (flags & CITY_POINTER_MODIFIER_ALT) != 0);
  SetCityShiftKeyPressed(ui, (flags & CITY_POINTER_MODIFIER_SHIFT) != 0);
  SetCityCtrlKeyPressed(ui, (flags & CITY_POINTER_MODIFIER_CONTROL) != 0);
}

// Supplied drag records advance on every read; pointer samples stay at release/cancellation for their caller.
static CityZoneDragInput Sc2kNextZoneDragInput(Game *game) {
  if (game->zone_drag_input != NULL)
    return game->zone_drag_input[game->zone_drag_index++];
  CityPointerCommandState *pointer = &game->pointer_command;
  CityPoint point = ReadCityCommandPointer(pointer, pointer->index);
  ApplyCityCommandPointerModifiers(game->ui, pointer, pointer->index);
  u32 flags;
  if (pointer->samples != NULL) {
    u32 sample = pointer->samples[pointer->index].flags;
    flags = (sample & CITY_POINTER_MODIFIER_SHIFT) != 0 ? CITY_DRAG_INPUT_SHIFT_CANCEL
                                                      : sample & CITY_POINTER_SAMPLE_RELEASE;
  } else {
    flags = pointer->index + 1 == pointer->count ? pointer->release_flags : 0;
  }
  if (flags == 0)
    ++pointer->index;
  return (CityZoneDragInput){point, flags};
}

static CityZoneDragInput Sc2kNextCityMapZoneInput(Game *game) {
  Drawing *drawing = game->drawing;
  CityMapViewConfig *view = drawing->default_map_view;
  CityZoneDragInput input = Sc2kNextZoneDragInput(game);
  view->pointer_x = input.point.x;
  view->pointer_y = input.point.y;
  return input;
}

static void PresentCityPlacementDragCost(City *city, CityMapViewConfig *view, int unit_cost,
    int screen_x, int screen_y) {
  city->construction.cost = 25 * city->construction.terrain_surcharge_count +
      (u32)unit_cost * city->construction.cell_count;
  DrawCityCanvasZoneCostLabel(city, NULL, view, (int)city->construction.cost, NULL, screen_x, screen_y);
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
    UpdateCityHighwayDragPreview(city, view, start, start);
  } else if (!UpdateCityLinePreview(city, start, start)) {
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
    input = transport ? Sc2kNextCityMapZoneInput(game) : Sc2kNextZoneDragInput(game);
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
      else if (transport) UpdateCityHighwayDragPreview(city, view, start, end);
      else UpdateCityLinePreview(city, start, end);
      end = tile;
      if (transport) UpdateCityHighwayDragPreview(city, view, start, end);
      else if (!zone) UpdateCityLinePreview(city, start, end);
      PresentCityPlacementDragCost(city, view, unit_cost, screen.x, screen.y);
    }
  } while (!(input.flags & CITY_DRAG_INPUT_RELEASE));
  if (zone) UpdateCityZoneRectanglePreview(city, start, end, start);
  if (transport) UpdateCityHighwayDragPreview(city, view, start, end);
  else UpdateCityLinePreview(city, start, zone ? start : end);
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

CityPlacementIntent ResolveCityPlacementIntent(const Game *game, int tools, TileIndex start, TileIndex end,
    u32 options) {
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
  if (options & CITY_POINTER_MODIFIER_CONTROL) {
    group = CITY_TOOL_GROUP_DEMOLISH;
    variant = CITY_DEMOLISH_CLEAR;
  }
  int span = (options & CITY_POINTER_EXPLICIT_ANSWERS) ?
      ((options & CITY_POINTER_SPAN_CHOICE_MASK) >> CITY_POINTER_SPAN_CHOICE_SHIFT) - 1 : CITY_SPAN_CANCEL;
  bool border = options & CITY_POINTER_ACCEPT_BORDER;
  switch (group) {
  case CITY_TOOL_GROUP_DEMOLISH: {
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
  case CITY_TOOL_GROUP_TREE_WATER: {
    bool trees = variant == CITY_TREE_WATER_TREES;
    return RunCityCommand(game, trees ? CITY_COMMAND_PLANT_TREES_RECT : CITY_COMMAND_BUILD_CANALS_RECT,
        start, end, trees ? intent->tree_options : 0, 0, NULL, NULL);
  }
  case CITY_TOOL_GROUP_DISPATCH: {
    static const u8 kServices[] = {CITY_MAP_EVENT_POLICE, CITY_MAP_EVENT_FIRE, CITY_MAP_EVENT_MILITARY};
    return variant >= 0 && variant < 3 && RunCityCommand(game, CITY_COMMAND_DISPATCH_CITY_SERVICE,
        end, kServices[variant], 0, 0, NULL, NULL);
  }
  case CITY_TOOL_GROUP_PORT:
  case CITY_TOOL_GROUP_RESIDENTIAL:
  case CITY_TOOL_GROUP_COMMERCIAL:
  case CITY_TOOL_GROUP_INDUSTRIAL: {
    int zone = CityToolZoneKind(group, variant);
    return RunCityCommand(game, CITY_COMMAND_PLACE_ZONE_RECT, start, zone, end, 0, NULL, NULL);
  }
  case CITY_TOOL_GROUP_SIGN:
    PresentCityLabelEditor(city, end, false);
    return true;
  case CITY_TOOL_GROUP_ROAD:
    if (variant == CITY_ROAD_ROAD) {
      int style = EncodeCityRoadSpanChoice(span);
      return RunCityCommand(game, CITY_COMMAND_PLACE_ROAD, start, end,
          style << 8 | (border ? CITY_LINE_ACCEPT_BORDER : 0), 0, NULL, NULL);
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
    break;
  case CITY_TOOL_GROUP_RAIL:
    if (variant == CITY_RAIL_RAIL)
      return RunCityCommand(game, CITY_COMMAND_PLACE_RAIL, start, end,
          (border ? CITY_LINE_ACCEPT_BORDER : 0) | (span == CITY_SPAN_CANCEL ? CITY_LINE_CANCEL_SPAN : 0),
          0, NULL, NULL);
    if (variant == CITY_RAIL_SUBWAY)
      return RunCityCommand(game, CITY_COMMAND_PLACE_SUBWAY, start, end, 0, 0, NULL, NULL);
    if (variant == CITY_RAIL_SUBWAY_RAIL_CONNECTION)
      return RunCityCommand(game, CITY_COMMAND_PLACE_SUBWAY_RAIL_CONNECTION, start, 0, 0, 0, NULL, NULL);
    break;
  case CITY_TOOL_GROUP_POWER:
    if (variant == CITY_POWER_HYDRO)
      return RunCityCommand(game, CITY_COMMAND_PLACE_BUILDING, end, TK_HYDRO_POWER, -1, 0, NULL, NULL);
    if (variant == CITY_POWER_LINES)
      return RunCityCommand(game, CITY_COMMAND_PLACE_POWER_LINE, start, end,
          span == CITY_SPAN_CANCEL ? CITY_LINE_CANCEL_SPAN : 0, 0, NULL, NULL);
    break;
  case CITY_TOOL_GROUP_WATER:
    if (variant == CITY_WATER_PIPES)
      return RunCityCommand(game, CITY_COMMAND_PLACE_WATER_PIPE, start, end, variant, 0, NULL, NULL);
    break;
  default:
    break;
  }
  // Facility variants share placement after their group's line tools have been handled.
  int building = CityToolFacilityKind(group, variant);
  int facing = IsTileKindBuilding(building) ? intent->facility_facing : -1;
  return RunCityCommand(game, CITY_COMMAND_PLACE_BUILDING, end, building, facing, 0, NULL, NULL);
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
    for (size_t index = 0; index < sizeof(kEndpointXOffsets) / sizeof(kEndpointXOffsets[0]); ++index) {
      int point_x = screen.x + kEndpointXOffsets[index] * scale;
      int point_y = screen.y + dy;
      if (PickCityMapTile(city, view, point_x, point_y) == tile)
        return (CityZoneDragInput){{point_x, point_y}, CITY_DRAG_INPUT_RELEASE};
    }
  }
  // Cancel instead of placing at a different endpoint when no nearby point picks this tile.
  return (CityZoneDragInput){{screen.x, screen.y}, CITY_DRAG_INPUT_SHIFT_CANCEL};
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
