#include "drawing/sprites/sprites.h"
#include "city/economy/facilities.h"
#include "city/maps/trees.h"
#include "assets/resource_types.h"
#include "city/engine/command_types.h"
#include "city/events/news.h"
#include "city/infrastructure/types.h"
#include "city/maps/map_types.h"
#include "city/maps/tile_storage.h"
#include "city/maps/layers.h"
#include "city/presentation.h"
#include "city/tools/types.h"
#include "city/tools/tools.h"
#include "game/application.h"
#include "ui/map.h"
#include "drawing/drawing_internal.h"
#include "base/time.h"
#include "base/version.h"
#include "ui/text/text.h"
#include "ui/tools/tools.h"
#include "game/application_internal.h"
#include "city/engine/initialization.h"
#include "city/engine/questions.h"
#include "city/maps/map.h"
#include "game/errors.h"
#include "game/observation.h"
#include "game/state.h"
#include "audio/state.h"
#include "city/state.h"
#include "drawing/state.h"
#include "assets/state.h"
#include "ui/state.h"
#include "assets/catalog.h"
#include "ui/input.h"
#include "city/saveload/services.h"
#include "ui/charts/charts.h"
#include "ui/commands.h"

#include "assets/resources.h"

static u32 OpenCityMissionOutcomeWindow(City *city, int outcome);
static u32 OpenCityResourceAlert(City *city, int bitmap_id, int text_id);
static bool OpenCityNewspaperWithCallback(City *city, int edition, bool automatic,
    CityWindowCallback *callback, void *context);

void CloseCityWindowsForReplacement(Game *game) {
  CityInterfaceState *ui = game->ui;
  ui->startup_newspaper_open_at = 0;
  CloseCityManualBudget(game->city);
  CancelCitySpanCommands(game->city);
  ClearCityResourceNotices(ui);
  CloseCityMenu(&ui->desktop);
  CloseCityDropdown(&ui->desktop);
  HideCityTooltip(game);
  while (ui->desktop.count) Sc2kCanvasDesktopDismissTop(&ui->desktop);
  game->exit_requested = false;
  ApplicationUpdateDialogPause(game->city);
  // Replacement runs inside a command, where ordinary pause synchronization
  // is deferred. Finish the old windows' pause before loading the new speed.
  if (ui->dialog_pause_active && !ui->dialog_pause_requested &&
      EndCityDialogPause(game->city, 1))
    ui->dialog_pause_active = false;
}

void ApplicationRefreshMapData(City *city) {
  Drawing *drawing = city->game->drawing;
  i32 open = 0;
  for (i32 index = 0; index < city->game->ui->desktop.count; ++index)
    open |= city->game->ui->desktop.windows[index].kind == CITY_CANVAS_MAP_DATA;
  if (!open)
    return;
  int selection = ReadCityMapDataSelection(city);
  bool changed = BuildCityMapDataPreview(city, selection, drawing->data_map_pixels);
  if (changed && city->game->ui->map_data.drawing) city->game->ui->map_data.drawing->preview.valid = false;
  Sc2kCanvasDesktopUpdateMapData(&city->game->ui->desktop, selection, drawing->data_map_pixels);
}

void PresentCityTileInformationDialog(City *city, TileIndex tile) {
  if (!IsMapTileValid(city, tile)) return;
  CityInterfaceState *ui = city->game->ui;
  ApplicationUpdateDialogPause(city);
  if (CityTileHasFacilityLabel(city, tile) &&
      kFacilityClassByTile[ReadTile(city, tile)] != CITY_FACILITY_INFO_DEFAULT &&
      !(ui->advanced_query && IsAltKeyPressed(ui))) {
    int opened = FocusCityCanvasWindowKind(&ui->desktop, CITY_CANVAS_FACILITY_INFORMATION);
    int facility = CityFacilityOverlaySlot(ReadCityFacilityOverlay(city, tile));
    if (opened < 0)
      opened = Sc2kCanvasDesktopOpenFacilityInformation(&ui->desktop, (CityRect){85, 90, 365, 250}, facility);
    if (opened >= 0) {
      Window *window = &ui->desktop.windows[opened];
      window->data_map_selection = facility;
      window->confirmed_command = (CityCommand){CITY_COMMAND_EDIT_FACILITY_LABEL,
          MapTileX(city, tile), MapTileY(city, tile), 1};
      Sc2kDirtyMarkRect(&ui->desktop.dirty, window->bounds);
    }
    return;
  }
  int opened = FocusCityCanvasWindowKind(&ui->desktop, CITY_CANVAS_TILE_INFORMATION);
  bool reused = opened >= 0;
  if (!reused)
    opened = Sc2kCanvasDesktopOpenTileInformation(city, (CityRect){85, 90, 495, 250}, tile);
  if (opened < 0) {
    SetGameError(city->game, SC2K_ERROR_GUEST_ALLOCATION_FAILURE);
    return;
  }
  Window *window = &ui->desktop.windows[opened];
  Window replacement = *window;
  replacement.data_map_selection = tile;
  replacement.policy_flags &= ~3;
  replacement.title = "Tile Information";
  replacement.bounds.right = replacement.bounds.left + CITY_TILE_INFORMATION_WIDTH;
  replacement.bounds.bottom = replacement.bounds.top + CITY_TILE_INFORMATION_HEIGHT;
  if (ui->advanced_query && IsAltKeyPressed(ui)) {
    replacement.policy_flags |= 2;
    replacement.title = "Advanced Tile Query";
    replacement.bounds.right = replacement.bounds.left + 350;
    replacement.bounds.bottom = replacement.bounds.top + 248;
  }
  CityTileInformationSnapshot captured = {.window_id = window->window_id, .data.tile = tile};
  CaptureCityTileInformation(city, &captured);
  Sc2kDirtyMarkRect(&ui->desktop.dirty, window->bounds);
  ui->information_window = captured;
  replacement.information = &ui->information_window;
  *window = replacement;
  Sc2kDirtyMarkRect(&ui->desktop.dirty, window->bounds);
  ApplicationUpdateDialogPause(city);
}

static void OpenCityLabelEditor(City *city, NativeLabelDialog *pending, const CityOverlayName *name, bool facility) {
  CopyText(pending->name, sizeof(pending->name), (const char *)name->character);
  Window window = {0};
  window.kind = CITY_CANVAS_LABEL_EDIT;
  int left = IntMax(0, (city->game->ui->desktop.canvas_width - CITY_SIGN_EDITOR_WIDTH) / 2);
  int top = IntMax(18, (city->game->ui->desktop.canvas_height - 50) / 2);
  window.bounds = (CityRect){left, top, left + CITY_SIGN_EDITOR_WIDTH, top + 50};
  window.title = facility ? "Facility name" : "Map label";
  window.message = pending->name;
  window.text_caret = strlen(pending->name);
  int opened = Sc2kCanvasDesktopOpen(&city->game->ui->desktop, window);
  if (opened >= 0)
    pending->window_id = city->game->ui->desktop.windows[opened].window_id;
  else if (facility) {
    CityCommand cancel = pending->command;
    cancel.parameter2 = CITY_DIALOG_CANCEL;
    ExecuteRecordedCityCommandWithPayload(city->game, cancel, NULL, 0, (const u8 *)"", 0);
  }
  ApplicationUpdateDialogPause(city);
  pending->opening = false;
}

static void FinishCityFacilityEditor(City *city, NativeLabelDialog *pending, TileIndex tile, int token, i64 result) {
  if (result == CITY_FACILITY_OPENING_READY) {
    const CityQuestion *question = FindCityQuestion(city, token);
    pending->command = (CityCommand){CITY_COMMAND_REPLY_TO_QUESTION, token, CITY_DIALOG_OK, 0};
    OpenCityLabelEditor(city, pending, &question->context.facility_name.original_name, true);
  } else {
    pending->opening = false;
    if (result == CITY_FACILITY_OPENING_UNAVAILABLE && IsMapTileValid(city, tile))
      PresentCityTileInformationDialog(city, tile);
  }
}

