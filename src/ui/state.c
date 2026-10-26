#include "ui/state.h"

#include <stdlib.h>
#include <string.h>

#include "city/presentation.h"
#include "city/state.h"
#include "game/state.h"
#include "ui/console.h"

void InitializeCityInteractionState(CityInterfaceState *ui) {
  ui->resume_speed = CITY_SPEED_NORMAL;
  ui->settings.tab = GAME_OPTIONS_TAB_GRAPHICS;
  ui->settings.preview_scale = 400;
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

void EnableCityDeveloperTools(City *city) {
  if (city->game->ui) city->game->ui->developer_enabled = true;
}

bool IsCityOutcomeNoticeActive(const City *city) {
  return city->game->ui && city->game->ui->outcome_dialog_active;
}

bool IsCityPresentationMinimized(const City *city) {
  return city->game->ui && city->game->ui->main_window_iconic;
}

void ReleaseCityInteractionState(CityInterfaceState *ui) {
  free(ui->signs.entries); free(ui->signs.order);
  free(ui->desktop.placement.cells);
  ReleaseCityConsole(ui);
  ui->pointer_stroke_count = 0;
}
