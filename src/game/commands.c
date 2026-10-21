#include "city/maps/trees.h"
#include "audio/types.h"
#include "city/engine/city_types.h"
#include "city/engine/command_types.h"
#include "city/maps/tile_storage.h"
#include "city/presentation.h"
#include "game/application.h"
#include "base/time.h"
#include "audio/audio.h"
#include "ui/text/text.h"
#include "ui/tools/tools.h"
#include "ui/frontend.h"
#include "game/application_internal.h"
#include "city/engine/initialization.h"
#include "city/engine/simulation.h"
#include "city/saveload/file.h"
#include "game/errors.h"
#include "game/observation.h"
#include "game/state.h"
#include "audio/state.h"
#include "city/state.h"
#include "drawing/state.h"
#include "assets/state.h"
#include "ui/state.h"
#include "drawing/maps/placement.h"
#include "ui/commands.h"
#include "ui/dialogs/dialogs.h"

// Game owns the command-error marker; damage its old and new Drawing footprints before changing it.
void SetCityErrorTile(Game *game, TileIndex tile) {
  Drawing *drawing = game->drawing;
  if (!IsMapTileValid(game->city, tile)) tile = INVALID_TILE;
  if (game->error_tile == tile) return;
  TileIndex changed[] = {game->error_tile, tile};
  for (int i = 0; i < 2; ++i) {
    if (!IsMapTileValid(game->city, changed[i]) || !drawing->default_map_view) continue;
    CityRect b = CityMapPlacementBounds(game->city, changed[i], false);
    int scale = drawing->map_pixel_scale;
    MarkScreenDirty(&drawing->screen_damage,
        (CityRect){b.left * scale + drawing->scroll_pixels.x, b.top * scale + drawing->scroll_pixels.y,
            b.right * scale + drawing->scroll_pixels.x, b.bottom * scale + drawing->scroll_pixels.y});
  }
  game->error_tile = tile;
}

static bool IsCityConstructionCommand(u32 command) {
  switch (command) {
    case CITY_COMMAND_PLACE_BUILDING:
    case CITY_COMMAND_PLACE_ZONE_RECT:
    case CITY_COMMAND_PLACE_WATER_PIPE:
    case CITY_COMMAND_PLACE_SUBWAY:
    case CITY_COMMAND_PLACE_ROAD:
    case CITY_COMMAND_PLACE_RAIL:
    case CITY_COMMAND_PLACE_POWER_LINE:
    case CITY_COMMAND_PLANT_TREES_RECT:
    case CITY_COMMAND_BUILD_CANALS_RECT:
    case CITY_COMMAND_BUILD_BRIDGE:
    case CITY_COMMAND_BULLDOZE_RECT:
    case CITY_COMMAND_BULLDOZE:
    case CITY_COMMAND_APPLY_BULLDOZER_TOOL:
    case CITY_COMMAND_ADJUST_TERRAIN_HEIGHT:
    case CITY_COMMAND_PLACE_ROAD_TERRAIN_SPAN:
    case CITY_COMMAND_PLACE_TRANSPORT_LINE:
    case CITY_COMMAND_PLACE_HIGHWAY_ONRAMP:
    case CITY_COMMAND_PLACE_SUBWAY_RAIL_CONNECTION:
    case CITY_COMMAND_CARVE_WATER_CHANNEL:
    case CITY_COMMAND_APPLY_WATER_TILE:
    case CITY_COMMAND_LOWER_TERRAIN_TILE:
    case CITY_COMMAND_RAISE_TERRAIN_TILE:
    case CITY_COMMAND_STEP_TERRAIN_HEIGHT:
    case CITY_COMMAND_LEVEL_TERRAIN_TILE:
    case CITY_COMMAND_STRETCH_TERRAIN_TILE:
    case CITY_COMMAND_PLACE_STREAM:
      return true;
    default:
      return false;
  }
}

