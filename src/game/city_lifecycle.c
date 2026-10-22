#include "city/saveload/state.h"
#include "city/engine/city_name.h"
#include "game/city_lifecycle.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/catalog.h"
#include "assets/state.h"
#include "city/disasters/types.h"
#include "city/economy/economy.h"
#include "city/economy/facilities.h"
#include "city/economy/types.h"
#include "city/engine/city_types.h"
#include "city/engine/command_types.h"
#include "city/engine/commands.h"
#include "city/engine/initialization.h"
#include "city/engine/simulation.h"
#include "city/events/events.h"
#include "city/events/news.h"
#include "city/maps/layers.h"
#include "city/maps/map_types.h"
#include "city/maps/tile_storage.h"
#include "city/military.h"
#include "city/presentation.h"
#include "city/saveload/file.h"
#include "city/saveload/legacy_text.h"
#include "city/saveload/runtime.h"
#include "city/saveload/services.h"
#include "city/saveload/summary.h"
#include "city/saveload/types.h"
#include "city/state.h"
#include "city/tools/types.h"
#include "drawing/maps/map_render.h"
#include "drawing/maps/map_view.h"
#include "drawing/state.h"
#include "game/application.h"
#include "game/errors.h"
#include "game/observation.h"
#include "game/settings.h"
#include "game/state.h"
#include "ui/city_lifecycle.h"
#include "ui/commands.h"
#include "ui/dialogs/dialogs.h"
#include "ui/map.h"
#include "ui/state.h"
#include "ui/text/text.h"
#include "ui/tools/tools.h"

// City setup and loading

static bool OpenCityPath(City *city, const char *path);

bool ApplyCityTerrainMap(City *city, const CityTerrainMap *map, const CityTerrainSettings *settings) {
  if (!InstallCityTerrainMap(city, map, settings)) return false;
  city->game->drawing->scroll_pixels = (CityPoint){0};
  CenterCityMapViewAndResetOverlays(city, city->map_width / 2, city->map_height / 2, 0);
  RefreshCityMapViews(city->game);
  return true;
}

// Native creation consumes accepted values; it never opens a decision window.
static bool CreateCityWithTerrain(City *city, const CityCreationOptions *options, const char *path) {
  CityCreationOptions accepted = *options;
  options = &accepted;
  if (city->simulation_work.phase) FinishCitySimulationWork(city);
  ClearGameError(city->game);
  CityTerrainMap map = {0};
  bool ready = path ? ReadCityFileTerrain(path, &options->terrain, &map, false) :
      GenerateTerrainMap(&options->terrain, &map);
  if (!ready) {
    ReleaseCityTerrainMap(&map);
    if (path) PresentCityFileNotice(city, "The selected map could not be read.", true);
    else SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    return false;
  }
  if (!ResizeCityMap(city, map.width, map.height)) {
    ReleaseCityTerrainMap(&map);
    SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    return false;
  }
  Game *game = city->game;
  CloseCityAuxiliaryPresentation(city);
  game->idle_state = CITY_IDLE_NEW_CITY;
  game->idle_entry = 1;
  ResetCitySimulationState(city);
  RecalculateCityMapClientOrigins(city->game);
  InitializeNewCityDocument(city);
  ResetCityConstructionToolOptions(city->game);
  city->settings.timer_mode = CITY_TIMER_INITIALIZING;
  bool applied = ApplyCityTerrainMap(city, &map, &options->terrain);
  ReleaseCityTerrainMap(&map);
  if (!applied) return false;
  AssignCityName(city, (int)strlen(options->city_name), (const u8 *)options->city_name);
  CopyText((char *)city->overlay_names.entry[0].character, sizeof(city->overlay_names.entry[0].character),
      options->mayor_name);
  ApplyCitySetupValues(city, (options->start_year - 1900) / 50, options->start_year, options->difficulty);
  game->idle_state = options->edit_terrain ? CITY_IDLE_TOOL_PREVIEW : CITY_IDLE_PLAY;
  game->idle_entry = 1;
  if (options->edit_terrain) {
    city->settings.timer_mode = CITY_TIMER_STOPPED;
    RebuildCityToolbar(city);
    UpdateCityStatus(city);
  } else {
    RecordRankedCityEvent(city, CITY_NEWS_CITY_FOUNDED, 0);
    SetCityTimerMode(city, CITY_TIMER_RUNNING);
  }
  city->geography.settings_preserved = options->edit_terrain;
  game->city_active = true;
  return GetGameError(game) == 0;
}

