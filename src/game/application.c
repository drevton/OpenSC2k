#include "game/graphics.h"
#include "ui/updates.h"
#include "ui/cursor/cursor.h"
#include "base/file.h"
#include "game/timing.h"
#include "city/engine/clock.h"
#include "city/engine/simulation.h"
#include "game/application.h"

#include <errno.h>
#include <stdlib.h>

#include "assets/assets.h"
#include "assets/catalog.h"
#include "assets/state.h"
#include "game/audio.h"
#include "audio/backend.h"
#include "audio/state.h"
#include "base/time.h"
#include "base/version.h"
#include "city/engine/city_types.h"
#include "city/maps/layers.h"
#include "city/saveload/file.h"
#include "city/saveload/runtime.h"
#include "game/city_lifecycle.h"
#include "city/state.h"
#include "drawing/display.h"
#include "drawing/maps/map_effects.h"
#include "drawing/maps/map_events.h"
#include "drawing/maps/map_render.h"
#include "drawing/palette.h"
#include "drawing/state.h"
#include "game/commands.h"
#include "game/errors.h"
#include "game/frame.h"
#include "game/palette.h"
#include "game/state.h"
#include "game/update.h"
#include "ui/city_lifecycle.h"
#include "ui/commands.h"
#include "ui/console.h"
#include "ui/dialogs/budget.h"
#include "ui/dialogs/dialogs.h"
#include "ui/dialogs/file_dialog.h"
#include "ui/dialogs/game_options.h"
#include "ui/dialogs/map_data.h"
#include "ui/dialogs/scenario.h"
#include "ui/dialogs/spans.h"
#include "ui/input.h"
#include "ui/map.h"
#include "ui/map_views.h"
#include "ui/music/jukebox.h"
#include "ui/news/newspaper.h"
#include "ui/news/newspaper_text.h"
#include "ui/signs.h"
#include "ui/sdl.h"
#include "ui/state.h"
#include "ui/startup/startup.h"
#include "ui/text/text.h"
#include "ui/tools/tools.h"
#include "ui/windows/state.h"
#include "ui/windows/windows.h"

static void PrintUsage(const char *name);

// One startup allocation backs the native owners; map-size-dependent data is separate.
typedef struct {
  Game game;
  City city;
  Drawing drawing;
  CityInterfaceState ui;
  CityAudioState audio;
  CityResourceState assets;
} GameStorage;

static void PrintUsage(const char *name) {
  printf(
      "Usage: %s [city.SC2] [--assets DIR] [--frames N] [--headless] [--close-after SECONDS]\n"
      "  [--screenshot FILE.ppm] [--input-script FILE] [--live-input-script FILE]\n"
      "  [--screenshot-display FILE.ppm (physical UI pixels)]\n"
      "  [--ui-scale 1..5 (quarter steps, or continuous with Ctrl in Game Options)]\n"
      "  [--debug-city (at least $1 billion, no automatic newspapers, all buildings unlocked)]\n"
      "    --debug-cities is an alias; F6: full map repaint; F7: repaint coalescing\n"
      "    F8: redraw areas; F9: render statistics\n"
      "    F11: toggle 2x SDL frame magnification around the pointer in debug-city mode\n"
      "  [--profile-redraw] [--disable-map-cache] [--open-newspaper 0..6]\n"
      "  [--newspaper-timer-ms 1..60000 (diagnostic timer acceleration)]\n"
      "  [--benchmark-redraw N (requires --headless; repeats drawing without simulation ticks)]\n"
      "Cmd/Ctrl+O: Open; Cmd/Ctrl+S: Save; Cmd/Ctrl+Shift+S: Save As; Space: start/pause; B: budget; J: Jazz Jukebox; "
      "O: ordinances; N: newspaper; arrows: pan; "
      "R: rotate; 1/2/3/4: zoom; F8: extra viewport; Escape: quit\n",
      name);
}

static bool RunInputScript(Game *game, const char *path, const char *city_path) {
  FILE *script = fopen(path, "r");
  if (script == NULL)
    return false;
  game->input_script = script;
  game->input_script_city_path = city_path;
  // This is the headless/script host loop. A suspended action leaves the next
  // script line unread; each iteration advances at most one continuation step.
  while (!game->execution_failed && !game->close_requested) {
    PollGameSettings(game, MonotonicSeconds());
    SampleGameTime(game, MonotonicSeconds());
    PollCityButtonFeedback(game, ReadGameTime(game, GAME_TIME_REALTIME));
    if (game->animation_token) {
      if (!StepCityAnimation(game) && !game->observation) WaitForCityCanvasFrame(1);
    } else if (!ApplicationNextScriptAction(game)) break;
  }
  game->input_script = NULL;
  game->input_script_city_path = NULL;
  i32 ok = !ferror(script);
  return fclose(script) == 0 && ok && !game->execution_failed;
}