// UI action context is separate from simulation flags and legacy command admission results.
const char *CityCommandFailureSummary(const CityCommand *command) {
  switch (command->id) {
  case CITY_COMMAND_PLANT_TREES_RECT:
    return command->parameter3 & CITY_TREE_REMOVE ? "Can't remove trees here..." : "Can't plant trees here...";
  case CITY_COMMAND_BULLDOZE_RECT: case CITY_COMMAND_BULLDOZE: case CITY_COMMAND_APPLY_BULLDOZER_TOOL:
    return "Can't demolish here...";
  case CITY_COMMAND_BUILD_CANALS_RECT:
    return "Can't build canal here...";
  case CITY_COMMAND_BUILD_BRIDGE:
    return "Can't build bridge here...";
  case CITY_COMMAND_PLACE_BUILDING: return "Can't build here...";
  case CITY_COMMAND_RAISE_TERRAIN_TILE: return "Can't raise land here...";
  case CITY_COMMAND_LOWER_TERRAIN_TILE: return "Can't lower land here...";
  case CITY_COMMAND_ADJUST_TERRAIN_HEIGHT:
  case CITY_COMMAND_STEP_TERRAIN_HEIGHT: return "Can't alter terrain here...";
  case CITY_COMMAND_LEVEL_TERRAIN_TILE: return "Can't level terrain here...";
  case CITY_COMMAND_STRETCH_TERRAIN_TILE: return "Can't stretch terrain here...";
  case CITY_COMMAND_PLACE_STREAM: return "Can't place a stream here...";
  case CITY_COMMAND_PLACE_ROAD_TERRAIN_SPAN:
    return "Can't build tunnel here...";
  case CITY_COMMAND_PLACE_ROAD: case CITY_COMMAND_PLACE_HIGHWAY_ONRAMP:
  case CITY_COMMAND_PLACE_TRANSPORT_LINE:
    return "Can't build road here...";
  case CITY_COMMAND_PLACE_RAIL: case CITY_COMMAND_PLACE_SUBWAY: case CITY_COMMAND_PLACE_SUBWAY_RAIL_CONNECTION:
    return "Can't build railway here...";
  case CITY_COMMAND_PLACE_WATER_PIPE: return "Can't build water infrastructure here...";
  case CITY_COMMAND_PLACE_POWER_LINE: return "Can't build power infrastructure here...";
  case CITY_COMMAND_PLACE_ZONE_RECT: return "Can't zone land here...";
  case CITY_COMMAND_DISPATCH_CITY_SERVICE:
    return "Can't dispatch service here...";
  case CITY_COMMAND_EDIT_MAP_LABEL: case CITY_COMMAND_EDIT_FACILITY_LABEL:
  case CITY_COMMAND_RENAME_SIGN: case CITY_COMMAND_MOVE_SIGN:
    return "Can't change sign here...";
  case CITY_COMMAND_ASSIGN_STADIUM_TEAM: return "Can't select stadium team...";
  case CITY_COMMAND_ISSUE_BOND: return "Can't issue bond...";
  case CITY_COMMAND_REDEEM_BOND: return "Can't repay bond...";
  case CITY_COMMAND_TOGGLE_CITY_POLICY: case CITY_COMMAND_BUDGET_RATE: case CITY_COMMAND_SET_BUDGET_RATE:
  case CITY_COMMAND_SET_INDUSTRY_TAX_BASELINE:
    return "Can't change city policy...";
  case CITY_COMMAND_CHEAT: return "Can't apply cheat...";
  default:
    return IsCityConstructionCommand(command->id) ? "Can't build here..." : "Can't complete action...";
  }
}

bool IsCityCommandFailureResource(u32 resource) {
  return resource == CITY_NOTICE_PLACEMENT_AT_MAP_EDGE || resource == CITY_NOTICE_PLACEMENT_PROTEST ||
      resource == CITY_NOTICE_MARINA_REQUIRES_SHORELINE || resource == CITY_NOTICE_HYDRO_REQUIRES_WATERFALL ||
      (resource >= CITY_NOTICE_TUNNEL_CANNOT_CROSS_TUNNELS && resource <= CITY_NOTICE_TUNNEL_REQUIRES_HILLSIDE) ||
      (resource >= CITY_NOTICE_NO_POLICE_TO_DISPATCH && resource <= CITY_NOTICE_CANNOT_DISPATCH_MILITARY_HERE) ||
      (resource >= CITY_NOTICE_SHORELINE_NEEDS_CLEARING && resource <= CITY_NOTICE_SPAN_BLOCKED_BY_CROSSING) ||
      resource == CITY_NOTICE_CANNOT_PLACE_LABEL_HERE || resource == CITY_NOTICE_SIGN_LIMIT_REACHED ||
      resource == CITY_NOTICE_SELECT_VALID_STADIUM_SPORT || resource == CITY_NOTICE_BOND_LIMIT_REACHED ||
      resource == CITY_NOTICE_BOND_REPAYMENT_REQUIRES_FUNDS || resource == CITY_NOTICE_INSUFFICIENT_OPERATING_FUNDS;
}

