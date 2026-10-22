#include "city/saveload/runtime.h"
#include "city/saveload/state.h"
#include "city/engine/commands.h"

#include <string.h>

#include "game/audio.h"
#include "audio/types.h"
#include "city/disasters/disasters.h"
#include "city/economy/budget.h"
#include "city/economy/facilities.h"
#include "city/engine/cheats.h"
#include "city/engine/initialization.h"
#include "city/engine/questions.h"
#include "city/engine/simulation.h"
#include "city/events/emergency.h"
#include "city/events/events.h"
#include "city/infrastructure/bridges.h"
#include "city/infrastructure/highways.h"
#include "city/infrastructure/lines.h"
#include "city/infrastructure/surface_lines.h"
#include "city/infrastructure/tunnels.h"
#include "city/maps/buildings.h"
#include "city/maps/labels.h"
#include "city/maps/layers.h"
#include "city/maps/map.h"
#include "city/maps/trees.h"
#include "city/maps/water.h"
#include "city/military.h"
#include "city/presentation.h"
#include "city/state.h"
#include "city/tools/tools.h"
#include "game/errors.h"
#include "game/observation.h"
#include "game/state.h"
#include "game/update.h"

// Translate between serialized command fields and native tile arguments in either direction. Encode reads args and
// replaces command; decode reads command and replaces args, using City only for coordinate conversion.
static void TranslateCityCommandArguments(const City *city, CityCommand *command, CityCommandArguments *args,
    bool encode) {
  CityPoint point = {-1, -1};
  if (encode && IsMapTileValid(city, args->tile)) {
    point = MapTilePt(city, args->tile);
  }
  switch (command->id) {
  case CITY_COMMAND_LEVEL_TERRAIN_TILE:
  case CITY_COMMAND_STRETCH_TERRAIN_TILE:
  case CITY_COMMAND_PLACE_STREAM:
  case CITY_COMMAND_PLACE_BUILDING:
  case CITY_COMMAND_PLACE_WATER_PIPE:
  case CITY_COMMAND_PLACE_SUBWAY:
  case CITY_COMMAND_PLACE_ROAD:
  case CITY_COMMAND_PLACE_RAIL:
  case CITY_COMMAND_PLACE_POWER_LINE:
  case CITY_COMMAND_PLACE_TRANSPORT_LINE:
  case CITY_COMMAND_RENAME_SIGN:
  case CITY_COMMAND_MOVE_SIGN:
  case CITY_COMMAND_BULLDOZE_RECT:
  case CITY_COMMAND_PLANT_TREES_RECT:
  case CITY_COMMAND_BUILD_CANALS_RECT:
  case CITY_COMMAND_BUILD_BRIDGE:
  case CITY_COMMAND_LOWER_TERRAIN_TILE:
  case CITY_COMMAND_RAISE_TERRAIN_TILE:
  case CITY_COMMAND_STEP_TERRAIN_HEIGHT:
    if (encode) {
      *command = (CityCommand){command->id, (int)args->tile, args->parameter1, args->parameter2};
    } else {
      *args = (CityCommandArguments){(TileIndex)command->parameter1, command->parameter2, command->parameter3, 0};
    }
    break;
  case CITY_COMMAND_PLACE_HIGHWAY_ONRAMP:
  case CITY_COMMAND_PLACE_SUBWAY_RAIL_CONNECTION:
  case CITY_COMMAND_BULLDOZE:
  case CITY_COMMAND_APPLY_BULLDOZER_TOOL:
  case CITY_COMMAND_PLACE_ROAD_TERRAIN_SPAN:
  case CITY_COMMAND_EDIT_MAP_LABEL:
  case CITY_COMMAND_EDIT_FACILITY_LABEL:
  case CITY_COMMAND_ASSIGN_STADIUM_TEAM:
  case CITY_COMMAND_CARVE_WATER_CHANNEL:
  case CITY_COMMAND_APPLY_WATER_TILE:
    if (encode) {
      *command = (CityCommand){command->id, point.x, point.y, args->parameter1};
    } else {
      *args = (CityCommandArguments){MapTileXYOrInvalid(city, command->parameter1, command->parameter2),
          command->parameter3, 0, 0};
    }
    break;
  case CITY_COMMAND_DISPATCH_CITY_SERVICE:
    if (encode) {
      *command = (CityCommand){command->id, args->parameter1, point.x, point.y};
    } else {
      *args = (CityCommandArguments){MapTileXYOrInvalid(city, command->parameter2, command->parameter3),
          command->parameter1, 0, 0};
    }
    break;
  case CITY_COMMAND_PLACE_ZONE_RECT:
    if (encode) {
      *command = (CityCommand){command->id, args->parameter1, (int)args->tile, args->parameter2};
    } else {
      *args = (CityCommandArguments){(TileIndex)command->parameter2, command->parameter1, command->parameter3, 0};
    }
    break;
  case CITY_COMMAND_SET_EVENT_STAGE_AT_TILE:
    if (encode) {
      *command = (CityCommand){command->id, point.x - 4, point.y - 4, 0};
    } else {
      *args = (CityCommandArguments){INVALID_TILE, 0, 0, 0};
      if (command->parameter1 >= -4 && command->parameter1 < city->map_width - 4 &&
          command->parameter2 >= -4 && command->parameter2 < city->map_height - 4)
        args->tile = MapTileXY(city, command->parameter1 + 4, command->parameter2 + 4);
    }
    break;
  default:
    break;
  }
}

