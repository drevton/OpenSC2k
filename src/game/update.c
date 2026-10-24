#include "game/update.h"

#include "base/time.h"
#include "game/palette.h"
#include "game/city_lifecycle.h"
#include "game/commands.h"
#include "ui/city_lifecycle.h"

// Game startup, presentation cadence and simulation scheduling.
#include "city/disasters/types.h"
#include "game/application.h"
#include "ui/input.h"
#include "city/engine/simulation.h"

#include "city/engine/city_types.h"
#include "game/state.h"
#include "city/events/events.h"
#include "city/presentation.h"
#include "city/state.h"
#include "city/saveload/file.h"
#include "game/errors.h"
#include "game/observation.h"
#include "game/audio.h"
#include "audio/state.h"
#include "drawing/state.h"
#include "ui/map.h"
#include "ui/news/newspaper.h"
#include "ui/startup/startup.h"
#include "drawing/maps/map_render.h"
#include "ui/charts/charts.h"
#include "ui/commands.h"
#include "ui/dialogs/dialogs.h"
#include "ui/state.h"
#include "ui/windows/windows.h"
#include "ui/tools/tools.h"

// Input advances a waiting startup screen; SDL already dispatches controls.
void RecordGameInput(Game *game) {
  if (game->idle_state != CITY_IDLE_PLAY) game->idle_input = 1;
}

// Apply focus suppression, palette, music, tool visibility and map damage in their existing order.
void HandleApplicationActivation(Game *game, int active) {
  City *city = game->city;
  Drawing *drawing = game->drawing;
  CityInterfaceState *ui = game->ui;
  CityAudioState *audio = game->audio;
  if (game->exit_requested)
    return;
  game->ranked_events_suppressed = active == 0;
  if (active == 1 && drawing->palette.background_enabled == 0) {
    drawing->palette.background_enabled = 1;
    if (!ui->main_window_iconic) {
      if (drawing->palette.refresh_deferred == 0)
        RefreshActiveCityPalette(game);
      Sc2kDirtyMarkRect(&ui->desktop.dirty, drawing->client_rect);
      SetCityToolsVisible(game, true);
      if (audio->music_active == 0)
        PlayNextCityEventMusicSequence(game);
      PlayCitySound(city, INVALID_TILE, CITY_SOUND_ACTIVATION_REQUEST);
    }
    SetCityCursor(city, CITY_CURSOR_ARROW, 1);
  } else if (active == 0 && drawing->palette.background_enabled == 1) {
    drawing->palette.background_enabled = 0;
    StopCityMusicSequence(game);
    StopCityPlatformSound(game);
    Sc2kDirtyMarkRect(&ui->desktop.dirty, drawing->client_rect);
    InvalidateCityMap(game, NULL);
  }
}

// Poll one pending activation input, notifying observers before applying its UI effects. The installed comparison
// adapter retains its own input stream; ordinary polling leaves unrelated events queued.
void PumpGameActivation(Game *game) {
  const GameObservation *observer = game->observation;
  if (observer && observer->pump_activation) {
    observer->pump_activation(game);
    return;
  }
  bool active = false;
  int present = game->poll_activation != NULL ? game->poll_activation(game->activation_context, &active) : -1;
  if (present < 0) return;
  if (game->activation_polled != NULL)
    game->activation_polled(game, present != 0, active);
  if (present)
    HandleApplicationActivation(game, active);
}

