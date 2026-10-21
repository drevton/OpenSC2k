#include "city/economy/facilities.h"
#include "city/infrastructure/tunnels.h"
#include "city/maps/buildings.h"
#include "city/infrastructure/bridges.h"
#include "city/infrastructure/highways.h"
#include "city/infrastructure/surface_lines.h"
#include "city/maps/water.h"
#include "city/maps/trees.h"
#include "city/military.h"
#include "audio/types.h"
#include "city/disasters/types.h"
#include "city/economy/types.h"
#include "city/engine/city_types.h"
#include "city/engine/command_types.h"
#include "city/infrastructure/types.h"
#include "city/maps/map_types.h"
#include "city/maps/tile_storage.h"
#include "city/tools/types.h"
#include "city/maps/layers.h"
#include "city/presentation.h"
#include "city/engine/commands.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "audio/audio.h"
#include "city/disasters/disasters.h"
#include "city/economy/economy.h"
#include "city/engine/initialization.h"
#include "city/engine/questions.h"
#include "city/engine/simulation.h"
#include "city/events/events.h"
#include "city/events/news.h"
#include "city/growth/growth.h"
#include "city/infrastructure/infrastructure.h"
#include "city/maps/map.h"
#include "city/state.h"
#include "city/tools/tools.h"
#include "city/saveload/file.h"
#include "game/errors.h"
#include "game/observation.h"
#include "game/state.h"
#include "audio/state.h"
#include "assets/state.h"
#include "city/saveload/services.h"
#include "assets/catalog.h"
#include "city/terrain/terrain.h"
#include "base/text.h"

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
CityCommand EncodeCityCommand(City *city, CityCommandId id, TileIndex tile, int parameter1, int parameter2,
    int parameter3) {
  CityCommand command = {id, parameter1, parameter2, parameter3};
  CityCommandArguments args = {tile, parameter1, parameter2, parameter3};
  TranslateCityCommandArguments(city, &command, &args, true);
  return command;
}

// The original cash-only menu command has no other game or host effects. Uses clamped signed treasury arithmetic;
// caller submits the shared action and owns any status refresh.
void Add500000ToCityCash(City *city) {
  city->indicators.cash = AddInt64Clamped(city->indicators.cash, CITY_CASH_MENU_BONUS);
}

// Store an agreed simulation speed and refresh its status presentation. The caller validates the speed enum; this does
// not advance simulation or persist a local preference.
void SelectCitySimulationSpeed(City *city, int speed) {
  assert(speed >= CITY_SPEED_PAUSED && speed <= CITY_SPEED_UNLIMITED);
  city->settings.speed = speed;
  RefreshCitySpeedStatus(city, 0);
}

// Move one sector rate by a caller-selected step within its directional limits. Direction is positive to increase and
// nonpositive to decrease; the caller supplies a valid existing rate and step.
static void ChangeBudgetRate(EconomySector *sector, int direction, int step, int maximum) {
  int rate = (int)sector->current_rate;
  if (direction > 0) {
    if (rate < maximum)
      rate += step;
  } else if (rate > 0) {
    rate -= step;
  }
  sector->current_rate = (u32)rate;
}

// Clamp a sector's funding rate to zero through the supplied maximum. Requires a live sector and nonnegative maximum;
// only current_rate changes.
static void ClampBudgetRate(EconomySector *sector, int maximum) {
  int rate = (int)sector->current_rate;
  rate = IntClamp(rate, 0, maximum);
  sector->current_rate = (u32)rate;
}

// Apply a directional funding or tax step to the selected budget category. The caller validates the category; property
// tax updates all three tax sectors together.
static void AdjustCityBudgetRate(City *city, int category, int direction) {
  CityEconomyMonths *economy = &city->economy;
  if (category == CITY_BUDGET_PROPERTY_TAX) {
    ChangeBudgetRate(&economy->sector[CITY_ECONOMY_RESIDENTIAL], direction, 1, 20);
    ChangeBudgetRate(&economy->sector[CITY_ECONOMY_COMMERCIAL], direction, 1, 20);
    if ((direction > 0 && (int)economy->sector[CITY_ECONOMY_INDUSTRIAL].current_rate < 20) ||
        (direction < 0 && (int)economy->sector[CITY_ECONOMY_INDUSTRIAL].current_rate > 0))
      AdjustIndustryTaxRates(city, direction);
    ClampBudgetRate(&economy->sector[CITY_ECONOMY_RESIDENTIAL], 20);
  } else if (category >= CITY_BUDGET_POLICE && category <= CITY_BUDGET_HEALTH) {
    ChangeBudgetRate(&economy->sector[category + 2], direction, 5, 100);
    ClampBudgetRate(&economy->sector[category + 2], 100);
  } else if (category == CITY_BUDGET_EDUCATION) {
    ChangeBudgetRate(&economy->sector[CITY_ECONOMY_SCHOOLS], direction, 5, 100);
    ChangeBudgetRate(&economy->sector[CITY_ECONOMY_COLLEGES], direction, 5, 100);
    ClampBudgetRate(&economy->sector[CITY_ECONOMY_COLLEGES], 100);
  } else if (category == CITY_BUDGET_TRANSIT) {
    for (int index = CITY_ECONOMY_ROADS; index < CITY_ECONOMY_SECTOR_COUNT; ++index)
      ChangeBudgetRate(&economy->sector[index], direction, 5, 100);
    if (direction > 0 && city->budget_transit_clamp_rate > 100)
      city->budget_transit_clamp_rate = 100;
    if (direction < 0 && city->budget_transit_clamp_rate < 0)
      city->budget_transit_clamp_rate = 0;
  }
}

// Apply the budget policy to the city. Policy must identify an admitted ordinance; applies its funding effects and
// refreshes the attached budget controls.
static void ToggleCityBudgetPolicy(City *city, int policy) {
  u32 bit = 1u << policy;
  int cost = Phase0PolicyCost(city, policy);
  EconomySector *policy_budget = &city->economy.sector[CITY_ECONOMY_ORDINANCES];
  city->unlocks.ordinance_flags ^= bit;
  if ((city->unlocks.ordinance_flags & bit) != 0) {
    policy_budget->current_amount += cost;
  } else {
    policy_budget->current_amount -= cost;
  }
  if (city->indicators.year_flag != 0)
    policy_budget->projected_annual_total = policy_budget->current_amount * 12;
  else
    policy_budget->projected_annual_total =
        policy_budget->accumulated + policy_budget->current_amount * (11 - city->clock.month);
  UpdateCityPolicyControls(city, policy, cost);
}

// Rebuild the active policy total, then refresh city unlock availability. Requires initialized economy/unlock state;
// recomputes derived availability without toggling an ordinance.
void RebuildCityPolicyAvailability(City *city) {
  RestoreCityCursorAfterDialog(city);
  EconomySector *policy_budget = &city->economy.sector[CITY_ECONOMY_ORDINANCES];
  policy_budget->current_amount = 0;
  for (int policy = 0; policy < CITY_ORDINANCE_COUNT; ++policy)
    if ((city->unlocks.ordinance_flags & (1u << policy)) != 0)
      policy_budget->current_amount += Phase0PolicyCost(city, policy);
  RebuildCityUnlockState(city);
}

