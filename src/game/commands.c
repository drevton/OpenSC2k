#include "game/commands.h"

#include <stdlib.h>
#include <string.h>

#include "city/engine/simulation.h"
#include "city/state.h"
#include "game/errors.h"
#include "game/observation.h"
#include "game/state.h"
#include "game/timing.h"
#include "ui/commands.h"
#include "ui/dialogs/dialogs.h"
#include "ui/dialogs/information.h"
#include "ui/dialogs/messages.h"
#include "ui/dialogs/name_editor.h"
#include "ui/map.h"
#include "ui/signs.h"
#include "ui/state.h"

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

// Reserve callback storage before execution can change City. Synchronous completion frees it;
// a suspended command retains its arguments, initial outcome and treasury baseline until completion.
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

// Retain the captured observation and UI feedback in Game-owned storage. Allocation failure leaves the caller's
// scope, notices and failure handling untouched; later animation delivery or teardown consumes the retained record.
bool RetainCityCommandObservation(Game *game, const CityCommandObservationScope *observation) {
  CityTimedCommand *work = malloc(sizeof(*work));
  if (!work) return false;
  *work = (CityTimedCommand){.observer = observation->observer, .scope = observation->scope,
      .before_tick = observation->before_tick, .packet = observation->packet, .feedback = *observation->feedback};
  game->timed_command = work;
  return true;
}

void ReleaseCityPostedCommands(Game *game) {
  if (game->timed_command) {
    CityTimedCommand *work = game->timed_command;
    if (work->observer->command_executed) work->observer->command_executed(work->scope);
    if (work->observer->end_command)
      work->observer->end_command(game, work->scope, 0, GetGameError(game), work->before_tick);
    DiscardCityCommandNotices(work->feedback.notices);
    free(work);
    game->timed_command = NULL;
  }
  while (game->pending_commands) {
    CityPendingCommand *pending = game->pending_commands;
    game->pending_commands = pending->next;
    free(pending);
  }
}

// Keep a suspended callback attached to its live tick token. Final completion consumes the record before invoking
// caller code, and merges the captured host/gameplay errors and cancellation with the final outcome.
void CompletePendingCityCommand(Game *game, int token, CityCommandResult result) {
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
// Every normal return writes completion when supplied, before later presentation can change live errors.
static i64 ExecuteCommandPayload(Game *game, CityCommand packet, const u8 *text, u32 length,
    const CityCommandArguments *native_arguments, CityCommandResult *completion) {
  if (game->observation && game->observation->execute_command) {
    CityCommandResult result;
    i64 value = game->observation->execute_command(game, packet, NULL, 0, text, length, NULL,
        native_arguments, &result);
    if (completion) *completion = result;
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
  if (admitted && !error && !feedback.cancelled && !game->execution_failed)
    PresentCityCommandAudio(city, packet.id, &args, result.succeeded, result.cost);
  if (completion) *completion = result;
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
    CompletePendingCityCommand(game, packet.parameter1, result);
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
  CityCommandResult result;
  CityInterfaceState *ui = game->ui;
  bool previous_errors = ui->command_errors_enabled;
  u32 previous_depth = ui->command_error_depth;
  ui->command_errors_enabled = true;
  ui->command_error_depth = game->command_depth;
  i64 value = ExecuteCommandPayload(game, packet, payload ? payload->text : NULL, payload ? payload->length : 0,
      &args, &result);
  ui->command_errors_enabled = previous_errors;
  ui->command_error_depth = previous_depth;
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

// Pending notices and decisions stay as events; completion keeps the original caller token.
void DeliverCitySimulationEvents(Game *game, int token, i64 result, bool cancelled) {
  CompletePendingCityCommand(game, token, (CityCommandResult){.value = result,
      .error = result < 0 ? result : 0, .host_error = GetGameError(game), .cancelled = cancelled});
  if (GetGameError(game)) game->execution_failed = true;
  if (!game->execution_failed && !game->close_requested && result >= 2 &&
      FindCityTickContinuation(game->city, (int)result)) OpenCityTickContinuation(game->city, (int)result);
}
