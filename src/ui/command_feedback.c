#include "ui/commands.h"

#include <stdlib.h>

#include "city/maps/trees.h"
#include "city/presentation.h"
#include "city/state.h"
#include "game/errors.h"
#include "game/state.h"
#include "ui/state.h"

// Collect presentation details in the caller's borrowed feedback scope. City owns command results;
// the command runner restores an enclosing scope after nested work and decides when to present this one.
void ReportCityCommandFailure(City *city, i64 error, TileIndex blocker) {
  if (!city->game->ui) return;
  CityCommandFeedback *feedback = city->game->ui->command_feedback;
  if (!feedback) return;
  feedback->reported = true;
  // Preserve the first refusal, but prioritize running out of money during a drag.
  if (!feedback->error || error == CITY_COMMAND_COST_TOO_EXPENSIVE) {
    feedback->error = error;
    if (error) feedback->blocker = blocker;
  }
}

void ReportCityStadiumPlacement(City *city, TileIndex tile) {
  if (!city->game->ui) return;
  CityCommandFeedback *feedback = city->game->ui->command_feedback;
  if (feedback) feedback->stadium_tile = tile;
}

void ReportCityTileInspection(City *city, TileIndex tile) {
  if (!city->game->ui) return;
  CityCommandFeedback *feedback = city->game->ui->command_feedback;
  if (feedback) feedback->inspection_tile = tile;
}

void ReportCityCommandResult(City *city, i64 error) {
  ReportCityCommandFailure(city, error, INVALID_TILE);
}

void ReportCityResourceNotice(City *city, int bitmap, int text) {
  CityCommandFeedback *feedback = city->game->ui ? city->game->ui->command_feedback : NULL;
  if (!feedback) return;
  CityResourceNotice *notice = malloc(sizeof(*notice));
  if (!notice) {
    SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    return;
  }
  *notice = (CityResourceNotice){.next = feedback->notices, .bitmap = bitmap, .text = text};
  feedback->notices = notice;
  feedback->notice_shown = true;
}

// Release a detached command-notice list that has not been opened. An active window's callback retains its own notice.
void DiscardCityCommandNotices(CityResourceNotice *notice) {
  while (notice) {
    CityResourceNotice *next = notice->next;
    free(notice);
    notice = next;
  }
}

void ReportCityCommandBlocker(City *city, TileIndex blocker) {
  CityCommandFeedback *feedback = city->game->ui ? city->game->ui->command_feedback : NULL;
  if (feedback && feedback->blocker == INVALID_TILE) feedback->blocker = blocker;
}

void CancelCityCommandFeedback(City *city) {
  if (!city->game->ui) return;
  CityCommandFeedback *feedback = city->game->ui->command_feedback;
  if (feedback) feedback->cancelled = true;
}

// Command summaries describe the resolved action. Returned wording is borrowed immutable presentation data.
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
  case CITY_COMMAND_CARVE_WATER_CHANNEL: case CITY_COMMAND_APPLY_WATER_TILE:
    return "Can't build here...";
  default:
    return "Can't complete action...";
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