// Grant rewards and cash; update the owned city status presentation. Unlocks all rewards and adds the fixed cash bonus
// with clamping; requires attached presentation owners for its feedback.
void GrantAllSpecialFacilitiesAndCash(City *city) {
  memset(&city->economic_tables.milestone_years, 0, sizeof(city->economic_tables.milestone_years));
  city->unlocks.disaster_level = 6;
  RebuildCityUnlockState(city);
  for (int facility = 0; facility < 5; ++facility)
    SetSpecialFacilityAvailability(city, facility, 1);
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  PlayCitySound(city, INVALID_TILE, CITY_SOUND_CIVIC_PLACEMENT);
  city->indicators.cash = AddInt64Clamped(city->indicators.cash, CITY_CASH_MENU_BONUS);
  RefreshCityRewardToolbar(city);
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

static void FinishCityCommandWork(City *city, CityCommandId id);

// Terrain math, genuine progress lifetime and final map presentation.

// Find or create the cheat's bed-and-breakfast house using deterministic City state. May consume RNG and edit tiles;
// the Boolean result reports whether the house effect succeeded.
static bool BuildCheatHouse(City *city) {
  TileIndex house = city->cheats.house ? city->cheats.house - 1 : INVALID_TILE;
  if (IsMapTileValid(city, house) && city->tile[house].kind == TK_BED_BREAKFAST_INN &&
      !ReadCityZoneKind(&city->tile[house])) {
    CityPoint point = MapTilePt(city, house);
    CenterCityMapOnEvent(city, point.x, point.y);
    return true;
  }
  for (int attempt = 0; attempt < 100; ++attempt) {
    int x = 1 + Sc2kCrtRandRange(city, city->map_width - 1);
    int y = 1 + Sc2kCrtRandRange(city, city->map_height - 1);
    TileIndex tile = MapTileXY(city, x, y), wind = MapTileXY(city, x - 1, y), sign = MapTileXY(city, x, y - 1);
    TileIndex cells[] = {tile, wind, sign};
    bool available = true;
    int height = ReadCityLandHeight(&city->tile[tile]);
    for (int i = 0; i < 3; ++i) {
      const TileInfo *tp = &city->tile[cells[i]];
      if (!IsFlatDryUnbuiltTile(tp) || ReadUnderground(city, cells[i]) || ReadCityZoneKind(tp) ||
          ReadCityLandHeight(tp) != height) available = false;
    }
    if (!available || PlaceSpecialZoneFootprint(city, tile, TK_BED_BREAKFAST_INN, 1) < 0) continue;
    PlaceSpecialZoneFootprint(city, wind, TK_WIND_POWER, 1);
    int slot = FindFreeOverlayNameSlot(city);
    if (slot > 0) CmdRenameCitySign(city, 0, sign, slot, (const u8 *)"Ilona's House", 13);
    city->cheats.house = tile + 1;
    CenterCityMapOnEvent(city, x, y);
    return true;
  }
  PresentCityCheatNotice(city, "Sorry, no room to build Ilona's house!");
  return false;
}

// Apply the selected cheat with explicit gameplay options and its original admission/result behavior. Rejects
// unsupported cheat values; CITY_COMMAND_TEST checks the action without RNG or effects and execution may change
// multiple City systems.
static i64 CmdCheat(City *city, u32 flags, TileIndex tile, int cheat, int options, int reserved) {
  (void)tile;
  int invalid = CITY_COMMAND_COST_INVALID;
  if (reserved || cheat < 0 || cheat >= CITY_CHEAT_COUNT) return invalid;
  bool demolish = cheat == CITY_CHEAT_REPENT && city->cheats.church == 2;
  bool military = cheat == CITY_CHEAT_MILITARY;
  int allowed = demolish ? CITY_CHEAT_CONFIRM_DEMOLITION :
      military ? CITY_CHEAT_ACCEPT_MILITARY | CITY_CHEAT_DECLINE_MILITARY : 0;
  if ((options & ~allowed) || (military && options == allowed)) return invalid;
  if (!(city->fixes & (1 << CITY_FIX_RESTORED_CHEATS)))
    return CITY_COMMAND_COST_BLOCKED;
  if ((demolish || military) && !options)
    return flags & CITY_COMMAND_TEST ? CITY_COMMAND_NEEDS_CONFIRMATION : CITY_COMMAND_COST_REJECTED;
  if (flags & CITY_COMMAND_TEST)
    return cheat == CITY_CHEAT_FUND && city->indicators.bond_count >= 50 ? CITY_COMMAND_COST_BLOCKED : 0;
  const char *notice = NULL;
  switch (cheat) {
  case CITY_CHEAT_FUND:
    if (city->indicators.bond_count >= 50) return -CITY_NOTICE_BOND_LIMIT_REACHED;
    ApplyCityBondIssue(city, 25);
    break;
  case CITY_CHEAT_CASS:
    city->indicators.cash = AddInt64Clamped(city->indicators.cash, 250);
    if (Sc2kCrtRandRange(city, 16)) break;
    // The unlucky cash code starts a firestorm at the center.
    // fall through
  case CITY_CHEAT_FIRE_STORM:
  case CITY_CHEAT_NOAH:
    city->disasters.pending_type = cheat == CITY_CHEAT_NOAH ? CITY_DISASTER_MAJOR_FLOOD : CITY_DISASTER_FIRE_STORM;
    SetDisasterLocation(&city->disasters.location, city->geography.center_x, city->geography.center_y);
    break;
  case CITY_CHEAT_THE_WORKS: GrantAllSpecialFacilitiesAndCash(city); break;
  case CITY_CHEAT_MOSES:
    if (city->settings.timer_mode == CITY_TIMER_EVENT && (city->disasters.last_type == CITY_DISASTER_FLOOD ||
        city->disasters.last_type == CITY_DISASTER_MAJOR_FLOOD || city->disasters.last_type == CITY_DISASTER_HURRICANE)) {
      city->disasters.event_decay_ticks = 0;
      city->disasters.event_cleanup_ticks = 0;
    }
    break;
  case CITY_CHEAT_DEBUG: EnableCityDeveloperTools(city); break;
  case CITY_CHEAT_MILITARY: {
    CityMilitaryPlacement placement = BeginCityMilitaryPlacement(city, options == CITY_CHEAT_ACCEPT_MILITARY);
    FinishCityMilitaryPlacement(city, placement);
    if (placement.notice) ReportCityResourceNotice(city, 0, placement.notice);
    break;
  }
  case CITY_CHEAT_JOKE:
    notice = "Why did the mayor cross the road? To rezone the other side.";
    break;
  case CITY_CHEAT_WEBB: if (!BuildCheatHouse(city)) return CITY_COMMAND_COST_BLOCKED; break;
  case CITY_CHEAT_OOPS:
    city->cheats.church = IntMin(city->cheats.church + 1, 2);
    notice = "Same to you, buddy!";
    break;
  case CITY_CHEAT_REPENT:
    if (demolish) {
      city->cheats.church = 1;
      for (TileIndex tile = 0; tile < (u32)city->map_cell_count; ++tile)
        if (city->tile[tile].kind == TK_CHURCH && !ReadCityZoneKind(&city->tile[tile])) {
          if (!ReadTerrainCode(&city->tile[tile])) WriteZoneByte(city, tile, ZONE_RESIDENTIAL_HIGH);
          ReplaceCityTileVariant(city, tile, TK_RUBBLE, Sc2kCrtRandRange(city, 4));
          InvalidateTile(city, tile);
        }
    } else {
      city->cheats.church = 0;
      notice = "Oh, go on...";
    }
    break;
  default: return CITY_COMMAND_COST_INVALID;
  }
  city->cheats.used = 1;
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  if (notice) PresentCityCheatNotice(city, notice);
  return true;
}

static i64 ExecuteCityCommand(City *city, CityCommandId id, u32 flags, TileIndex location,
    int parameter1, int parameter2, int parameter3);

static CityConstructionChoices ReadCityConstructionChoices(CityCommandId id, int parameter1, int parameter2);

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

// Quote demolition over unique admitted footprints in an inclusive rectangle. Validates endpoints and option bits;
// reports cost/error without removing tiles, charging cash or generating effects.
CityCommandCost GetCityBulldozeRectangleCost(City *city, TileIndex start, TileIndex end, int options) {
  CityCommandCost quote = {.error = CITY_COMMAND_COST_INVALID, .error_tile = INVALID_TILE};
  if (!IsMapTileValid(city, start) || !IsMapTileValid(city, end) ||
      (options & ~CITY_BULLDOZER_FLAGS_MASK)) return quote;
  CityRect bounds = MapTileRectangle(city, start, end);
  bool underground = (options & CITY_BULLDOZER_UNDERGROUND) != 0;
  bool alt = (options & CITY_BULLDOZER_ALT) != 0;
  bool eligible = false;
  int first_error = CITY_COMMAND_COST_NOTHING_TO_REMOVE;
  for (int y = bounds.top; y < bounds.bottom; ++y)
    for (int x = bounds.left; x < bounds.right; ++x) {
      TileIndex tile = MapTileXY(city, x, y);
      int error = GetCityBulldozeTileError(city, tile, underground, alt);
      if (!error) {
        eligible = true;
        quote.cost += GetCityBulldozeTileCost(city, tile, underground);
      } else if (error != CITY_COMMAND_COST_NOTHING_TO_REMOVE) first_error = error;
    }
  quote.error = !eligible ? first_error : CITY_COMMAND_COST_OK;
  return quote;
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
  CityInfrastructureKind kind;
  switch (id) {
  case CITY_COMMAND_PLACE_ROAD:
    kind = CITY_INFRASTRUCTURE_ROAD;
    choices.supplied = true;
    choices.span = DecodeCityRoadSpanChoice((parameter2 >> 8) & 255);
    choices.border = parameter2 & CITY_LINE_ACCEPT_BORDER;
    break;
  case CITY_COMMAND_PLACE_RAIL:
    kind = CITY_INFRASTRUCTURE_RAIL;
    choices.supplied = true;
    choices.span = parameter2 & CITY_LINE_CANCEL_SPAN ? CITY_SPAN_CANCEL : CITY_SPAN_RAIL;
    choices.border = parameter2 & CITY_LINE_ACCEPT_BORDER;
    break;
  case CITY_COMMAND_PLACE_POWER_LINE:
    kind = CITY_INFRASTRUCTURE_POWER;
    choices.supplied = true;
    choices.span = parameter2 & CITY_LINE_CANCEL_SPAN ? CITY_SPAN_CANCEL : CITY_SPAN_POWER;
    break;
  case CITY_COMMAND_PLACE_WATER_PIPE: kind = CITY_INFRASTRUCTURE_PIPE; break;
  case CITY_COMMAND_PLACE_SUBWAY: kind = CITY_INFRASTRUCTURE_SUBWAY; break;
  case CITY_COMMAND_PLACE_TRANSPORT_LINE:
    kind = CITY_INFRASTRUCTURE_HIGHWAY;
    choices.supplied = true;
    choices.span = (parameter2 & 255) == CITY_ROAD_SPAN_CANCEL ? CITY_SPAN_CANCEL : parameter2 & 255;
    choices.border = parameter2 & 256;
    break;
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
  choices.endpoint_supplied = true;
  choices.end = parameter1;
  choices.unit_cost = kCityInfrastructure[kind].unit_cost;
  return choices;
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

static void FinishCityBuildingCommand(City *city, TileIndex tile, int kind, int orientation);

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
    return flags & CITY_COMMAND_TEST && result >= 0 ? 0 : result;
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

// Cmd handlers return raw TEST prices or meaningful execution results. DoCityCommand
// owns ordinary payment and completion observation; retained stages remain explicit.
// Enable the shared debug-building state for this City. All payload values are reserved and must be zero; TEST returns
// admission without changing state.
static i64 CmdEnableDebugCity(City *city, u32 flags, TileIndex location,
    int parameter1, int parameter2, int parameter3) {
  (void)location;
  int invalid = CITY_COMMAND_COST_INVALID;
  if (parameter1 || parameter2 || parameter3) return invalid;
  if (flags & CITY_COMMAND_TEST) return 0;
  city->cheats.debug_buildings = true;
  city->cheats.used = 1;
  if (city->indicators.cash < 1000000000) city->indicators.cash = 1000000000;
  city->newspaper.enabled = 0;
  city->indicators.ranked_disaster_gate = 0;
  memset(&city->economic_tables.milestone_years, 0, sizeof(city->economic_tables.milestone_years));
  RebuildCityUnlockState(city);
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  return true;
}

// Unlock the supported invention state when its gameplay fix permits the action. Reserved payload values must be zero;
// TEST has no effects and execution preserves the handler's result.
static i64 CmdAllInventions(City *city, u32 flags, TileIndex location, int parameter1, int parameter2, int parameter3) {
  (void)location;
  int invalid = CITY_COMMAND_COST_INVALID;
  if (!(city->fixes & (1 << CITY_FIX_ALL_INVENTIONS)) || parameter1 || parameter2 ||
      parameter3) return invalid;
  if (flags & CITY_COMMAND_TEST) return 0;
  memset(&city->economic_tables.milestone_years, 0, sizeof(city->economic_tables.milestone_years));
  RebuildCityUnlockState(city);
  city->cheats.used = 1;
  PlayCitySound(city, INVALID_TILE, CITY_SOUND_CIVIC_PLACEMENT);
  return true;
}

// Set the selected Boolean simulation rule from an agreed command value. Rejects non-Boolean values and reserved
// arguments; TEST only validates, while execution changes shared City rules.
static i64 CmdSetSimulationOptions(City *city, u32 flags, TileIndex location,
    CityCommandId id, int parameter1, int parameter2, int parameter3) {
  (void)location;
  if ((parameter1 != 0 && parameter1 != 1) || parameter2 || parameter3)
    return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) return 0;
  if (id == CITY_COMMAND_SET_TREE_LIFECYCLE) city->tree_lifecycle = parameter1 != 0;
  else if (id == CITY_COMMAND_SET_INDIVIDUAL_FACILITIES)
    return SetCityIndividualFacilities(city, parameter1 != 0) ? true : CITY_COMMAND_COST_BLOCKED;
  else city->protect_terrain_structures = parameter1 != 0;
  return true;
}

// Enable or disable one gameplay-affecting runtime fix. Rejects presentation-only fixes, invalid indices and reserved
// arguments; TEST has no effects and execution updates City deterministically.
static i64 CmdSetFix(City *city, u32 flags, TileIndex location, int parameter1, int parameter2, int parameter3) {
  (void)location;
  int invalid = CITY_COMMAND_COST_INVALID;
  if ((u32)parameter1 >= CITY_FIX_COUNT ||
      !(CITY_FIX_SIMULATION_MASK & (1ull << parameter1)) || (u32)parameter2 > 1 || parameter3)
    return invalid;
  if (flags & CITY_COMMAND_TEST) return 0;
  if (parameter2) city->fixes |= 1ull << parameter1;
  else city->fixes &= ~(1ull << parameter1);
  if (parameter1 == CITY_FIX_ASSET_VALUES || parameter1 == CITY_FIX_TREATMENT_VALUE ||
      parameter1 == CITY_FIX_DAILY_VALUATION)
    RecalculateCityAssetValue(city);
  if (parameter1 == CITY_FIX_LOADED_CONNECTIONS && parameter2)
    RecountCityIndustryConnections(city);
  return 1;
}

// Apply the resolved event-stage action using the legacy offset around the selected tile. Rejects unrepresentable
// offsets; TEST does not touch events and execution preserves the original offset convention.
static i64 CmdSetEventStageAtTile(City *city, u32 flags, TileIndex location) {
  CityPoint point = IsMapTileValid(city, location) ? MapTilePt(city, location) : (CityPoint){-1, -1};
  int invalid = CITY_COMMAND_COST_INVALID;
  if (point.x - 4 < -32768 || point.x - 4 > 32767 || point.y - 4 < -32768 ||
      point.y - 4 > 32767 || 0 != 0)
    return invalid;
  if (flags & CITY_COMMAND_TEST) return 0;
  SetCityMapEventStageAtTileOffset(city, point.x - 4, point.y - 4);
  return 1;
}

// Place the specified building kind with automatic orientation (-1) or an explicit orientation (0 through 3). Rejects
// invalid tiles, unsupported kinds and nonzero reserved input; TEST quotes admission, while execution preserves the
// placement result and invalidates the footprint.
static i64 CmdPlaceBuilding(City *city, u32 flags, TileIndex tile, int kind, int orientation, int reserved) {
  if (!IsMapTileValid(city, tile) || kind < 0 || kind >= TK_COUNT || !kCityBuildings[kind].size ||
      orientation < -1 || orientation > 3 || reserved ||
      (orientation >= 0 && (!IsTileKindBuilding(kind) || kind == TK_HYDRO_POWER)))
    return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) return CityCommandPriceResult(GetCityBuildingCost(city, tile, kind));
  return PlaceCityBuilding(city, tile, kind);
}

static void FinishCityBuildingCommand(City *city, TileIndex tile, int kind, int orientation) {
  FinishCityBuildingPlacement(city, tile, kind);
  if (orientation >= 0) {
    int size = kCityBuildings[kind].size;
    CityPoint point = MapTilePt(city, tile);
    TileIndex anchor = MapTileXY(city, point.x - (size > 2), point.y - (size > 2));
    InvalidateTileRange(city, anchor, size, size);
    for (int dy = 0; dy < size; ++dy) for (int dx = 0; dx < size; ++dx) {
      TileInfo *cell = &city->tile[anchor + dy * city->map_width + dx];
      WriteZoneByte(city, anchor + dy * city->map_width + dx,
          (ReadTileZoneByte(cell) & ~CITY_BUILDING_ORIENTATION_MASK) | EncodeBuildingOrientation(orientation));
    }
    InvalidateTileRange(city, anchor, size, size);
  }
  if (kind == TK_RAIL_STATION) TrySpawnTile237Events(city, tile);
  if (kCityBuildings[kind].reward) SetSpecialFacilityAvailability(city, kCityBuildings[kind].reward - 1, 0);
}

// Validate or execute a sign move using its captured source slot and destination. TEST normalizes an admitted move to
// zero cost; execution returns the sign operation's actual result or negative error.
static i64 CmdMoveSign(City *city, u32 flags, TileIndex location, int parameter1, int parameter2) {
  i64 result = CmdMoveCitySign(city, flags, location, parameter1, parameter2);
  return flags & CITY_COMMAND_TEST && result >= 0 ? 0 : result;
}

// Commit a staged map-label text answer at a valid tile. Requires an active bounded text payload and OK/cancel choice;
// TEST validates only and execution returns label-slot or placement errors.
static i64 CmdEditMapLabel(City *city, u32 flags, TileIndex location, int parameter1) {
  int invalid = CITY_COMMAND_COST_INVALID;
  if (!city->command_name_active || !IsMapTileValid(city, location) ||
      (parameter1 != CITY_DIALOG_OK && parameter1 != CITY_DIALOG_CANCEL))
    return invalid;
  if (flags & CITY_COMMAND_TEST) return 0;
  TileIndex tile = location;
  const GameObservation *observer = city->game->observation;
  i64 applied = 1;
  if (observer && observer->edit_map_label) observer->edit_map_label(city, tile);
  else applied = ApplyCityMapLabelChoice(city, tile, &city->command_name, parameter1);
  RefreshCityMapViews(city->game);
  return applied;
}

// Apply a shared reply to the matching pending question identity. Rejects unsupported reply fields or stale/unready
// questions; TEST preserves the slot and execution resumes its real continuation.
static i64 CmdReplyToQuestion(City *city, u32 flags, TileIndex location,
    int parameter1, int parameter2, int parameter3) {
  (void)location;
  if (flags & CITY_COMMAND_TEST)
    return CanReplyToCityQuestion(city, parameter1,
        (CityQuestionReply){parameter2, NULL, 0, parameter3}) ? 0 : CITY_COMMAND_COST_INVALID;
  return ReplyToCityQuestion(city, parameter1,
  (CityQuestionReply){parameter2, NULL, 0, parameter3});
}

// Commit a staged text answer for the selected facility's label. Requires the active bounded name payload and
// supported answer; TEST checks admission and execution reads the live facility state after callbacks.
static i64 CmdEditFacilityLabel(City *city, u32 flags, TileIndex location, int parameter1) {
  int invalid = CITY_COMMAND_COST_INVALID;
  if (!city->command_name_active || !IsMapTileValid(city, location) ||
      (parameter1 != CITY_DIALOG_OK && parameter1 != CITY_DIALOG_CANCEL))
    return invalid;
  const TileIndex tile_index = location;
  int slot = ReadCityFacilityOverlay(city, tile_index);
  if (!IsCityFacilityOverlay(slot))
    return invalid;
  if (flags & CITY_COMMAND_TEST) return 0;
  const GameObservation *observer = city->game->observation;
  if (observer && observer->edit_facility_label) observer->edit_facility_label(city, tile_index);
  else ApplyCityFacilityLabelChoice(city, tile_index, &city->command_name, parameter1);
  RefreshCityMapViews(city->game);
  return 1;
}

// Assign the selected sports team to the stadium at the resolved tile. Choice must be admitted by the team's available
// mask; TEST does not alter names/records and execution retains the original rejection result.
static i64 CmdAssignStadiumTeam(City *city, u32 flags, TileIndex location, int parameter1) {
  int invalid = CITY_COMMAND_COST_INVALID;
  if (!city->command_name_active || !IsMapTileValid(city, location) || parameter1 < -1 || parameter1 > 4)
    return invalid;
  const TileIndex tile_index = location;
  int slot = ReadCityFacilityOverlay(city, tile_index);
  if (!IsCityFacilityOverlay(slot) ||
      CityFacilityRecordAt(city, CityFacilityOverlaySlot(slot))->type != TK_STADIUM)
    return invalid;
  if (flags & CITY_COMMAND_TEST) return 0;
  ApplyCityStadiumTeamChoice(city, tile_index, parameter1, (const char *)city->command_name.name.character);
  return parameter1 == -1 ? -CITY_NOTICE_SELECT_VALID_STADIUM_SPORT : 1;
}

// Dispatch the explicitly selected emergency service at a valid destination tile. Unsupported service values are
// rejected; TEST is read-only, and execution preserves service availability and placement results.
static i64 CmdDispatchCityService(City *city, u32 flags, TileIndex location, int event_type) {
  int service;
  switch (event_type) {
  case CITY_MAP_EVENT_POLICE: service = CITY_DISPATCH_POLICE; break;
  case CITY_MAP_EVENT_FIRE: service = CITY_DISPATCH_FIRE; break;
  case CITY_MAP_EVENT_MILITARY: service = CITY_DISPATCH_MILITARY; break;
  default: return CITY_COMMAND_COST_INVALID;
  }
  if (!IsMapTileValid(city, location) || !CanDispatchCityService(city, service)) return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) return 0;
  DispatchCityService(city, location, service);
  return 1;
}

