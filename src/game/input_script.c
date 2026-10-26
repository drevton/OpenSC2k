#include "game/application.h"

#include <string.h>

#include "base/text.h"
#include "base/time.h"
#include "city/engine/clock.h"
#include "city/engine/simulation.h"
#include "city/events/events.h"
#include "city/events/news.h"
#include "city/infrastructure/types.h"
#include "city/maps/labels.h"
#include "city/state.h"
#include "drawing/state.h"
#include "game/audio.h"
#include "game/commands.h"
#include "game/errors.h"
#include "game/frame.h"
#include "game/graphics.h"
#include "game/observation.h"
#include "game/state.h"
#include "game/update.h"
#include "ui/charts/charts.h"
#include "ui/dialogs/alerts.h"
#include "ui/dialogs/budget.h"
#include "ui/dialogs/information.h"
#include "ui/dialogs/name_editor.h"
#include "ui/dialogs/scenario.h"
#include "ui/input.h"
#include "ui/news/newspaper.h"
#include "ui/sdl.h"
#include "ui/state.h"
#include "ui/tools/tools.h"
#include "ui/windows/tooltips.h"

// Local drag options: the low byte selects the construction stage, and bit 8 selects the retained drag view
// for infrastructure tracking.
static void TrackScriptPlacementDrag(Game *game, const CityPointerInput input[2],
    TileIndex start, int cost, int options, bool infrastructure) {
  City *city = game->city;
  CityZoneDragInput samples[2] = {{input[0].point, input[0].flags}, {input[1].point, input[1].flags}};
  // The tracker consumes until release; malformed scripts must not read beyond these two samples.
  if (game->zone_drag_input || input[0].flags || !(input[1].flags & CITY_DRAG_INPUT_MASK) ||
      (input[1].flags & ~CITY_DRAG_INPUT_MASK)) return;
  if (!IsMapTileValid(city, start) || cost < (infrastructure ? -32768 : 0) || cost > 32767) return;
  if (infrastructure && ((options & ~0x1FF) || (options & 255) > CITY_PLACEMENT_ALLOW_MILITARY)) return;
  ClearGameError(game);
  game->zone_drag_endpoint = -1;
  game->zone_drag_input = samples;
  game->zone_drag_index = 0;
  if (infrastructure) {
    city->construction.stage = options & 255;
    game->zone_drag_endpoint = TrackCityPlacementDrag(game,
        options & 256 ? game->drawing->drag_map_view : NULL, start, cost, CITY_DRAG_LINE);
  } else game->zone_drag_endpoint = TrackCityZoneDrag(game, start, cost);
  game->zone_drag_input = NULL;
  game->zone_drag_index = 0;
}