const char *CityCommandFailureReason(i64 error) {
  switch (error) {
  case CITY_COMMAND_COST_INVALID: return "... this action is not available here.";
  case CITY_COMMAND_COST_BLOCKED: return "... the site is unsuitable or obstructed.";
  case CITY_COMMAND_COST_TOO_EXPENSIVE: return "... not enough money.";
  case CITY_COMMAND_COST_REJECTED: return "... the action was refused.";
  case CITY_COMMAND_COST_WRONG_TREE_CLIMATE: return "... wrong terrain for tree type.";
  case CITY_COMMAND_COST_TREE_LIMIT: return "... there is no room for another tree on this tile.";
  case CITY_COMMAND_COST_NOTHING_TO_REMOVE: return "... there is nothing to remove here.";
  case CITY_COMMAND_COST_ALREADY_BUILT: return "... this building already exists in the city.";
  case CITY_COMMAND_COST_REMOVE_RAIL: return "... must remove railway track first.";
  case CITY_COMMAND_COST_REMOVE_ROAD: return "... must remove road first.";
  case CITY_COMMAND_COST_REMOVE_POWER_LINE: return "... must remove power line first.";
  case CITY_COMMAND_COST_REMOVE_BUILDING: return "... must remove building first.";
  case CITY_COMMAND_COST_REMOVE_UNDERGROUND: return "... must remove underground construction first.";
  case CITY_COMMAND_COST_ALREADY_AT_SEA_LEVEL: return "... already at sea level.";
  case CITY_COMMAND_COST_BRIDGE_SAME_SPOT: return "... a bridge can't start and end in the same spot.";
  case CITY_COMMAND_COST_CLEAR_DEEP_WATER: return "... water too deep.";
  default: return error < CITY_COMMAND_COST_CLEAR_DEEP_WATER && error >= -65535 ? CityNoticeText((int)-error) :
      "... the action could not be completed.";
  }
}

// Stack-owned completion survives nested UI work without reading its later errors.
typedef struct {
  CityCommandResult result;
  bool written;
} CityRunCompletion;

// Only callback-bearing suspended actions allocate this small caller record.
// It owns explicit completion data, never an execution stack or a City copy.
typedef struct CityPendingCommand {
  struct CityPendingCommand *next;
  int token;
  u32 id;
  CityCommandArguments arguments;
  CityCommandCallback *callback;
  void *context;
  i64 cash_before;
  CityCommandResult result;
} CityPendingCommand;

typedef struct CityTimedCommand {
  const GameObservation *observer;
  void *scope;
  u32 before_tick;
  CityCommand packet;
  CityCommandFeedback feedback;
} CityTimedCommand;

void ReleaseCityPostedCommands(Game *game) {
  if (game->timed_command) {
    CityTimedCommand *work = game->timed_command;
    if (work->observer->command_executed) work->observer->command_executed(work->scope);
    if (work->observer->end_command)
      work->observer->end_command(game, work->scope, 0, GetGameError(game), work->before_tick);
    free(work);
    game->timed_command = NULL;
  }
  while (game->pending_commands) {
    CityPendingCommand *pending = game->pending_commands;
    game->pending_commands = pending->next;
    free(pending);
  }
}

static void CompleteCityCommandCallback(Game *game, int token, CityCommandResult result) {
  CityPendingCommand **link = &game->pending_commands;
  while (*link && (*link)->token != token) link = &(*link)->next;
  if (!*link) return;
  CityPendingCommand *pending = *link;
  if (result.value >= 2 && FindCityTickContinuation(game->city, (int)result.value)) {
    pending->token = (int)result.value;
    return;
  }
  *link = pending->next;
  result.cost = pending->cash_before - game->city->indicators.cash;
  if (result.value < 0) result.error = result.value;
  if (!result.error) result.error = pending->result.error;
  if (!result.host_error) result.host_error = pending->result.host_error;
  result.cancelled |= pending->result.cancelled;
  result.succeeded = !result.error && !result.host_error && !result.cancelled;
  CityCommandArguments args = pending->arguments;
  CityCommandCallback *callback = pending->callback;
  u32 id = pending->id;
  void *context = pending->context;
  free(pending); // Consume before the callback can submit another command.
  callback(game, id, &result, args.tile, args.parameter1, args.parameter2, args.parameter3, context);
}