// Recall existing emergency units according to the shared action contract. Payload fields are reserved and must be
// zero; TEST reports admission without removing event markers or changing dispatch state.
static i64 CmdRecallDispatch(City *city, u32 flags, TileIndex location,
    int parameter1, int parameter2, int parameter3) {
  int invalid = CITY_COMMAND_COST_INVALID;
  if (location != INVALID_TILE || parameter1 || parameter2 || parameter3) return invalid;
  bool recalled = false;
  for (int slot = CITY_EVENT_SLOT_FIRST; slot < city->events.capacity; ++slot) {
    int type = city->events.event[slot].type;
    if (type != CITY_MAP_EVENT_POLICE && type != CITY_MAP_EVENT_FIRE && type != CITY_MAP_EVENT_MILITARY) continue;
    if (flags & CITY_COMMAND_TEST) return 0;
    InvalidateCityMapEvent(city, slot);
    ClearCityMapEventSlot(city, slot);
    recalled = true;
  }
  if (!recalled) return invalid;
  city->event_runtime.police_index = 0;
  city->event_runtime.fire_index = 0;
  city->event_runtime.military_index = 0;
  return 1;
}

// Demolish the structure at a valid resolved tile, optionally queuing its visual animation. Animation must be zero or
// one; TEST only checks admission, and execution reports Game error status while Drawing owns the queued pixels.
static i64 CmdBulldoze(City *city, u32 flags, TileIndex location, int parameter1) {
  int invalid = CITY_COMMAND_COST_INVALID;
  if (!IsMapTileValid(city, location) || (parameter1 != 0 && parameter1 != 1))
    return invalid;
  if (flags & CITY_COMMAND_TEST) return 0;
  ClearGameError(city->game);
  return AdvanceCityDemolition(city, location, parameter1 != 0);
}