// Decode the current command record; old command streams are not supported. Returns decoded values only; it does not
// validate admission, execute an action or support retired command encodings.
CityCommandArguments DecodeCityCommand(const City *city, CityCommand command) {
  CityCommandArguments args = {INVALID_TILE, command.parameter1, command.parameter2, command.parameter3};
  TranslateCityCommandArguments(city, &command, &args, false);
  return args;
}

// Pack a resolved action and its native tile arguments into the current command record. Parameters must follow that
// command's schema; admission and execution happen later, and invalid tiles remain representable.
CityCommand EncodeCityCommand(const City *city, CityCommandId id, TileIndex tile, int parameter1, int parameter2,
    int parameter3) {
  CityCommand command = {id, parameter1, parameter2, parameter3};
  CityCommandArguments args = {tile, parameter1, parameter2, parameter3};
  TranslateCityCommandArguments(city, &command, &args, true);
  return command;
}

// Read the active command's staged text reply into caller-owned state and answer outputs. Returns false when no text
// reply is active; cancellation writes the answer but leaves the text output untouched.
bool ReadCityNameCommandAnswer(City *city, CityNameCommandInput *state, int *answer) {
  if (!city->command_name_active)
    return false;
  if (city->command_name_answer != CITY_DIALOG_CANCEL)
    *state = city->command_name;
  *answer = city->command_name_answer;
  return true;
}

static i64 ExecuteCityCommand(City *city, CityCommandId id, u32 flags, TileIndex location,
    int parameter1, int parameter2, int parameter3);

// Convert a cost quote into the command result convention. Successful quotes return their cost; either sign of an
// admission error is normalized to a negative result.
i64 CityCommandPriceResult(CityCommandCost quote) {
  return quote.error > 0 ? -quote.error : quote.error ? quote.error : quote.cost;
}

// Construction prices describe work independently of the editor and treasury. Apply the
// common policy only at the command/query boundary or the caller's actual payment point.
static i64 CityCommandCharge(const City *city, i64 cost) {
  return city->settings.timer_mode == CITY_TIMER_STOPPED ? 0 : cost;
}

i64 AffordableCityCommandCost(const City *city, i64 cost) {
  if (cost < 0) return CITY_COMMAND_COST_BLOCKED;
  cost = CityCommandCharge(city, cost);
  return cost && city->indicators.cash < cost ? CITY_COMMAND_COST_TOO_EXPENSIVE : cost;
}

CityCommandCost ApplyCityCommandCostPolicy(const City *city, CityCommandCost quote) {
  if (quote.error) return quote;
  i64 cost = AffordableCityCommandCost(city, quote.cost);
  quote.cost = CityCommandCharge(city, quote.cost);
  if (cost < 0) quote.error = cost;
  return quote;
}

// A retained caller may have already accepted its payment decision before a notice.
// Debit the live cost at its original commit point without repeating that decision.
void DebitCityCommandCost(City *city, i64 cost) {
  city->indicators.cash = AddInt64Clamped(city->indicators.cash, -CityCommandCharge(city, cost));
}

// Retained or partial-work stages validate live treasury and debit once. Ordinary
// commands are paid by DoCityCommand, independently of their meaningful result.
i64 PayCityCommandCost(City *city, i64 cost) {
  i64 charge = AffordableCityCommandCost(city, cost);
  if (charge >= 0) DebitCityCommandCost(city, cost);
  return charge;
}

// Finish an in-progress simulation pass before a command borrows its working maps or route queue. Commands that do not
// conflict leave the pass suspended; this is execution preparation and must not run during TEST queries.
static void FinishCityCommandWork(City *city, CityCommandId id) {
  // These commands do not borrow the pass's working maps or route queue.
  // Other mutations enter between complete phases, just as in stock mode.
  if (!city->simulation_work.phase || id == CITY_COMMAND_SET_SIMULATION_SPEED ||
      id == CITY_COMMAND_ANSWER_MILITARY_OFFER) return;
  FinishGameCitySimulationWork(city->game);
}