static void CompleteCityFacilityNotice(City *city, void *context) {
  NativeLabelDialog *pending = context;
  TileIndex tile = pending->facility_opening.tile;
  int token = pending->facility_opening.token;
  bool resume = !city->game->close_requested && !city->game->execution_failed;
  i64 result = ResumeCityFacilityNameQuestion(city, token, resume);
  if (resume) FinishCityFacilityEditor(city, pending, tile, token, result);
  else pending->opening = false;
  ApplicationUpdateDialogPause(city);
}

void PresentCityLabelEditor(City *city, TileIndex tile, bool facility) {
  int x = MapTileX(city, tile), y = MapTileY(city, tile);
  ApplicationUpdateDialogPause(city);
  if (!IsMapTileValid(city, tile))
    return;
  int slot = facility ? ReadCityFacilityOverlay(city, tile) : ReadCitySignOverlay(city, tile);
  if (facility && !CityTileHasFacilityLabel(city, tile)) {
    PresentCityTileInformationDialog(city, tile);
    return;
  }
  if (!facility &&
      (!CanEditCityMapSign(city, tile) ||
          (slot == CITY_OVERLAY_NONE && FindFreeOverlayNameSlot(city) == 0))) {
    ExecuteRecordedCityCommandWithPayload(city->game, (CityCommand){CITY_COMMAND_EDIT_MAP_LABEL, x, y, 1}, NULL, 0,
        (const u8 *)"", 0);
    return;
  }
  for (i32 index = 0; index < 16; ++index) {
    NativeLabelDialog *pending = &city->game->ui->label_dialogs[index];
    if (pending->window_id || pending->continuation || pending->opening)
      continue;
    pending->opening = true;
    pending->command = (CityCommand){facility ? CITY_COMMAND_EDIT_FACILITY_LABEL : CITY_COMMAND_EDIT_MAP_LABEL,
        x, y, 1};
    if (facility && !city->game->observation) {
      OpenCityLabelEditor(city, pending, CityOverlayNameAt(city, slot), true);
      return;
    }
    if (facility) {
      if (city->game->ui->next_label_transaction == 0x7FFFFFFF)
        city->game->ui->next_label_transaction = 0;
      i32 token = ++city->game->ui->next_label_transaction;
      const GameObservation *observer = city->game->observation;
      i64 result = observer && observer->begin_facility_name ? observer->begin_facility_name(city, token, tile) :
          BeginCityFacilityNameQuestion(city, token, tile);
      if (result == CITY_FACILITY_OPENING_NOTICE) {
        pending->facility_opening.tile = tile;
        pending->facility_opening.token = token;
        u32 window = OpenCityResourceAlert(city, CITY_BITMAP_NOTICE_407, CITY_NOTICE_MAYOR_PARADE);
        int opened = FocusCityCanvasWindow(&city->game->ui->desktop, window);
        if (opened < 0 || !AttachCityWindowCallback(&city->game->ui->desktop.windows[opened],
            CompleteCityFacilityNotice, pending)) {
          if (window) Sc2kCanvasDesktopDismissWindow(&city->game->ui->desktop, window);
          ResumeCityFacilityNameQuestion(city, token, false);
          pending->opening = false;
          SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
          city->game->execution_failed = true;
        }
      } else {
        FinishCityFacilityEditor(city, pending, tile, token, result);
      }
      return;
    }
    int name_slot = slot ? slot : FindFreeOverlayNameSlot(city);
    CityOverlayName empty = {0};
    OpenCityLabelEditor(city, pending, slot ? CityOverlayNameAt(city, name_slot) : &empty, false);
    return;
  }
}

void AnswerCityLabelEditor(City *city, NativeLabelDialog *pending, i32 accept) {
  CityCommand packet = pending->command;
  if (pending->continuation || packet.id == CITY_COMMAND_REPLY_TO_QUESTION)
    packet.parameter2 = accept ? 1 : 2;
  else
    packet.parameter3 = accept ? 1 : 2;
  if (!pending->continuation) {
    Sc2kCanvasDesktopDismissWindow(&city->game->ui->desktop, pending->window_id);
    pending->window_id = 0;
    ApplicationUpdateDialogPause(city);
  }
  ExecuteRecordedCityCommandWithPayload(city->game, packet, NULL, 0, (const u8 *)pending->name, (u32)strlen(pending->name));
}

void PresentCityStadiumTeamDialog(City *city, TileIndex tile) {
  int x = MapTileX(city, tile), y = MapTileY(city, tile);
  ApplicationUpdateDialogPause(city);
  for (i32 index = 0; index < 16; ++index) {
    NativeStadiumDialog *pending = &city->game->ui->stadium_dialogs[index];
    if (pending->window_id || pending->retained)
      continue;
    if (city->game->ui->next_label_transaction == 0x7FFFFFFF)
      city->game->ui->next_label_transaction = 0;
    int token = ++city->game->ui->next_label_transaction;
    int available = city->infrastructure.stadium_team_mask == CITY_TEAM_MASK_ALL ? CITY_TEAM_MASK_ALL :
        ~city->infrastructure.stadium_team_mask & CITY_TEAM_MASK_ALL;
    if (city->game->observation) {
      if (!QueueCityStadiumTeamQuestion(city, token, tile)) return;
      const CityQuestion *question = FindCityQuestion(city, token);
      available = question->context.stadium_team.available_teams;
      pending->command = (CityCommand){CITY_COMMAND_REPLY_TO_QUESTION, token, CITY_DIALOG_OK, -1};
    } else {
      pending->command = EncodeCityCommand(city, CITY_COMMAND_ASSIGN_STADIUM_TEAM, tile, -1, 0, 0);
    }
    pending->name[0] = '\0';
    pending->answer = CITY_DIALOG_UNANSWERED;
    Window window = {0};
    window.kind = CITY_CANVAS_STADIUM_TEAM;
    window.bounds = (CityRect){175, 100, 425, 310};
    window.title = "Stadium team";
    window.message = pending->name;
    window.text_caret = strlen(pending->name);
    window.stadium_team_choices = available;
    window.stadium_team_selection = -1;
    for (i32 team = 0; team < 5; ++team)
      window.stadium_team_labels[team] = kCitySports[team];
    i32 opened = Sc2kCanvasDesktopOpen(&city->game->ui->desktop, window);
    if (opened >= 0)
      pending->window_id = city->game->ui->desktop.windows[opened].window_id;
    else if (city->game->observation)
      ExecuteRecordedCityCommand(city->game, CITY_COMMAND_REPLY_TO_QUESTION, token, CITY_DIALOG_CANCEL, -1);
    ApplicationUpdateDialogPause(city);
    return;
  }
}

// Keep the complete game placement caller on its stack while canvas events run.
void PresentCityStadiumPlacementDialog(City *city, TileIndex tile) {
  CityCanvasStadiumRequest request = {CITY_CANVAS_STADIUM_DIALOG_REQUEST, tile};
  RunPausedCityModalDialog(city, &request);
}

void SelectCityStadiumTeam(City *city, NativeStadiumDialog *pending, Window *window, int team) {
  window->stadium_team_selection = team;
  if (pending->command.id == CITY_COMMAND_ASSIGN_STADIUM_TEAM) pending->command.parameter1 = team;
  else pending->command.parameter3 = team;
  CopyText(pending->name, sizeof(pending->name),
      (const char *)CityOverlayNameAt(city, CITY_TEAM_NAME_FIRST + team)->character);
  Sc2kDirtyMarkRect(&city->game->ui->desktop.dirty, window->bounds);
}