// Execute the retained single-cell demolition action with resolved layer/options. Validates tile and allowed bits;
// TEST uses read-only error/cost probes rather than performing effects or consuming RNG.
static i64 CmdApplyBulldozerTool(City *city, u32 flags, TileIndex location,
    int parameter1) {
  int invalid = CITY_COMMAND_COST_INVALID;
  if (!IsMapTileValid(city, location) || (parameter1 & ~CITY_BULLDOZER_FLAGS_MASK) != 0)
    return invalid;
  if (flags & CITY_COMMAND_TEST) {
    bool underground = parameter1 & CITY_BULLDOZER_UNDERGROUND;
    int error = GetCityBulldozeTileError(city, location, underground, parameter1 & CITY_BULLDOZER_ALT);
    if (error) return error == CITY_COMMAND_COST_CLEAR_DEEP_WATER ? error : CITY_COMMAND_COST_BLOCKED;
    return GetCityBulldozeTileCost(city, location, underground);
  }
  ClearGameError(city->game);
  return ApplyCityBulldozerAtTile(city, location, parameter1);
}

// Demolish admitted structures in a resolved inclusive rectangle without charging a footprint twice. Validates
// endpoints/options; TEST is read-only and execution uses the same footprint admission rules.
static i64 CmdBulldozeRect(City *city, u32 flags, TileIndex location, TileIndex end, int options) {
  CityCommandCost quote = GetCityBulldozeRectangleCost(city, location, end, options);
  if (flags & CITY_COMMAND_TEST) return CityCommandPriceResult(quote);
  if (quote.error) return quote.error;
  i64 affordable = AffordableCityCommandCost(city, quote.cost);
  if (affordable < 0) return affordable;
  CityRect bounds = MapTileRectangle(city, location, end);
  bool underground = (options & CITY_BULLDOZER_UNDERGROUND) != 0;
  bool alt = (options & CITY_BULLDOZER_ALT) != 0;
  TileIndex endpoints[2] = {location, end};
  TileIndex endpoint_count = location == end ? 1 : 2;
  bool large[2];
  for (int i = 0; i < endpoint_count; ++i) {
    CityTileArea footprint = ResolveBulldozeFootprint(city, endpoints[i]);
    large[i] = footprint.width > 1 || footprint.height > 1;
  }
  bool demolished = false;
  i64 failure = 0;
  ClearGameError(city->game);
  PlayCitySoundWithDelay(city->game, CITY_SOUND_BULLDOZER_WORK, -1);
  for (int y = bounds.top; y < bounds.bottom && !GetGameError(city->game); ++y)
    for (int x = bounds.left; x < bounds.right && !GetGameError(city->game); ++x) {
      TileIndex tile = MapTileXY(city, x, y);
      if (IsCityBulldozeGroundNoop(&city->tile[tile], underground)) continue;
      if (CanBulldozeCityTile(city, tile, underground, alt)) {
        i64 result = ApplyCityBulldozerAtTile(city, tile, options | CITY_BULLDOZER_NO_EFFECT);
        if (result < 0) failure = result;
        else demolished |= result > 0;
      }
    }
  StopCitySound(city->game, CITY_SOUND_BULLDOZER_WORK);
  // Bare ground is admitted without changing tiles. Its loop otherwise starts
  // and stops within this call, so acknowledge the successful tool action once.
  if (!demolished && !failure && !GetGameError(city->game) && !(options & CITY_BULLDOZER_NO_EFFECT))
    PlayCitySound(city, location, CITY_SOUND_EXPLOSION);
  if ((demolished || (!failure && !GetGameError(city->game))) && !(options & CITY_BULLDOZER_NO_EFFECT))
    for (int i = 0; i < endpoint_count; ++i)
      ShowCityDemolitionEffect(city, endpoints[i], large[i]);
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  return failure ? failure : 1;
}