static int PollGameCanvasActivation(void *context, bool *active) {
  return PollCanvasActivation(context, active);
}

typedef struct {
  GameStartupOptions startup;
  const GameApplicationHooks *hooks;
  void *context;
  const char *assets, *image_path;
  int frames, open_newspaper, nested_newspaper, ui_scale, benchmark_redraw;
  bool image_physical, ui_scale_override, city_path_supplied;
  bool debug_city, profile_redraw, disable_map_cache, profile_newspaper;
  double close_after, newspaper_seconds;
} ApplicationOptions;

static int ParseApplicationOptions(int argc, char **argv, ApplicationOptions *options) {
  const GameApplicationHooks *hooks = options->hooks;
  void *context = options->context;
  for (i32 index = 1; index < argc; ++index) {
    const char *arg = argv[index];
    if (hooks && hooks->parse_option) {
      int consumed = hooks->parse_option(context, arg, index + 1 < argc ? argv[index + 1] : NULL);
      if (consumed < 0) return 2;
      if (consumed) { index += consumed - 1; continue; }
    }
    if (!strcmp(arg, "--help")) {
      PrintUsage(argv[0]);
      if (hooks && hooks->usage) hooks->usage();
      return 0;
    }
    if (!strcmp(arg, "--debug-city") || !strcmp(arg, "--debug-cities")) {
      options->debug_city = true;
      continue;
    }
    if (!strcmp(arg, "--headless")) {
      options->startup.headless = 1;
      continue;
    }
    if (!strcmp(arg, "--profile-redraw")) {
      options->profile_redraw = 1;
      continue;
    }
    if (!strcmp(arg, "--disable-map-cache")) {
      options->disable_map_cache = 1;
      continue;
    }
    if (arg[0] != '-') {
      options->startup.city_path = arg;
      options->city_path_supplied = true;
      continue;
    }
    if (index + 1 == argc) {
      PrintUsage(argv[0]);
      if (hooks && hooks->usage) hooks->usage();
      return 2;
    }
    const char *value = argv[++index];
    if (!strcmp(arg, "--assets")) {
      options->assets = value;
      continue;
    } else if (!strcmp(arg, "--ui-scale")) {
      char *end;
      double scale = strtod(value, &end);
      if (*end || !(scale >= 1 && scale <= 5))
        return 2;
      options->ui_scale = (i32)(scale * 100 + 0.5);
      options->ui_scale_override = true;
      continue;
    }
    if (!strcmp(arg, "--newspaper-timer-ms")) {
      char *end;
      long milliseconds = strtol(value, &end, 10);
      if (*end || milliseconds < 1 || milliseconds > 60000)
        return 1;
      options->newspaper_seconds = milliseconds / 1000.0;
    } else if (!strcmp(arg, "--nest-newspaper")) {
      char *end;
      long edition = strtol(value, &end, 10);
      if (*end || edition < 0 || edition > 6)
        return 1;
      options->nested_newspaper = (i32)edition;
    } else if (!strcmp(arg, "--open-newspaper")) {
      char *end;
      long edition = strtol(value, &end, 10);
      if (*end || edition < 0 || edition > 6)
        return 1;
      options->open_newspaper = (i32)edition;
      options->profile_newspaper = 1;
    } else if (!strcmp(arg, "--input-script"))
      options->startup.script_path = value;
    else if (!strcmp(arg, "--live-input-script"))
      options->startup.live_script_path = value;
    else if (!strcmp(arg, "--screenshot") || !strcmp(arg, "--screenshot-display")) {
      options->image_path = value;
      options->image_physical = !strcmp(arg, "--screenshot-display");
    } else if (!strcmp(arg, "--benchmark-redraw")) {
      char *end;
      long number = strtol(value, &end, 10);
      if (*end || number < 1 || number > 10000)
        return 2;
      options->benchmark_redraw = (i32)number;
    }
    else if (!strcmp(arg, "--frames") || !strcmp(arg, "--close-after")) {
      char *end;
      errno = 0;
      double number = strtod(value, &end);
      if (errno || *end || !(number >= 0 && number <= 2147483647) ||
          (!strcmp(arg, "--frames") && number != (double)(i32)number))
        return 2;
      if (!strcmp(arg, "--frames"))
        options->frames = (i32)number;
      else
        options->close_after = number;
    } else {
      PrintUsage(argv[0]);
      if (hooks && hooks->usage) hooks->usage();
      return 2;
    }
  }
  if (hooks && hooks->validate && !hooks->validate(context, &options->startup)) return 2;
  if (options->startup.headless && options->startup.live_script_path != NULL) {
    fprintf(stderr, "--live-input-script requires the SDL window.\n");
    return 2;
  }
  if (options->benchmark_redraw && !options->startup.headless) {
    fprintf(stderr, "--benchmark-redraw requires --headless.\n");
    return 2;
  }
  return -1;
}