// Decode resolved command options into the construction choices used by placement. Inputs follow the command schema;
// the returned value contains no UI selections or borrowed pointers.
static CityConstructionChoices ReadCityConstructionChoices(CityCommandId id, int parameter1, int parameter2) {
  CityConstructionChoices choices = {.span = CITY_SPAN_CANCEL};
  switch (id) {
  case CITY_COMMAND_PLACE_ROAD:
    return ReadCityLineCommandChoices(CITY_INFRASTRUCTURE_ROAD, parameter1, parameter2);
  case CITY_COMMAND_PLACE_RAIL:
    return ReadCityLineCommandChoices(CITY_INFRASTRUCTURE_RAIL, parameter1, parameter2);
  case CITY_COMMAND_PLACE_POWER_LINE:
    return ReadCityLineCommandChoices(CITY_INFRASTRUCTURE_POWER, parameter1, parameter2);
  case CITY_COMMAND_PLACE_WATER_PIPE:
    return ReadCityLineCommandChoices(CITY_INFRASTRUCTURE_PIPE, parameter1, parameter2);
  case CITY_COMMAND_PLACE_SUBWAY:
    return ReadCityLineCommandChoices(CITY_INFRASTRUCTURE_SUBWAY, parameter1, parameter2);
  case CITY_COMMAND_PLACE_TRANSPORT_LINE:
    return ReadCityLineCommandChoices(CITY_INFRASTRUCTURE_HIGHWAY, parameter1, parameter2);
  case CITY_COMMAND_PLACE_ZONE_RECT:
    if (parameter1 >= 0 && parameter1 < ZONE_RESERVED_FIRST && kCityZoneCosts[parameter1] >= 0) {
      choices.endpoint_supplied = true;
      choices.end = parameter2;
      choices.unit_cost = kCityZoneCosts[parameter1];
    }
    return choices;
  case CITY_COMMAND_RAISE_TERRAIN_TILE:
  case CITY_COMMAND_LOWER_TERRAIN_TILE:
  case CITY_COMMAND_STEP_TERRAIN_HEIGHT:
    choices.terrain_options = parameter2;
    return choices;
  default:
    return choices;
  }
}

// Describe changed information without naming UI windows or widgets. Construction
// changes the map and its costs; economy and local notices have narrower effects.
static u32 CityCommandUpdates(CityCommandId id) {
  switch (id) {
  case CITY_COMMAND_BUDGET_RATE: case CITY_COMMAND_SET_BUDGET_RATE:
  case CITY_COMMAND_ISSUE_BOND: case CITY_COMMAND_REDEEM_BOND:
  case CITY_COMMAND_ADD_CITY_CASH_BONUS: case CITY_COMMAND_TOGGLE_AUTO_BUDGET:
    return CITY_UPDATE_FINANCES | CITY_UPDATE_STATUS;
  case CITY_COMMAND_SET_INDUSTRY_TAX_BASELINE:
    return CITY_UPDATE_INDUSTRY | CITY_UPDATE_FINANCES;
  case CITY_COMMAND_TOGGLE_CITY_POLICY:
    return CITY_UPDATE_POLICIES | CITY_UPDATE_FINANCES;
  case CITY_COMMAND_SET_SIMULATION_SPEED: case CITY_COMMAND_TOGGLE_NO_DISASTERS:
  case CITY_COMMAND_RECALL_DISPATCH: case CITY_COMMAND_DISPATCH_CITY_SERVICE:
    return CITY_UPDATE_STATUS;
  case CITY_COMMAND_EDIT_MAP_LABEL: case CITY_COMMAND_EDIT_FACILITY_LABEL:
  case CITY_COMMAND_ASSIGN_STADIUM_TEAM: case CITY_COMMAND_RENAME_SIGN: case CITY_COMMAND_MOVE_SIGN:
    return CITY_UPDATE_SIGNS;
  case CITY_COMMAND_PLACE_ZONE_RECT: case CITY_COMMAND_PLACE_WATER_PIPE:
  case CITY_COMMAND_PLACE_SUBWAY: case CITY_COMMAND_PLACE_ROAD: case CITY_COMMAND_PLACE_RAIL:
  case CITY_COMMAND_PLACE_POWER_LINE: case CITY_COMMAND_BULLDOZE: case CITY_COMMAND_APPLY_BULLDOZER_TOOL:
  case CITY_COMMAND_ADJUST_TERRAIN_HEIGHT: case CITY_COMMAND_PLACE_ROAD_TERRAIN_SPAN:
  case CITY_COMMAND_PLACE_TRANSPORT_LINE: case CITY_COMMAND_PLACE_HIGHWAY_ONRAMP:
  case CITY_COMMAND_PLACE_SUBWAY_RAIL_CONNECTION: case CITY_COMMAND_CARVE_WATER_CHANNEL:
  case CITY_COMMAND_APPLY_WATER_TILE: case CITY_COMMAND_LOWER_TERRAIN_TILE:
  case CITY_COMMAND_RAISE_TERRAIN_TILE: case CITY_COMMAND_STEP_TERRAIN_HEIGHT:
  case CITY_COMMAND_LEVEL_TERRAIN_TILE: case CITY_COMMAND_STRETCH_TERRAIN_TILE:
  case CITY_COMMAND_PLACE_STREAM:
  case CITY_COMMAND_BULLDOZE_RECT: case CITY_COMMAND_PLANT_TREES_RECT:
  case CITY_COMMAND_BUILD_CANALS_RECT: case CITY_COMMAND_BUILD_BRIDGE: case CITY_COMMAND_PLACE_BUILDING:
    return CITY_UPDATE_MAP | CITY_UPDATE_UTILITIES | CITY_UPDATE_POPULATION |
        CITY_UPDATE_FINANCES | CITY_UPDATE_STATUS;
  default:
    // Cheats, gameplay rules and continued questions can change several domains.
    return CITY_UPDATE_ALL;
  }
}