// Raise one terrain tile or move it one step toward an explicit target height. Validates the action's payload; TEST
// quotes the corresponding edit and execution preserves the zero-step and signed-error contracts.
static i64 CmdRaiseOrStepTerrain(City *city, u32 flags, TileIndex location,
    CityCommandId id, int parameter1, int parameter2, int parameter3) {
  if ((parameter2 & ~CITY_TERRAIN_OPTIONS_MASK) || parameter3)
    return CITY_COMMAND_COST_INVALID;
  int invalid = CITY_COMMAND_COST_INVALID;
  bool raise = id == CITY_COMMAND_RAISE_TERRAIN_TILE;
  int options = parameter1;
  int tool = raise ? options : (options & CITY_TERRAIN_COMMAND_TOOL_MASK) >> CITY_TERRAIN_COMMAND_TOOL_SHIFT;
  if (!IsMapTileValid(city, location) ||
      tool < CITY_DEMOLISH_CLEAR || tool > CITY_DEMOLISH_DEZONE ||
      (!raise && (options & ~CITY_TERRAIN_COMMAND_STEP_MASK) != 0))
    return invalid;
  TileIndex tile = location;
  if (flags & CITY_COMMAND_TEST) {
    if (!raise) {
      int height = ReadCityLandHeight(&city->tile[tile]);
      int target = options & CITY_TERRAIN_COMMAND_HEIGHT_MASK;
      if (height == target) return 0;
      raise = height < target;
    }
    return CityCommandPriceResult(GetCityTerrainCost(city, tile, raise, tool, parameter2));
  }
  ClearGameError(city->game);
  if (city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE)) {
    if (!raise) {
      int height = ReadCityLandHeight(&city->tile[tile]);
      int target = options & CITY_TERRAIN_COMMAND_HEIGHT_MASK;
      if (height == target) return 1;
      raise = height < target;
    }
    return EditCityTerrainCell(city, tile, raise, tool);
  }
  return raise ? RaiseCityTerrainCell(city, tile, tool) :
      StepCityTerrainTowardHeight(city, tile, options & CITY_TERRAIN_COMMAND_HEIGHT_MASK, tool);
}

// Place a road tunnel from its valid starting tile using the resolved confirmation bit. TEST returns the tunnel charge
// without editing terrain; execution retains the tunnel routine's negative-error or completion result.
static i64 CmdPlaceRoadTerrainSpan(City *city, u32 flags, TileIndex tile, int options) {
  if (!IsMapTileValid(city, tile) || (options & ~CITY_TUNNEL_CONFIRMED)) return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) {
    CityInfrastructureSpan tunnel = FindCityRoadTunnel(city, tile);
    if (tunnel.end == INVALID_TILE) return tunnel.error ? -tunnel.error : CITY_COMMAND_COST_BLOCKED;
    return (i64)tunnel.length * CITY_ROAD_TUNNEL_COST;
  }
  return PlaceCityRoadTunnel(city, tile, (options & CITY_TUNNEL_CONFIRMED) != 0);
}

// Place a highway line with explicit span, neighbor-connection and orientation choices. Rejects unsupported payload
// bits and endpoints; TEST quotes geometry and affordability without construction or UI selection reads.
static i64 CmdPlaceTransportLine(City *city, u32 flags, TileIndex location,
    CityCommandId id, int parameter1, int parameter2, int parameter3) {
  int invalid = CITY_COMMAND_COST_INVALID;
  int options = parameter2;
  int span_style = options & 255;
  if (!IsMapTileValid(city, location) || !IsMapTileValid(city, parameter1) ||
      options < 0 || (options & ~CITY_HIGHWAY_COMMAND_MASK) != 0 ||
      (options >> CITY_HIGHWAY_FACING_SHIFT) > 4 || span_style > CITY_ROAD_SPAN_CANCEL)
    return invalid;
  int orientation = (options >> CITY_HIGHWAY_FACING_SHIFT) - 1;
  TileIndex start = location, end = parameter1;
  if (IsCityCompactHighwayLine(city, start, end)) {
    i64 result = CmdBuildCityCompactHighway(city, flags, start, end, orientation,
        span_style == CITY_ROAD_SPAN_CANCEL ? CITY_SPAN_CANCEL : span_style);
    if (flags & CITY_COMMAND_TEST) return result;
    return result < 0 ? result : 1;
  }
  if (flags & CITY_COMMAND_TEST) {
    int styles = span_style == CITY_ROAD_SPAN_CANCEL ? 0 : 1 << span_style;
    CityCommandCost quote = GetCityInfrastructureCost(city, start, end, CITY_INFRASTRUCTURE_HIGHWAY, styles, NULL);
    if (options & 256) quote.cost += GetCityLineConnectionCost(city, start, end, CITY_INFRASTRUCTURE_HIGHWAY);
    return CityCommandPriceResult(quote);
  }

  return ExecuteEndpointLineCommand(city->game, id, location, parameter1);
}

// Apply the supplied ZoneKind to an inclusive rectangle with resolved endpoints. Invalid endpoints and zone codes are
// rejected; TEST only quotes cost, and execution updates the construction working state before committing eligible
// cells.
static i64 CmdPlaceZoneRect(City *city, u32 flags, TileIndex tile, int zone, TileIndex end) {
  if (!IsMapTileValid(city, tile) || !IsMapTileValid(city, end) ||
      zone < 0 || zone >= ZONE_RESERVED_FIRST || kCityZoneCosts[zone] < 0) return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) return CityCommandPriceResult(GetCityZoneRectangleCost(city, tile, end, zone));
  return PlaceCityZoneRectangleAt(city, tile, end, zone);
}