bool StepCityAnimation(Game *game) {
  if (!game->animation_token || !StepGameDelay(game, &game->animation_delay)) return false;
  int token = game->animation_token;
  game->animation_token = 0;
  CityTimedCommand *work = game->timed_command;
  CityCommandFeedback *previous_feedback = game->ui->command_feedback;
  const CityCommand *previous_command = game->ui->active_command;
  if (work) {
    game->ui->command_feedback = &work->feedback;
    game->ui->active_command = &work->packet;
  }
  ++game->command_depth;
  i64 result = ResumeCityTick(game->city, token, true, 0);
  --game->command_depth;
  game->ui->command_feedback = previous_feedback;
  game->ui->active_command = previous_command;
  const CityTickContinuation *pending = result >= 2 ? FindCityTickContinuation(game->city, (int)result) : NULL;
  if (!pending && work) {
    game->timed_command = NULL;
    int error = GetGameError(game);
    if (work->observer->command_executed) work->observer->command_executed(work->scope);
    if (work->observer->end_command)
      work->observer->end_command(game, work->scope, result, error, work->before_tick);
    if (error) game->execution_failed = true;
    PresentCityCommandNotices(game->city, work->feedback.notices);
    free(work);
  }
  DeliverCitySimulationEvents(game, token, result, false);
  return true;
}

