// Game startup, presentation cadence and simulation scheduling.
#include "city/disasters/types.h"
#include "game/application.h"
#include "city/engine/simulation.h"

#include <string.h>

#include "ui/frontend.h"
#include "city/engine/city_types.h"
#include "game/state.h"
#include "city/engine/initialization.h"
#include "city/events/events.h"
#include "city/state.h"
#include "city/saveload/file.h"
#include "game/errors.h"
#include "game/observation.h"
#include "game/state.h"
#include "audio/state.h"
#include "drawing/state.h"
#include "assets/state.h"
#include "ui/map.h"
#include "ui/news/newspaper.h"
#include "ui/startup/services.h"
#include "drawing/drawing.h"
#include "drawing/maps/map_render.h"
#include "ui/charts/charts.h"
#include "ui/commands.h"
#include "ui/dialogs/dialogs.h"
#include "ui/state.h"
#include "ui/tools/tools.h"

// Startup choices, asynchronous file requests and ordinary simulation idle.
void UpdateGame(Game *game) {
  City *city = game->city;
  Drawing *drawing = game->drawing;
  CityInterfaceState *ui = game->ui;
  i32 animated_fast = drawing->palette.fast_due == 1;
  if (animated_fast) {
    AnimateCityPalette(city, 0);
    drawing->palette.fast_due = 0;
  }
  if (drawing->palette.slow_due == 1) {
    AnimateCityPalette(city, 1);
    drawing->palette.slow_due = 0;
  }
  if (game->idle_state == CITY_IDLE_TOOL_PREVIEW) {
    if (city->settings.timer_mode == CITY_TIMER_STOPPED) {
      if (game->idle_inputs.active_view != 0) UpdateCityMapToolPreview(game);
      return;
    }
    game->idle_state = CITY_IDLE_PLAY;
    game->idle_entry = 1;
    CompleteCityTerrainEditing(game);
  }
  if (game->idle_state == CITY_IDLE_STARTUP_MENU) {
    // The retained tick finishes its remaining outcome notices before startup can begin.
    if (city->tick_continuations) return;
    if (game->exit_requested) return;
    if (game->idle_entry != 0) {
      u32 path_mode = game->path_open_flag;
      if (path_mode == CITY_PATH_OPEN_NONE) {
        if (game->observation && game->observation->choose_startup_menu) {
          ui->startup_choice = game->observation->choose_startup_menu(city);
          game->idle_state = CITY_IDLE_STARTUP_CHOICE;
        } else if (!OpenCityStartupMenu(city)) {
          SetGameError(game, SC2K_ERROR_STARTUP_ABORT);
          return;
        }
      } else if (path_mode == CITY_PATH_OPEN_CITY || path_mode == CITY_PATH_OPEN_SCENARIO) {
        game->idle_state = CITY_IDLE_COMMAND_LINE_FILE;
      }
    }
    game->idle_entry = 0;
    int choice;
    if (game->idle_state == CITY_IDLE_STARTUP_MENU && TakeCityStartupMenuChoice(city, &choice)) {
      ui->startup_choice = choice;
      game->idle_state = CITY_IDLE_STARTUP_CHOICE;
    }
    if (game->idle_state != CITY_IDLE_STARTUP_MENU) game->idle_entry = 1;
    return;
  }
  if (game->idle_state == CITY_IDLE_STARTUP_CHOICE) {
    if (game->idle_entry != 0) {
      game->idle_entry = 0;
      if ((!game->observation || !game->observation->choose_startup_menu) &&
          (ui->startup_choice == CITY_STARTUP_LOAD_CITY ||
              ui->startup_choice == CITY_STARTUP_NEW_CITY || ui->startup_choice == CITY_STARTUP_LOAD_SCENARIO ||
              ui->startup_choice == CITY_STARTUP_EDIT_MAP)) {
        if (ui->startup_choice == CITY_STARTUP_NEW_CITY) RequestNewCity(game);
        else if (ui->startup_choice == CITY_STARTUP_EDIT_MAP) RequestCityTerrainEditor(game);
        else if (ui->startup_choice == CITY_STARTUP_LOAD_SCENARIO) RequestCityScenarioOpen(game);
        else RequestCityFileOpen(game);
        return;
      }
      switch (ui->startup_choice) {
      case CITY_STARTUP_LOAD_CITY:
        game->idle_state = CITY_IDLE_LOAD_CITY;
        ui->restore_frame_tools = false;
        OpenCityFromFileDialog(city);
        break;
      case CITY_STARTUP_NEW_CITY:
        game->idle_state = CITY_IDLE_NEW_CITY;
        HandleNewCityCommand(city);
        break;
      case CITY_STARTUP_EDIT_MAP:
        game->idle_state = CITY_IDLE_TERRAIN;
        InitializeNewCityTerrainSession(city);
        game->idle_state = (u32)CITY_IDLE_PLAY;
        break;
      case CITY_STARTUP_LOAD_SCENARIO:
        game->idle_state = CITY_IDLE_LOAD_SCENARIO;
        ui->restore_frame_tools = false;
        OpenCityScenarioFromFileDialog(city);
        break;
      case CITY_STARTUP_QUIT:
        ui->save_prompt_active = true;
        HandleCityExitCommand(city);
        if (!game->observation || !game->observation->choose_startup_menu) {
          game->idle_state = CITY_IDLE_STARTUP_MENU;
          game->idle_entry = 1;
          return;
        }
        break;
      default:
        break;
      }
      game->idle_entry = 1;
    }
    return;
  }
  if (game->idle_state == CITY_IDLE_COMMAND_LINE_FILE) {
    if (game->idle_entry != 0) {
      game->idle_entry = 0;
      SetCityCursor(city, ui->selected_cursor, 1);
      ui->wait_cursor_active = 0;
      SetCityCursor(city, ui->selected_cursor, 1);

      bool opening_city = game->path_open_flag == CITY_PATH_OPEN_CITY;
      if (opening_city) {
        ui->restore_frame_tools = false;
        game->idle_state = CITY_IDLE_LOAD_CITY;
      } else {
        game->idle_state = CITY_IDLE_LOAD_SCENARIO;
        ui->restore_frame_tools = false;
      }
      // Game retains the filename until shutdown, including across nested notices.
      const char *path = CityPathText(&game->command_line_path);
      if (opening_city && (!game->observation || !game->observation->choose_startup_menu))
        RequestCityFilePath(game, path);
      else if (opening_city)
        OpenCityFromPath(city, path);
      else if (!game->observation || !game->observation->choose_startup_menu)
        OpenCityScenarioPath(game, path, true);
      else
        OpenCityScenarioFromPath(city, path);
    }
    return;
  }
  if (game->idle_state >= CITY_IDLE_LOAD_CITY && game->idle_state <= CITY_IDLE_LOAD_SCENARIO)
    return;
  if (game->idle_blocked != 0 || game->delay_blocked || ui->dialog_pause_depth)
    return;
  // Event movement consumes City RNG; enter only between completed phases.
  if (animated_fast && !city->simulation_work.phase) {
    FlushCityMapSelectionRedraw(city);
    UpdateCityMapEventTimer(city);
    drawing->map_state.selection_redraw_state = 0xFFFF;
    if (game->idle_inputs.active_view) UpdateCityMapToolPreview(game);
  }
  if (city->settings.timer_mode == CITY_TIMER_RUNNING && city->settings.speed != CITY_SPEED_PAUSED &&
      game->idle_inputs.active_view != 0 && city->cadence_counter.tick_due != 0) {
    if (game->idle_inputs.simulation_enabled != 0) {
      if (game->observation && game->observation->show_annual_budget) AdvanceCitySimulationPhase(city);
      else {
        u32 options = CITY_TICK_DEFER_DIALOGS | CITY_TICK_FROM_IDLE;
        if (game->idle_inputs.terrain_water_allowed) options |= CITY_TICK_ALLOW_TERRAIN_WATER;
        i64 result = AdvanceCitySimulationPhaseWithOptions(city, options);
        if (result >= 2) {
          DeliverCitySimulationEvents(game, 0, result, false);
          return;
        }
        if (!result) return;
      }
    }
    i64 result = FinishCityIdleSimulation(game, game->idle_inputs.terrain_water_allowed);
    if (result >= 2) DeliverCitySimulationEvents(game, 0, result, false);
    return;
  } else if (city->settings.timer_mode == CITY_TIMER_EVENT && city->settings.speed > CITY_SPEED_PAUSED &&
      city->cadence_counter.tick_due != 0) {
    u32 options = CITY_TICK_DEFER_DIALOGS | CITY_TICK_FROM_EVENT_IDLE;
    if (game->idle_inputs.active_view) options |= CITY_TICK_EVENT_VIEW_ACTIVE;
    if (game->idle_inputs.terrain_water_allowed) options |= CITY_TICK_ALLOW_TERRAIN_WATER;
    i64 result = AdvanceCityDisasterWithOptions(city, options);
    if (result >= 2) DeliverCitySimulationEvents(game, 0, result, false);
    else if (result) CompleteCityIdleSimulation(game, options);
    return;
  }
  return;
}