// Construct a road, rail, power, pipe or subway line selected by the action ID. Endpoints and family-specific option
// bits are validated; TEST is read-only and execution uses resolved span/border choices without consulting UI tools.
static i64 CmdPlaceInfrastructureLine(City *city, u32 flags, TileIndex start,
    CityCommandId id, TileIndex end, int options, int reserved) {
  if (!IsMapTileValid(city, start) || !IsMapTileValid(city, end) || reserved) return CITY_COMMAND_COST_INVALID;
  CityInfrastructureKind kind;
  int allowed = 0;
  switch (id) {
  case CITY_COMMAND_PLACE_ROAD:
    kind = CITY_INFRASTRUCTURE_ROAD;
    allowed = CITY_LINE_ACCEPT_BORDER | 65280;
    if (DecodeCityRoadSpanChoice((options >> 8) & 255) < CITY_SPAN_CANCEL) return CITY_COMMAND_COST_INVALID;
    break;
  case CITY_COMMAND_PLACE_RAIL:
    kind = CITY_INFRASTRUCTURE_RAIL;
    allowed = CITY_LINE_ACCEPT_BORDER | CITY_LINE_CANCEL_SPAN;
    break;
  case CITY_COMMAND_PLACE_POWER_LINE:
    kind = CITY_INFRASTRUCTURE_POWER;
    allowed = CITY_LINE_CANCEL_SPAN;
    break;
  case CITY_COMMAND_PLACE_WATER_PIPE: kind = CITY_INFRASTRUCTURE_PIPE; break;
  default: kind = CITY_INFRASTRUCTURE_SUBWAY; break;
  }
  if (options & ~allowed) return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) {
    CityConstructionChoices choices = ReadCityConstructionChoices(id, end, options);
    int styles = choices.supplied ? choices.span < 0 ? 0 : 1 << choices.span : -1;
    CityCommandCost quote = GetCityInfrastructureCost(city, start, end, kind, styles, NULL);
    if (quote.error) return CityCommandPriceResult(quote);
    if (choices.border) quote.cost += GetCityLineConnectionCost(city, start, end, kind);
    return quote.cost;
  }
  i64 placed = ExecuteEndpointLineCommand(city->game, id, start, end);
  if (placed > 0 && city->indicators.population < 50000) {
    if (kind == CITY_INFRASTRUCTURE_POWER) ScanPowerGridPhase(city);
    if (kind == CITY_INFRASTRUCTURE_PIPE) ScanCityMapPhase20(city);
  }
  return placed;
}

// Connect an eligible road cell to its neighboring highway and charge the onramp price. Rejects unused parameters and
// unsuitable tiles; TEST performs admission and affordability only.
static i64 CmdPlaceHighwayOnramp(City *city, u32 flags, TileIndex location,
    int parameter1) {
  int invalid = CITY_COMMAND_COST_INVALID;
  if (!IsMapTileValid(city, location) || parameter1 != 0)
    return invalid;
  if (flags & CITY_COMMAND_TEST)
    return CanPlaceCityOnramp(city, location) ? CITY_ONRAMP_COST :
        CITY_COMMAND_COST_BLOCKED;
  return PlaceCityHighwayOnramp(city, location);
}

// Join eligible surface rail to a subway connection at the resolved tile. Rejects unused parameters and unsuitable
// terrain; TEST is read-only and a successful connection keeps its original zero-cost contract.
static i64 CmdPlaceSubwayRailConnection(City *city, u32 flags, TileIndex location,
    int parameter1) {
  int invalid = CITY_COMMAND_COST_INVALID;
  if (!IsMapTileValid(city, location) || parameter1 != 0)
    return invalid;
  if (flags & CITY_COMMAND_TEST)
    return CanPlaceCitySubwayRailConnection(city, location) ? 0 : CITY_COMMAND_COST_BLOCKED;
  return PlaceCitySubwayRailConnection(city, location);
}

// Apply one directional step to an admitted budget category. Rejects unsupported direction/category and reserved
// fields; TEST does not change rates, while execution updates the matching shared sector values.
static i64 CmdBudgetRate(City *city, u32 flags, TileIndex location, int parameter1, int parameter2, int parameter3) {
  (void)location;
  int invalid = CITY_COMMAND_COST_INVALID;
  const CityBudgetState *session = FindCityBudget(city, parameter3);
  if (parameter1 < 0 || parameter1 >= CITY_BUDGET_CATEGORY_COUNT ||
      (parameter2 != -1 && parameter2 != 1) ||
      (parameter3 != 0 && (!session || session->close_requested)))
    return invalid;
  if (flags & CITY_COMMAND_TEST) return 0;
  RestartCityBudgetHelperAfterRateEdit(city, parameter3);
  AdjustCityBudgetRate(city, parameter1, parameter2);
  RefreshBudgetDialogEstimates(city);
  return 1;
}

// Direct values are shared City input; no local budget window or sound is involved. CITY_COMMAND_TEST queries
// admission without applying effects; execution returns the action result or a negative error.
static i64 CmdSetBudgetRate(City *city, u32 flags, TileIndex location, int kind, int rate, int options) {
  (void)location;
  int maximum = kind <= CITY_BUDGET_RATE_INDUSTRIAL ? 20 : 100;
  if (kind < 0 || kind >= CITY_BUDGET_RATE_COUNT || rate < 0 || rate > maximum || options)
    return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) return 0;
  CityEconomyMonths *economy = &city->economy;
  switch (kind) {
  case CITY_BUDGET_RATE_RESIDENTIAL:
  case CITY_BUDGET_RATE_COMMERCIAL:
    economy->sector[kind].current_rate = rate;
    break;
  case CITY_BUDGET_RATE_INDUSTRIAL:
    for (int industry = 0; industry < CITY_INDUSTRY_COUNT; ++industry) city->industry_baseline.value[industry] = rate;
    AdjustIndustryTaxRates(city, 0);
    break;
  case CITY_BUDGET_RATE_POLICE:
  case CITY_BUDGET_RATE_FIRE:
  case CITY_BUDGET_RATE_HEALTH:
    economy->sector[CITY_ECONOMY_POLICE + kind - CITY_BUDGET_RATE_POLICE].current_rate = rate;
    break;
  case CITY_BUDGET_RATE_EDUCATION:
    economy->sector[CITY_ECONOMY_SCHOOLS].current_rate = rate;
    economy->sector[CITY_ECONOMY_COLLEGES].current_rate = rate;
    break;
  case CITY_BUDGET_RATE_TRANSIT:
    for (int sector = CITY_ECONOMY_ROADS; sector < CITY_ECONOMY_SECTOR_COUNT; ++sector)
      economy->sector[sector].current_rate = rate;
    break;
  default: break;
  }
  RefreshBudgetDialogEstimates(city);
  return 1;
}

// Set the agreed industry-tax baseline within the supported percentage range. Rejects reserved payload and
// out-of-range values; TEST has no effects and execution updates shared tax calculations.
static i64 CmdSetIndustryTaxBaseline(City *city, u32 flags, TileIndex location,
    int parameter1, int parameter2, int parameter3) {
  (void)location;
  int invalid = CITY_COMMAND_COST_INVALID;
  int industry = parameter1;
  int rate = parameter2;
  if ((industry < 0 && industry != -1) || industry >= CITY_INDUSTRY_COUNT || rate < 0 || rate > 20 ||
      parameter3 != 0)
    return invalid;
  if (flags & CITY_COMMAND_TEST) return 0;
  if (industry == -1) {
    for (int index = 0; index < CITY_INDUSTRY_COUNT; ++index)
      city->industry_baseline.value[index] = rate;
    AdjustIndustryTaxRates(city, 0);
  } else if (city->industry_baseline.value[industry] != rate) {
    city->industry_baseline.value[industry] = rate;
    AdjustIndustryTaxRates(city, 0);
  }
  return 1;
}

// Toggle an admitted city ordinance and rebuild its availability/cost effects. Payload selects a valid policy and
// contains no local window identity; TEST validates without changing rates, unlocks or policy state.
static i64 CmdToggleCityPolicy(City *city, u32 flags, TileIndex location,
    int parameter1, int parameter2, int parameter3) {
  (void)location;
  int invalid = CITY_COMMAND_COST_INVALID;
  int policy = parameter1;
  if (policy < 0 || policy >= CITY_ORDINANCE_COUNT || parameter2 != 0 || parameter3 != 0)
    return invalid;
  if (flags & CITY_COMMAND_TEST) return 0;
  ToggleCityBudgetPolicy(city, policy);
  if (!city->game->observation) {
    RebuildCityPolicyAvailability(city);
    InitializeCityOrdinancesDialog(city);
  }
  return 1;
}

// Consume the explicit military/disaster reward answer on the supported shared path. Rejects unsupported
// answers/reserved input; TEST leaves the pending choice and City unchanged.
static i64 CmdDisasterUnlockAnswer(City *city, u32 flags, TileIndex location,
    int parameter1, int parameter2, int parameter3) {
  (void)location;
  int invalid = CITY_COMMAND_COST_INVALID;
  if ((parameter1 != CITY_DIALOG_UNANSWERED && parameter1 != CITY_DIALOG_YES &&
          parameter1 != CITY_DIALOG_NO) ||
      parameter2 != 0 || parameter3 != 0)
    return invalid;
  if (flags & CITY_COMMAND_TEST) return 0;
  Sc2kQueueUnlockBonusAnswer(city, parameter1);
  return 1;
}