bool ApplicationNextScriptAction(Game *game) {
  char line[CITY_FILE_DIALOG_PATH_LIMIT + 64];
  if (game->execution_failed || game->close_requested || game->input_script == NULL ||
      fgets(line, sizeof(line), game->input_script) == NULL)
    return false;
  size_t length = strlen(line);
  if (length && line[length - 1] == '\n')
    line[--length] = '\0';
  if (length && line[length - 1] == '\r')
    line[--length] = '\0';
  if (length == 0 || line[0] == '#')
    return true;
  if (game->observation && game->observation->script_action && game->observation->script_action(game, line))
    return true;
  if (!strcmp(line, "tick")) {
    DeliverCitySimulationEvents(game, 0, AdvanceCitySimulationPhaseWithOptions(game->city, 0), false);
  } else if (!strcmp(line, "close")) {
    // Diagnostic teardown bypasses interactive Save questions.
    game->close_requested = 1;
  } else if (!strcmp(line, "quit")) {
    CanvasSDLEvent event = {.kind = CANVAS_SDL_CLOSE};
    DispatchCityCanvasEvent(game, &event);
  } else if (!strcmp(line, "redraw")) {
    if (!RedrawCityCanvas(game->city, NULL))
      game->execution_failed = 1;
  } else if (!strcmp(line, "scenario_instructions")) {
    ApplicationOpenScenarioInstructions(game->city, game->input_script_city_path);
  } else if (!strcmp(line, "pending_disaster")) {
    DeliverCitySimulationEvents(game, 0,
        AdvanceCityDisasterWithOptions(game->city, CITY_TICK_ALLOW_TERRAIN_WATER), false);
  } else if (!strcmp(line, "sound_complete")) {
    CompleteCitySoundPlayback(game);
  } else if (!strcmp(line, "budget_helper_timer")) {
    AdvanceCityBudgetHelper(game->city, game->ui->annual_budget_window);
    ApplicationBudgetSync(game->city);
  } else if (strncmp(line, "key ", 4) == 0) {
    CityHotkey key = CityHotkeyFromString(line + 4);
    CanvasSDLEvent event = {.kind = CANVAS_SDL_KEY_DOWN, .key = key,
        .shift = !!(key & CITY_HOTKEY_SHIFT), .ctrl = !!(key & CITY_HOTKEY_CTRL), .alt = !!(key & CITY_HOTKEY_ALT)};
    DispatchCityCanvasEvent(game, &event);
  } else if (strncmp(line, "screenshot ", 11) == 0) {
    if (!RedrawCityCanvas(game->city, NULL) || !ScreenshotCityCanvas(game, line + 11, false))
      game->execution_failed = true;
  } else if (strncmp(line, "type ", 5) == 0) {
    const char *text = line + 5;
    while (*text) {
      CanvasSDLEvent event = {.kind = CANVAS_SDL_TEXT_INPUT};
      CopyText(event.text, sizeof(event.text), text);
      text += strlen(event.text);
      DispatchCityCanvasEvent(game, &event);
    }
  } else if (!strcmp(line, "activate") || !strcmp(line, "deactivate")) {
    HandleApplicationActivation(game, !strcmp(line, "activate"));
  } else {
    CanvasSDLEvent event = {0};
    char trailing;
    u32 event_clock;
    if (sscanf(line, "event_timer %u %c", &event_clock, &trailing) == 1) {
      SetSimulationFrameTime(game->city, event_clock);
      UpdateCityMapEventTimer(game->city);
      return true;
    }
    if (sscanf(line, "idle_timer %u %c", &event_clock, &trailing) == 1) {
      SetSimulationFrameTime(game->city, event_clock);
      UpdateCityCadence(game->city);
      RunGameIdle(game, (GameIdleInputs){.active_view = true, .simulation_enabled = true,
          .terrain_water_allowed = true});
      return true;
    }
    u32 budget_frames;
    if (sscanf(line, "budget_frames %u %c", &budget_frames, &trailing) == 1) {
      // Diagnostic time input drives the ordinary deadline gate, not the timer command.
      AdvanceCityClock(game->city, CITY_CLOCK_EXPLICIT_ADVANCE, budget_frames);
      ApplicationBudgetPoll(game->city);
      return true;
    }
    i32 panel, control, value;
    int tile_x, tile_y, start, cost, options;
    CityPointerInput zone_input[2];
    if (sscanf(line, "zone_drag %d %d %d %d %d %u %d %d %u %c", &tile_x,
        &tile_y, &cost, &zone_input[0].point.x, &zone_input[0].point.y,
        &zone_input[0].flags, &zone_input[1].point.x, &zone_input[1].point.y, &zone_input[1].flags, &trailing) == 9) {
      TrackScriptPlacementDrag(game, zone_input, MapTileXYOrInvalid(game->city, tile_x, tile_y), cost, 0, false);
      return true;
    }
    if (sscanf(line, "infrastructure_drag %d %d %d %d %d %u %d %d %u %c", &start,
        &cost, &options, &zone_input[0].point.x, &zone_input[0].point.y,
        &zone_input[0].flags, &zone_input[1].point.x, &zone_input[1].point.y, &zone_input[1].flags, &trailing) == 9) {
      TrackScriptPlacementDrag(game, zone_input, (TileIndex)start, cost, options, true);
      return true;
    }
    CityCommand command;
    if (sscanf(line, "command %u %d %d %d %c", &command.id, &command.parameter1,
        &command.parameter2, &command.parameter3, &trailing) == 4) {
      ExecuteRecordedCityCommandWithInput(game, command, NULL, 0);
      return true;
    }
    if (sscanf(line, "chart %d %d %d %c", &panel, &control, &value, &trailing) == 3) {
      SelectCityChartControl(game->city, panel, control, value);
      return true;
    }
    i32 facility;
    if (sscanf(line, "inspect %d %d %c", &tile_x, &tile_y, &trailing) == 2) {
      PresentCityTileInformationDialog(game->city, MapTileXYOrInvalid(game->city, tile_x, tile_y));
      return true;
    }
    if (sscanf(line, "label %d %d %d %c", &tile_x, &tile_y, &facility, &trailing) == 3 &&
        (facility == 0 || facility == 1)) {
      TileIndex tile = MapTileXYOrInvalid(game->city, tile_x, tile_y);
      if (tile != INVALID_TILE) PresentCityLabelEditor(game->city, tile, facility);
      return true;
    }
    if (sscanf(line, "stadium %d %d %c", &tile_x, &tile_y, &trailing) == 2) {
      TileIndex tile = MapTileXYOrInvalid(game->city, tile_x, tile_y);
      if (tile != INVALID_TILE) {
        PresentCityStadiumTeamDialog(game->city, tile);
        return true;
      }
    }
    CityCommand text_command;
    char encoded[66];
    if (sscanf(line, "text %u %d %d %d %65s %c", &text_command.id, &text_command.parameter1,
          &text_command.parameter2, &text_command.parameter3, encoded, &trailing) == 5) {
      u8 text[CITY_OVERLAY_NAME_BYTES];
      u32 length = !strcmp(encoded, "-") ? 0u : (u32)strlen(encoded);
      u32 limit = 2 * CITY_OVERLAY_NAME_BYTES;
      i32 valid = length <= limit && (length & 1) == 0;
      for (u32 index = 0; valid && index < length; ++index) {
        char digit = encoded[index];
        i32 nibble = digit >= '0' && digit <= '9' ? digit - '0' :
            digit >= 'a' && digit <= 'f' ? digit - 'a' + 10 :
            digit >= 'A' && digit <= 'F' ? digit - 'A' + 10 : -1;
        if (nibble < 0)
          valid = 0;
        else if ((index & 1) == 0)
          text[index / 2] = (u8)(nibble << 4);
        else
          text[index / 2] |= (u8)nibble;
      }
      if (!valid)
        game->execution_failed = 1;
      else
        ExecuteRecordedCityCommandWithPayload(game, text_command, NULL, 0, text, length / 2);
      return true;
    }
    i32 bitmap_id, text_id;
    i32 edition;
    if (sscanf(line, "newspaper %d %c", &edition, &trailing) == 1 && IsCityNewspaperEditionValid(edition)) {
      PresentCityNewspaperEdition(game->city, edition);
      return true;
    }
    if (sscanf(line, "alert %d %d %c", &bitmap_id, &text_id, &trailing) == 2) {
      PresentCityResourceAlert(game->city, bitmap_id, text_id);
      return true;
    }
    int width, height;
    if (sscanf(line, "resize %d %d %c", &width, &height, &trailing) == 2 &&
        width > 0 && height > 0 && game->ui->display) {
      Sc2kSDLResize(game->ui->display, width, height);
      if (!RedrawCityCanvas(game->city, game->ui->display)) game->execution_failed = true;
      return true;
    }
    float wheel_x, wheel_y;
    if (sscanf(line, "scroll %f %f %c", &wheel_x, &wheel_y, &trailing) == 2) {
      event.x = (i32)(wheel_x * 1024.0f);
      event.y = (i32)(wheel_y * 1024.0f);
      ScrollCityCanvas(game, &event);
      return true;
    }
    if (sscanf(line, "click %d %d %c", &event.x, &event.y, &trailing) == 2) {
      event.button = CANVAS_SDL_BUTTON_LEFT;
      BeginCityCanvasButtonFeedback(game, &event);
      HandleCityCanvasPointerDown(game, &event);
      HandleCityCanvasPointerUp(game, &event);
      return true;
    }
    char action[8];
    u32 flags;
    if (sscanf(line, "%7s %d %d %u %c", action, &event.x, &event.y, &flags, &trailing) == 4 &&
        (flags & ~(u32)CITY_POINTER_MODIFIER_MASK) == 0 &&
        (!strcmp(action, "down") || !strcmp(action, "move") || !strcmp(action, "up"))) {
      event.button = CANVAS_SDL_BUTTON_LEFT;
      event.alt = !!(flags & CITY_POINTER_MODIFIER_ALT);
      event.shift = !!(flags & CITY_POINTER_MODIFIER_SHIFT);
      event.ctrl = !!(flags & CITY_POINTER_MODIFIER_CONTROL);
      if (!strcmp(action, "down")) {
        BeginCityCanvasButtonFeedback(game, &event);
        HandleCityCanvasPointerDown(game, &event);
      } else if (!strcmp(action, "move")) {
        CityPoint point = CityInterfacePointerPixels(game, &event);
        MoveCityTooltipPointer(game, point.x, point.y, MonotonicSeconds());
        HandleCityCanvasPointerMove(game, &event);
      } else
        HandleCityCanvasPointerUp(game, &event);
      return true;
    }
    fprintf(stderr, "Unknown input-script action: %s\n", line);
    game->execution_failed = 1;
    return false;
  }
  return true;
}