// Shared City commands keep admission, feedback, observer hooks and completion in one place.
static i64 ExecuteCommandPayload(Game *game, CityCommand packet, const u8 *text, u32 length,
    const CityCommandArguments *native_arguments, CityRunCompletion *completion) {
  if (game->observation && game->observation->execute_command) {
    CityCommandResult result;
    i64 value = game->observation->execute_command(game, packet, NULL, 0, text, length, NULL,
        native_arguments, &result);
    if (completion) *completion = (CityRunCompletion){result, true};
    return value;
  }
  // A nested question can pump input and reuse the editor's storage.
  u8 text_copy[128];
  if (text && length <= sizeof(text_copy)) {
    memcpy(text_copy, text, length);
    text = text_copy;
  }
  City *city = game->city;
  CityInterfaceState *ui = game->ui;
  CityCommandArguments args = native_arguments ? *native_arguments : DecodeCityCommand(city, packet);
  bool construction = IsCityConstructionCommand(packet.id);
  bool show_cost = !game->command_depth && construction;
  i64 cash_before = city->indicators.cash;
  const char *summary = ui->command_errors_enabled && game->command_depth == ui->command_error_depth ?
      CityCommandFailureSummary(&packet) : NULL;
  CityCommandFeedback feedback = {.blocker = INVALID_TILE, .stadium_tile = INVALID_TILE,
      .inspection_tile = INVALID_TILE, .construction = construction};
  CityCommandFeedback *previous_feedback = ui->command_feedback;
  const CityCommand *previous_command = ui->active_command;
  ui->command_feedback = &feedback;
  ui->active_command = &packet;
  ++game->command_depth;
  ClearGameError(game);
  const GameObservation *observer = game->observation;
  void *scope = NULL;
  u32 before_tick = city->clock.tick;
  CityCommandObservation observation = {.packet = packet, .text = text, .length = length};
  if (observer && observer->prepare_command) observer->prepare_command(game);
  bool admitted = !observer || !observer->begin_command || observer->begin_command(game, &observation, &scope);
  i64 value = !admitted ? 0 : text ? DoCityCommandWithText(city, packet.id, 0, args.tile,
      args.parameter1, args.parameter2, args.parameter3, text, length) :
      DoCityCommand(city, packet.id, 0, args.tile, args.parameter1, args.parameter2, args.parameter3);
  int error = GetGameError(game);
  if (error && value >= 0) value = CITY_COMMAND_COST_REJECTED;
  if (!admitted) game->execution_failed = true;
  if (admitted && observer && observer->command_executed) observer->command_executed(scope);
  if (value >= 0 && !error && packet.id == CITY_COMMAND_EDIT_MAP_LABEL && ui->signs.indexed) RebuildCitySigns(city);
  if (!error && show_cost) ShowCityConstructionCost(city, args.tile, cash_before - city->indicators.cash);
  if (error) game->execution_failed = true;
  i64 failure = value < 0 ? value : 0;
  CityCommandResult result = {.value = value, .cost = cash_before - city->indicators.cash, .error = failure,
      .host_error = error, .cancelled = feedback.cancelled,
      .succeeded = !failure && !error && !feedback.cancelled};
  if (admitted && !error && !feedback.cancelled && !game->execution_failed) {
    int sound = CITY_SOUND_CONSTRUCTION;
    bool present = true;
    switch (packet.id) {
    case CITY_COMMAND_PLACE_POWER_LINE:
      sound = CITY_SOUND_POWER_LINES;
      break;
    case CITY_COMMAND_PLACE_BUILDING:
      switch (args.parameter1) {
      case TK_MAYORS_HOUSE: case TK_CITY_HALL: case TK_STATUE: case TK_BRAUN_LLAMA_DOME:
        sound = CITY_SOUND_CIVIC_PLACEMENT; break;
      case TK_SCHOOL: case TK_COLLEGE: case TK_LIBRARY: case TK_MUSEUM:
        sound = CITY_SOUND_EDUCATION; break;
      case TK_FIRE_STATION: sound = CITY_SOUND_FIRE_DISPATCH; break;
      case TK_PRISON: sound = CITY_SOUND_PRISON; break;
      case TK_POLICE_STATION: case TK_HOSPITAL: sound = CITY_SOUND_POLICE_OR_HOSPITAL; break;
      case TK_PARK: case TK_LARGE_PARK: case TK_STADIUM: case TK_MARINA: case TK_ZOO:
        sound = args.parameter1 == TK_ZOO ? CITY_SOUND_SAILBOAT_OR_ZOO : CITY_SOUND_CIVIC_PLACEMENT;
        if (result.succeeded) PlayCityEventMusicById(game, CITY_MUSIC_TRACK_10);
        break;
      default: break;
      }
      break;
    case CITY_COMMAND_PLACE_ZONE_RECT:
      present = args.parameter1 != ZONE_NONE;
      if (args.parameter1 != ZONE_SEAPORT && args.parameter1 != ZONE_AIRPORT) sound = CITY_SOUND_TREE_OR_ZONE_PLACEMENT;
      break;
    case CITY_COMMAND_PLACE_TRANSPORT_LINE:
      present = !result.succeeded || result.cost > 0;
      break;
    case CITY_COMMAND_PLACE_ROAD:
    case CITY_COMMAND_PLACE_RAIL:
    case CITY_COMMAND_PLACE_SUBWAY:
    case CITY_COMMAND_PLACE_HIGHWAY_ONRAMP:
    case CITY_COMMAND_PLACE_ROAD_TERRAIN_SPAN:
    case CITY_COMMAND_PLACE_SUBWAY_RAIL_CONNECTION:
      break;
    case CITY_COMMAND_PLACE_WATER_PIPE:
      break;
    default:
      present = false;
      break;
    }
    if (present) PlayCitySound(city, args.tile, result.succeeded ? sound : CITY_SOUND_PLACEMENT_REJECTED);
  }
  if (completion) *completion = (CityRunCompletion){result, true};
  if (admitted && observer && observer->end_command) observer->end_command(game, scope, value, error, before_tick);
  --game->command_depth;
  ui->active_command = previous_command;
  ui->command_feedback = previous_feedback;
  if (!game->command_depth && !game->close_requested) ApplicationSyncDialogPause(city);
  if (summary && construction && (!failure || failure == CITY_COMMAND_COST_NOTHING_TO_REMOVE) &&
      !error && !feedback.cancelled)
    CloseCityCommandErrorWindows(&ui->desktop);
  if (summary && failure && failure != CITY_COMMAND_COST_NOTHING_TO_REMOVE && !feedback.cancelled &&
      !error && !game->execution_failed &&
      !game->close_requested && !feedback.notice_shown) {
    // A marker locates a separate obstruction; the clicked tile needs only the error message.
    TileIndex blocker = feedback.blocker == args.tile ? INVALID_TILE : feedback.blocker;
    OpenCityCommandErrorWindow(game, summary, CityCommandFailureReason(failure), false, blocker);
    ++ui->command_error_serial;
  }
  if (!error && !game->execution_failed && !game->close_requested && feedback.stadium_tile != INVALID_TILE)
    PresentCityStadiumTeamDialog(city, feedback.stadium_tile);
  if (!error && !game->execution_failed && !game->close_requested && feedback.inspection_tile != INVALID_TILE)
    PresentCityTileInformationDialog(city, feedback.inspection_tile);
  PresentCityCommandNotices(city, feedback.notices);
  if (packet.id == CITY_COMMAND_ANSWER_MILITARY_OFFER)
    CompleteCityCommandCallback(game, packet.parameter1, result);
  return value;
}