bool CreateCityFromOptions(City *city, const CityCreationOptions *options) {
  return CreateCityWithTerrain(city, options, NULL);
}

bool CreateCityFromTerrainFile(City *city, const CityCreationOptions *options, const char *path) {
  return CreateCityWithTerrain(city, options, path);
}

bool ConfigureCityFromOptions(City *city, const CityCreationOptions *options) {
  CityCreationOptions accepted = *options;
  options = &accepted;
  if (city->simulation_work.phase) FinishCitySimulationWork(city);
  ClearGameError(city->game);
  ResetCitySimulationState(city);
  AssignCityName(city, (int)strlen(options->city_name), (const u8 *)options->city_name);
  CopyText((char *)city->overlay_names.entry[0].character, sizeof(city->overlay_names.entry[0].character),
      options->mayor_name);
  ApplyCitySetupValues(city, (options->start_year - 1900) / 50, options->start_year, options->difficulty);
  return GetGameError(city->game) == 0;
}

// Terrain creation belongs to the new-city preview; this command edits the existing map.
void EnterCityTerrainEditor(City *city) {
  if (city->simulation_work.phase) FinishCitySimulationWork(city);
  ClearGameError(city->game);
  Game *game = city->game;
  game->idle_entry = 1;
  game->idle_state = CITY_IDLE_TERRAIN;
  city->geography.settings_preserved = 1;
  if (!InitializeCityDocumentForIdleMode(city)) return;
  ShowCityMapToolWindows(city);
  game->idle_state = CITY_IDLE_TOOL_PREVIEW;
  game->idle_entry = 1;
  SetCityTimerMode(city, CITY_TIMER_STOPPED);
  UpdateCityStatus(city);
  city->game->ui->replacement_prompt_suppressed = false;
  city->game->city_active = true;
}

// Complete the admitted load continuation: reset replacement UI, apply launch debug policy, invalidate the canvas
// and present import warnings. Admission and failure continuation remain with the caller.
void CompleteCityLoad(City *city) {
  ResetCityReplacementPresentation(city);
  if (city->game->debug_city) DoCityCommand(city, CITY_COMMAND_ENABLE_DEBUG_CITY, 0, INVALID_TILE, 0, 0, 0);
  InvalidateLoadedCityCanvas(city);
  ShowCityLoadWarnings(city);
}

void PrepareCityFileOpen(City *city) {
  Game *game = city->game;
  game->city_active = false;
  InvalidateLoadedCityCanvas(city);
  city->game->drawing->palette.direct_animation = 0;
  game->idle_state = CITY_IDLE_LOAD_CITY;
  game->idle_entry = 1;
  CloseCityAuxiliaryPresentation(city);
  ResetCitySimulationState(city);
  InitializeCityDocumentForIdleMode(city);
}

void FinishCityFileOpen(City *city, bool loaded, u32 error_serial) {
  Game *game = city->game;
  if (loaded == 0) {
    // The codec may already have presented a more specific load failure.
    if (game->ui->command_error_serial == error_serial)
      ShowCityLoadCommandResource(city, CITY_NOTICE_LOAD_CITY_FAILED);
    city->game->ui->restore_frame_tools = false;
    game->idle_state = CITY_IDLE_STARTUP_MENU;
    game->idle_entry = 1;
    ChangeCityLoadDirectory(city->game->file, CityPathText(&city->game->city_working_directory));
    CloseCityAuxiliaryPresentation(city);
    SetCityToolsVisible(city->game, 0);
    city->game->city_active = false;
  } else {
    CompleteCityLoad(city);
    ShowCityMapToolWindows(city);
    int mode = city->settings.timer_mode;
    game->idle_entry = 1;
    game->idle_state = mode == CITY_TIMER_STOPPED ? CITY_IDLE_TOOL_PREVIEW : CITY_IDLE_PLAY;
    if (mode == CITY_TIMER_STOPPED && !game->observation) RebuildCityToolbar(city);
    else SetCityTimerMode(city, mode);
    city->geography.settings_preserved = 1;
    if (city->settings.timer_mode != CITY_TIMER_STOPPED)
      city->geography.settings_preserved = 0;
    UpdateCityStatus(city);
    if (city->game->ui->restore_frame_tools)
      SetCityToolsVisible(city->game, 1);
    city->game->ui->replacement_prompt_suppressed = false;
    city->game->city_active = true;
    ChangeCityLoadDirectory(city->game->file, CityPathText(&city->game->city_working_directory));
  }
  RebuildCityToolbar(city);
}