// These commands commit one resolved action without charging intermediate work. Retained
// selectors and partial legacy terrain/demolition callers keep their own payment stages.
static bool CityCommandHasSinglePayment(const City *city, CityCommandId id, TileIndex tile, int parameter1) {
  switch (id) {
  case CITY_COMMAND_BUILD_BRIDGE:
  case CITY_COMMAND_PLACE_BUILDING:
  case CITY_COMMAND_PLACE_HIGHWAY_ONRAMP:
  case CITY_COMMAND_PLACE_ZONE_RECT:
  case CITY_COMMAND_PLANT_TREES_RECT:
  case CITY_COMMAND_LEVEL_TERRAIN_TILE:
  case CITY_COMMAND_STRETCH_TERRAIN_TILE:
    return true;
  case CITY_COMMAND_RAISE_TERRAIN_TILE:
  case CITY_COMMAND_LOWER_TERRAIN_TILE:
  case CITY_COMMAND_STEP_TERRAIN_HEIGHT:
    return city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE);
  case CITY_COMMAND_PLACE_TRANSPORT_LINE:
    return IsMapTileValid(city, tile) && IsMapTileValid(city, parameter1) &&
        IsCityCompactHighwayLine(city, tile, parameter1);
  default:
    return false;
  }
}

// Validate and execute one resolved shared action at the City command boundary. Only CITY_COMMAND_TEST is accepted as
// a flag; queries do not mutate, while execution preserves each action's result and observer ordering.
i64 DoCityCommand(City *city, CityCommandId id, u32 flags, TileIndex tile, int parameter1, int parameter2,
    int parameter3) {
  if (!IsCityCommandId(id)) return CITY_COMMAND_COST_INVALID;
  CityCommand command = EncodeCityCommand(city, id, tile, parameter1, parameter2, parameter3);
  if (flags & ~CITY_COMMAND_TEST) return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) {
    i64 price = ExecuteCityCommand(city, id, flags, tile, parameter1, parameter2, parameter3);
    // Bond repayment retains its financial eligibility/confirmation contract, including in the editor.
    return price < 0 || id == CITY_COMMAND_REDEEM_BOND ? price : AffordableCityCommandCost(city, price);
  }
  Game *game = city->game;
  CityFileRuntime file;
  bool owns_file = game->file == NULL;
  if (owns_file) BeginCityFileOperation(game, &file);
  FinishCityCommandWork(city, id);
  // Only this caller suppresses feedback; a command pumped by a notice has its own policy.
  bool previous_suppression = SetCityDemolitionSuppressed(city,
      id == CITY_COMMAND_APPLY_BULLDOZER_TOOL && (parameter1 & CITY_BULLDOZER_NO_EFFECT) != 0);
  CityConstructionChoices previous_choices = city->command_choices;
  city->command_choices = game->observation ? (CityConstructionChoices){0} :
      ReadCityConstructionChoices(id, parameter1, parameter2);
  i64 price = -1;
  if (CityCommandHasSinglePayment(city, id, tile, parameter1)) {
    price = ExecuteCityCommand(city, id, CITY_COMMAND_TEST, tile, parameter1, parameter2, parameter3);
    // Building execution checks its fixed price before footprint/protest decisions. Preserve that order
    // even when the read-only geometry quote rejects; hydro checks its geometry before funds instead.
    if (id == CITY_COMMAND_PLACE_BUILDING && price != CITY_COMMAND_COST_INVALID &&
        parameter1 >= 0 && parameter1 < TK_COUNT && parameter1 != TK_HYDRO_POWER)
      price = kCityBuildings[parameter1].cost;
    // A TEST query estimates an unchosen highway style. Execution must preserve a resolved
    // cancellation before testing funds, while dry compact lines still have a real price.
    if (id == CITY_COMMAND_PLACE_TRANSPORT_LINE && price >= 0 && (parameter2 & 255) == CITY_ROAD_SPAN_CANCEL) {
      CityCompactHighwayPlan plan;
      if (PlanCityCompactHighway(city, tile, parameter1, (parameter2 >> CITY_HIGHWAY_FACING_SHIFT) - 1, &plan))
        price = CityCompactHighwayCost(&plan, CITY_SPAN_CANCEL);
    }
  }
  i64 charge = price >= 0 ? AffordableCityCommandCost(city, price) : 0;
  // Admission failures still reach the real handler for its original blocker/notice and return value.
  i64 result = charge < 0 ? charge : ExecuteCityCommand(city, id, flags, tile, parameter1, parameter2, parameter3);
  if (price >= 0 && result >= 0) DebitCityCommandCost(city, price);
  // Stadium presentation can retain this caller. Payment must precede it, and completion follows its return.
  if (id == CITY_COMMAND_PLACE_BUILDING && result > 0)
    FinishCityBuildingCommand(city, tile, parameter1, parameter2);
  if (id == CITY_COMMAND_BUILD_BRIDGE)
    PlayCitySound(city, tile, result >= 0 ? CITY_SOUND_BRIDGE_CONSTRUCTION : CITY_SOUND_PLACEMENT_REJECTED);
  if (result >= 0 && GetGameError(game)) result = CITY_COMMAND_COST_REJECTED;
  // Executing commands can leave partial changes even when their result is an error.
  InvalidateCityInformation(game, CityCommandUpdates(id));
  city->command_choices = previous_choices;
  SetCityDemolitionSuppressed(city, previous_suppression);
  if (game->command_completed != NULL)
    game->command_completed(game->observer, city, &command, result);
  if (owns_file) EndCityFileOperation(&file);
  return result;
}