void AnswerCityStadiumEditor(City *city, NativeStadiumDialog *pending, bool accept) {
  CityCommand packet = pending->command;
  if (packet.id == CITY_COMMAND_ASSIGN_STADIUM_TEAM) {
    if (accept && ExecuteRecordedCityCommandWithPayload(city->game, packet, NULL, 0,
        (const u8 *)pending->name, strlen(pending->name)) < 0) return;
    pending->answer = accept ? CITY_DIALOG_OK : CITY_DIALOG_CANCEL;
    Sc2kCanvasDesktopDismissWindow(&city->game->ui->desktop, pending->window_id);
    pending->window_id = 0;
    ApplicationUpdateDialogPause(city);
    return;
  }
  packet.parameter2 = accept ? CITY_DIALOG_OK : CITY_DIALOG_CANCEL;
  if (ExecuteRecordedCityCommandWithPayload(city->game, packet, NULL, 0, (const u8 *)pending->name,
      (u32)strlen(pending->name)) < 0)
    return;
  if (FindCityQuestion(city, packet.parameter1) != NULL)
    return;
  pending->answer = packet.parameter2;
  Sc2kCanvasDesktopDismissWindow(&city->game->ui->desktop, pending->window_id);
  pending->window_id = 0;
  ApplicationUpdateDialogPause(city);
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

static Window *OpenCityCommandConfirmation(City *city, CityCommand command, const char *title,
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
  ApplicationUpdateDialogPause(city);
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
      CityCommandCost quote = GetCityConstructionCost(city, args.tile, args.parameter1,
          rail ? CITY_TOOL_GROUP_RAIL : CITY_TOOL_GROUP_ROAD, rail ? CITY_RAIL_BRIDGE : CITY_ROAD_BRIDGE);
      blocker = quote.error_tile;
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
  Window *window = OpenCityCommandConfirmation(city, (CityCommand){0},
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
  OpenCityCommandConfirmation(city, command, "Build tunnel", text, CITY_MESSAGE_WARNING);
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
  int shown_rate = issue ? (city->fixes & (1 << CITY_FIX_BOND_RATING) ? CityBondInterestRate(city) : rate) :
      city->economic_tables.bond_rates.rate[0];
  char text[128];
  snprintf(text, sizeof(text), issue ? "Issue a $10,000 bond at %d%% annual interest?" :
      "Redeem the oldest $10,000 bond at %d%% annual interest?", shown_rate);
  command.parameter2 = CITY_BOND_CONFIRMED;
  OpenCityCommandConfirmation(city, command, issue ? "Issue city bond" : "Redeem city bond", text,
      CITY_MESSAGE_INFORMATION);
}

void AnswerCityConfirmation(City *city, u32 window_id, bool accept) {
  CityCommand confirmed = {0};
  CityCanvasDesktop *desktop = &city->game->ui->desktop;
  for (int index = 0; index < desktop->count; ++index) {
    Window *window = &desktop->windows[index];
    if (window->window_id != window_id) continue;
    confirmed = window->confirmed_command;
    if (confirmed.id) break;
    if (!window->confirmation) continue;
    CityConfirmationState *pending = window->confirmation;
    confirmed = pending->command;
    pending->answer = accept ? CITY_DIALOG_YES : CITY_DIALOG_NO;
    if (confirmed.id == CITY_COMMAND_DISASTER_UNLOCK_ANSWER)
      confirmed.parameter1 = pending->answer;
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

int OpenCityBudgetWindow(City *city) {
  CityInterfaceState *ui = city->game->ui;
  CityBudgetPanel budget;
  Sc2kCanvasBudgetFromCity(city, &budget);
  Window window = {.kind = CITY_CANVAS_BUDGET,
      .bounds = {(kDefaultCityCanvasWidth - CITY_BUDGET_WINDOW_WIDTH) / 2,
          (kDefaultCityCanvasHeight - CITY_BUDGET_WINDOW_HEIGHT) / 2,
          (kDefaultCityCanvasWidth + CITY_BUDGET_WINDOW_WIDTH) / 2,
          (kDefaultCityCanvasHeight + CITY_BUDGET_WINDOW_HEIGHT) / 2}, .budget = budget};
  int slot = Sc2kCanvasDesktopOpen(&ui->desktop, window);
  if (slot < 0) {
    SetGameError(city->game, SC2K_ERROR_GUEST_ALLOCATION_FAILURE);
    return -1;
  }
  return slot;
}

void OpenCityBudget(City *city) {
  if (!city->game->observation && city->settings.timer_mode == CITY_TIMER_STOPPED) return;
  if (FocusCityCanvasWindowKind(&city->game->ui->desktop, CITY_CANVAS_BUDGET) >= 0) return;
  if (city->game->observation && city->game->observation->open_manual_budget) {
    city->game->observation->open_manual_budget(city);
    return;
  }
  CityInterfaceState *ui = city->game->ui;
  if (Sc2kMainWindowIsIconic(ui)) return;
  int slot = OpenCityBudgetWindow(city);
  if (slot < 0) return;
  ApplicationBudgetBegin(city, ui->desktop.windows[slot].window_id, true);
  ApplicationUpdateDialogPause(city);
  RefreshCityChartPanels(city);
}

typedef struct {
  u32 window;
  int token;
  bool budget_opener, mission_notice, paused;
  CityConfirmationState reply;
} CityTickWindowCompletion;

static void CompleteCityTickWindow(City *city, void *context) {
  CityTickWindowCompletion *pending = context;
  CityInterfaceState *ui = city->game->ui;
  ApplicationUpdateDialogPause(city);
  if (pending->budget_opener) {
    if (FindCityBudgetWindowState(city, pending->window)) ApplicationBudgetEnd(city, pending->window);
    if (ui->annual_budget_window == pending->window) ui->annual_budget_window = 0;
    RefreshCityChartPanels(city);
  }
  ApplicationUpdateDialogPause(city);
  if (pending->paused) EndCityDialogPause(city, pending->token);
  if (pending->mission_notice) ui->outcome_dialog_active = 0;
  bool resume = !city->game->close_requested && !city->game->execution_failed;
  int answer = pending->reply.answer;
  const CityTickContinuation *work = FindCityTickContinuation(city, pending->token);
  i64 result = work && work->dialog == CITY_TICK_DIALOG_MILITARY_OFFER && resume ?
      ExecuteRecordedCityCommand(city->game, CITY_COMMAND_ANSWER_MILITARY_OFFER, pending->token, answer, 0) :
      ResumeCityTick(city, pending->token, resume, resume ? answer : 0);
  int pending_token = pending->token;
  free(pending);
  ApplicationUpdateDialogPause(city);
  DeliverCitySimulationEvents(city->game, pending_token, result, !resume);
}

void OpenCityTickContinuation(City *city, int token) {
  CityInterfaceState *ui = city->game->ui;
  const CityTickContinuation *work = FindCityTickContinuation(city, token);
  if (!work) return;
  CityTickDialog dialog = work->dialog;
  if (dialog == CITY_TICK_ANIMATION) {
    city->game->animation_token = token;
    return;
  }
  int edition = work->edition;
  int notice = work->notice;
  if (dialog == CITY_TICK_DIALOG_MILITARY_RESULT) notice = work->military.notice;
  int outcome = work->outcome;
  CityTickWindowCompletion *pending = calloc(1, sizeof(*pending));
  int slot = -1;
  bool opening = false;
  if (!pending) goto failed;
  pending->token = token;
  pending->mission_notice = dialog == CITY_TICK_DIALOG_MISSION;
  if (pending->mission_notice) ui->outcome_dialog_active = 1;
  pending->paused = BeginCityDialogPause(city, token) > 0;
  if (!pending->paused) goto failed;
  if (dialog == CITY_TICK_DIALOG_BUDGET) {
    slot = FocusCityCanvasWindowKind(&ui->desktop, CITY_CANVAS_BUDGET);
    opening = slot < 0;
    if (opening) slot = OpenCityBudgetWindow(city);
  } else if (dialog == CITY_TICK_DIALOG_MISSION) {
    opening = true;
    u32 window = OpenCityMissionOutcomeWindow(city, outcome);
    if (window) slot = FocusCityCanvasWindow(&ui->desktop, window);
  } else if (dialog == CITY_TICK_DIALOG_ECONOMY_NOTICE || dialog == CITY_TICK_DIALOG_MILITARY_RESULT) {
    if (!OpenCityNoticeWithCallback(city, "City notice", CityNoticeText(notice),
        CITY_MESSAGE_INFORMATION | CITY_MESSAGE_OK, CompleteCityTickWindow, pending)) goto failed;
    return;
  } else if (dialog == CITY_TICK_DIALOG_MILITARY_OFFER) {
    opening = true;
    slot = Sc2kCanvasDesktopOpenConfirm(&ui->desktop, (CityRect){125, 100, 475, 240},
        "Military base", CityNoticeText(CITY_NOTICE_MILITARY_BASE_OFFER),
        CITY_MESSAGE_ERROR | CITY_MESSAGE_YES_NO);
    if (slot >= 0) {
      pending->reply.answer = CITY_DIALOG_NO;
      pending->reply.window_id = ui->desktop.windows[slot].window_id;
      ui->desktop.windows[slot].confirmation = &pending->reply;
    }
  } else {
    bool hidden = dialog == CITY_TICK_DIALOG_MONTHLY_NEWSPAPER ? ui->newspaper_delivery_disabled :
        ui->extra_newspaper_disabled;
    if (!city->game->observation && hidden) {
      ApplyCityNewspaperPageEffects(city, edition);
      CompleteCityTickWindow(city, pending);
      return;
    }
    if (!OpenCityNewspaperWithCallback(city, edition, true, CompleteCityTickWindow, pending)) goto failed;
    return;
  }
  if (slot < 0 || !AttachCityWindowCallback(&ui->desktop.windows[slot], CompleteCityTickWindow, pending))
    goto failed;
  pending->window = ui->desktop.windows[slot].window_id;
  pending->budget_opener = dialog == CITY_TICK_DIALOG_BUDGET && opening;
  if (pending->budget_opener) {
    ui->annual_budget_window = pending->window;
    ApplicationBudgetBegin(city, pending->window, false);
  }
  ApplicationUpdateDialogPause(city);
  RefreshCityChartPanels(city);
  return;
failed:
  if (opening && slot >= 0) Sc2kCanvasDesktopDismissWindow(&ui->desktop, ui->desktop.windows[slot].window_id);
  if (pending && pending->paused)
    EndCityDialogPause(city, token);
  if (pending && pending->mission_notice) ui->outcome_dialog_active = 0;
  free(pending);
  ResumeCityTick(city, token, false, 0);
  if (!GetGameError(city->game)) SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
  city->game->execution_failed = true;
}

static u32 OpenCityResourceAlert(City *city, int bitmap_id, int text_id) {
  const RunSprite *bitmap = ReadCityPicture(city->game->resources, bitmap_id);
  const char *text = CityNoticeText(text_id);
  int slot;
  if (bitmap != NULL)
    slot = Sc2kCanvasDesktopOpenAdvisor(&city->game->ui->desktop, (CityRect){110, 100, 490, 300},
        "City notice", text, bitmap, CITY_MESSAGE_INFORMATION | CITY_MESSAGE_OK);
  else
    slot = Sc2kCanvasDesktopOpenNotice(&city->game->ui->desktop, (CityRect){110, 100, 490, 300},
        "City notice", text, CITY_MESSAGE_INFORMATION | CITY_MESSAGE_OK);
  ApplicationUpdateDialogPause(city);
  return slot >= 0 ? city->game->ui->desktop.windows[slot].window_id : 0;
}

static int PresentResourceAlertPaused(City *city, void *context) {
  const int *resource = context;
  u32 window_id = OpenCityResourceAlert(city, resource[0], resource[1]);
  WaitForCityResourceAlert(city, window_id, resource[0], resource[1]);
  return CITY_DIALOG_OK;
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
  while (notice) {
    CityResourceNotice *next = notice->next;
    free(notice);
    notice = next;
  }
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
  u32 window = OpenCityResourceAlert(city, notice->bitmap, notice->text);
  int slot = FocusCityCanvasWindow(&ui->desktop, window);
  if (slot < 0 || !AttachCityWindowCallback(&ui->desktop.windows[slot], CompleteCityResourceNotice, notice)) {
    if (window) Sc2kCanvasDesktopDismissWindow(&ui->desktop, window);
    ClearCityResourceNotices(ui);
    SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    city->game->execution_failed = true;
    return;
  }
  notice->window_id = window;
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

void PresentCityResourceAlert(City *city, i32 bitmap_id, i32 text_id) {
  if (!city->game->observation) {
    OpenCityResourceAlert(city, bitmap_id, text_id);
    return;
  }
  // Preserve the City caller's ordering while the canvas remains responsive.
  int resource[2] = {bitmap_id, text_id};
  RunPausedCityAction(city, PresentResourceAlertPaused, resource);
}

void ApplicationOpenScenarioInstructions(City *city, const char *path) {
  CityInterfaceState *ui = city->game->ui;
  ApplicationUpdateDialogPause(city);
  for (i32 index = 0; index < ui->desktop.count; ++index)
    if (ui->desktop.windows[index].window_id == ui->scenario_instructions_window)
      return;
  ui->scenario_instructions_window = 0;
  if (!ReadCityScenarioInstructions(path, ui->scenario_instructions))
    return;
  i32 slot = Sc2kCanvasDesktopOpenNotice(&ui->desktop, (CityRect){70, 65, 530, 395},
      "Scenario instructions", ui->scenario_instructions, CITY_MESSAGE_INFORMATION | CITY_MESSAGE_OK);
  if (slot >= 0)
    ui->scenario_instructions_window = ui->desktop.windows[slot].window_id;
  ApplicationUpdateDialogPause(city);
}

static u32 OpenCityMissionOutcomeWindow(City *city, int outcome) {
  static const i16 kBitmapIds[3] = {CITY_BITMAP_NOTICE_403, CITY_BITMAP_NOTICE_128, CITY_BITMAP_NOTICE_406};
  static const i16 kTextIds[3] = {CITY_NOTICE_MISSION_FAILED, CITY_NOTICE_MISSION_WON, CITY_NOTICE_BANKRUPT};
  return OpenCityResourceAlert(city, kBitmapIds[outcome], kTextIds[outcome]);
}

static int PresentMissionNoticePaused(City *city, void *context) {
  int outcome = *(const int *)context;
  u32 window_id = OpenCityMissionOutcomeWindow(city, outcome);
  WaitForCityMissionNotice(city, window_id, outcome);
  return CITY_DIALOG_OK;
}

void PresentCityMissionOutcome(City *city, i32 outcome) {
  ObserveCityPresentation(city->game, CITY_PRESENTATION_MISSION_NOTICE, 0, outcome, 0, 0, NULL);
  RunPausedCityAction(city, PresentMissionNoticePaused, &outcome);
}

// The window owns the captured page; opening it never retains the caller.
u32 OpenCityNewspaperPage(City *city, CityNewspaperDialog *paint, int edition, double close_at) {
  CityInterfaceState *ui = city->game->ui;
  ui->startup_newspaper_open_at = 0;
  NativeNewspaperDialog *page = calloc(1, sizeof(*page));
  if (!page) {
    SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    city->game->execution_failed = true;
    return 0;
  }
  bool captured = paint ? NativeCaptureNewspaper(city, paint, page) :
      ComposeCityNewspaperPreview(city, edition, page);
  int width = IntMin(530, ui->desktop.canvas_width - 24);
  int height = IntMin(425, ui->desktop.canvas_height - 32);
  int left = (ui->desktop.canvas_width - width) / 2;
  int top = IntMax(22, (ui->desktop.canvas_height - height) / 2);
  int slot = captured ? Sc2kCanvasDesktopOpenNewspaperPage(&ui->desktop,
      (CityRect){left, top, left + width, top + height}, &page->page) : -1;
  if (slot < 0) {
    NativeReleaseNewspaperPage(page);
    free(page);
    if (!GetGameError(city->game)) SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    city->game->execution_failed = true;
    return 0;
  }
  Window *window = &ui->desktop.windows[slot];
  Drawing *drawing = city->game->drawing;
  if (drawing->newspaper_opening_window) {
    MarkScreenDirty(&drawing->screen_damage, drawing->newspaper_opening_bounds);
    Sc2kDirtyMarkRect(&ui->desktop.window_damage, drawing->newspaper_opening_bounds);
    ReleaseCityNewspaperOpening(drawing);
  }
  if (window->newspaper) {
    NativeReleaseNewspaperPage(window->newspaper);
    free(window->newspaper);
  }
  window->newspaper = page;
  window->newspaper_page = page->page;
  page->opening_started = MonotonicSeconds();
  page->opening_frame = 0;
  Sc2kDirtyMarkRect(&ui->desktop.dirty, window->bounds);
  page->close_at = close_at;
  u32 id = window->window_id;
  ApplicationUpdateDialogPause(city);
  return id;
}

void PresentCityNewspaperEdition(City *city, i32 edition) {
  if (city->game->debug_city) return;
  CityInterfaceState *ui = city->game->ui;
  CityNewspaperDialog *previous_owner = ui->newspaper_modal_owner;
  ui->newspaper_modal_owner = NULL;
  OpenCityNewspaperPage(city, NULL, edition, 0);
  ui->newspaper_modal_owner = previous_owner;
}

bool InitializeCityNewspaperWindow(City *city, CityNewspaperDialog *paint) {
  CityInterfaceState *ui = city->game->ui;
  CityNewspaperDialog *previous_owner = ui->newspaper_modal_owner;
  CityNewspaperDialog *previous_initializing = ui->initializing_newspaper;
  bool previous_closed = ui->newspaper_closed_during_initialization;
  ui->newspaper_modal_owner = ui->initializing_newspaper = paint;
  ui->newspaper_closed_during_initialization = false;
  InitializeCityNewspaperDialog(city, paint);
  bool ended = ui->newspaper_closed_during_initialization;
  ui->newspaper_modal_owner = previous_owner;
  ui->initializing_newspaper = previous_initializing;
  ui->newspaper_closed_during_initialization = previous_closed;
  return !ended;
}

bool OpenInitializedCityNewspaper(City *city, int edition, bool ended) {
  if (ended) return true;
  CityNewspaperDialog paint = {.edition_index = edition};
  if (!OpenCityNewspaperPage(city, &paint, edition,
      MonotonicSeconds() + city->game->ui->newspaper_timer_seconds)) return false;
  RefreshCityChartPanels(city);
  return true;
}

typedef struct {
  int token;
  bool ended;
  CityWindowCallback *callback;
  void *context;
} CityNewspaperWindowCompletion;

static bool FinishCityNewspaperOpening(City *city, const CityNewspaperWindowCompletion *opening) {
  bool resume = !city->game->close_requested && !city->game->execution_failed;
  if (!ResumeCityTick(city, opening->token, resume, 0)) return false;
  if (!resume || opening->ended) {
    if (opening->callback) opening->callback(city, opening->context);
    return true;
  }
  int slot = FocusCityCanvasWindowKind(&city->game->ui->desktop, CITY_CANVAS_NEWSPAPER);
  return slot >= 0 && (!opening->callback || AttachCityWindowCallback(&city->game->ui->desktop.windows[slot],
      opening->callback, opening->context));
}

static void CompleteCityNewspaperOpening(City *city, void *context) {
  CityNewspaperWindowCompletion opening = *(CityNewspaperWindowCompletion *)context;
  free(context);
  if (!FinishCityNewspaperOpening(city, &opening)) {
    if (!GetGameError(city->game)) SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    city->game->execution_failed = true;
    if (opening.callback) opening.callback(city, opening.context);
  }
  ApplicationUpdateDialogPause(city);
}

static bool OpenCityNewspaperWithCallback(City *city, int edition, bool automatic,
    CityWindowCallback *callback, void *context) {
  if (automatic && city->game->debug_city) {
    // The UI dispatcher opens this caller. Execute the page command directly to avoid reentering it.
    SelectCityNewspaperEdition(city, edition);
    if (GetGameError(city->game)) return false;
    if (callback) callback(city, context);
    return true;
  }
  i64 token = BeginCityNewspaperOpening(city, edition);
  if (!token) return false;
  const CityTickContinuation *work = FindCityTickContinuation(city, (int)token);
  CityNewspaperWindowCompletion opening = {.token = (int)token,
      .ended = (work->outcome & CITY_NEWSPAPER_INITIALIZATION_ENDED) != 0, .callback = callback, .context = context};
  if (!work->notice) return FinishCityNewspaperOpening(city, &opening);
  CityNewspaperWindowCompletion *pending = malloc(sizeof(*pending));
  u32 window = 0;
  if (pending) {
    *pending = opening;
    window = OpenCityResourceAlert(city, CITY_BITMAP_NOTICE_407, CITY_NOTICE_MAYOR_PARADE);
    int slot = FocusCityCanvasWindow(&city->game->ui->desktop, window);
    if (slot >= 0 && AttachCityWindowCallback(&city->game->ui->desktop.windows[slot],
        CompleteCityNewspaperOpening, pending)) return true;
  }
  if (window) Sc2kCanvasDesktopDismissWindow(&city->game->ui->desktop, window);
  free(pending);
  ResumeCityTick(city, (int)token, false, 0);
  SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
  city->game->execution_failed = true;
  return false;
}

static void ScheduleCityStartupNewspaper(City *city) {
  city->game->ui->startup_newspaper_edition = city->newspaper.edition_index;
  city->game->ui->startup_newspaper_open_at = -1;
}

void OpenCityManualNewspaper(City *city, int edition) {
  city->game->ui->startup_newspaper_open_at = 0;
  const GameObservation *observer = city->game->observation;
  if (observer && observer->open_manual_newspaper) {
    observer->open_manual_newspaper(city, edition);
    return;
  }
  OpenCityNewspaperPage(city, NULL, edition, MonotonicSeconds() + city->game->ui->newspaper_timer_seconds);
}

bool AnswerCityOverlayNameDialog(City *city, i32 token, i32 answer, const u8 *text, u32 length) {
  for (i32 index = 0; index < 16; ++index) {
    NativeLabelDialog *pending = &city->game->ui->label_dialogs[index];
    if (!pending->continuation || pending->answer || pending->command.parameter1 != token)
      continue;
    if (length > CITY_OVERLAY_NAME_MAX_LENGTH)
      return false;
    if (length)
      memmove(pending->name, text, length);
    pending->name[length] = '\0';
    pending->answer = answer;
    Sc2kCanvasDesktopDismissWindow(&city->game->ui->desktop, pending->window_id);
    pending->window_id = 0;
    ApplicationUpdateDialogPause(city);
    return true;
  }
  return false;
}


static void SaveCityFileChoice(City *city, const char *path, void *context) {
  (void)context;
  // Restore the live speed before serializing; the temporary picker pause is UI state.
  ApplicationUpdateDialogPause(city);
  if (path) {
    u32 error_serial = city->game->ui->command_error_serial;
    ClearGameError(city->game);
    if (SaveCityToPath(city, (const u8 *)path, (u32)strlen(path), true))
      SetText(&city->game->ui->file_dialog.destination, CityPathText(&city->game->city_path));
    else if (error_serial == city->game->ui->command_error_serial)
      PresentCityFileNotice(city, "The city could not be saved.", true);
  }
  ApplicationUpdateDialogPause(city);
}

void RequestCitySave(Game *game) {
  if (game->observation && game->observation->open_save_dialog) {
    game->observation->open_save_dialog(game->city);
    return;
  }
  if (!game->city_active || NativeSaveDialogOpen(game->city)) return;
  if (!OpenCitySaveDialog(game->city, SaveCityFileChoice, NULL))
    PresentCityFileNotice(game->city, "The save dialog could not be opened.", true);
  ApplicationUpdateDialogPause(game->city);
}

typedef struct {
  char *path;
  bool startup, scenario, from_path;
  CityFileRuntime scenario_file;
} CityFileOpenRequest;

static void CompleteScenarioFileRequest(City *city, void *context) {
  CityFileOpenRequest *request = context;
  ApplicationUpdateDialogPause(city);
  ResumeCityFileOperation(&request->scenario_file);
  if (!city->game->close_requested && !city->game->execution_failed) {
    if (FinishCityScenarioLoad(city, request->from_path))
      SetText(&city->game->ui->file_dialog.destination, CityPathText(&city->game->city_path));
  }
  CancelCityScenarioLoad(&request->scenario_file);
  EndCityFileOperation(&request->scenario_file);
  free(request->path);
  free(request);
  ApplicationUpdateDialogPause(city);
}

static void FinishCityFileRequest(City *city, CityFileOpenRequest *request, bool accept) {
  Game *game = city->game;
  ApplicationUpdateDialogPause(city);
  if (!game->close_requested && !game->execution_failed) {
    if (accept && request->scenario) {
      BeginCityFileOperation(game, &request->scenario_file);
      game->ui->save_prompt_active = false;
      bool loaded = OpenCitySelectedScenario(city, (const u8 *)request->path, strlen(request->path),
          request->from_path, true);
      SuspendCityFileOperation(&request->scenario_file);
      if (loaded) {
        const char *instructions = request->scenario_file.scenario_instructions;
        if (instructions[0] && OpenCityNoticeWithCallback(city, "Scenario instructions", instructions,
            CITY_MESSAGE_INFORMATION | CITY_MESSAGE_OK, CompleteScenarioFileRequest, request)) return;
        CompleteScenarioFileRequest(city, request);
        return;
      }
      ResumeCityFileOperation(&request->scenario_file);
      CancelCityScenarioLoad(&request->scenario_file);
      EndCityFileOperation(&request->scenario_file);
    } else if (accept && !request->scenario && OpenCitySelectedFile(city, (const u8 *)request->path,
        strlen(request->path), request->from_path))
      SetText(&game->ui->file_dialog.destination, CityPathText(&game->city_path));
    if ((request->startup || request->from_path) && !accept) {
      if (request->from_path) game->path_open_flag = CITY_PATH_OPEN_NONE;
      game->idle_state = CITY_IDLE_STARTUP_MENU;
      game->idle_entry = 1;
    }
  }
  free(request->path);
  free(request);
  ApplicationUpdateDialogPause(city);
}

static void OpenCityFileChoice(City *city, const char *path, void *context) {
  CityFileOpenRequest *request = context;
  if (path) SetText(&request->path, path);
  FinishCityFileRequest(city, request, path != NULL);
}

void RequestCityFileOpen(Game *game) {
  if (game->observation && game->observation->open_file_dialog) {
    game->observation->open_file_dialog(game->city);
    return;
  }
  if (NativeSaveDialogOpen(game->city)) return;
  CityFileOpenRequest *request = calloc(1, sizeof(*request));
  if (!request) {
    PresentCityFileNotice(game->city, "The file dialog could not be opened.", true);
    if (game->idle_state == CITY_IDLE_STARTUP_CHOICE) {
      game->idle_state = CITY_IDLE_STARTUP_MENU;
      game->idle_entry = 1;
    }
    return;
  }
  request->startup = game->idle_state == CITY_IDLE_STARTUP_CHOICE;
  if (request->startup) game->ui->restore_frame_tools = false;
  game->ui->save_prompt_active = false;
  CityPath directory = {0};
  bool directory_ready = GetCitySaveDirectory(game, &directory);
  CityFilePickerOptions options = {"*.sc2", CityPathText(&directory), "Open City",
      true, OpenCityFileChoice, request};
  bool opened = directory_ready && OpenCityFileDialog(game->city, &options);
  FreeCityPathStorage(&directory);
  if (!opened) {
    PresentCityFileNotice(game->city, "The file dialog could not be opened.", true);
    FinishCityFileRequest(game->city, request, false);
  }
  ApplicationUpdateDialogPause(game->city);
}

void RequestCityFilePath(Game *game, const char *path) {
  CityFileOpenRequest *request = calloc(1, sizeof(*request));
  if (!request) {
    SetGameError(game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    return;
  }
  request->from_path = true;
  OpenCityFileChoice(game->city, path, request);
}

void OpenCityScenarioPath(Game *game, const char *path, bool from_path) {
  CityFileOpenRequest *request = calloc(1, sizeof(*request));
  if (!request) {
    PresentCityFileNotice(game->city, "The scenario could not be opened.", true);
    game->idle_state = CITY_IDLE_STARTUP_MENU;
    game->idle_entry = 1;
    return;
  }
  request->scenario = true;
  request->from_path = from_path;
  SetText(&request->path, path);
  FinishCityFileRequest(game->city, request, true);
}

void RequestCityScenarioOpen(Game *game) {
  if (game->observation && game->observation->open_scenario_dialog) {
    game->observation->open_scenario_dialog(game->city);
    return;
  }
  if (game->ui->scenario_window || NativeSaveDialogOpen(game->city)) return;
  CityFileOpenRequest *request = calloc(1, sizeof(*request));
  if (!request) {
    PresentCityFileNotice(game->city, "The scenario window could not be opened.", true);
    if (game->idle_state == CITY_IDLE_STARTUP_CHOICE) {
      game->idle_state = CITY_IDLE_STARTUP_MENU;
      game->idle_entry = 1;
    }
    return;
  }
  request->scenario = true;
  game->ui->save_prompt_active = false;
  request->startup = game->idle_state == CITY_IDLE_STARTUP_CHOICE;
  if (request->startup) game->ui->restore_frame_tools = false;
  if (!OpenCityScenarioSelection(game->city, OpenCityFileChoice, request)) {
    PresentCityFileNotice(game->city, "The scenario window could not be opened.", true);
    FinishCityFileRequest(game->city, request, false);
  }
  ApplicationUpdateDialogPause(game->city);
}

enum { CITY_REPLACEMENT_NEW_CITY, CITY_REPLACEMENT_TERRAIN_EDITOR };

typedef struct {
  CityCreationOptions options;
  char *terrain_path;
  u8 kind;
  bool startup;
} CityReplacementRequest;

static void FinishCityReplacementRequest(City *city, CityReplacementRequest *request, bool accept) {
  Game *game = city->game;
  ApplicationUpdateDialogPause(city);
  if (!game->close_requested && !game->execution_failed) {
    if (accept && request->kind == CITY_REPLACEMENT_TERRAIN_EDITOR) {
      HideCityAuxiliaryWindowsAndStopSound(city);
      EnterCityTerrainEditor(city);
      if (!GetGameError(game)) ResetCityReplacementPresentation(city);
      if (!GetGameError(game) && request->startup) {
        game->idle_state = CITY_IDLE_PLAY;
        game->idle_entry = 1;
      }
    } else if (accept && (request->terrain_path ?
        CreateCityFromTerrainFile(city, &request->options, request->terrain_path) :
        CreateCityFromOptions(city, &request->options))) {
      ApplyGameCityDefaults(game);
      ResetCityReplacementPresentation(city);
      CityInterfaceState *ui = game->ui;
      int climate = request->options.terrain.climate;
      ui->desktop.menu.tree_climate = climate == CITY_TERRAIN_SNOWY ? CLIMATE_ARCTIC :
          climate == CITY_TERRAIN_DESERT ? CLIMATE_DESERT_50 :
          climate == CITY_TERRAIN_SIMCITY ? CLIMATE_SIMCITY : CLIMATE_TEMPERATE;
      ui->desktop.menu.tree_species = CITY_TREE_RANDOM;
      ui->replacement_prompt_suppressed = false;
      ShowCityMapToolWindows(city);
      if (!request->options.edit_terrain && !game->ranked_events_suppressed &&
          !game->resources->facility_resources_unavailable) ScheduleCityStartupNewspaper(city);
    } else if (request->startup) {
      game->idle_state = CITY_IDLE_STARTUP_MENU;
      game->idle_entry = 1;
    }
  }
  free(request->terrain_path);
  free(request);
  ApplicationUpdateDialogPause(city);
}

void CompleteCityTerrainEditing(Game *game) {
  if (game->ui->desktop.selected_tool == CITY_CANVAS_TOOL_STRETCH_TERRAIN ||
      game->ui->desktop.selected_tool == CITY_CANVAS_TOOL_STREAM) {
    SelectCityToolWithModifiers(game, CITY_TOOLBAR_BUTTON_CENTER, 0, 0);
    game->ui->desktop.selected_tool = CITY_CANVAS_TOOL_NONE;
  }
  ResetCityReplacementPresentation(game->city);
  if (!game->ranked_events_suppressed && !game->resources->facility_resources_unavailable)
    ScheduleCityStartupNewspaper(game->city);
}

static void AcceptCityCreationOptions(City *city, const CityCreationOptions *options,
    CityCreationTerrain terrain, void *context) {
  CityReplacementRequest *request = context;
  if (!options) {
    FinishCityReplacementRequest(city, request, false);
    return;
  }
  request->options = *options;
  if (terrain.path) {
    request->terrain_path = DuplicateText(terrain.path);
    if (!request->terrain_path) {
      SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
      FinishCityReplacementRequest(city, request, false);
      return;
    }
  }
  FinishCityReplacementRequest(city, request, true);
}

void RequestNewCity(Game *game) {
  if (game->observation && game->observation->open_new_city_setup) {
    game->observation->open_new_city_setup(game->city);
    return;
  }
  if (game->ui->setup_window || NativeSaveDialogOpen(game->city)) return;
  CityReplacementRequest *request = calloc(1, sizeof(*request));
  if (!request) {
    PresentCityFileNotice(game->city, "The city setup window could not be opened.", true);
    if (game->idle_state == CITY_IDLE_STARTUP_CHOICE) {
      game->idle_state = CITY_IDLE_STARTUP_MENU;
      game->idle_entry = 1;
    }
    return;
  }
  request->kind = CITY_REPLACEMENT_NEW_CITY;
  request->startup = game->idle_state == CITY_IDLE_STARTUP_CHOICE;
  if (!OpenCityCreationWindow(game->city, true, AcceptCityCreationOptions, request)) {
    PresentCityFileNotice(game->city, "The city setup window could not be opened.", true);
    FinishCityReplacementRequest(game->city, request, false);
  }
  ApplicationUpdateDialogPause(game->city);
}

static void AcceptCityConfiguration(City *city, const CityCreationOptions *options,
    CityCreationTerrain terrain, void *context) {
  (void)context;
  if (!options || city->game->close_requested || city->game->execution_failed) return;
  if (terrain.current) {
    if (!ConfigureCityFromOptions(city, options)) return;
    if (!options->edit_terrain &&
        !RunCityCommand(city->game, CITY_COMMAND_START_CITY, INVALID_TILE, 0, 0, 0, NULL, NULL)) return;
  } else {
    if (!(terrain.path ? CreateCityFromTerrainFile(city, options, terrain.path) : CreateCityFromOptions(city, options)))
      return;
    ApplyGameCityDefaults(city->game);
  }
  ResetCityReplacementPresentation(city);
  if (!options->edit_terrain && !city->game->ranked_events_suppressed &&
      !city->game->resources->facility_resources_unavailable) ScheduleCityStartupNewspaper(city);
}

bool RequestCityConfiguration(Game *game) {
  return OpenCityCreationWindow(game->city, false, AcceptCityConfiguration, NULL);
}

void RequestCityTerrainEditor(Game *game) {
  if (game->ui->setup_window || NativeSaveDialogOpen(game->city)) return;
  CityReplacementRequest *request = calloc(1, sizeof(*request));
  if (!request) {
    PresentCityFileNotice(game->city, "The terrain editor could not be opened.", true);
    game->idle_state = CITY_IDLE_STARTUP_MENU;
    game->idle_entry = 1;
    return;
  }
  request->kind = CITY_REPLACEMENT_TERRAIN_EDITOR;
  request->startup = game->idle_state == CITY_IDLE_STARTUP_CHOICE;
  game->ui->save_prompt_active = false;
  FinishCityReplacementRequest(game->city, request, true);
}

void RecordCityFileDialogCheckpoint(City *city, u32 kind, i32 token, i32 answer, const char *path) {
  ObserveCityPresentation(city->game, kind, token, 0, 0, answer, path);
}

void ReleaseCityFileNotices(City *city, i32 all) {
  NativeFileNotice **link = &city->game->ui->file_notices;
  while (*link != NULL) {
    NativeFileNotice *notice = *link;
    i32 open = 0;
    for (i32 index = 0; index < city->game->ui->desktop.count; ++index)
      open |= city->game->ui->desktop.windows[index].window_id == notice->window_id;
    if (all || !open) { *link = notice->next; free(notice); }
    else link = &notice->next;
  }
}

void PresentCityFileNotice(City *city, const char *text, bool error) {
  if (city->game->execution_failed || city->game->close_requested) return;
  ReleaseCityFileNotices(city, 0);
  size_t length = strlen(text);
  NativeFileNotice *notice = malloc(sizeof(*notice) + length + 1);
  if (notice == NULL) { city->game->execution_failed = 1; return; }
  memcpy(notice->text, text, length + 1u);
  CityInterfaceState *ui = city->game->ui;
  const char *summary = ui->active_command ?
      CityCommandFailureSummary(ui->active_command) : NULL;
  if (!summary && (city->game->idle_state == CITY_IDLE_LOAD_CITY ||
      city->game->idle_state == CITY_IDLE_LOAD_SCENARIO)) summary = "Can't load game...";
  int index;
  if (error) {
    index = OpenCityCommandErrorWindow(city->game, summary ? summary : "Can't complete file operation...",
        notice->text, true, INVALID_TILE);
    ++ui->command_error_serial;
    if (ui->command_feedback) ui->command_feedback->notice_shown = true;
  } else index = Sc2kCanvasDesktopOpenNotice(&ui->desktop, (CityRect){110, 90, 490, 210},
      "City file", notice->text, CITY_MESSAGE_INFORMATION | CITY_MESSAGE_OK);
  if (index < 0) { free(notice); return; }
  notice->window_id = city->game->ui->desktop.windows[index].window_id;
  notice->next = city->game->ui->file_notices;
  city->game->ui->file_notices = notice;
  ApplicationUpdateDialogPause(city);
}

void SetCityHostWindowTitle(City *city, const char *text) {
  (void)text;
  if (city->game->drawing->shake_display != NULL)
    Sc2kSDLTitle(city->game->drawing->shake_display, SC2K_APP_NAME);
}

void InvalidateLoadedCityCanvas(City *city) {
  city->game->drawing->main_cache.valid = 0;
  Sc2kDirtyMarkRect(&city->game->ui->desktop.dirty,
      (CityRect){0, 0, kDefaultCityCanvasWidth, kDefaultCityCanvasHeight});
}


void ApplicationStartupMenuBeep(Game *game) {
  PlayCityButtonBeep(game);
}

void PresentCityDisasterUnlockOffer(City *city) {
  const GameObservation *observer = city->game->observation;
  if (observer && observer->present_unlock_offer) observer->present_unlock_offer(city);

}

// Event insertion is game-owned; this leaf presents the selected original edition.
void PresentRankedCityEvent(City *city, u32 event_code, u32 event_data) {
  CityNewspaperDialog dialog = {0};
  dialog.edition_index = city->newspaper.edition_index;
  RunPausedCityNewspaperDialog(city, &dialog);
}

bool AttachCityWindowCallback(Window *window, CityWindowCallback *callback, void *context) {
  if (!callback) return false;
  CityWindowCompletion *completion = malloc(sizeof(*completion));
  if (!completion) return false;
  *completion = (CityWindowCompletion){window->completion, callback, context};
  window->completion = completion;
  return true;
}

bool OpenCityNoticeWithCallback(City *city, const char *title, const char *text, u32 flags,
    CityWindowCallback *callback, void *context) {
  int slot = Sc2kCanvasDesktopOpenNotice(&city->game->ui->desktop, (CityRect){70, 65, 530, 395},
      title, text, flags);
  if (slot < 0) return false;
  Window *window = &city->game->ui->desktop.windows[slot];
  if (!AttachCityWindowCallback(window, callback, context)) {
    Sc2kCanvasDesktopDismissWindow(&city->game->ui->desktop, window->window_id);
    return false;
  }
  window->owned_message = DuplicateText(text);
  window->message = window->owned_message;
  ApplicationUpdateDialogPause(city);
  return true;
}

void CompleteCityWindowCallbacks(City *city) {
  if (city->game->command_depth) return;
  CityInterfaceState *ui = city->game->ui;
  while (ui->closed_windows) {
    CityWindowCompletion *closed = ui->closed_windows, *pending = NULL;
    ui->closed_windows = NULL;
    // Resume in dismissal order, including windows closed together by replacement.
    while (closed) {
      CityWindowCompletion *next = closed->next;
      closed->next = pending;
      pending = closed;
      closed = next;
    }
    while (pending) {
      CityWindowCompletion *next = pending->next;
      CityWindowCallback *callback = pending->callback;
      void *context = pending->context;
      free(pending);
      callback(city, context);
      pending = next;
    }
  }
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
    if (!OpenCityCommandConfirmation(game->city, (CityCommand){CITY_COMMAND_CHEAT, cheat, options, 0},
        military ? "Military base" : "Confirm", text, CITY_MESSAGE_ERROR)) return 0;
    return result;
  }
  return ExecuteRecordedCityCommand(game, CITY_COMMAND_CHEAT, cheat, 0, 0);
}

// Handles this window's painting and input; paint coordinates are local to the window.
bool MessageWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    DrawCityMessageBox(event->canvas, window);
    return true;
  }
  if (event->kind != WINDOW_INPUT) return false;
  const CanvasSDLEvent *input = event->input;
  if (input->kind == CANVAS_SDL_KEY_DOWN && window->kind == CITY_CANVAS_CONFIRM) {
    if (input->key == CITY_KEY_RETURN || input->key == CITY_KEY_ESCAPE)
      AnswerCityConfirmation(game->city, window->window_id, input->key == CITY_KEY_RETURN);
    return true;
  }
  if (input->kind == CANVAS_SDL_POINTER_DOWN && input->button == CANVAS_SDL_BUTTON_LEFT) {
    int answer = CityMessageAnswerAt(window, event->x, event->y);
    if (answer == CITY_DIALOG_YES || answer == CITY_DIALOG_NO) {
      AnswerCityConfirmation(game->city, window->window_id, answer == CITY_DIALOG_YES);
      return true;
    }
    if (answer == CITY_DIALOG_OK) {
      Sc2kCanvasDesktopDismissWindow(&game->ui->desktop, window->window_id);
      ApplicationUpdateDialogPause(game->city);
      return true;
    }
  }
  return false;
}

// Edit bounded labels and stadium names. Answers may close the window and resume
// a retained caller, so no window pointer is used after submitting an answer.
bool NameEditorWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    if (window->kind == CITY_CANVAS_LABEL_EDIT) CanvasDrawLabelEditor(event->canvas, window);
    else CanvasDrawStadiumTeamEditor(event->canvas, window);
    return true;
  }
  if (event->kind != WINDOW_INPUT) return false;
  City *city = game->city;
  CityCanvasDesktop *desktop = &game->ui->desktop;
  const CanvasSDLEvent *input = event->input;
  NativeLabelDialog *label = NULL;
  NativeStadiumDialog *stadium = NULL;
  for (int i = 0; i < 16; ++i) {
    if (window->kind == CITY_CANVAS_LABEL_EDIT && game->ui->label_dialogs[i].window_id == window->window_id)
      label = &game->ui->label_dialogs[i];
    if (window->kind == CITY_CANVAS_STADIUM_TEAM && game->ui->stadium_dialogs[i].window_id == window->window_id)
      stadium = &game->ui->stadium_dialogs[i];
  }
  if (!label && !stadium) return false;
  Window *target = FindCityCanvasWindow(desktop, window->window_id);
  char *name = label ? label->name : stadium->name;
  CityRect field = CityNameEditorBounds(window, CITY_NAME_EDITOR_INPUT);
  int answer = -1;
  if (input->kind == CANVAS_SDL_KEY_DOWN) {
    CityHotkey key = input->key;
    if (key == CITY_KEY_RETURN || key == CITY_KEY_ESCAPE) answer = key == CITY_KEY_RETURN;
    else if (HandleCityTextEditKey(name, &target->text_caret, &target->text_select_all, key))
      Sc2kDirtyMarkRect(&desktop->dirty, field);
    else if (stadium && (key == CITY_KEY_UP || key == CITY_KEY_DOWN)) {
      int direction = key == CITY_KEY_DOWN ? 1 : -1;
      int start = target->stadium_team_selection;
      if (start == -1 && direction < 0) start = 5;
      for (int team = start + direction; team >= 0 && team < 5; team += direction)
        if (target->stadium_team_choices & (1 << team)) {
          SelectCityStadiumTeam(city, stadium, target, team);
          break;
        }
    }
  } else if (input->kind == CANVAS_SDL_POINTER_DOWN && input->button == CANVAS_SDL_BUTTON_LEFT) {
    CityRect b = window->bounds;
    if (CanvasCloseAt(b, event->x, event->y)) answer = 0;
    else if (CanvasPointInRect(CityNameEditorBounds(window, CITY_NAME_EDITOR_OK), event->x, event->y)) answer = 1;
    else if (CanvasPointInRect(CityNameEditorBounds(window, CITY_NAME_EDITOR_CANCEL), event->x, event->y)) answer = 0;
    if (answer < 0) {
      int team = stadium ? Sc2kCanvasStadiumTeamAt(target, event->x, event->y) : -1;
      if (team >= 0) SelectCityStadiumTeam(city, stadium, target, team);
      else if (CanvasPointInRect(field, event->x, event->y)) {
        target->text_caret = CityTextBoxCaretAt(&game->resources->interface_font, field,
            name, target->text_caret, &target->text_scroll, event->x);
        target->text_select_all = 0;
        Sc2kDirtyMarkRect(&desktop->dirty, field);
      } else return false;
    }
  } else if (input->kind == CANVAS_SDL_TEXT_INPUT) {
    InsertCityTextInput(name, CITY_OVERLAY_NAME_MAX_LENGTH + 1,
        &target->text_caret, &target->text_select_all, input->text, false);
    Sc2kDirtyMarkRect(&desktop->dirty, field);
  } else return false;
  if (answer >= 0) {
    if (label) AnswerCityLabelEditor(city, label, answer);
    else AnswerCityStadiumEditor(city, stadium, answer);
    ApplicationUpdateDialogPause(city);
  }
  return true;
}