typedef struct {
  City *city;
  const char *path;
  bool from_path, loaded;
} CityFileOpenAction;

static void LoadSelectedCityFile(void *context) {
  CityFileOpenAction *action = context;
  if (action->from_path) {
    action->loaded = OpenCityPath(action->city, action->path);
    return;
  }
  PrepareCityFileOpen(action->city);
  u32 error_serial = action->city->game->ui->command_error_serial;
  action->loaded = LoadCityFile(action->city, action->path);
  FinishCityFileOpen(action->city, action->loaded, error_serial);
}

bool OpenCitySelectedFile(City *city, const u8 *text, int length, bool from_path) {
  CityFileRuntime operation;
  bool owns_operation = city->game->file == NULL;
  if (owns_operation) BeginCityFileOperation(city->game, &operation);
  char *path = NULL;
  SetTextN(&path, (const char *)text, length);
  if (city->simulation_work.phase) FinishCitySimulationWork(city);
  CityFileOpenAction action = {.city = city, .path = path, .from_path = from_path};
  ClearGameError(city->game);
  RunCityFileCommand(city->game->file, LoadSelectedCityFile, &action);
  free(path);
  if (owns_operation) EndCityFileOperation(&operation);
  return action.loaded && !GetGameError(city->game);
}

static bool OpenCityPath(City *city, const char *path) {
  Game *game = city->game;
  PrepareCityFileOpen(city);
  u32 error_serial = game->ui->command_error_serial;
  bool loaded = LoadCityFile(city, path);
  bool opened = loaded && city->name.length != 0;
  if (opened) {
    CompleteCityLoad(city);
    ShowCityMapToolWindows(city);
    int mode = city->settings.timer_mode;
    game->idle_state = mode == CITY_TIMER_STOPPED ? CITY_IDLE_TOOL_PREVIEW : CITY_IDLE_PLAY;
    game->idle_entry = 1;
    if (mode == CITY_TIMER_STOPPED && !game->observation) RebuildCityToolbar(city);
    else SetCityTimerMode(city, mode);
    UpdateCityStatus(city);
    if (city->game->ui->restore_frame_tools)
      SetCityToolsVisible(city->game, 1);
    city->geography.settings_preserved = mode == CITY_TIMER_STOPPED;
    const char *directory = CityPathText(&city->game->city_working_directory);
    city->game->ui->replacement_prompt_suppressed = false;
    city->game->city_active = true;
    ChangeCityLoadDirectory(city->game->file, directory);
  } else {
    game->path_open_flag = 0;
    FinishCityFileOpen(city, false, error_serial);
  }
  return opened;
}

// Retained path caller shares completion with explicit native loads.
void OpenCityFromPath(City *city, const char *path) {
  OpenCityPath(city, path);
}

void ApplyCitySetupValues(City *city, int era, int start_year, int difficulty) {
  InitializeCitySetupState(city, start_year ? start_year : 50 * (era + 38),
      1900 + 50 * era, difficulty + CITY_DIFFICULTY_EASY);
  if (city->game->debug_city) DoCityCommand(city, CITY_COMMAND_ENABLE_DEBUG_CITY, 0, INVALID_TILE, 0, 0, 0);
  UpdateCityStatus(city);
}

// Initialize an existing map for the requested document mode.
bool InitializeCityDocumentForIdleMode(City *city) {
  Game *game = city->game;
  // Retained diagnostic callers cannot request the deleted stock generator.
  if (game->idle_state == CITY_IDLE_NEW_CITY && !city->geography.settings_preserved) {
    return false;
  }
  RecalculateCityMapClientOrigins(city->game);
  switch (game->idle_state) {
  case CITY_IDLE_NEW_CITY:
    InitializeNewCityDocument(city);
    break;
  case CITY_IDLE_TERRAIN:
    ResetCitySimulationState(city);
    InitializeNewCitySimulationState(city);
    RefreshCityMapViews(city->game);
    break;
  case CITY_IDLE_LOAD_CITY:
    InitializeNewCityDocument(city);
    break;
  case CITY_IDLE_LOAD_SCENARIO:
    city->mission.active = 1;
    InitializeNewCityDocument(city);
    break;
  default:
    UpdateCityStatus(city);
    RefreshCityMapViews(city->game);
    break;
  }
  ResetCityConstructionToolOptions(city->game);
  return true;
}