// Startup choices, asynchronous file requests and ordinary simulation idle.
void UpdateGame(Game *game) {
  City *city = game->city;
  Drawing *drawing = game->drawing;
  CityInterfaceState *ui = game->ui;
  i32 animated_fast = drawing->palette.fast_due == 1;
  if (animated_fast) {
    AnimateCityPalette(game, 0);
    drawing->palette.fast_due = 0;
  }
  if (drawing->palette.slow_due == 1) {
    AnimateCityPalette(game, 1);
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
      if (!game->observation || !game->observation->choose_startup_menu) {
        switch (ui->startup_choice) {
        case CITY_STARTUP_LOAD_CITY:
          RequestCityFileOpen(game);
          return;
        case CITY_STARTUP_NEW_CITY:
          RequestNewCity(game);
          return;
        case CITY_STARTUP_EDIT_MAP:
          RequestCityTerrainEditor(game);
          return;
        case CITY_STARTUP_LOAD_SCENARIO:
          RequestCityScenarioOpen(game);
          return;
        default:
          break;
        }
      }
      switch (ui->startup_choice) {
      case CITY_STARTUP_LOAD_CITY:
        game->idle_state = CITY_IDLE_LOAD_CITY;
        ui->restore_frame_tools = false;
        OpenCityFromFileDialog(city);
        break;
      case CITY_STARTUP_NEW_CITY:
        game->idle_state = CITY_IDLE_NEW_CITY;
        RunCityCreationSetup(city);
        break;
      case CITY_STARTUP_EDIT_MAP:
        game->idle_state = CITY_IDLE_TERRAIN;
        RunCityTerrainEditorSetup(city);
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
      bool observed_startup = game->observation && game->observation->choose_startup_menu;
      if (opening_city) {
        if (observed_startup)
          OpenCityFromPath(city, path);
        else
          RequestCityFilePath(game, path);
      } else {
        if (observed_startup)
          OpenCityScenarioFromPath(city, path);
        else
          OpenCityScenarioPath(game, path, true);
      }
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
      AnimateCityPalette(game, 0);
      palette->fast_due = 0;
    }
    if (palette->slow_due == 1) {
      AnimateCityPalette(game, 1);
      palette->slow_due = 0;
    }
  }
  if (audio->sound_delay != 0) {
    --audio->sound_delay;
    if (audio->sound_delay == 0)
      CompleteCitySoundPlayback(game);
  }
}

// Simulation passes must not append their feedback to the surrounding player command.
void FinishGameCitySimulationWork(Game *game) {
  City *city = game->city;
  CityInterfaceState *ui = game->ui;
  CityCommandFeedback *feedback = ui ? ui->command_feedback : NULL;
  if (ui) ui->command_feedback = NULL;
  FinishCitySimulationWork(city);
  if (ui) ui->command_feedback = feedback;
}

// Defer teardown until the retained original Quit/Save caller has returned.
void RequestCityApplicationExit(Game *game) {
  game->close_requested = 1;
}

// Host time controls delivery count, never the deterministic work inside a batch.
// Always finish one batch, even if preparing/presenting the frame used its budget.
void RunCitySimulationFrame(Game *game, double deadline) {
  do {
    RunGameIdle(game, (GameIdleInputs){
        .active_view = true, .simulation_enabled = true, .terrain_water_allowed = true});
    if (game->execution_failed) break;
    City *city = game->city;
    if (game->close_requested || game->idle_blocked || game->delay_blocked ||
        game->idle_state != (u32)CITY_IDLE_PLAY || city->settings.speed == CITY_SPEED_PAUSED ||
        city->settings.timer_mode != CITY_TIMER_RUNNING || !city->cadence_counter.tick_due ||
        (!city->simulation_work.phase && city->settings.speed != CITY_SPEED_UNLIMITED)) break;
  } while (MonotonicSeconds() < deadline);
}

void RunGameIdle(Game *game, GameIdleInputs inputs) {
  GameIdleInputs previous = game->idle_inputs;
  game->idle_inputs = inputs;
  ClearGameError(game);
  UpdateGame(game);
  game->idle_inputs = previous;
  DeliverCitySimulationEvents(game, 0, 1, false);
  if (game->observation) return;
  City *city = game->city;
  CityInterfaceState *ui = game->ui;
  if (ui->newspaper_month != city->clock.month) {
    ui->newspaper_month = city->clock.month;
    if (!city->newspaper.enabled && !ui->newspaper_delivery_disabled &&
        (ui->newspaper_month == CITY_MONTH_APRIL || ui->newspaper_month == CITY_MONTH_AUGUST))
      OpenCityManualNewspaper(city, city->newspaper.edition_index);
  }
}