static bool RunHeadlessApplication(Game *game, const ApplicationOptions *options) {
  City *city = game->city;
  Drawing *drawing = game->drawing;
  const GameApplicationHooks *hooks = options->hooks;
  void *context = options->context;
  const char *city_path = options->startup.city_path, *script_path = options->startup.script_path;
  int frames = options->frames;

    if (!PrepareCityMapSprites(game)) return false;
    if (script_path != NULL && !RunInputScript(game, script_path, city_path))
      return false;
    for (i32 index = 0; index < frames && !game->execution_failed && !game->close_requested; ++index) {
      i64 advanced = hooks && hooks->advance_phase ? hooks->advance_phase(game, context) :
          AdvanceCitySimulationPhaseWithOptions(game->city, 0);
      DeliverCitySimulationEvents(game, 0, advanced, false);
    }
    if (game->execution_failed || !RedrawCityCanvas(city, NULL))
      return false;
    if (drawing->benchmark_redraw) {
      // Exclude startup and the first draw, which warms resource and allocator caches.
      drawing->snapshot_seconds = drawing->map_seconds = drawing->canvas_seconds = 0;
      double benchmark_started = MonotonicSeconds();
      for (i32 index = 0; index < drawing->benchmark_redraw; ++index) {
        drawing->main_cache.valid = 0;
        if (!RedrawCityCanvas(city, NULL))
          return false;
      }
      double milliseconds = 1000.0 / drawing->benchmark_redraw;
      double frame_cpu_ms = (MonotonicSeconds() - benchmark_started) * milliseconds;
      fprintf(stderr, "redraw_samples=%d snapshot_ms=%.3f map_ms=%.3f canvas_ms=%.3f "
          "total_ms=%.3f frame_cpu_ms=%.3f\n",
          drawing->benchmark_redraw, drawing->snapshot_seconds * milliseconds, drawing->map_seconds * milliseconds,
          drawing->canvas_seconds * milliseconds,
          (drawing->snapshot_seconds + drawing->map_seconds + drawing->canvas_seconds) * milliseconds, frame_cpu_ms);
    }

  return true;
}