// Issue one admitted bond using the resolved confirmation policy. TEST checks live eligibility without changing
// valuation, cash or debt; execution preserves rejection and the original bond effect contract.
static i64 CmdIssueBond(City *city, u32 flags, TileIndex location,
    CityCommandId id, int parameter1, int parameter2, int parameter3) {
  CityCommand recorded_command = EncodeCityCommand(city, id, location, parameter1, parameter2, parameter3);
  int invalid = CITY_COMMAND_COST_INVALID;
  if (parameter1 < -32768 || parameter1 > 32767 ||
      (parameter2 != CITY_BOND_CONFIRMED && parameter2 != CITY_BOND_ASK) ||
      parameter3 != 0)
    return invalid;
  if (!(flags & CITY_COMMAND_TEST) && city->game->observation && city->game->observation->execute_bond_command)
    return city->game->observation->execute_bond_command(city, &recorded_command);
  if (!CanCityIssueBond(city)) return CITY_COMMAND_COST_BLOCKED;
  if (parameter2 == CITY_BOND_ASK)
    return flags & CITY_COMMAND_TEST ? CITY_COMMAND_NEEDS_CONFIRMATION : CITY_COMMAND_COST_REJECTED;
  if (flags & CITY_COMMAND_TEST) return 0;
  ApplyCityBondIssue(city, city->fixes & (1 << CITY_FIX_BOND_RATING) ? CityBondInterestRate(city) : parameter1);
  return 1;
}

// Repay the oldest admitted bond using the resolved confirmation policy. TEST checks funds and availability without
// mutation; execution retains the captured rate across any supported confirmation continuation.
static i64 CmdRedeemBond(City *city, u32 flags, TileIndex location,
    CityCommandId id, int parameter1, int parameter2, int parameter3) {
  CityCommand recorded_command = EncodeCityCommand(city, id, location, parameter1, parameter2, parameter3);
  int invalid = CITY_COMMAND_COST_INVALID;
  if (parameter1 != 0 ||
      (parameter2 != CITY_BOND_CONFIRMED && parameter2 != CITY_BOND_ASK) || parameter3 != 0 ||
      city->indicators.bond_count == 0)
    return invalid;
  if (!(flags & CITY_COMMAND_TEST) && city->game->observation && city->game->observation->execute_bond_command)
    return city->game->observation->execute_bond_command(city, &recorded_command);
  if (city->indicators.cash < 10000) return CITY_COMMAND_COST_TOO_EXPENSIVE;
  if (parameter2 == CITY_BOND_ASK)
    return flags & CITY_COMMAND_TEST ? CITY_COMMAND_NEEDS_CONFIRMATION : CITY_COMMAND_COST_REJECTED;
  if (flags & CITY_COMMAND_TEST) return 10000;
  ApplyCityBondRedemption(city, city->economic_tables.bond_rates.rate[0]);
  return 1;
}

// Found the configured city on the edited map. Admission is independent of local tools and windows.
static i64 CmdStartCity(City *city, u32 flags, TileIndex tile, int parameter1, int parameter2, int parameter3) {
  if (tile != INVALID_TILE || parameter1 || parameter2 || parameter3) return CITY_COMMAND_COST_INVALID;
  if (city->settings.timer_mode != CITY_TIMER_STOPPED || !city->name.length) return CITY_COMMAND_COST_BLOCKED;
  if (flags & CITY_COMMAND_TEST) return 0;
  RecordRankedCityEvent(city, CITY_NEWS_CITY_FOUNDED, 0);
  city->geography.settings_preserved = 0;
  SetCityTimerMode(city, CITY_TIMER_RUNNING);
  return 1;
}

// Set an agreed valid City speed independently of local dialog pauses. Rejects unsupported speed/reserved values; TEST
// validates only and execution refreshes speed presentation after the shared change.
static i64 CmdSetSimulationSpeed(City *city, u32 flags, TileIndex location,
    int parameter1, int parameter2, int parameter3) {
  (void)location;
  int invalid = CITY_COMMAND_COST_INVALID;
  if (parameter1 < CITY_SPEED_PAUSED || parameter1 > CITY_SPEED_UNLIMITED ||
      parameter2 != 0 || parameter3 != 0)
    return invalid;
  if (flags & CITY_COMMAND_TEST) return 0;
  SelectCitySimulationSpeed(city, parameter1);
  return 1;
}

// Apply the fixed shared cash bonus with clamped treasury arithmetic. Payload is reserved and must be zero; TEST
// reports admission without crediting cash.
static i64 CmdAddCityCashBonus(City *city, u32 flags, TileIndex location,
    int parameter1, int parameter2, int parameter3) {
  (void)location;
  int invalid = CITY_COMMAND_COST_INVALID;
  if (parameter1 != 0 || parameter2 != 0 || parameter3 != 0)
    return invalid;
  if (flags & CITY_COMMAND_TEST) return 0;
  Add500000ToCityCash(city);
  return 1;
}

// Toggle the City's shared random-disaster suppression rule. Reserved payload must be zero; TEST changes neither City
// nor local preferences.
static i64 CmdToggleNoDisasters(City *city, u32 flags, TileIndex location,
    int parameter1, int parameter2, int parameter3) {
  (void)location;
  int invalid = CITY_COMMAND_COST_INVALID;
  if (parameter1 != 0 || parameter2 != 0 || parameter3 != 0)
    return invalid;
  if (flags & CITY_COMMAND_TEST) return 0;
  city->indicators.event_flag = city->indicators.event_flag == 0;
  return 1;
}

// Queue the explicitly selected disaster through its deterministic selector. Rejects unsupported kinds and reserved
// input; TEST performs no location selection or random draws.
static i64 CmdStartDisaster(City *city, u32 flags, TileIndex location,
    int kind, int parameter2, int parameter3) {
  int invalid = CITY_COMMAND_COST_INVALID;
  static const struct { int kind; void (*select)(City *); } kSelections[] = {
    {CITY_DISASTER_FIRE, SelectCityFireDisaster},
    {CITY_DISASTER_FLOOD, SelectCityFloodDisaster},
    {CITY_DISASTER_SCHEDULED_PLANE_CRASH, SelectCityAirCrashDisaster},
    {CITY_DISASTER_TORNADO, SelectCityTornadoDisaster},
    {CITY_DISASTER_EARTHQUAKE, SelectCityEarthquakeDisaster},
    {CITY_DISASTER_MONSTER, SelectCityMonsterDisaster},
    {CITY_DISASTER_HURRICANE, SelectCityHurricaneDisaster},
    {CITY_DISASTER_RIOTS, SelectCityRiotsDisaster},
    {CITY_DISASTER_MICROWAVE, SelectCityMicrowaveDisaster},
    {CITY_DISASTER_POLLUTION, SelectCityPollutionDisaster},
    {CITY_DISASTER_VOLCANO, SelectCityVolcanoDisaster},
    {CITY_DISASTER_FIRE_STORM, SelectCityFireStormDisaster},
    {CITY_DISASTER_MAJOR_FLOOD, SelectCityMajorFloodDisaster},
    {CITY_DISASTER_MASS_RIOTS, SelectCityMassRiotsDisaster},
    {CITY_DISASTER_MELTDOWN, SelectCityMeltdownDisaster},
  };
  if (!parameter2 && !parameter3) for (int index = 0; index < sizeof(kSelections) / sizeof(*kSelections); ++index) {
    if (kind != kSelections[index].kind) continue;
    if (flags & CITY_COMMAND_TEST) return 0;
    kSelections[index].select(city);
    return 1;
  }
  if (location != INVALID_TILE || parameter2 || parameter3 ||
      (kind != CITY_DISASTER_CRASH && kind != CITY_DISASTER_HELICOPTER_CRASH &&
       kind != CITY_DISASTER_CHEMICAL_SPILL)) return invalid;
  TileIndex tile = MapTileXYOrInvalid(city, city->geography.center_x, city->geography.center_y);
  if (tile == INVALID_TILE) return invalid;
  int slot = kind == CITY_DISASTER_CHEMICAL_SPILL ? 0 : FindFreeCityEventSlot(city);
  if (kind != CITY_DISASTER_CHEMICAL_SPILL && !slot) return invalid;
  if (flags & CITY_COMMAND_TEST) return 0;
  if (slot) {
    CityMapEvent *event = &city->events.event[slot];
    memset(event, 0, sizeof(*event));
    event->tile = tile;
    event->previous_overlay = ReadCityStockBaseOverlay(city, tile);
    if (kind == CITY_DISASTER_HELICOPTER_CRASH) {
      event->type = CITY_MAP_EVENT_HELICOPTER;
      event->state = event->next_slot = CITY_HELICOPTER_CRASH;
      event->height = 16;
    } else {
      event->type = CITY_MAP_EVENT_EXPLOSION;
      event->next_slot = CITY_DISASTER_CRASH;
      event->direction = CITY_EXPLOSION_START;
      event->explosion.trigger_disaster = 1;
    }
    LinkCityMapEvent(city, slot);
    InvalidateCityMapEvent(city, slot);
  }
  SetDisasterLocation(&city->disasters.location, city->geography.center_x, city->geography.center_y);
  city->disasters.pending_type = kind;
  return 1;
}