// Stage a bounded text payload for a supported shared action, execute it, then release the temporary binding. Text is
// borrowed for the call; invalid IDs, flags, lengths and nested text bindings return a negative error.
i64 DoCityCommandWithText(City *city, CityCommandId id, u32 flags, TileIndex tile,
    int parameter1, int parameter2, int parameter3, const u8 *text, u32 length) {
  if (!IsCityCommandId(id)) return CITY_COMMAND_COST_INVALID;
  if (flags & ~CITY_COMMAND_TEST) return CITY_COMMAND_COST_INVALID;
  int limit = CITY_OVERLAY_NAME_MAX_LENGTH;
  if ((id != CITY_COMMAND_RENAME_SIGN && id != CITY_COMMAND_EDIT_MAP_LABEL &&
      id != CITY_COMMAND_EDIT_FACILITY_LABEL && id != CITY_COMMAND_ASSIGN_STADIUM_TEAM &&
      id != CITY_COMMAND_REPLY_TO_QUESTION) || length > limit || (length && !text) ||
      (length && memchr(text, 0, length))) return CITY_COMMAND_COST_INVALID;
  if (id == CITY_COMMAND_RENAME_SIGN) {
    if (parameter2 || parameter3) return CITY_COMMAND_COST_INVALID;
    i64 result = CmdRenameCitySign(city, flags, tile, parameter1, text, length);
    if (!(flags & CITY_COMMAND_TEST) && result >= 0) InvalidateCityInformation(city->game, CITY_UPDATE_SIGNS);
    return result;
  }
  if (id == CITY_COMMAND_REPLY_TO_QUESTION) {
    CityQuestionReply reply = {parameter2, text, length, parameter3};
    if (flags & CITY_COMMAND_TEST) return CanReplyToCityQuestion(city, parameter1, reply) ? 0 : CITY_COMMAND_COST_INVALID;
    i64 result = ReplyToCityQuestion(city, parameter1, reply);
    if (result >= 0) InvalidateCityInformation(city->game, CITY_UPDATE_ALL);
    return result;
  }
  if (flags & CITY_COMMAND_TEST) {
    if (!IsMapTileValid(city, tile) || parameter2 || parameter3) return CITY_COMMAND_COST_INVALID;
    int slot = ReadCityFacilityOverlay(city, tile);
    if (id == CITY_COMMAND_EDIT_MAP_LABEL)
      return CanEditCityMapSign(city, tile) &&
          (ReadCitySignOverlay(city, tile) || FindFreeOverlayNameSlot(city)) &&
          (parameter1 == CITY_DIALOG_OK || parameter1 == CITY_DIALOG_CANCEL) ? 0 : CITY_COMMAND_COST_BLOCKED;
    if (!IsCityFacilityOverlay(slot)) return CITY_COMMAND_COST_BLOCKED;
    if (id == CITY_COMMAND_ASSIGN_STADIUM_TEAM)
      return parameter1 >= -1 && parameter1 < CITY_TEAM_NAME_COUNT &&
          CityFacilityRecordAt(city, CityFacilityOverlaySlot(slot))->type == TK_STADIUM ? 0 :
          CITY_COMMAND_COST_INVALID;
    return parameter1 == CITY_DIALOG_OK || parameter1 == CITY_DIALOG_CANCEL ? 0 : CITY_COMMAND_COST_INVALID;
  }
  CityNameCommandInput previous = city->command_name;
  int previous_answer = city->command_name_answer;
  bool previous_active = city->command_name_active;
  city->command_name = (CityNameCommandInput){0};
  if (length) memcpy(city->command_name.name.character, text, length);
  city->command_name.length = length;
  city->command_name_answer = parameter1;
  city->command_name_active = true;
  i64 result = DoCityCommand(city, id, flags, tile, parameter1, parameter2, parameter3);
  city->command_name = previous;
  city->command_name_answer = previous_answer;
  city->command_name_active = previous_active;
  return result;
}

