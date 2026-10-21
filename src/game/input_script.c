#include "city/infrastructure/types.h"
#include "city/maps/map_types.h"
#include "base/time.h"
#include "ui/tools/tools.h"
#include "game/application_internal.h"
#include "city/economy/economy.h"
#include "city/engine/simulation.h"
#include "game/observation.h"

bool ScreenshotCityCanvas(City *city, const char *path, int physical) {
  Drawing *drawing = city->game->drawing;
  FILE *file = fopen(path, "wb");
  if (file == NULL)
    return false;
  i32 factor = physical ? drawing->map_pixel_scale : 1;
  const Canvas *canvas = &drawing->framebuffer;
  i32 output_width = physical ? canvas->width : kDefaultCityCanvasWidth;
  i32 output_height = physical ? canvas->height : kDefaultCityCanvasHeight;
  i32 ok = fprintf(file, "P6\n%d %d\n255\n", output_width, output_height) > 0;
  for (i32 index = 0; ok && index < output_width * output_height; ++index) {
    i32 x = index % output_width, y = index / output_width;
    u32 color = canvas->pixels[(size_t)(y * drawing->map_pixel_scale / factor) * canvas->stride +
        x * drawing->map_pixel_scale / factor];
    u8 rgb[] = {(u8)(color >> 16), (u8)(color >> 8), (u8)color};
    ok = fwrite(rgb, 1, 3, file) == 3;
  }
  return fclose(file) == 0 && ok;
}