i64 ExecuteRecordedCityCommandWithPayload(Game *game, CityCommand packet, const CityPointerInput *input, u32 count,
    const u8 *text, u32 length) {
  if (game->observation && game->observation->execute_command) {
    if (game->observation->stock_command_id) packet.id = game->observation->stock_command_id(packet.id);
    return game->observation->execute_command(game, packet, input, count, text, length, NULL, NULL, NULL);
  }
  if (input || count) return 0;
  return ExecuteCommandPayload(game, packet, text, length, NULL, NULL);
}

bool ExecuteRecordedCityCommandWithInput(Game *game, CityCommand packet, const CityPointerInput *input, u32 count) {
  return ExecuteRecordedCityCommandWithPayload(game, packet, input, count, NULL, 0) >= 0;
}

i64 ExecuteRecordedCityCommand(Game *game, u32 id, i32 a, i32 b, i32 c) {
  return ExecuteRecordedCityCommandWithPayload(game, (CityCommand){id, a, b, c}, NULL, 0, NULL, 0);
}

static bool RunCommandWithPayload(Game *game, u32 id, TileIndex tile, int parameter1, int parameter2,
    int parameter3, const CityCommandPayload *payload, CityCommandCallback *callback, void *context) {
  CityCommand packet = EncodeCityCommand(game->city, id, tile, parameter1, parameter2, parameter3);
  CityCommandArguments args = {tile, parameter1, parameter2, parameter3};
  CityPendingCommand *pending = callback ? malloc(sizeof(*pending)) : NULL;
  if (callback && !pending) {
    CityCommandResult result = {.host_error = SC2K_ERROR_HOST_ALLOCATION_FAILURE};
    callback(game, id, &result, tile, parameter1, parameter2, parameter3, context);
    return false;
  }
  i64 cash_before = game->city->indicators.cash;
  CityRunCompletion completion = {0};
  CityInterfaceState *ui = game->ui;
  bool previous_errors = ui->command_errors_enabled;
  u32 previous_depth = ui->command_error_depth;
  ui->command_errors_enabled = true;
  ui->command_error_depth = game->command_depth;
  i64 value = ExecuteCommandPayload(game, packet, payload ? payload->text : NULL, payload ? payload->length : 0,
      &args, &completion);
  ui->command_errors_enabled = previous_errors;
  ui->command_error_depth = previous_depth;
  // Setup/menu shortcuts and admission failures return before the execution scope.
  if (!completion.written) {
    i32 host_error = GetGameError(game);
    completion.result = (CityCommandResult){.value = value, .host_error = host_error,
        .error = value < 0 ? value : 0, .succeeded = value >= 0 && !host_error};
  }
  CityCommandResult result = completion.result;
  if (pending && value >= 2 && FindCityTickContinuation(game->city, (int)value)) {
    *pending = (CityPendingCommand){.next = game->pending_commands, .token = (int)value,
        .id = id, .arguments = args, .callback = callback, .context = context,
        .cash_before = cash_before, .result = result};
    game->pending_commands = pending;
    return true;
  }
  free(pending);
  if (callback) callback(game, id, &result, tile, parameter1, parameter2, parameter3, context);
  return result.succeeded;
}