// Dispatch an admitted action to its domain handler without submitting another action. The caller supplies decoded
// arguments and supported flags; the handler's signed result is propagated unchanged.
static i64 ExecuteCityCommand(City *city, CityCommandId id, u32 flags, TileIndex location,
    int parameter1, int parameter2, int parameter3) {
  switch (id) {
  case CITY_COMMAND_PLACE_STREAM:
    if (parameter2 || parameter3) return CITY_COMMAND_COST_INVALID;
    return CmdPlaceStream(city, flags, location, parameter1);
  case CITY_COMMAND_LEVEL_TERRAIN_TILE:
  case CITY_COMMAND_STRETCH_TERRAIN_TILE:
    if (parameter3) return CITY_COMMAND_COST_INVALID;
    return id == CITY_COMMAND_LEVEL_TERRAIN_TILE ?
        CmdLevelTerrainTile(city, flags, location, parameter1, parameter2) :
        CmdStretchTerrainTile(city, flags, location, parameter1, parameter2);
  case CITY_COMMAND_START_CITY:
    return CmdStartCity(city, flags, location, parameter1, parameter2, parameter3);
  case CITY_COMMAND_ANSWER_MILITARY_OFFER:
    return CmdAnswerMilitaryOffer(city, flags, location, parameter1, parameter2, parameter3);
  case CITY_COMMAND_BUILD_BRIDGE:
    return CmdBuildCityBridge(city, flags, location, parameter1, parameter2);
  case CITY_COMMAND_CHEAT:
    return CmdCheat(city, flags, parameter1, parameter2, parameter3);
  case CITY_COMMAND_ENABLE_DEBUG_CITY:
    return CmdEnableDebugCity(city, flags, parameter1, parameter2, parameter3);
  case CITY_COMMAND_ALL_INVENTIONS:
    return CmdAllInventions(city, flags, parameter1, parameter2, parameter3);
  case CITY_COMMAND_SET_TREE_LIFECYCLE:
    return CmdSetTreeLifecycle(city, flags, parameter1, parameter2, parameter3);
  case CITY_COMMAND_SET_INDIVIDUAL_FACILITIES:
    return CmdSetIndividualFacilities(city, flags, parameter1, parameter2, parameter3);
  case CITY_COMMAND_SET_TERRAIN_PROTECTION:
    return CmdSetTerrainProtection(city, flags, parameter1, parameter2, parameter3);
  case CITY_COMMAND_PLACE_BUILDING:
    return CmdPlaceBuilding(city, flags, location, parameter1, parameter2, parameter3);
  case CITY_COMMAND_SET_FIX:
    return CmdSetCityFix(city, flags, parameter1, parameter2, parameter3);
  case CITY_COMMAND_SET_EVENT_STAGE_AT_TILE:
    return CmdCrashCityHelicopterAtTile(city, flags, location);
  case CITY_COMMAND_MOVE_SIGN:
    return CmdMoveCitySign(city, flags, location, parameter1, parameter2);
  case CITY_COMMAND_EDIT_MAP_LABEL:
    return CmdEditMapLabel(city, flags, location, parameter1);
  case CITY_COMMAND_REPLY_TO_QUESTION:
    return CmdReplyToQuestion(city, flags, parameter1, parameter2, parameter3);
  case CITY_COMMAND_EDIT_FACILITY_LABEL:
    return CmdEditFacilityLabel(city, flags, location, parameter1);
  case CITY_COMMAND_ASSIGN_STADIUM_TEAM:
    return CmdAssignStadiumTeam(city, flags, location, parameter1);
  case CITY_COMMAND_DISPATCH_CITY_SERVICE:
    return CmdDispatchCityService(city, flags, location, parameter1);
  case CITY_COMMAND_RECALL_DISPATCH:
    return CmdRecallDispatch(city, flags, location, parameter1, parameter2, parameter3);
  case CITY_COMMAND_START_DISASTER:
    return CmdStartDisaster(city, flags, location, parameter1, parameter2, parameter3);
  case CITY_COMMAND_BULLDOZE:
    return CmdBulldoze(city, flags, location, parameter1);
  case CITY_COMMAND_APPLY_BULLDOZER_TOOL:
    return CmdApplyBulldozerTool(city, flags, location, parameter1);
  case CITY_COMMAND_PLANT_TREES_RECT:
    return CmdPlantTreesRect(city, flags, location, parameter1, parameter2);
  case CITY_COMMAND_BUILD_CANALS_RECT:
    return CmdBuildCanalsRect(city, flags, location, parameter1, parameter2);
  case CITY_COMMAND_BULLDOZE_RECT:
    return CmdBulldozeRect(city, flags, location, parameter1, parameter2);
  case CITY_COMMAND_RAISE_TERRAIN_TILE:
  case CITY_COMMAND_STEP_TERRAIN_HEIGHT:
    return CmdRaiseOrStepTerrain(city, flags, location, id == CITY_COMMAND_RAISE_TERRAIN_TILE,
        parameter1, parameter2, parameter3);
  case CITY_COMMAND_ADJUST_TERRAIN_HEIGHT:
    return CmdAdjustTerrainHeight(city, flags, location, parameter1);
  case CITY_COMMAND_PLACE_ROAD_TERRAIN_SPAN:
    return CmdPlaceRoadTerrainSpan(city, flags, location, parameter1);
  case CITY_COMMAND_PLACE_TRANSPORT_LINE:
    return CmdPlaceTransportLine(city, flags, location, parameter1, parameter2, parameter3);
  case CITY_COMMAND_PLACE_ZONE_RECT:
    return CmdPlaceZoneRect(city, flags, location, parameter1, parameter2);
  case CITY_COMMAND_PLACE_WATER_PIPE:
    return CmdPlaceInfrastructureLine(city, flags, location, CITY_INFRASTRUCTURE_PIPE,
        parameter1, parameter2, parameter3);
  case CITY_COMMAND_PLACE_SUBWAY:
    return CmdPlaceInfrastructureLine(city, flags, location, CITY_INFRASTRUCTURE_SUBWAY,
        parameter1, parameter2, parameter3);
  case CITY_COMMAND_PLACE_ROAD:
    return CmdPlaceInfrastructureLine(city, flags, location, CITY_INFRASTRUCTURE_ROAD,
        parameter1, parameter2, parameter3);
  case CITY_COMMAND_PLACE_RAIL:
    return CmdPlaceInfrastructureLine(city, flags, location, CITY_INFRASTRUCTURE_RAIL,
        parameter1, parameter2, parameter3);
  case CITY_COMMAND_PLACE_POWER_LINE:
    return CmdPlaceInfrastructureLine(city, flags, location, CITY_INFRASTRUCTURE_POWER,
        parameter1, parameter2, parameter3);
  case CITY_COMMAND_PLACE_HIGHWAY_ONRAMP:
    return CmdPlaceHighwayOnramp(city, flags, location, parameter1);
  case CITY_COMMAND_PLACE_SUBWAY_RAIL_CONNECTION:
    return CmdPlaceSubwayRailConnection(city, flags, location, parameter1);
  case CITY_COMMAND_SET_BUDGET_RATE:
    return CmdSetBudgetRate(city, flags, location, parameter1, parameter2, parameter3);
  case CITY_COMMAND_BUDGET_RATE:
    return CmdBudgetRate(city, flags, location, parameter1, parameter2, parameter3);
  case CITY_COMMAND_SET_INDUSTRY_TAX_BASELINE:
    return CmdSetIndustryTaxBaseline(city, flags, location, parameter1, parameter2, parameter3);
  case CITY_COMMAND_TOGGLE_CITY_POLICY:
    return CmdToggleCityPolicy(city, flags, location, parameter1, parameter2, parameter3);
  case CITY_COMMAND_DISASTER_UNLOCK_ANSWER:
    return CmdSetMilitaryRewardAnswer(city, flags, parameter1, parameter2, parameter3);
  case CITY_COMMAND_ISSUE_BOND:
    return CmdIssueBond(city, flags, location, parameter1, parameter2, parameter3);
  case CITY_COMMAND_REDEEM_BOND:
    return CmdRedeemBond(city, flags, location, parameter1, parameter2, parameter3);
  case CITY_COMMAND_SET_SIMULATION_SPEED:
    return CmdSetSimulationSpeed(city, flags, parameter1, parameter2, parameter3);
  case CITY_COMMAND_ADD_CITY_CASH_BONUS:
    return CmdAddCityCashBonus(city, flags, parameter1, parameter2, parameter3);
  case CITY_COMMAND_TOGGLE_NO_DISASTERS:
    return CmdToggleNoDisasters(city, flags, parameter1, parameter2, parameter3);
  case CITY_COMMAND_TOGGLE_AUTO_BUDGET:
    return CmdToggleAutoBudget(city, flags, location, parameter1, parameter2, parameter3);
  case CITY_COMMAND_CARVE_WATER_CHANNEL:
    return CmdCarveWaterChannel(city, flags, location, parameter1);
  case CITY_COMMAND_LOWER_TERRAIN_TILE:
    return CmdLowerTerrainTile(city, flags, location, parameter1, parameter2, parameter3);
  case CITY_COMMAND_APPLY_WATER_TILE:
    return CmdApplyWaterTile(city, flags, location, parameter1);
  case CITY_COMMAND_GRANT_SPECIAL_FACILITIES_AND_CASH:
    return CmdGrantSpecialFacilitiesAndCash(city, flags, parameter1, parameter2, parameter3);
  default:
    return CITY_COMMAND_COST_INVALID;
  }
}