static bool RunLiveApplication(Game *game, CanvasSDL *display, const ApplicationOptions *options) {
  City *city = game->city;
  Drawing *drawing = game->drawing;
  CityInterfaceState *ui = game->ui;
  CityAudioState *audio = game->audio;
  const GameApplicationHooks *hooks = options->hooks;
  void *context = options->context;
  const char *city_path = options->startup.city_path, *script_path = options->startup.script_path;
  const char *live_script_path = options->startup.live_script_path;
  int frames = options->frames, open_newspaper = options->open_newspaper;
  double close_after = options->close_after;
  bool show_startup_menu = !hooks && !options->startup.headless && !options->city_path_supplied &&
      !script_path && !live_script_path;

    SetCityMapNavigationPresentation(game, true, true);
    ui->command_display = display;
    ui->display = display;
    drawing->vehicles.enabled = true;
    ConfigureCityDiagnostics(game);
    if (drawing->options.fullscreen && Sc2kSDLFullscreen(display, 1))
      fprintf(stderr, "Fullscreen: %s\n", Sc2kSDLError());
    SyncCityInterface(game, display, 1);
    // Drawing installs the batch scale before launching workers; desired scales stay local until then.
    int loading_scales[2], loading_count = CityMapSpriteScales(drawing, loading_scales);
    int loading_completed = 0, loading_total = 1;
    int loading_preview_width = (CITY_BUILDING_PREVIEW_WIDTH * drawing->options.scale + 99) / 100;
    int loading_preview_height = (CITY_BUILDING_PREVIEW_HEIGHT * drawing->options.scale + 99) / 100;
    bool loading = true, sources_ready = false, preparing = false;
    double next_frame_time = MonotonicSeconds();
    while (!game->close_requested && !game->execution_failed &&
        (loading || !close_after || MonotonicSeconds() - game->host_start_time < close_after)) {
      PollGameSettings(game, MonotonicSeconds());
      CanvasSDLEvent event;
      if (loading) {
        bool resized = false;
        int wait_ms = IntMax(0, (int)((next_frame_time - MonotonicSeconds()) * 1000));
        for (int count = 0; count < 64 && Sc2kSDLEvent(display, &event, count ? 0 : wait_ms); ++count) {
          if (event.kind == CANVAS_SDL_CLOSE) { game->close_requested = true; return true; }
          if (event.kind == CANVAS_SDL_EXPOSE) resized = true;
        }
        if (resized) {
          SyncCityInterface(game, display, 1);
          int scales[2], count = CityMapSpriteScales(drawing, scales);
          int preview_width = (CITY_BUILDING_PREVIEW_WIDTH * drawing->options.scale + 99) / 100;
          int preview_height = (CITY_BUILDING_PREVIEW_HEIGHT * drawing->options.scale + 99) / 100;
          if (count != loading_count || memcmp(scales, loading_scales, count * sizeof(int)) ||
              preview_width != loading_preview_width || preview_height != loading_preview_height) {
            FinishMapSpritePreparation(&drawing->sprite_cache, true);
            FreeMapSpriteScales(&drawing->sprite_cache);
            memcpy(loading_scales, scales, count * sizeof(int));
            loading_count = count;
            loading_preview_width = preview_width;
            loading_preview_height = preview_height;
            loading_completed = 0;
            preparing = false;
          }
        }
        if (game->execution_failed) return false;
        if (preparing) {
          loading_completed = MapSpritePreparationProgress(&drawing->sprite_cache, &loading_total);
          if (loading_completed < 0) { fprintf(stderr, "Sprite preparation failed\n"); return false; }
        }
        CityIntroStage stage = sources_ready ? CITY_INTRO_PREPARING_SPRITES : CITY_INTRO_PREPARING_GRAPHICS;
        if (!DrawCityLoadingScreen(game, display, stage, loading_completed, loading_total, false)) return false;
        int refresh = drawing->options.refresh_rate;
        double frame_interval = 1.0 / (refresh > 0 ? refresh : 60);
        next_frame_time = MonotonicSeconds() + frame_interval;
        if (!sources_ready) {
          if (!PrepareCityMapSpriteSources(game)) return false;
          sources_ready = true;
          continue;
        }
        if (!preparing) {
          if (!StartMapSpritePreparation(&drawing->sprite_cache, loading_scales, loading_count,
              loading_preview_width, loading_preview_height, drawing->colors)) {
            fprintf(stderr, "Cannot start sprite preparation workers: %s\n", Sc2kSDLError());
            return false;
          }
          preparing = true;
          continue;
        }
        if (loading_completed < loading_total) continue;
        if (!FinishMapSpritePreparation(&drawing->sprite_cache, false)) return false;
        preparing = false;
        SyncCityInterface(game, display, 1);
        if (script_path != NULL && !RunInputScript(game, script_path, city_path)) return false;
        for (int index = 0; index < frames && !game->execution_failed && !game->close_requested; ++index) {
          i64 advanced = hooks && hooks->advance_phase ? hooks->advance_phase(game, context) :
              AdvanceCitySimulationPhaseWithOptions(game->city, 0);
          DeliverCitySimulationEvents(game, 0, advanced, false);
        }
        if (game->execution_failed || game->close_requested) return false;
        game->host_start_time = MonotonicSeconds();
        game->activation_context = display;
        game->poll_activation = PollGameCanvasActivation;
        StartGameCadence(game, game->host_start_time);
        game->close_deadline = close_after ? game->host_start_time + close_after : 0;
        if (!hooks) {
          ui->terrain_setup_enabled = true;
          ui->terrain_settings = (CityTerrainSettings){.seed = (u32)(u64)(MonotonicSeconds() * 1000000),
              .features = kRiver, .hills = 12, .water = 5, .trees = 15, .width = 128, .height = 128,
              .smoothness = CITY_TERRAIN_SMOOTH, .variety = CITY_TERRAIN_VARIETY_LOW,
              .small_rivers = CITY_TERRAIN_SMALL_RIVERS_MEDIUM,
              .climate = CITY_TERRAIN_NORMAL, .snow_coverage = 40, .desert_coverage = 50};
          CopyText(ui->terrain_mayor, sizeof(ui->terrain_mayor), "Mayor");
          if (show_startup_menu) {
            game->idle_state = CITY_IDLE_STARTUP_MENU;
            game->idle_entry = 1;
            if (!OpenCityStartupMenu(city)) return false;
          }
        }
        if (audio->jukebox.playing) NativeAudioPlay(audio);
        if (!RedrawCityCanvas(city, display))
          return false;
        if (open_newspaper >= 0) {
          Sc2kSDLTitle(display, "SimCity newspaper lifetime diagnostic");
          OpenCityManualNewspaper(city, open_newspaper);
        }
        loading = false;
        next_frame_time = MonotonicSeconds();
        continue;
      }
      i32 dirty = 0;
      int wait_ms = 16;
      // A cost label uses presentation time, including while the city is paused.
      if (drawing->cost_texts_active)
        wait_ms = IntMin(wait_ms,
            IntMax(0, (int)((drawing->cost_text_time + 1.0 / 60 - MonotonicSeconds()) * 1000)));
      if (city->settings.speed == CITY_SPEED_PAUSED && game->idle_state == (u32)CITY_IDLE_PLAY &&
          !ui->music_press.active && !ui->viewport_pressed_window && !ui->signs.pressed_window &&
          !ui->desktop.menu.pressed && !ui->button_feedback.repeating &&
          !drawing->cost_texts_active &&
          !ui->pointer_stroke_count &&
          !audio->jukebox.playing && !audio->music_notify && !audio->sound_playing && !audio->cached_sound_playing) {
        // SDL wakes immediately for input; a quiet paused city needs only its next cadence.
        wait_ms = IntClamp((int)((game->next_cadence_time - MonotonicSeconds()) * 1000) + 1,
            0, CITY_CADENCE_INTERVAL_MS);
      }
      if (ui->startup_newspaper_open_at > 0)
        wait_ms = IntMin(wait_ms, IntMax(0, (int)((ui->startup_newspaper_open_at - MonotonicSeconds()) * 1000) + 1));
      bool batch_frame = city->game->spread_simulation && city->map_cell_count > CITY_MAP_CELL_COUNT;
      if (batch_frame && city->settings.speed != CITY_SPEED_PAUSED && city->cadence_counter.tick_due)
        wait_ms = IntMin(wait_ms, IntMax(0, (int)((next_frame_time - MonotonicSeconds()) * 1000)));
      CityTooltip *tooltip = &ui->desktop.tooltip;
      if (tooltip->pointer_valid && tooltip->text[0] && !tooltip->visible)
        wait_ms = IntMin(wait_ms, IntMax(0, (int)((tooltip->show_at - MonotonicSeconds()) * 1000) + 1));
      if (game->animation_token && game->animation_delay.active) {
        u32 now = (u32)(u64)(ReadGameTime(game, GAME_TIME_REALTIME) * 1000);
        i32 remaining = (i32)(game->animation_delay.deadline - now);
        wait_ms = IntMin(wait_ms, IntMax(0, remaining));
      }
      if (game->delay_blocked) WaitForCityCanvasFrame(wait_ms);
      double frame_started = 0;
      for (i32 count = 0; count < 64 && !game->delay_blocked && !game->close_requested && !game->execution_failed &&
          Sc2kSDLEvent(display, &event, count ? 0 : wait_ms); ++count) {
        if (!count) {
          frame_started = MonotonicSeconds();
          SampleGameTime(game, frame_started);
        }
        // Redraw hover only when it crosses a cell or changes its footprint.
        // Key releases need no frame unless their modifiers change the preview.
        // Other release effects publish their own UI damage during dispatch.
        bool release_changes_preview = event.kind == CANVAS_SDL_KEY_UP &&
            (ui->placement_hover_shift != event.shift || ui->placement_hover_ctrl != (event.ctrl & 1));
        if (event.kind != CANVAS_SDL_NONE &&
            (event.kind != CANVAS_SDL_KEY_UP || release_changes_preview) &&
            (event.kind != CANVAS_SDL_POINTER_MOVE || ui->desktop.dragging != CITY_CANVAS_DRAG_NONE ||
                ui->pointer_stroke_count || ui->jukebox_slider || ui->options_slider || ui->pan_button))
          dirty = 1;
        DispatchCityCanvasEvent(city, &event);
        if (live_script_path != NULL && event.kind == CANVAS_SDL_FOCUS_GAINED) {
          if (!RunInputScript(game, live_script_path, city_path))
            return false;
          live_script_path = NULL;
        }
        dirty |= ui->placement_dirty || (ui->desktop.dirty.count != 0 || ui->desktop.window_damage.count != 0);
      }
      if (game->close_requested || game->execution_failed)
        break;
      if (!game->delay_blocked) {
        CompleteCityAbandon(game);
        CompleteCityWindowCallbacks(city);
        CompleteCityScenarioSelection(city);
        CompleteCityCreationWindow(city);
        CompleteCityFileDialogs(city);
      }
      if (!frame_started) frame_started = MonotonicSeconds();
      batch_frame = city->game->spread_simulation && city->map_cell_count > CITY_MAP_CELL_COUNT;
      SampleGameTime(game, MonotonicSeconds());
      bool poll_timers = !game->animation_delay.active || game->animation_delay.poll_timers;
      i32 timer_ready = poll_timers && !ui->pointer_stroke_count && PollGameCadence(game, MonotonicSeconds());
      if (timer_ready) {
        if (game->delay_blocked)
          UpdateCityCadence(city);
        else {
          UpdateCityCadence(game->city);
          ApplicationBudgetPoll(city);
        }
      }
      // Consume queued cadence work; the fastest speed retains tick_due for the next idle pass.
      i32 idle_pending = game->idle_state != (u32)CITY_IDLE_PLAY ||
          drawing->palette.fast_due == 1 || drawing->palette.slow_due == 1 ||
          (city->settings.speed != CITY_SPEED_PAUSED && city->cadence_counter.tick_due != 0);
      bool run_idle = !game->delay_blocked && !ui->pointer_stroke_count &&
          (!ui->dialog_pause_requested || ui->dialog_pause_active) && (timer_ready || idle_pending);
      if (run_idle && !batch_frame) {
        RunGameIdle(game, (CityIdleInputs){true, true, true});
        // A paused cadence still delivers commands, but unchanged pixels stay retained.
        dirty |= game->idle_state != (u32)CITY_IDLE_PLAY;
      }
      if (Sc2kCanvasJukeboxTick(ui, (u32)(u64)(MonotonicSeconds() * 1000))) {
        DirtyCityMusicWindows(game);
        dirty = 1;
      }
      if (!game->delay_blocked) {
        dirty |= PollCityToolbar(game, (u32)(u64)(MonotonicSeconds() * 1000));
        dirty |= PollCityMapViewControls(city);
        dirty |= PollCitySigns(city);
      }
      dirty |= PollCityMusic(game);
      if (!game->delay_blocked && NativeAudioSoundFinished(audio)) {
        CompleteCitySoundPlayback(game);
        dirty = 1;
      }
      SampleGameTime(game, MonotonicSeconds());
      double ui_time = ReadGameTime(game, GAME_TIME_REALTIME);
      double world_time = ReadGameTime(game, GAME_TIME_UNPAUSED);
      if (ui->startup_newspaper_open_at > 0 && ui_time >= ui->startup_newspaper_open_at &&
          !game->delay_blocked && !ui->dialog_pause_requested && !game->close_requested && !game->execution_failed) {
        int edition = ui->startup_newspaper_edition;
        ui->startup_newspaper_open_at = 0;
        OpenCityManualNewspaper(city, edition);
        dirty = 1;
      }
      dirty |= StepCityAnimation(game);
      bool palette_dirty = AnimateCityCanvasPalette(game, world_time);
      dirty |= PrepareCityCanvasDamage(city);
      dirty |= UpdateCityWindowTimers(game, ui_time);
      dirty |= UpdateCityVehicleAnimation(city, world_time);
      dirty |= UpdateCityRoadTraffic(city, (u32)(u64)(world_time * 256));
      dirty |= UpdateCityPowerAnimation(drawing, city->settings.speed, world_time);
      dirty |= UpdateCityCostTexts(drawing, ui_time);
      dirty |= UpdateCityDemolitionEffects(drawing, ui_time);
      dirty |= UpdateCityScreenShake(drawing, world_time);
      if (dirty && !PrepareCityCanvas(city, display)) game->execution_failed = 1;
      // Measure CPU preparation separately: SDL presentation may wait for vsync.
      double frame_work = MonotonicSeconds() - frame_started;
      if ((dirty || palette_dirty) && !game->execution_failed) {
        if (PresentCityDesktop(city, display) != 0) game->execution_failed = 1;
        else if (ui->startup_newspaper_open_at < 0) ui->startup_newspaper_open_at = MonotonicSeconds() + 0.5;
      }
      int refresh = drawing->options.refresh_rate;
      double frame_interval = 1.0 / (refresh > 0 ? refresh : 60);
      double remaining = frame_interval - frame_work;
      next_frame_time = MonotonicSeconds() + (remaining > 0 ? remaining : 0);
      if (batch_frame && run_idle && !game->execution_failed && !game->close_requested)
        RunCitySimulationFrame(game, next_frame_time);
    }

  return true;
}