// The ordinary-idle caller can itself wait for a pending disaster's newspaper.
i64 FinishCityIdleSimulation(Game *game, bool terrain_water_allowed) {
  City *city = game->city;
  if (!city->simulation_work.phase && city->disasters.pending_type != CITY_DISASTER_NONE) {
    u32 options = CITY_TICK_DEFER_DIALOGS | CITY_TICK_FROM_IDLE;
    if (terrain_water_allowed) options |= CITY_TICK_ALLOW_TERRAIN_WATER;
    i64 result = AdvanceCityDisasterWithOptions(city, options);
    if (result != 1) return result;
  }
  CompleteCityIdleSimulation(game, CITY_TICK_FROM_IDLE);
  return GetGameError(game) == 0;
}

void CompleteCityIdleSimulation(Game *game, u32 options) {
  City *city = game->city;
  bool event_idle = (options & CITY_TICK_FROM_EVENT_IDLE) != 0;
  if (event_idle) ++city->event_runtime.tick_sample;
  if ((event_idle || !city->simulation_work.phase) && city->settings.speed != CITY_SPEED_UNLIMITED)
    city->cadence_counter.tick_due = 0;
}

void AdvanceGameCadencePresentation(Game *game, u32 counter, bool animate) {
  City *city = game->city;
  CityAudioState *audio = game->audio;
  CityPaletteState *palette = &game->drawing->palette;
  if (animate) {
    if (counter % 8 == 0) {
      palette->slow_due = 1;
    }
    palette->fast_due = 1;
  }
  if (palette->direct_animation != 0) {
    if (palette->fast_due == 1) {
      AnimateCityPalette(city, 0);
      palette->fast_due = 0;
    }
    if (palette->slow_due == 1) {
      AnimateCityPalette(city, 1);
      palette->slow_due = 0;
    }
  }
  if (audio->sound_delay != 0) {
    --audio->sound_delay;
    if (audio->sound_delay == 0)
      CompleteCitySoundPlayback(city->game);
  }
}

// Simulation passes must not append their feedback to the surrounding player command.
void FinishGameCitySimulationWork(Game *game) {
  City *city = game->city;
  CityInterfaceState *ui = city->game->ui;
  CityCommandFeedback *feedback = ui ? ui->command_feedback : NULL;
  if (ui) ui->command_feedback = NULL;
  FinishCitySimulationWork(city);
  if (ui) ui->command_feedback = feedback;
}