static void TrackScriptPlacementDrag(Game *game, const CityCommand *arguments,
    const CityPointerInput input[2], bool infrastructure) {
  City *city = game->city;
  CityZoneDragInput samples[2] = {{input[0].point, input[0].flags}, {input[1].point, input[1].flags}};
  // The tracker consumes until release; malformed scripts must not read beyond these two samples.
  if (city->zone_drag_input || input[0].flags || !(input[1].flags & CITY_DRAG_INPUT_MASK) ||
      (input[1].flags & ~CITY_DRAG_INPUT_MASK)) return;
  TileIndex start = infrastructure ? (TileIndex)arguments->parameter1 :
      MapTileXYOrInvalid(city, arguments->parameter1, arguments->parameter2);
  int cost = infrastructure ? arguments->parameter2 : arguments->parameter3;
  if (!IsMapTileValid(city, start) || cost < (infrastructure ? -32768 : 0) || cost > 32767) return;
  if (infrastructure && ((arguments->parameter3 & ~0x1FF) ||
      (arguments->parameter3 & 255) > CITY_PLACEMENT_ALLOW_MILITARY)) return;
  ClearGameError(game);
  city->zone_drag_endpoint = -1;
  city->zone_drag_input = samples;
  city->zone_drag_index = 0;
  if (infrastructure) {
    city->construction.stage = arguments->parameter3 & 255;
    city->zone_drag_endpoint = TrackCityPlacementDrag(game,
        arguments->parameter3 & 256 ? game->drawing->drag_map_view : NULL,
        start, cost, CITY_DRAG_LINE);
  } else city->zone_drag_endpoint = TrackCityZoneDrag(game, start, cost);
  city->zone_drag_input = NULL;
  city->zone_drag_index = 0;
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
    DispatchCityCanvasEvent(game->city, &event);
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
    DispatchCityCanvasEvent(game->city, &event);
  } else if (strncmp(line, "screenshot ", 11) == 0) {
    if (!RedrawCityCanvas(game->city, NULL) || !ScreenshotCityCanvas(game->city, line + 11, 0)) game->execution_failed = 1;
  } else if (strncmp(line, "type ", 5) == 0) {
    const char *text = line + 5;
    while (*text) {
      CanvasSDLEvent event = {.kind = CANVAS_SDL_TEXT_INPUT};
      CopyText(event.text, sizeof(event.text), text);
      text += strlen(event.text);
      DispatchCityCanvasEvent(game->city, &event);
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
      RunGameIdle(game, (CityIdleInputs){true, true, true});
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
    CityCommand scalar;
    CityPointerInput zone_input[2];
    if (sscanf(line, "zone_drag %d %d %d %d %d %u %d %d %u %c", &scalar.parameter1,
        &scalar.parameter2, &scalar.parameter3, &zone_input[0].point.x, &zone_input[0].point.y,
        &zone_input[0].flags, &zone_input[1].point.x, &zone_input[1].point.y, &zone_input[1].flags, &trailing) == 9) {
      TrackScriptPlacementDrag(game, &scalar, zone_input, false);
      return true;
    }
    if (sscanf(line, "infrastructure_drag %d %d %d %d %d %u %d %d %u %c", &scalar.parameter1,
        &scalar.parameter2, &scalar.parameter3, &zone_input[0].point.x, &zone_input[0].point.y,
        &zone_input[0].flags, &zone_input[1].point.x, &zone_input[1].point.y, &zone_input[1].flags, &trailing) == 9) {
      TrackScriptPlacementDrag(game, &scalar, zone_input, true);
      return true;
    }
    if (sscanf(line, "command %u %d %d %d %c", &scalar.id, &scalar.parameter1,
        &scalar.parameter2, &scalar.parameter3, &trailing) == 4) {
      // Rejection is a command result, recorded for stock comparison.
      ExecuteRecordedCityCommandWithInput(game, scalar, NULL, 0);
      return true;
    }
    if (sscanf(line, "chart %d %d %d %c", &panel, &control, &value, &trailing) == 3) {
      SelectCityChartControl(game->city, panel, control, value);
      return true;
    }
    i32 stadium_x, stadium_y;
    i32 facility;
    if (sscanf(line, "inspect %d %d %c", &stadium_x, &stadium_y, &trailing) == 2) {
      PresentCityTileInformationDialog(game->city, MapTileXYOrInvalid(game->city, stadium_x, stadium_y));
      return true;
    }
    if (sscanf(line, "label %d %d %d %c", &stadium_x, &stadium_y, &facility, &trailing) == 3 &&
        (facility == 0 || facility == 1)) {
      TileIndex tile = MapTileXYOrInvalid(game->city, stadium_x, stadium_y);
      if (tile != INVALID_TILE) PresentCityLabelEditor(game->city, tile, facility);
      return true;
    }
    if (sscanf(line, "stadium %d %d %c", &stadium_x, &stadium_y, &trailing) == 2) {
      TileIndex tile = MapTileXYOrInvalid(game->city, stadium_x, stadium_y);
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
    if (sscanf(line, "newspaper %d %c", &edition, &trailing) == 1 && edition >= 0 && edition < 6) {
      PresentCityNewspaperEdition(game->city, edition);
      return true;
    }
    if (sscanf(line, "alert %d %d %c", &bitmap_id, &text_id, &trailing) == 2) {
      PresentCityResourceAlert(game->city, bitmap_id, text_id);
      return true;
    }
    int width, height;
    if (sscanf(line, "resize %d %d %c", &width, &height, &trailing) == 2 && width > 0 && height > 0 && game->drawing->ui_display) {
      Sc2kSDLResize(game->drawing->ui_display, width, height);
      if (!RedrawCityCanvas(game->city, game->drawing->ui_display)) game->execution_failed = true;
      return true;
    }
    float wheel_x, wheel_y;
    if (sscanf(line, "scroll %f %f %c", &wheel_x, &wheel_y, &trailing) == 2) {
      event.x = (i32)(wheel_x * 1024.0f);
      event.y = (i32)(wheel_y * 1024.0f);
      ScrollCityCanvas(game->city, &event);
      return true;
    }
    if (sscanf(line, "click %d %d %c", &event.x, &event.y, &trailing) == 2) {
      event.button = CANVAS_SDL_BUTTON_LEFT;
      BeginCityCanvasButtonFeedback(game, &event);
      HandleCityCanvasPointerDown(game->city, &event);
      HandleCityCanvasPointerUp(game->city, &event);
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
        HandleCityCanvasPointerDown(game->city, &event);
      } else if (!strcmp(action, "move")) {
        int x, y;
        CityInterfacePixels(game, &event, &x, &y);
        MoveCityTooltipPointer(game, x, y, MonotonicSeconds());
        HandleCityCanvasPointerMove(game->city, &event);
      } else
        HandleCityCanvasPointerUp(game->city, &event);
      return true;
    }
    fprintf(stderr, "Unknown input-script action: %s\n", line);
    game->execution_failed = 1;
    return false;
  }
  return true;
}
