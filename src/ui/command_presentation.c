#include "ui/commands.h"
#include "base/text.h"

#include <stdio.h>
#include <stdlib.h>

#include "audio/types.h"
#include "city/economy/budget.h"
#include "city/engine/commands.h"
#include "city/infrastructure/bridges.h"
#include "city/infrastructure/types.h"
#include "city/presentation.h"
#include "city/state.h"
#include "city/tools/tools.h"
#include "city/tools/types.h"
#include "game/audio.h"
#include "game/commands.h"
#include "game/errors.h"
#include "game/observation.h"
#include "game/state.h"
#include "ui/dialogs/alerts.h"
#include "ui/dialogs/budget.h"
#include "ui/dialogs/dialogs.h"
#include "ui/dialogs/messages.h"
#include "ui/state.h"
#include "ui/text/text.h"
#include "ui/windows/windows.h"

// Player command presentation: cues, resolved confirmation choices and queued result notices.
// Simulation continuations and their retained notice windows belong to dialogs/presentation.c.

// Choose the construction cue from the resolved action and its result. The caller controls feedback eligibility;
// event music for successful recreation buildings starts before the placement effect.
void PresentCityCommandAudio(City *city, CityCommandId id, const CityCommandArguments *args,
    bool succeeded, i64 cost) {
  Game *game = city->game;
  int sound = CITY_SOUND_CONSTRUCTION;
  bool present = true;
  switch (id) {
  case CITY_COMMAND_PLACE_POWER_LINE:
    sound = CITY_SOUND_POWER_LINES;
    break;
  case CITY_COMMAND_PLACE_BUILDING:
    switch (args->parameter1) {
    case TK_MAYORS_HOUSE: case TK_CITY_HALL: case TK_STATUE: case TK_BRAUN_LLAMA_DOME:
      sound = CITY_SOUND_CIVIC_PLACEMENT; break;
    case TK_SCHOOL: case TK_COLLEGE: case TK_LIBRARY: case TK_MUSEUM:
      sound = CITY_SOUND_EDUCATION; break;
    case TK_FIRE_STATION: sound = CITY_SOUND_FIRE_DISPATCH; break;
    case TK_PRISON: sound = CITY_SOUND_PRISON; break;
    case TK_POLICE_STATION: case TK_HOSPITAL: sound = CITY_SOUND_POLICE_OR_HOSPITAL; break;
    case TK_PARK: case TK_LARGE_PARK: case TK_STADIUM: case TK_MARINA: case TK_ZOO:
      sound = args->parameter1 == TK_ZOO ? CITY_SOUND_SAILBOAT_OR_ZOO : CITY_SOUND_CIVIC_PLACEMENT;
      if (succeeded) MaybePlayCityEventMusic(game, CITY_MUSIC_TRACK_10);
      break;
    default: break;
    }
    break;
  case CITY_COMMAND_PLACE_ZONE_RECT:
    present = args->parameter1 != ZONE_NONE;
    if (args->parameter1 != ZONE_SEAPORT && args->parameter1 != ZONE_AIRPORT) sound = CITY_SOUND_TREE_OR_ZONE_PLACEMENT;
    break;
  case CITY_COMMAND_PLACE_TRANSPORT_LINE:
    present = !succeeded || cost > 0;
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
  if (present) PlayCitySound(city, args->tile, succeeded ? sound : CITY_SOUND_PLACEMENT_REJECTED);
}

static i64 CheckCityBondCommand(City *city, CityCommand command) {
  i64 result = DoCityCommand(city, command.id, CITY_COMMAND_TEST, INVALID_TILE,
      command.parameter1, command.parameter2, command.parameter3);
  if (result < 0 && result != CITY_COMMAND_NEEDS_CONFIRMATION) {
    const char *detail = command.id == CITY_COMMAND_ISSUE_BOND ?
        CityNoticeText(city->indicators.bond_count >= 50 ? CITY_NOTICE_BOND_LIMIT_REACHED :
            CITY_NOTICE_BOND_CREDIT_TOO_LOW) : city->indicators.bond_count == 0 ?
        "There are no outstanding bonds." : CityNoticeText(CITY_NOTICE_BOND_REPAYMENT_REQUIRES_FUNDS);
    OpenCityCommandErrorWindow(city->game, CityCommandFailureSummary(&command), detail, false, INVALID_TILE);
  }
  return result;
}

// Build the window without reconciling pauses or running nested UI work. The caller finishes reply/callback
// attachment before ApplicationUpdateDialogPause; the returned stack entry is borrowed only until that call.
static Window *CreateCityCommandConfirmation(City *city, CityCommand command, const char *title,
    const char *text, u32 style) {
  char *message = NULL;
  SetText(&message, text);
  int opened = Sc2kCanvasDesktopOpenConfirm(&city->game->ui->desktop, (CityRect){150, 60, 450, 168},
      title, message, style | CITY_MESSAGE_YES_NO);
  if (opened < 0) {
    free(message);
    SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    return NULL;
  }
  Window *window = &city->game->ui->desktop.windows[opened];
  window->confirmed_command = command;
  window->owned_message = message;
  return window;
}

typedef struct {
  CityConfirmationState reply;
  CityCommand command;
} CityRouteConfirmation;

i64 CheckCityConstructionCommand(City *city, CityCommand command) {
  CityCommandArguments args = DecodeCityCommand(city, command);
  i64 price = DoCityCommand(city, command.id, CITY_COMMAND_TEST, args.tile,
      args.parameter1, args.parameter2, args.parameter3);
  if (price < 0 && city->game->ui->command_errors_enabled) {
    TileIndex blocker = INVALID_TILE;
    if (command.id == CITY_COMMAND_BUILD_BRIDGE) {
      bool rail = (args.parameter2 & CITY_BRIDGE_COMMAND_RAIL) != 0;
      CityInfrastructureSpan span = FindCityBridgeSpan(city, args.tile, args.parameter1, rail);
      blocker = span.error_tile;
    }
    if (blocker == args.tile) blocker = INVALID_TILE;
    OpenCityCommandErrorWindow(city->game, CityCommandFailureSummary(&command),
        CityCommandFailureReason(price), false, blocker);
  }
  return price;
}

static void CompleteCityRouteConfirmation(City *city, void *context) {
  CityRouteConfirmation *pending = context;
  CityCommand command = pending->command;
  int answer = pending->reply.answer;
  free(pending);
  ApplicationUpdateDialogPause(city);
  if (!answer || city->game->close_requested || city->game->execution_failed) return;
  if (answer == CITY_DIALOG_YES)
    command.parameter3 |= command.id == CITY_COMMAND_PLACE_TRANSPORT_LINE ? 256 : CITY_LINE_ACCEPT_BORDER;
  if (CheckCityConstructionCommand(city, command) >= 0)
    ExecuteRecordedCityCommandWithInput(city->game, command, NULL, 0);
}

void PresentCityNeighborConfirmation(City *city, CityCommand command, int cost) {
  CityRouteConfirmation *pending = calloc(1, sizeof(*pending));
  if (!pending) {
    SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    return;
  }
  pending->command = command;
  char text[128];
  char currency[CITY_CURRENCY_TEXT_CAPACITY];
  FormatCityCurrency(city->game, currency, cost);
  snprintf(text, sizeof(text), "Connect this route to the neighboring city for %s?", currency);
  Window *window = CreateCityCommandConfirmation(city, (CityCommand){0},
      "Neighbor connection", text, CITY_MESSAGE_ERROR);
  if (!window) {
    free(pending);
    return;
  }
  if (!AttachCityWindowCallback(window, CompleteCityRouteConfirmation, pending)) {
    Sc2kCanvasDesktopDismissWindow(&city->game->ui->desktop, window->window_id);
    free(pending);
    SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    return;
  }
  pending->reply.window_id = window->window_id;
  window->confirmation = &pending->reply;
  ApplicationUpdateDialogPause(city);
}

void PresentCityTunnelConfirmation(City *city, TileIndex tile) {
  int options = CITY_TUNNEL_CONFIRMED;
  CityCommand command = EncodeCityCommand(city, CITY_COMMAND_PLACE_ROAD_TERRAIN_SPAN, tile, options, 0, 0);
  i64 price = CheckCityConstructionCommand(city, command);
  if (price < 0) return;
  char text[128];
  char currency[CITY_CURRENCY_TEXT_CAPACITY];
  FormatCityCurrency(city->game, currency, price);
  snprintf(text, sizeof(text), "Build this tunnel for %s?", currency);
  if (CreateCityCommandConfirmation(city, command, "Build tunnel", text, CITY_MESSAGE_WARNING))
    ApplicationUpdateDialogPause(city);
}

void PresentCityBondConfirmation(City *city, u32 id, int rate) {
  Game *game = city->game;
  if (game->observation && game->observation->open_bond_confirmation) {
    game->observation->open_bond_confirmation(city, id, rate);
    return;
  }
  CityCommand command = {id, id == CITY_COMMAND_ISSUE_BOND ? rate : 0, CITY_BOND_ASK, 0};
  if (CheckCityBondCommand(city, command) != CITY_COMMAND_NEEDS_CONFIRMATION) return;
  bool issue = id == CITY_COMMAND_ISSUE_BOND;
  int shown_rate = issue ? (city->fixes & (1ull << CITY_FIX_BOND_RATING) ? CityBondInterestRate(city) : rate) :
      city->economic_tables.bond_rates.rate[0];
  char text[128];
  snprintf(text, sizeof(text), issue ? "Issue a $10,000 bond at %d%% annual interest?" :
      "Redeem the oldest $10,000 bond at %d%% annual interest?", shown_rate);
  command.parameter2 = CITY_BOND_CONFIRMED;
  if (CreateCityCommandConfirmation(city, command, issue ? "Issue city bond" : "Redeem city bond", text,
      CITY_MESSAGE_INFORMATION))
    ApplicationUpdateDialogPause(city);
}

void AnswerCityConfirmation(City *city, u32 window_id, bool accept) {
  CityCommand confirmed = {0};
  CityCanvasDesktop *desktop = &city->game->ui->desktop;
  for (int index = 0; index < desktop->count; ++index) {
    Window *window = &desktop->windows[index];
    if (window->window_id != window_id) continue;
    confirmed = window->confirmed_command;
    int answer = accept ? CITY_DIALOG_YES : CITY_DIALOG_NO;
    if (window->confirmation) window->confirmation->answer = answer;
    if (confirmed.id == CITY_COMMAND_DISASTER_UNLOCK_ANSWER) confirmed.parameter1 = answer;
    break;
  }
  Sc2kCanvasDesktopDismissWindow(&city->game->ui->desktop, window_id);
  ApplicationUpdateDialogPause(city);
  if (confirmed.id == CITY_COMMAND_DISASTER_UNLOCK_ANSWER)
    ExecuteRecordedCityCommandWithInput(city->game, confirmed, NULL, 0);
  else if (confirmed.id == CITY_COMMAND_ISSUE_BOND || confirmed.id == CITY_COMMAND_REDEEM_BOND) {
    if (accept && CheckCityBondCommand(city, confirmed) >= 0)
      ExecuteRecordedCityCommandWithInput(city->game, confirmed, NULL, 0);
    ApplicationBudgetSync(city);
  } else if (confirmed.id == CITY_COMMAND_CHEAT && confirmed.parameter1 == CITY_CHEAT_MILITARY) {
    confirmed.parameter2 = accept ? CITY_CHEAT_ACCEPT_MILITARY : CITY_CHEAT_DECLINE_MILITARY;
    ExecuteRecordedCityCommandWithInput(city->game, confirmed, NULL, 0);
  } else if (accept && confirmed.id != 0) {
    ExecuteRecordedCityCommandWithInput(city->game, confirmed, NULL, 0);
  } else if (!accept && confirmed.id == CITY_COMMAND_CHEAT) {
    PresentCityCheatNotice(city, "Oh, go on...");
  }
}

void ClearCityResourceNotices(CityInterfaceState *ui) {
  CityResourceNotice *notice = ui->resource_notices;
  ui->resource_notices = ui->resource_notice_tail = NULL;
  // The open window's callback still owns its current notice.
  if (notice && notice->window_id) {
    CityResourceNotice *next = notice->next;
    notice->next = NULL;
    notice = next;
  }
  DiscardCityCommandNotices(notice);
}

static void OpenNextCityResourceNotice(City *city);

static void CompleteCityResourceNotice(City *city, void *context) {
  CityResourceNotice *notice = context;
  CityInterfaceState *ui = city->game->ui;
  bool current = ui->resource_notices == notice;
  if (current) {
    ui->resource_notices = notice->next;
    if (!notice->next) ui->resource_notice_tail = NULL;
  }
  free(notice);
  if (current) OpenNextCityResourceNotice(city);
  ApplicationUpdateDialogPause(city);
}

static void OpenNextCityResourceNotice(City *city) {
  CityInterfaceState *ui = city->game->ui;
  CityResourceNotice *notice = ui->resource_notices;
  if (!notice || notice->window_id) return;
  if (city->game->close_requested || city->game->execution_failed) {
    ClearCityResourceNotices(ui);
    return;
  }
  int slot = CreateCityResourceAlert(city, notice->bitmap, notice->text);
  u32 window = slot >= 0 ? ui->desktop.windows[slot].window_id : 0;
  if (window) slot = FocusCityCanvasWindow(&ui->desktop, window);
  if (slot < 0 || !AttachCityWindowCallback(&ui->desktop.windows[slot], CompleteCityResourceNotice, notice)) {
    if (window) Sc2kCanvasDesktopDismissWindow(&ui->desktop, window);
    ClearCityResourceNotices(ui);
    SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    return;
  }
  notice->window_id = window;
  ApplicationUpdateDialogPause(city);
}

void PresentCityCommandNotices(City *city, CityResourceNotice *notices) {
  if (!notices) return;
  CityInterfaceState *ui = city->game->ui;
  CityResourceNotice *ordered = NULL, *tail = notices;
  while (notices) {
    CityResourceNotice *next = notices->next;
    notices->next = ordered;
    ordered = notices;
    notices = next;
  }
  if (ui->resource_notice_tail) ui->resource_notice_tail->next = ordered;
  else ui->resource_notices = ordered;
  ui->resource_notice_tail = tail;
  OpenNextCityResourceNotice(city);
}

void PresentCityCheatNotice(City *city, const char *text) {
  CityInterfaceState *ui = city->game->ui;
  int opened = Sc2kCanvasDesktopOpenNotice(&ui->desktop, (CityRect){125, 100, 475, 220}, "Message", text,
      CITY_MESSAGE_INFORMATION | CITY_MESSAGE_OK);
  if (opened < 0) return;
  ApplicationUpdateDialogPause(city);
}

i64 RequestCityCheat(Game *game, CityCheat cheat) {
  i64 result = DoCityCommand(game->city, CITY_COMMAND_CHEAT, CITY_COMMAND_TEST, INVALID_TILE, cheat, 0, 0);
  if (result == CITY_COMMAND_NEEDS_CONFIRMATION) {
    bool military = cheat == CITY_CHEAT_MILITARY;
    int options = military ? CITY_CHEAT_ACCEPT_MILITARY : CITY_CHEAT_CONFIRM_DEMOLITION;
    const char *text = military ? CityNoticeText(CITY_NOTICE_MILITARY_BASE_OFFER) : "Tea, Father?";
    if (!CreateCityCommandConfirmation(game->city, (CityCommand){CITY_COMMAND_CHEAT, cheat, options, 0},
        military ? "Military base" : "Confirm", text, CITY_MESSAGE_ERROR)) return 0;
    ApplicationUpdateDialogPause(game->city);
    return result;
  }
  return ExecuteRecordedCityCommand(game, CITY_COMMAND_CHEAT, cheat, 0, 0);
}