// Toggle automatic budget handling through the shared action path. All payload fields are reserved and must be zero;
// TEST reports admission without changing the shared flag or local preferences.
static i64 CmdToggleAutoBudget(City *city, u32 flags, TileIndex location,
    int parameter1, int parameter2, int parameter3) {
  (void)location;
  if (parameter1 || parameter2 || parameter3) return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) return 0;
  city->indicators.auto_budget_enabled = city->indicators.auto_budget_enabled == 0;
  return 1;
}

// Lower one admitted terrain tile with the explicit charge/tool contract. Rejects invalid tile/payload; TEST quotes
// the selected charging behavior and execution preserves uncharged lowering semantics.
static i64 CmdLowerTerrainTile(City *city, u32 flags, TileIndex location,
    int parameter1, int parameter2, int parameter3) {
  if ((parameter2 & ~CITY_TERRAIN_OPTIONS_MASK) || parameter3)
    return CITY_COMMAND_COST_INVALID;
  int invalid = CITY_COMMAND_COST_INVALID;
  int options = parameter1;
  int tool = (options & CITY_TERRAIN_COMMAND_TOOL_MASK) >> CITY_TERRAIN_COMMAND_TOOL_SHIFT;
  if (!IsMapTileValid(city, location) ||
      (options & ~CITY_TERRAIN_COMMAND_LOWER_MASK) != 0 || tool > CITY_DEMOLISH_DEZONE)
    return invalid;
  TileIndex tile = location;
  if (flags & CITY_COMMAND_TEST) {
    CityCommandCost quote = GetCityTerrainCost(city, location, false, tool, parameter2);
    if (quote.error) return CityCommandPriceResult(quote);
    return options & CITY_TERRAIN_COMMAND_CHARGE ? quote.cost : 0;
  }
  ClearGameError(city->game);
  if (city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE)) return EditCityTerrainCell(city, tile, false, tool);
  return LowerCityTerrainCell(city, tile, (options & CITY_TERRAIN_COMMAND_CHARGE) != 0, tool);
}

// Apply the shared reward-unlock and cash grant action. Requires zero reserved payload; TEST produces no reward,
// treasury or presentation effects.
static i64 CmdGrantSpecialFacilitiesAndCash(City *city, u32 flags, TileIndex location,
    int parameter1, int parameter2, int parameter3) {
  (void)location;
  int invalid = CITY_COMMAND_COST_INVALID;
  if (parameter1 != 0 || parameter2 != 0 || parameter3 != 0)
    return invalid;
  if (flags & CITY_COMMAND_TEST) return 0;
  GrantAllSpecialFacilitiesAndCash(city);
  return GetGameError(city->game) ? CITY_COMMAND_COST_REJECTED : 1;
}

// Apply an explicit answer to the retained military offer and continue its result path. Validates answer/payload
// before effects; TEST neither consumes the answer nor searches or zones a military site.
static i64 CmdAnswerMilitaryOffer(City *city, u32 flags, TileIndex location, int token, int answer, int reserved) {
  const CityTickContinuation *work = FindCityTickContinuation(city, token);
  if (location != INVALID_TILE || !work || work->dialog != CITY_TICK_DIALOG_MILITARY_OFFER || reserved ||
      (answer != CITY_DIALOG_YES && answer != CITY_DIALOG_NO))
    return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) return 0;
  return ResumeCityTick(city, token, true, answer);
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
    return CmdCheat(city, flags, location, parameter1, parameter2, parameter3);
  case CITY_COMMAND_ENABLE_DEBUG_CITY:
    return CmdEnableDebugCity(city, flags, location, parameter1, parameter2, parameter3);
  case CITY_COMMAND_ALL_INVENTIONS:
    return CmdAllInventions(city, flags, location, parameter1, parameter2, parameter3);
  case CITY_COMMAND_SET_TREE_LIFECYCLE:
  case CITY_COMMAND_SET_INDIVIDUAL_FACILITIES:
  case CITY_COMMAND_SET_TERRAIN_PROTECTION:
    return CmdSetSimulationOptions(city, flags, location, id, parameter1, parameter2, parameter3);
  case CITY_COMMAND_PLACE_BUILDING:
    return CmdPlaceBuilding(city, flags, location, parameter1, parameter2, parameter3);
  case CITY_COMMAND_SET_FIX:
    return CmdSetFix(city, flags, location, parameter1, parameter2, parameter3);
  case CITY_COMMAND_SET_EVENT_STAGE_AT_TILE:
    return CmdSetEventStageAtTile(city, flags, location);
  case CITY_COMMAND_MOVE_SIGN:
    return CmdMoveSign(city, flags, location, parameter1, parameter2);
  case CITY_COMMAND_EDIT_MAP_LABEL:
    return CmdEditMapLabel(city, flags, location, parameter1);
  case CITY_COMMAND_REPLY_TO_QUESTION:
    return CmdReplyToQuestion(city, flags, location, parameter1, parameter2, parameter3);
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
    return CmdRaiseOrStepTerrain(city, flags, location, id, parameter1, parameter2, parameter3);
  case CITY_COMMAND_ADJUST_TERRAIN_HEIGHT:
    return CmdAdjustTerrainHeight(city, flags, location, parameter1);
  case CITY_COMMAND_PLACE_ROAD_TERRAIN_SPAN:
    return CmdPlaceRoadTerrainSpan(city, flags, location, parameter1);
  case CITY_COMMAND_PLACE_TRANSPORT_LINE:
    return CmdPlaceTransportLine(city, flags, location, id, parameter1, parameter2, parameter3);
  case CITY_COMMAND_PLACE_ZONE_RECT:
    return CmdPlaceZoneRect(city, flags, location, parameter1, parameter2);
  case CITY_COMMAND_PLACE_WATER_PIPE:
  case CITY_COMMAND_PLACE_SUBWAY:
  case CITY_COMMAND_PLACE_ROAD:
  case CITY_COMMAND_PLACE_RAIL:
  case CITY_COMMAND_PLACE_POWER_LINE:
    return CmdPlaceInfrastructureLine(city, flags, location, id, parameter1, parameter2, parameter3);
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
    return CmdDisasterUnlockAnswer(city, flags, location, parameter1, parameter2, parameter3);
  case CITY_COMMAND_ISSUE_BOND:
    return CmdIssueBond(city, flags, location, id, parameter1, parameter2, parameter3);
  case CITY_COMMAND_REDEEM_BOND:
    return CmdRedeemBond(city, flags, location, id, parameter1, parameter2, parameter3);
  case CITY_COMMAND_SET_SIMULATION_SPEED:
    return CmdSetSimulationSpeed(city, flags, location, parameter1, parameter2, parameter3);
  case CITY_COMMAND_ADD_CITY_CASH_BONUS:
    return CmdAddCityCashBonus(city, flags, location, parameter1, parameter2, parameter3);
  case CITY_COMMAND_TOGGLE_NO_DISASTERS:
    return CmdToggleNoDisasters(city, flags, location, parameter1, parameter2, parameter3);
  case CITY_COMMAND_TOGGLE_AUTO_BUDGET:
    return CmdToggleAutoBudget(city, flags, location, parameter1, parameter2, parameter3);
  case CITY_COMMAND_CARVE_WATER_CHANNEL:
    return CmdCarveWaterChannel(city, flags, location, parameter1);
  case CITY_COMMAND_LOWER_TERRAIN_TILE:
    return CmdLowerTerrainTile(city, flags, location, parameter1, parameter2, parameter3);
  case CITY_COMMAND_APPLY_WATER_TILE:
    return CmdApplyWaterTile(city, flags, location, parameter1);
  case CITY_COMMAND_GRANT_SPECIAL_FACILITIES_AND_CASH:
    return CmdGrantSpecialFacilitiesAndCash(city, flags, location, parameter1, parameter2, parameter3);
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