// Reset only the original fields, then clear ALTM marker bits.
void ResetCitySimulationState(City *city) {
  CloseCityWindowsForReplacement(city->game);
  Drawing *drawing = city->game->drawing;
  CityInterfaceState *ui = city->game->ui;
  city->disasters.pending_type = CITY_DISASTER_NONE;
  city->indicators.event_flag = 0;
  city->indicators.auto_budget_enabled = 0;
  city->settings.auto_goto = 1;
  EmptyCityName(city);
  FreeCityPathStorage(&city->game->city_path);
  city->unlocks.disaster_level = 0;
  city->clock.tick = 0;
  ui->reset_word = 0;
  city->unlocks.military_base = CITY_MILITARY_BASE_NONE;
  city->geography.start_year = CITY_START_YEAR_1900;
  city->geography.rotation = CITY_MAP_ROTATION_0;
  ui->tool_group = CITY_TOOL_GROUP_CENTER;
  ui->terrain_tool = CITY_TERRAIN_TOOL_RAISE;
  drawing->map_state.data_mode = CITY_DATA_MODE_NONE;
  drawing->map_state.selection_redraw_state = 0xFFFF;
  drawing->map_state.zone_cost_label_rect = (CityRect){0};
  city->indicators.year_flag = 0;
  city->newspaper.edition_index = CITY_NEWSPAPER_EDITION_FIRST;
  city->geography.center_y = city->map_height / 2;
  city->indicators.sector_population_total = 0;
  city->geography.center_x = city->map_width / 2;
  city->settings.speed = CITY_SPEED_PAUSED;
  RefreshCityToolbarButtonStyles(city, true);
  for (int y = 0; y < city->map_height; ++y)
    for (int x = 0; x < city->map_width; ++x) {
      const TileIndex cell_index = MapTileXY(city, x, y);
      TileInfo *tp = &city->tile[cell_index];
      tp->altitude &= CITY_ALTITUDE_CLEAR_TUNNEL;
    }
}

// Scenario loading

void CloseCityScenarioInput(CityScenarioInput *input) {
  void *stream = input->stream;
  input->stream = NULL; // Close consumes the wrapper even if it throws.
  if (stream) CityFileCloseStream(stream);
}

static void CloseScenarioInput(void *context) {
  CloseCityScenarioInput(context);
}

bool PrepareCityScenarioLoad(City *city, const CityScenarioInput *input) {
  Game *game = city->game;
  CityInterfaceState *ui = game->ui;
  CloseCityAuxiliaryPresentation(city);
  ResetCitySimulationState(city);
  if (!InitializeCityDocumentForIdleMode(city)) return false;
  u32 error_serial = ui->command_error_serial;
  bool loaded = LoadCityScenarioMetadata(city, input->path);
  if (!loaded) {
    if (input->from_path) {
      // The codec may already have presented a more specific load failure.
      if (ui->command_error_serial == error_serial)
        ShowCityLoadCommandResource(city, CITY_NOTICE_LOAD_CITY_FAILED);
      game->path_open_flag = 0;
    }
    if (!ui->restore_frame_tools) {
      game->idle_state = CITY_IDLE_STARTUP_MENU;
      game->idle_entry = 1;
    }
    if (!input->from_path || !ui->restore_frame_tools) return false;
  }
  CompleteCityLoad(city);
  ShowCityMapToolWindows(city);
  game->idle_state = CITY_IDLE_PLAY;
  game->idle_entry = 1;
  SetCityTimerMode(city, CITY_TIMER_RUNNING);
  return true;
}

bool LoadCityScenarioInstructions(City *city, CityScenarioInput *input,
    char text[CITY_SCENARIO_INSTRUCTION_BYTES + 1]) {
  Game *game = city->game;
  input->stream = CityFileOpenStream(game->file, input->path);
  if (!input->stream) {
    ReportCityChunkError(game->file, CITY_NOTICE_ERROR_OPENING_FILE, 1);
    if (input->from_path) game->path_open_flag = 0;
    return false;
  }
  if (!CityFileReadTaggedStream(input->stream, "TEXT", CITY_SCENARIO_INSTRUCTIONS_VERSION,
      (u8 *)text, CITY_SCENARIO_INSTRUCTION_BYTES + 1)) {
    ReportCityChunkError(game->file, CITY_NOTICE_ERROR_EXTRACTING_SCENARIO_RESOURCE, 1);
    if (input->from_path) game->path_open_flag = 0;
    if (!game->ui->restore_frame_tools) {
      game->idle_state = CITY_IDLE_STARTUP_MENU;
      game->idle_entry = 1;
    }
    return false;
  }
  for (char *p = text; *p; ++p)
    if (*p == '\r' || *p == '\t') *p = ' ';
  DecodeCityScenarioText(text, CITY_SCENARIO_INSTRUCTION_BYTES + 1);
  return true;
}