static int CloseApplication(GameStorage *storage, CanvasSDL *display,
    const ApplicationOptions *options, int result) {
  Game *game = &storage->game;
  City *city = game->city;
  Drawing *drawing = game->drawing;
  CityAudioState *audio = game->audio;
  const GameApplicationHooks *hooks = options->hooks;
  void *context = options->context;
  CloseGameSettings(game);
  FinishMapSpritePreparation(&drawing->sprite_cache, true);
  game->close_requested = true;
  BeginCityInterfaceShutdown(game);
  game->activation_context = NULL;
  game->poll_activation = NULL;
  game->present_map = NULL;
  drawing->tile_damage_enabled = false;
  CompleteCityInterfaceCallers(game);
  ReleaseCityTickContinuations(city);
  ReleaseCityPostedCommands(game);
  ReleaseCityInterfacePresentation(game);
  if (audio->live_open) NativeAudioClose(audio);
  if (hooks && hooks->close_streams && !hooks->close_streams(context)) result = 1;
  if (display != NULL)
    Sc2kSDLClose(display);
  ReleaseDrawing(drawing);
  ReleaseCityInteractionState(game->ui);
  ReleaseGameCityAssets(game);
  if (hooks && hooks->release) hooks->release(game, context);
  ReleaseSimulation(city);
  ReleaseCityFilePaths(game);
  CloseCityDiagnostics(game);
  free(storage);
  return result;
}

