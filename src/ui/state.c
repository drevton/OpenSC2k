#include "city/presentation.h"
#include "ui/state.h"

#include <stdlib.h>
#include <string.h>

#include "city/engine/commands.h"
#include "game/state.h"
#include "audio/state.h"
#include "city/state.h"
#include "drawing/state.h"
#include "assets/state.h"
#include "game/errors.h"
#include "ui/commands.h"

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

void ReportCityCommandBlocker(City *city, TileIndex blocker) {
  CityCommandFeedback *feedback = city->game->ui ? city->game->ui->command_feedback : NULL;
  if (feedback && feedback->blocker == INVALID_TILE) feedback->blocker = blocker;
}

void CancelCityCommandFeedback(City *city) {
  if (!city->game->ui) return;
  CityCommandFeedback *feedback = city->game->ui->command_feedback;
  if (feedback) feedback->cancelled = true;
}

void InitializeCityInteractionState(CityInterfaceState *ui) {
  ui->resume_speed = CITY_SPEED_NORMAL;
  ui->floating_status = true;
  ui->scrollwheel_mode = CITY_SCROLLWHEEL_ZOOM;
  ui->panning_view = NULL;
  ui->pan_button = 0;
  ui->wheel_zoom_remainder = 0;
  ui->budget_policy_totals = (CityBudgetPolicyGroupTotals){0};
  ui->file_preview_open_result = 0;
  ui->periodic_year_interval = 0;
  ui->menu_removal_blocked = 0;
  ui->tools_menu_remove_pending = 1;
  ui->extra_menu_enabled = 0;
  ui->outcome_dialog_active = 0;
  ui->dialog_pause_depth = 0;
  ui->dialog_input_blocked = 0;
  memset(ui->tool_indices, 0, sizeof(ui->tool_indices));
  memset(ui->tool_labels, 0, sizeof(ui->tool_labels));
  ui->tool_group = 0;
  ui->terrain_tool = 0;
  ui->wait_cursor_active = 1;
  ui->cursor_id = -1;
  ui->input_loop_active = 0;
  ui->pointer_mode = 0;
  ui->policy_dialog_value = 0;
  ui->budget_scroll_direction = 0;
  ui->budget_scroll_latch = 0;
  ui->reset_word = 0;
  memset(&ui->selection, 0, sizeof(ui->selection));
  ui->replacement_prompt_suppressed = false;
  ui->restore_frame_tools = false;
  ui->exit_cancel_enabled = false;
  ui->save_prompt_active = false;
  ui->startup_choice = 0;
}

bool CanPresentCityFileDialogs(const CityInterfaceState *ui) {
  return ui->file_dialog.desktop != NULL;
}

void EnableCityDeveloperTools(City *city) {
  if (city->game->ui) city->game->ui->developer_enabled = true;
}

void SetCityProblemNotice(City *city, const char *text) {
  if (city->game->ui) SetCityProblemDescription(&city->game->ui->status, text);
}

bool IsCityOutcomeNoticeActive(const City *city) {
  return city->game->ui && city->game->ui->outcome_dialog_active;
}

bool SetCityDemolitionSuppressed(City *city, bool suppressed) {
  Drawing *drawing = city->game->drawing;
  bool previous = drawing && drawing->demolition_suppressed;
  if (drawing) drawing->demolition_suppressed = suppressed;
  return previous;
}

void RefreshCityLoadedMusicPreference(City *city) {
  CityAudioState *audio = city->game->audio;
  if (audio->live_open) audio->music_enabled = city->game->ui->jukebox.playing != 0;
}

void SetCitySavePromptActive(City *city, bool active) {
  if (city->game->ui) city->game->ui->save_prompt_active = active;
}

bool Sc2kMainWindowIsIconic(const CityInterfaceState *ui) {
  return ui->main_window_iconic;
}

bool IsCityPresentationMinimized(const City *city) {
  return city->game->ui && Sc2kMainWindowIsIconic(city->game->ui);
}