// The real post-instructions continuation operates on live City state.
void CompleteCityScenarioLoad(City *city, bool from_path) {
  UpdateCityStatus(city);
  if (from_path) SetCityToolsVisible(city->game, 1);
  city->geography.settings_preserved = 0;
  city->game->city_active = true;
  city->game->ui->replacement_prompt_suppressed = false;
}

static void ClosePendingScenarioInput(void *context) {
  CityFileRuntime *file = context;
  void *stream = file->scenario_input;
  file->scenario_input = NULL; // Close consumes the wrapper even if it throws.
  if (stream) CityFileCloseStream(stream);
}

void CancelCityScenarioLoad(CityFileRuntime *file) {
  if (!file->scenario_input) return;
  RunCityFileCommand(file, ClosePendingScenarioInput, file);
}

typedef struct {
  City *city;
  CityScenarioInput input;
  bool loaded, defer_instructions;
} CityScenarioLoadAction;

static void LoadSelectedScenarioFile(void *context) {
  CityScenarioLoadAction *action = context;
  City *city = action->city;
  CityFileRuntime *file = city->game->file;
  CityFileCleanup cleanup = {CloseScenarioInput, &action->input, NULL};
  PushCityFileCleanup(file, &cleanup);
  city->game->idle_state = CITY_IDLE_LOAD_SCENARIO;
  city->game->idle_entry = 1;
  bool loaded = PrepareCityScenarioLoad(city, &action->input) &&
      LoadCityScenarioInstructions(city, &action->input, file->scenario_instructions);
  if (loaded && !action->defer_instructions) CompleteCityScenarioLoad(city, action->input.from_path);
  PopCityFileCleanup(file, &cleanup);
  if (loaded && action->defer_instructions) {
    file->scenario_input = action->input.stream;
    action->input.stream = NULL;
  } else CloseScenarioInput(&action->input);
  action->loaded = loaded;
}

// Explicit options keep headless loads complete; UI may retain the instruction boundary.
bool OpenCitySelectedScenario(City *city, const u8 *text, int length, bool from_path, bool defer_instructions) {
  Game *game = city->game;
  CityFileRuntime operation;
  bool owns_operation = game->file == NULL;
  if ((defer_instructions && owns_operation) || (game->file && game->file->scenario_input)) return false;
  if (owns_operation) BeginCityFileOperation(game, &operation);
  char *path = NULL;
  SetTextN(&path, (const char *)text, length);
  if (city->simulation_work.phase) FinishCitySimulationWork(city);
  CityScenarioLoadAction action = {.city = city, .input = {.path = path, .from_path = from_path},
      .defer_instructions = defer_instructions};
  ClearGameError(game);
  RunCityFileCommand(game->file, LoadSelectedScenarioFile, &action);
  free(path);
  if (owns_operation) EndCityFileOperation(&operation);
  return action.loaded && !GetGameError(game);
}

static void FinishSelectedScenarioFile(void *context) {
  CityScenarioLoadAction *action = context;
  CityFileRuntime *file = action->city->game->file;
  CityFileCleanup cleanup = {ClosePendingScenarioInput, file, NULL};
  PushCityFileCleanup(file, &cleanup);
  CompleteCityScenarioLoad(action->city, action->input.from_path);
  PopCityFileCleanup(file, &cleanup);
  ClosePendingScenarioInput(file);
  action->loaded = true;
}

bool FinishCityScenarioLoad(City *city, bool from_path) {
  CityFileRuntime *file = city->game->file;
  if (!file || !file->scenario_input) return false;
  CityScenarioLoadAction action = {.city = city, .input = {.from_path = from_path}};
  RunCityFileCommand(file, FinishSelectedScenarioFile, &action);
  return action.loaded && !GetGameError(city->game);
}