int RunGameApplication(int argc, char **argv, const GameApplicationHooks *hooks, void *context) {
  ApplicationOptions options = {.hooks = hooks, .context = context, .assets = ".",
      .startup.city_path = "", .open_newspaper = -1, .nested_newspaper = -1, .newspaper_seconds = 60};
  int parsed = ParseApplicationOptions(argc, argv, &options);
  if (parsed >= 0) return parsed;
  GameStorage *storage = calloc(1, sizeof(*storage));
  u8 *maps = malloc(CityMapStorageSize(CITY_MAP_SIZE, CITY_MAP_SIZE));
  if (storage == NULL || maps == NULL) {
    free(storage);
    free(maps);
    fprintf(stderr, "Cannot allocate game state\n");
    return 1;
  }
  Game *game = &storage->game;
  City *city = &storage->city;
  InitializeSimulation(city, maps);
  *game = (Game){.city = city, .drawing = &storage->drawing, .ui = &storage->ui,
      .audio = &storage->audio, .resources = &storage->assets, .idle_inputs = {true, true, false},
      .default_city_fixes = CITY_FIX_DEFAULT_MASK, .repair_save_headers = true};
  city->game = game;
  city->fixes = game->default_city_fixes;
  Drawing *drawing = game->drawing;
  CityInterfaceState *ui = game->ui;
  CityAudioState *audio = game->audio;
  CityResourceState *resources = game->resources;
  ui->error_tile = INVALID_TILE;
  ui->main_window_available = true;
  ui->tools_visible = true;
  ui->file_dialog_redraw_pending = true;
  ui->newspaper_timer_seconds = 60.0;
  ui->diagnostic_nested_newspaper = -1;
  audio->sound_tile = INVALID_TILE;
  InitializeDrawing(drawing);
  game->present_map = ApplicationPresentMap;
  drawing->tile_damage_enabled = true;
  const char *assets = options.assets, *city_path = options.startup.city_path, *image_path = options.image_path;
  int image_physical = options.image_physical;
  bool ui_scale_override = options.ui_scale_override, headless = options.startup.headless;
  GameStartupOptions startup = options.startup;
  game->debug_city = options.debug_city;
  drawing->profile_redraw = options.profile_redraw;
  drawing->disable_map_cache = options.disable_map_cache;
  drawing->benchmark_redraw = options.benchmark_redraw;
  if (options.ui_scale_override) { drawing->options.scale = options.ui_scale; drawing->options.automatic = false; }
  ui->newspaper_timer_seconds = options.newspaper_seconds;
  ui->diagnostic_nested_newspaper = options.nested_newspaper;
  ui->profile_newspaper = options.profile_newspaper;
  i32 result = 1;
  CanvasSDL *display = NULL;
  if (!hooks) LoadGameSettings(game);
  if (!SetWorkingDirectory(assets)) {
    perror(assets);
    goto done;
  }
  if (!headless) {
    display = Sc2kSDLOpen(game, kDefaultCityCanvasWidth, kDefaultCityCanvasHeight, 2, 0);
    if (!display) {
      fprintf(stderr, "SDL: %s\n", Sc2kSDLError());
      goto done;
    }
    if (!LoadApplicationIntroAssets(resources)) {
      fprintf(stderr, "Unable to load intro artwork\n");
      goto done;
    }
    drawing->map_pixel_scale = Sc2kSDLPixelScale(display);
    if (!DrawCityLoadingScreen(game, display, CITY_INTRO_LOADING_CITY, 0, 100, true)) goto done;
  }
  SetCityMapNavigationPresentation(game, true, false);
  bool initialized = hooks && hooks->initialize_city ? hooks->initialize_city(game, context, city_path) :
      InitializeGameCity(game, city_path);
  if (!initialized) {
    fprintf(stderr, "Native startup failed: error %d\n", GetGameError(game));
    goto done;
  }
  if (display && !DrawCityLoadingScreen(game, display, CITY_INTRO_LOADING_GRAPHICS, 0, 100, false)) goto done;
  // City bootstrap replaces resource caches; load standalone UI art afterwards.
  if (!LoadApplicationIntroAssets(resources) || !LoadApplicationGraphicsAssets(game)) {
    fprintf(stderr, "Unable to load graphics assets\n");
    goto done;
  }
  if (!LoadCityAudioAssets(resources)) {
    fprintf(stderr, "Unable to load audio assets\n");
    goto done;
  }
  audio->assets = resources;
  drawing->demolition_enabled = true;
  InitializeNativeCityPalette(drawing);
  InitializeCityNewspaperLayouts(drawing);
  Sc2kSetCityMapClientRect(drawing, (CityRect){0, 0, kDefaultCityCanvasWidth, kDefaultCityCanvasHeight});
  drawing->default_map_view->view_rect = (CityRect){0, 0, kDefaultCityCanvasWidth, kDefaultCityCanvasHeight};
  InitializeCityMapView(game, MapTileXY(city, city->geography.center_x, city->geography.center_y), 1);
  SetCityStatusPresentation(&ui->status, &ui->desktop, kDefaultCityCanvasWidth);
  SetCityStatusMinimized(&ui->status, 0);
  ui->resume_speed = city->settings.speed;
  if (ui->resume_speed < CITY_SPEED_SLOW || ui->resume_speed > CITY_SPEED_UNLIMITED)
    ui->resume_speed = CITY_SPEED_NORMAL;
  ExecuteRecordedCityCommand(game, CITY_COMMAND_SET_SIMULATION_SPEED, 1, 0, 0);
  InitializeCityDisplayPalette(drawing, drawing->colors);
  if (!PrepareCityMapSpriteSources(game)) goto done;
  LoadCityCursorToolIcons(game, drawing->colors);
  Sc2kCanvasDesktopInit(&ui->desktop);
  ui->desktop.interface = ui;
  ui->desktop.game = game;
  ui->desktop.interface_font = &resources->interface_font;
  JukeboxInit(&audio->jukebox, (u32)(u64)(MonotonicSeconds() * 1000000));
  ui->music_collapsed = ui->music_pinned = 0;
  Sc2kCanvasSetJukeboxState(ui, &audio->jukebox);
  ui->desktop.canvas_width = kDefaultCityCanvasWidth;
  ui->desktop.canvas_height = kDefaultCityCanvasHeight;
  if (!BindCityChartPanels(city, &ui->desktop))
    goto done;
  if (!SetCityStartupMenuPresentation(city, &ui->desktop))
    goto done;
  SyncCityInterface(game, NULL, 1);
  SetCitySpanPresentation(ui, &ui->desktop);
  if (!InitializeCityFileDialogPresentation(city, &ui->desktop, city_path))
    goto done;
  if (!headless) ShowCityLoadWarnings(city);
  if (city->settings.timer_mode == CITY_TIMER_STOPPED) {
    if (game->city_active) {
      game->idle_state = CITY_IDLE_TOOL_PREVIEW;
      RebuildCityToolbar(city);
    } else SetCityTimerMode(city, CITY_TIMER_RUNNING);
  }
  if (!headless && city->mission.active != 0)
    ApplicationOpenScenarioInstructions(city, city_path);
  if (hooks && hooks->prepare && !hooks->prepare(game, context, &startup)) goto done;
  ui->dialog_pause_sync_enabled = 1;
  ui->auto_goto_disabled = !city->settings.auto_goto;
  ui->newspaper_delivery_disabled = !city->newspaper.enabled;
  ui->extra_newspaper_disabled = !city->indicators.ranked_disaster_gate;
  ui->newspaper_month = city->clock.month;
  SetCityMapDataPresentation(city, &ui->desktop);
  RefreshCityStatusPanel(city, &ui->status, city->settings.speed, true);
  if (GetGameError(game)) goto done;
  ApplicationSyncDialogPause(city);
  if (!headless && !game->close_requested) NativeAudioOpen(audio);
  if (!hooks) {
    if (!audio->live_open) { audio->master_volume = 100; audio->jukebox.playing = 1; }
    ApplyGameSettings(game, ui_scale_override);
  }
  if (headless && !RunHeadlessApplication(game, &options)) goto done;
  if (!headless && !game->close_requested && !RunLiveApplication(game, display, &options)) goto done;
  game->host_start_time = 0;
  game->activation_context = NULL;
  game->poll_activation = NULL;
  if (hooks && hooks->finish && !hooks->finish(game, context)) goto done;
  if (image_path && !ScreenshotCityCanvas(game, image_path, image_physical))
    goto done;
  if (ui->dialog_pause_active) {
    ui->dialog_pause_requested = 0;
    ApplicationSyncDialogPause(city);
  }
  if (hooks && hooks->print_status) hooks->print_status(game, context);
  else printf("native_tick=%u error=%d\n", city->clock.tick, game->execution_failed);
  if (ui->profile_newspaper)
    fprintf(stderr, "newspaper_done lifetimes=%u timers=%u depth=%u input_blocked=%u speed=%u\n",
        ui->newspaper_lifetimes, ui->newspaper_timer_deliveries, ui->dialog_pause_depth,
        ui->dialog_input_blocked, city->settings.speed);
  if (drawing->profile_redraw && drawing->profiled_redraws) {
    double milliseconds = 1000.0 / drawing->profiled_redraws;
    fprintf(stderr, "redraw_samples=%u snapshot_ms=%.3f map_ms=%.3f canvas_ms=%.3f total_ms=%.3f\n",
        drawing->profiled_redraws, drawing->snapshot_seconds * milliseconds, drawing->map_seconds * milliseconds,
        drawing->canvas_seconds * milliseconds,
        (drawing->snapshot_seconds + drawing->map_seconds + drawing->canvas_seconds) * milliseconds);
  }
  result = game->execution_failed ? 1 : 0;
done:
  return CloseApplication(storage, display, &options, result);
}