// Check membership in the supported shared-action registry. Retired numeric IDs and local UI operations are rejected;
// the predicate has no execution side effects.
bool IsCityCommandId(u32 id) {
  switch (id) {
  case CITY_COMMAND_PLACE_STREAM:
  case CITY_COMMAND_LEVEL_TERRAIN_TILE:
  case CITY_COMMAND_STRETCH_TERRAIN_TILE:
  case CITY_COMMAND_START_CITY:
  case CITY_COMMAND_PLACE_BUILDING:
  case CITY_COMMAND_ANSWER_MILITARY_OFFER:
  case CITY_COMMAND_BUDGET_RATE:
  case CITY_COMMAND_SET_BUDGET_RATE:
  case CITY_COMMAND_PLACE_ZONE_RECT:
  case CITY_COMMAND_PLACE_WATER_PIPE:
  case CITY_COMMAND_PLACE_SUBWAY:
  case CITY_COMMAND_PLACE_ROAD:
  case CITY_COMMAND_PLACE_RAIL:
  case CITY_COMMAND_PLACE_POWER_LINE:
  case CITY_COMMAND_BULLDOZE:
  case CITY_COMMAND_APPLY_BULLDOZER_TOOL:
  case CITY_COMMAND_ADJUST_TERRAIN_HEIGHT:
  case CITY_COMMAND_PLACE_ROAD_TERRAIN_SPAN:
  case CITY_COMMAND_TOGGLE_CITY_POLICY:
  case CITY_COMMAND_DISASTER_UNLOCK_ANSWER:
  case CITY_COMMAND_ISSUE_BOND:
  case CITY_COMMAND_REDEEM_BOND:
  case CITY_COMMAND_PLACE_TRANSPORT_LINE:
  case CITY_COMMAND_PLACE_HIGHWAY_ONRAMP:
  case CITY_COMMAND_DISPATCH_CITY_SERVICE:
  case CITY_COMMAND_SET_INDUSTRY_TAX_BASELINE:
  case CITY_COMMAND_PLACE_SUBWAY_RAIL_CONNECTION:
  case CITY_COMMAND_SET_SIMULATION_SPEED:
  case CITY_COMMAND_TOGGLE_AUTO_BUDGET:
  case CITY_COMMAND_GRANT_SPECIAL_FACILITIES_AND_CASH:
  case CITY_COMMAND_CARVE_WATER_CHANNEL:
  case CITY_COMMAND_APPLY_WATER_TILE:
  case CITY_COMMAND_LOWER_TERRAIN_TILE:
  case CITY_COMMAND_RAISE_TERRAIN_TILE:
  case CITY_COMMAND_STEP_TERRAIN_HEIGHT:
  case CITY_COMMAND_SET_EVENT_STAGE_AT_TILE:
  case CITY_COMMAND_EDIT_MAP_LABEL:
  case CITY_COMMAND_EDIT_FACILITY_LABEL:
  case CITY_COMMAND_ASSIGN_STADIUM_TEAM:
  case CITY_COMMAND_TOGGLE_NO_DISASTERS:
  case CITY_COMMAND_ADD_CITY_CASH_BONUS:
  case CITY_COMMAND_REPLY_TO_QUESTION:
  case CITY_COMMAND_RENAME_SIGN:
  case CITY_COMMAND_MOVE_SIGN:
  case CITY_COMMAND_SET_FIX:
  case CITY_COMMAND_CHEAT:
  case CITY_COMMAND_ALL_INVENTIONS:
  case CITY_COMMAND_BULLDOZE_RECT:
  case CITY_COMMAND_SET_TREE_LIFECYCLE:
  case CITY_COMMAND_PLANT_TREES_RECT:
  case CITY_COMMAND_SET_INDIVIDUAL_FACILITIES:
  case CITY_COMMAND_SET_TERRAIN_PROTECTION:
  case CITY_COMMAND_BUILD_CANALS_RECT:
  case CITY_COMMAND_BUILD_BRIDGE:
  case CITY_COMMAND_ENABLE_DEBUG_CITY:
  case CITY_COMMAND_RECALL_DISPATCH:
  case CITY_COMMAND_START_DISASTER:
    return true;
  default: return false;
  }
}