bool RunCityCommandWithPayload(Game *game, CityCommandId id, TileIndex tile, int a, int b,
    int c, const CityCommandPayload *payload, CityCommandCallback *callback, void *context) {
  if (!IsCityCommandId(id)) return false;
  return RunCommandWithPayload(game, id, tile, a, b, c, payload, callback, context);
}

bool RunCityCommand(Game *game, CityCommandId id, TileIndex tile, int a, int b,
    int c, CityCommandCallback *callback, void *context) {
  return RunCityCommandWithPayload(game, id, tile, a, b, c, NULL, callback, context);
}

void ApplicationSyncDialogPause(City *city) {
  CityInterfaceState *ui = city->game->ui;
  if (!ui->dialog_pause_sync_enabled || ui->dialog_pause_syncing || city->game->command_depth ||
      ui->dialog_pause_active == !!ui->dialog_pause_requested)
    return;
  ui->dialog_pause_syncing = 1;
  if (ui->dialog_pause_requested ? BeginCityDialogPause(city, 1) : EndCityDialogPause(city, 1))
    ui->dialog_pause_active = !!ui->dialog_pause_requested;
  ui->dialog_pause_syncing = 0;
}

void ApplicationUpdateDialogPause(City *city) {
  CityInterfaceState *ui = city->game->ui;
  ApplicationBudgetSync(city);
  if (city->game->command_depth == 0) {
    CityChartState *charts = &ui->charts;
    static const i16 codes[4] = {
        CITY_PANEL_HISTORY, CITY_PANEL_DEMOGRAPHICS, CITY_PANEL_NEIGHBORS, CITY_PANEL_INDUSTRY};
    for (i32 slot = 0; slot < 4; ++slot) {
      if (!charts->visible[slot])
        continue;
      i32 open = 0;
      for (i32 index = 0; index < ui->desktop.count; ++index)
        open |= Sc2kCanvasChartCode(ui->desktop.windows[index].kind) == codes[slot];
      if (!open)
        SetCityPanelVisibilityByCode(city, codes[slot]);
    }
  }
  if (city->game->command_depth == 0 && ui->map_data.visible) {
    i32 open = 0;
    for (i32 index = 0; index < ui->desktop.count; ++index)
      open |= ui->desktop.windows[index].kind == CITY_CANVAS_MAP_DATA;
    if (!open)
      SetCityPanelVisibilityByCode(city, CITY_PANEL_MAP_DATA);
  }
  ui->dialog_pause_requested = 0;
  CityCommand cancelled_labels[16];
  i32 cancelled_label_count = 0;
  CityCommand cancelled_stadiums[16];
  int cancelled_stadium_count = 0;
  i32 annual_open = 0;
  for (i32 index = 0; index < ui->desktop.count; ++index) {
    annual_open |= ui->desktop.windows[index].window_id == ui->annual_budget_window;
    i32 kind = ui->desktop.windows[index].kind;
    ui->dialog_pause_requested |= kind == CITY_CANVAS_BUDGET || kind == CITY_CANVAS_POLICIES ||
        kind == CITY_CANVAS_NOTICE || kind == CITY_CANVAS_EXIT || kind == CITY_CANVAS_ABANDON ||
        kind == CITY_CANVAS_ADVISOR || kind == CITY_CANVAS_CONFIRM || kind == CITY_CANVAS_NEWSPAPER;
  }
  if (!annual_open)
    ui->annual_budget_window = 0;
  ui->dialog_pause_requested |= annual_open;
  for (i32 index = 0; index < 16; ++index) {
    NativeStadiumDialog *pending = &ui->stadium_dialogs[index];
    if (!pending->window_id)
      continue;
    i32 open = 0;
    for (i32 window = 0; window < ui->desktop.count; ++window)
      open |= ui->desktop.windows[window].window_id == pending->window_id;
    if (!open) {
      CityCommand cancel = pending->command;
      cancel.parameter2 = CITY_DIALOG_CANCEL;
      if (cancel.id == CITY_COMMAND_REPLY_TO_QUESTION)
        cancelled_stadiums[cancelled_stadium_count++] = cancel;
      pending->window_id = 0;
      pending->answer = CITY_DIALOG_CANCEL;
    }
    ui->dialog_pause_requested |= open;
  }
  for (i32 index = 0; index < 16; ++index) {
    NativeLabelDialog *pending = &ui->label_dialogs[index];
    if (!pending->window_id)
      continue;
    i32 open = 0;
    for (i32 window = 0; window < ui->desktop.count; ++window)
      open |= ui->desktop.windows[window].window_id == pending->window_id;
    if (!open && !pending->opening && !pending->continuation && pending->command.id == CITY_COMMAND_REPLY_TO_QUESTION) {
      CityCommand cancel = pending->command;
      cancel.parameter2 = 2;
      cancelled_labels[cancelled_label_count++] = cancel;
    }
    if (!open)
      pending->window_id = 0;
    ui->dialog_pause_requested |= open;
  }
  if (ui->information_window.window_id) {
    CityTileInformationSnapshot *pending = &ui->information_window;
    i32 open = 0;
    for (i32 window = 0; window < ui->desktop.count; ++window)
      open |= ui->desktop.windows[window].window_id == pending->window_id;
    if (!open) {
      *pending = (CityTileInformationSnapshot){0};
    }
    ui->dialog_pause_requested |= open;
  }
  ui->dialog_pause_requested |= NativeSpanDialogWaiting(ui) || CityConnectionQuestionWaiting(ui) ||
      CityMissionNoticeWaiting(ui) || ui->startup_waiting || CityFileDialogWaiting(city) ||
      NativeScenarioDialogWaiting(city);
  ApplicationSyncDialogPause(city);
  for (int index = 0; index < cancelled_stadium_count; ++index)
    RunCityCommand(city->game, CITY_COMMAND_REPLY_TO_QUESTION,
        INVALID_TILE, cancelled_stadiums[index].parameter1, CITY_DIALOG_CANCEL, cancelled_stadiums[index].parameter3, NULL, NULL);
  for (i32 index = 0; index < cancelled_label_count; ++index)
    ExecuteRecordedCityCommandWithPayload(city->game, cancelled_labels[index], NULL, 0, (const u8 *)"", 0);
}

// Defer teardown until the retained original Quit/Save caller has returned.
void RequestCityApplicationExit(Game *game) {
  game->close_requested = 1;
}

// Host time controls delivery count, never the deterministic work inside a batch.
// Always finish one batch, even if preparing/presenting the frame used its budget.
void RunCitySimulationFrame(Game *game, double deadline) {
  City *city = game->city;
  do {
    RunGameIdle(game, (CityIdleInputs){true, true, true});
    if (game->execution_failed) break;
    if (game->close_requested || game->execution_failed || game->idle_blocked || game->delay_blocked ||
        game->idle_state != (u32)CITY_IDLE_PLAY || city->settings.speed == CITY_SPEED_PAUSED ||
        city->settings.timer_mode != CITY_TIMER_RUNNING || !city->cadence_counter.tick_due ||
        (!city->simulation_work.phase && city->settings.speed != CITY_SPEED_UNLIMITED)) break;
  } while (MonotonicSeconds() < deadline);
}

// Pending notices and decisions stay as events; completion keeps the original caller token.
void DeliverCitySimulationEvents(Game *game, int token, i64 result, bool cancelled) {
  CompleteCityCommandCallback(game, token, (CityCommandResult){.value = result,
      .host_error = GetGameError(game), .cancelled = cancelled});
  if (GetGameError(game)) game->execution_failed = true;
  if (!game->execution_failed && !game->close_requested && result >= 2 &&
      FindCityTickContinuation(game->city, (int)result)) OpenCityTickContinuation(game->city, (int)result);
}

void RunGameIdle(Game *game, CityIdleInputs inputs) {
  CityIdleInputs previous = game->idle_inputs;
  game->idle_inputs = inputs;
  ClearGameError(game);
  UpdateGame(game);
  game->idle_inputs = previous;
  DeliverCitySimulationEvents(game, 0, 1, false);
  if (!game->observation && game->ui->newspaper_month != game->city->clock.month) {
    game->ui->newspaper_month = game->city->clock.month;
    if (!game->city->newspaper.enabled && !game->ui->newspaper_delivery_disabled &&
        (game->ui->newspaper_month == CITY_MONTH_APRIL || game->ui->newspaper_month == CITY_MONTH_AUGUST))
      OpenCityManualNewspaper(game->city, game->city->newspaper.edition_index);
  }
}
