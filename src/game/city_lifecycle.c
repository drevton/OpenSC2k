#include "city/maps/layers.h"
#include "city/economy/facilities.h"
#include "city/maps/trees.h"
#include "city/military.h"
#include "city/saveload/types.h"
#include "city/saveload/summary.h"
#include "city/disasters/types.h"
#include "city/economy/types.h"
#include "city/engine/city_types.h"
#include "city/engine/command_types.h"
#include "city/events/events.h"
#include "city/events/news.h"
#include "city/maps/tile_storage.h"
#include "city/presentation.h"
#include "city/tools/types.h"
#include "drawing/maps/map_view.h"
#include "game/application.h"
#include "game/settings.h"
#include "city/saveload/legacy_text.h"
#include "ui/tools/tools.h"
#include <stddef.h>
#include <string.h>

#include <stdio.h>

#include "game/observation.h"
#include "audio/audio.h"
#include "city/economy/economy.h"
#include "city/engine/commands.h"
#include "city/engine/initialization.h"
#include "city/engine/simulation.h"
#include "city/saveload/file.h"
#include "game/errors.h"
#include "game/state.h"
#include "audio/state.h"
#include "assets/state.h"
#include "city/saveload/services.h"
#include "assets/catalog.h"
#include "ui/map.h"
#include "ui/startup/services.h"
#include "drawing/drawing.h"
#include "ui/commands.h"
#include "ui/dialogs/file_dialog.h"
#include "ui/text/text.h"

#include "city/state.h"
#include "drawing/state.h"
#include "ui/charts/charts.h"
#include "ui/state.h"

#include "city/saveload/runtime.h"
#include "city/saveload/city_file_internal.h"
#include "drawing/maps/map_render.h"
#include "ui/dialogs/scenario.h"
#include "city/maps/map_types.h"

// Load Commands

static void ConfigureCityFromSetupValues(City *city, CityNewCitySetupDialog *dialog);
static void ApplyCitySetupValues(City *city, int era, int start_year, int difficulty);

static int ChooseNewCitySetupAction(City *city, void *context);
static void ResetCityConstructionToolOptions(City *city);
static bool OpenCityPath(City *city, const char *path);

static bool ApplyCityTerrainMap(City *city, const CityTerrainMap *map, const CityTerrainSettings *settings) {
  for (TileIndex tile = 0; tile < map->cell_count; ++tile) {
    TileInfo *tp = &city->tile[tile];
    tp->altitude = map->altitude[tile];
    tp->zone = 0;
    tp->terrain = 0;
    if (map->legacy) tp->terrain = map->terrain[tile];
    else WriteTerrainCode(tp, map->terrain[tile]);
    tp->kind = map->legacy && !IsTileKindTree(map->buildings[tile]) ? TK_CLEAR : map->buildings[tile];
    tp->flags = map->flags[tile];
  }
  if (map->legacy) {
    BeginLegacyCityMapImport(city);
    if (!ConvertCityMapToNative(city)) {
      SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
      return false;
    }
  }
  RecountCityTileStatistics(city);
  city->random.crt = map->process_state; city->random.event = map->game_state;
  city->geography.rotation = 0;
  GenerateCityLandscape(city, map);
  city->geography.ocean = map->ocean; city->geography.river = map->river;
  city->geography.edit_height = map->water_level;
  city->geography.hills = settings->hills;
  city->geography.water = settings->water;
  city->geography.trees = settings->trees;
  city->game->drawing->scroll_pixels = (CityPoint){0};
  CenterCityMapViewAndResetOverlays(city, city->map_width / 2, city->map_height / 2, 0);
  RefreshCityMapViews(city->game);
  return true;
}

// Retain cancellation's prior idle result and the whole setup sequence.
int HandleNewCityCommand(City *city) {
  Game *game = city->game;
  CityInterfaceState *ui = game->ui;
  CityNewCitySetupDialog setup;
  bool generated_setup = ui->terrain_setup_enabled;
  if (generated_setup) {
    ConstructNewCitySetupDialog(city, &setup);
    CopyText(setup.city_name, sizeof(setup.city_name), "New City");
    ui->terrain_setup_active = true;
    int answer = RunPausedCityAction(city, ChooseNewCitySetupAction, &setup);
    ui->terrain_setup_active = false;
    if (answer != CITY_DIALOG_OK || GetGameError(game)) {
      DestroyNewCitySetupDialog(&setup);
      return 0;
    }
    if (!setup.city_name[0]) CopyText(setup.city_name, sizeof(setup.city_name), "New City");
  }
  ui->save_prompt_active = false;
  if (city->geography.settings_preserved == 0 && ConfirmCitySaveBeforeReplacement(city) == CITY_DIALOG_CANCEL) {
    if (generated_setup) {
      DestroyNewCitySetupDialog(&setup);
      return 0;
    }
    u32 old_idle = game->idle_state;
    if (old_idle != 0xFFFFFFFFu) {
      game->idle_state = CITY_IDLE_STARTUP_MENU;
      game->idle_entry = 1;
    }
    return (int)old_idle;
  }
  if (generated_setup && GetGameError(game)) {
    DestroyNewCitySetupDialog(&setup);
    return 0;
  }
  if (generated_setup && !ResizeCityMap(city, ui->terrain_preview->width, ui->terrain_preview->height)) {
    SetGameError(game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    DestroyNewCitySetupDialog(&setup);
    return 0;
  }
  if (city->geography.settings_preserved != 0)
    SetCityToolsVisible(city->game, 0);
  game->idle_state = CITY_IDLE_NEW_CITY;
  game->idle_entry = 1;
  if (generated_setup) {
    ResetCitySimulationState(city);
    RecalculateCityMapClientOrigins(city->game);
    InitializeNewCityDocument(city);
    ResetCityConstructionToolOptions(city);
  } else if (!InitializeCityDocumentForIdleMode(city)) return 0;
  city->settings.timer_mode = CITY_TIMER_INITIALIZING;
  ShowCityMapToolWindows(city);
  if (generated_setup) {
    if (!ApplyCityTerrainMap(city, ui->terrain_preview, &ui->terrain_settings)) return 0;
    ui->desktop.menu.tree_climate = ui->terrain_preview->climate == CITY_TERRAIN_SNOWY ? CLIMATE_ARCTIC :
        ui->terrain_preview->climate == CITY_TERRAIN_DESERT ? CLIMATE_DESERT_50 :
        ui->terrain_preview->climate == CITY_TERRAIN_SIMCITY ? CLIMATE_SIMCITY : CLIMATE_TEMPERATE;
    ui->desktop.menu.tree_species = CITY_TREE_RANDOM;
    CopyText((char *)city->overlay_names.entry[0].character, sizeof(city->overlay_names.entry[0].character),
        ui->terrain_mayor);
    ConfigureCityFromSetupValues(city, &setup);
  } else ConfigureNewCityFromSetupDialog(city);
  game->idle_state = CITY_IDLE_PLAY;
  game->idle_entry = 1;
  SetCityTimerMode(city, CITY_TIMER_RUNNING);
  city->geography.settings_preserved = 0;
  city->game->city_active = true;
  city->game->ui->replacement_prompt_suppressed = false;
  return 0;
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
  HideCityAuxiliaryWindowsAndStopSound(city);
  game->idle_state = CITY_IDLE_NEW_CITY;
  game->idle_entry = 1;
  ResetCitySimulationState(city);
  RecalculateCityMapClientOrigins(city->game);
  InitializeNewCityDocument(city);
  ResetCityConstructionToolOptions(city);
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

// Enter terrain editing with the existing map.
void InitializeNewCityTerrainSession(City *city) {
  Game *game = city->game;
  city->game->ui->save_prompt_active = false;
  if (ConfirmCitySaveBeforeReplacement(city) == CITY_DIALOG_CANCEL) {
    if (game->idle_state != (u32)CITY_IDLE_PLAY) {
      game->idle_state = CITY_IDLE_STARTUP_MENU;
      game->idle_entry = 1;
    }
    return;
  }

  EnterCityTerrainEditor(city);
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

static void PrepareCityFileOpen(City *city) {
  Game *game = city->game;
  game->city_active = false;
  InvalidateLoadedCityCanvas(city);
  city->game->drawing->palette.direct_animation = 0;
  game->idle_state = CITY_IDLE_LOAD_CITY;
  game->idle_entry = 1;
  HideCityAuxiliaryWindowsAndStopSound(city);
  ResetCitySimulationState(city);
  InitializeCityDocumentForIdleMode(city);
}

static void FinishCityFileOpen(City *city, bool loaded, u32 error_serial) {
  Game *game = city->game;
  if (loaded == 0) {
    // The codec may already have presented a more specific load failure.
    if (game->ui->command_error_serial == error_serial)
      ShowCityLoadResourceMessage(city, CITY_NOTICE_LOAD_CITY_FAILED, 0, 0xFFFFFFFFu);
    city->game->ui->restore_frame_tools = false;
    game->idle_state = CITY_IDLE_STARTUP_MENU;
    game->idle_entry = 1;
    ChangeCityLoadDirectory(city->game->file, CityPathText(&city->game->city_working_directory));
    HideCityAuxiliaryWindowsAndStopSound(city);
    SetCityToolsVisible(city->game, 0);
    city->game->city_active = false;
  } else {
    NotifyCityLoaded(city);
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
  RefreshCityToolbarAndClearOverlayMenuChecks(city);
}

// Picker cancellation, load failure and success retain distinct UI paths.
void OpenCityFromFileDialog(City *city) {
  Game *game = city->game;
  char *path = NULL;
  city->game->ui->save_prompt_active = false;
  city->game->drawing->palette.direct_animation = 1;
  const char *title = "Open City";
  CityPath directory = {0};
  GetCitySaveDirectory(city->game, &directory);
  int choice = ChooseCityCanvasOpenFile(city, "*.sc2", CityPathText(&directory), title, &path);
  if (choice != CITY_DIALOG_CANCEL && path != NULL && path[0] != 0) {
    PrepareCityFileOpen(city);
    CityFileCleanup cleanup = {free, directory.overflow, NULL};
    PushCityFileCleanup(city->game->file, &cleanup);
    CityFileCleanup path_cleanup = {free, path, NULL};
    PushCityFileCleanup(city->game->file, &path_cleanup);
    u32 error_serial = game->ui->command_error_serial;
    bool loaded = LoadCityFile(city, path);
    PopCityFileCleanup(city->game->file, &path_cleanup);
    PopCityFileCleanup(city->game->file, &cleanup);
    FinishCityFileOpen(city, loaded, error_serial);
  } else {
    city->game->ui->save_prompt_active = false;
    if (city->game->ui->restore_frame_tools) {
      SetCityToolsVisible(city->game, 1);
      RefreshCityToolbarAndClearOverlayMenuChecks(city);
    } else {
      game->idle_state = CITY_IDLE_STARTUP_MENU;
      game->idle_entry = 1;
    }
    city->game->drawing->palette.direct_animation = 0;
  }
  free(path);
  FreeCityPathStorage(&directory);
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
    NotifyCityLoaded(city);
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

static int ChooseNewCitySetupAction(City *city, void *context) {
  return ChooseNewCitySetupDialog(city, context);
}

// Whole accepted setup, including actual paused wrapper/unlock/status
// callees. Modal cancellation does not skip setup in the original.
void ConfigureNewCityFromSetupDialog(City *city) {

  ResetCitySimulationState(city);
  CityNewCitySetupDialog dialog;
  ConstructNewCitySetupDialog(city, &dialog);
  if (RunPausedCityAction(city, ChooseNewCitySetupAction, &dialog) == CITY_DIALOG_UNANSWERED) {
    DestroyNewCitySetupDialog(&dialog);
    return;
  }
  ConfigureCityFromSetupValues(city, &dialog);
}

// Apply accepted values and release the dialog's temporary name storage.
static void ConfigureCityFromSetupValues(City *city, CityNewCitySetupDialog *dialog) {
  CaptureCitySetupName(city, dialog);
  AssignCityName(city, (int)strlen(dialog->city_name), (const u8 *)dialog->city_name);
  if (city->name.length == 0)
    AssignCityName(city, dialog->previous_name.length, (const u8 *)CityNameText(&dialog->previous_name));
  int start_year = 0;
  start_year = dialog->start_year;
  ApplyCitySetupValues(city, dialog->era, start_year, dialog->difficulty);
  InsertRankedCityEvent(city, CITY_NEWS_CITY_FOUNDED, 0);
  DestroyNewCitySetupDialog(dialog);
}

static void ApplyCitySetupValues(City *city, int era, int start_year, int difficulty) {
  city->geography.start_year = start_year ? start_year : 50 * (era + 38);
  city->indicators.difficulty = difficulty + CITY_DIFFICULTY_EASY;
  switch (1900 + 50 * era) {
  case CITY_START_YEAR_1900:
    city->indicators.national_population = 10000;
    break;
  case CITY_START_YEAR_1950:
    city->indicators.national_population = 25000;
    break;
  case CITY_START_YEAR_2000:
    city->indicators.national_population = 60000;
    break;
  case CITY_START_YEAR_2050:
    city->indicators.national_population = 150000;
    break;
  }
  city->indicators.federal_interest_rate = 3;
  city->indicators.national_value = (3 * city->indicators.national_population) / 10;
  city->indicators.national_economy_trend = city->indicators.difficulty - CITY_DIFFICULTY_EASY;
  WriteHistorySample(city, CITY_HISTORY_GNP, 0, 3);
  WriteHistorySample(city, CITY_HISTORY_NATIONAL_POPULATION, 0, city->indicators.national_population);
  for (int index = 0; index < CITY_MILESTONE_COUNT; ++index) {
    int due = Sc2kCrtRandRange(city, 20) + CityMilestoneDefaultYear(index);
    city->economic_tables.milestone_years.due_year[index] = due < city->geography.start_year ? 0 : due;
  }
  RebuildCityUnlockState(city);
  switch ((int)city->indicators.difficulty) {
  case CITY_DIFFICULTY_EASY:
    city->indicators.cash = 20000;
    break;
  case CITY_DIFFICULTY_MEDIUM:
    city->indicators.cash = 10000;
    break;
  case CITY_DIFFICULTY_HARD: {
    city->indicators.cash = 10000;
    city->economic_tables.bond_rates.rate[city->indicators.bond_count] = city->indicators.federal_interest_rate;
    city->indicators.bond_rate_total = city->indicators.federal_interest_rate;
    ++city->indicators.bond_count;
    city->economy.sector[CITY_ECONOMY_BONDS].current_amount = city->indicators.bond_count;
    city->economy.sector[CITY_ECONOMY_BONDS].current_rate =
        (u32)(10000 * city->indicators.bond_rate_total / (int)city->indicators.bond_count);
    city->economy.sector[CITY_ECONOMY_BONDS].monthly_amount[0] = 1;
    city->economy.sector[CITY_ECONOMY_BONDS].monthly_rate[0] =
        (u32)(10000 * city->indicators.bond_rate_total / (int)city->indicators.bond_count);
    city->economy.sector[CITY_ECONOMY_BONDS].accumulated =
        city->economy.sector[CITY_ECONOMY_BONDS].current_rate * city->economy.sector[CITY_ECONOMY_BONDS].current_amount;
    city->economy.sector[CITY_ECONOMY_BONDS].projected_annual_total =
        12 * city->economy.sector[CITY_ECONOMY_BONDS].accumulated;
    break;
  }
  }
  if (city->geography.ocean != 0) {
    city->account_names.value[0] = 0;
    memcpy(city->geography.neighbor_names.name[0], "Ocean", 6u);
    city->primary_accounts.value[0] = 0;
    city->secondary_accounts.value[0] = 0;
    city->fourth_accounts.value[0] = 0;
  }
  RefreshCityNeighborNames(city);
  if (city->game->debug_city) DoCityCommand(city, CITY_COMMAND_ENABLE_DEBUG_CITY, 0, INVALID_TILE, 0, 0, 0);
  UpdateCityStatus(city);
}

static void ResetCityConstructionToolOptions(City *city) {
  CityInterfaceState *ui = city->game->ui;
  for (int group = 0; group < CITY_TOOL_GROUP_BUILD_COUNT; ++group) {
    int variant = group >= CITY_TOOL_GROUP_RESIDENTIAL && group <= CITY_TOOL_GROUP_INDUSTRIAL
        ? CITY_ZONE_DENSE : CITY_TOOL_VARIANT_DEFAULT;
    memcpy(ui->tool_labels[group], city->game->resources->tool_names.name[group][variant],
        sizeof(ui->tool_labels[group]));
    ui->tool_indices[group] = variant;
  }
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
  ResetCityConstructionToolOptions(city);
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

// Replacement Commands

// Native replacement follows the user's explicit New/Load/Abandon choices without a save question.
int ConfirmCitySaveBeforeReplacement(City *city) {
  city->game->ui->save_prompt_active = false;
  HideCityAuxiliaryWindowsAndStopSound(city);
  return CITY_DIALOG_YES;
}

// Close auxiliary panels in original order, then stop playback.
void HideCityAuxiliaryWindowsAndStopSound(City *city) {
  HideCityChartPanel(city, CITY_PANEL_HISTORY);
  HideCityChartPanel(city, CITY_PANEL_DEMOGRAPHICS);
  HideCityMapDataDialog(city);
  HideCityChartPanel(city, CITY_PANEL_NEIGHBORS);
  HideCityChartPanel(city, CITY_PANEL_INDUSTRY);
  SetCityToolsVisible(city->game, 0);
  StopAllCitySound(city->game);
}

// Scenario Commands

typedef struct {
  const char *path;
  void *stream;
  bool from_path;
} CityScenarioInput;

typedef struct {
  CityScenarioSelectionDialog *dialog;
  CityScenarioInput input;
} CityScenarioLifetime;

static void CloseScenarioInput(void *context) {
  CityScenarioInput *input = context;
  void *stream = input->stream;
  input->stream = NULL; // Close consumes the wrapper even if it throws.
  if (stream) CityFileCloseStream(stream);
}

static void DestroyScenarioLifetime(void *context) {
  CityScenarioLifetime *lifetime = context;
  CloseScenarioInput(&lifetime->input);
  DestroyCityScenarioSelectionDialog(lifetime->dialog);
}

static bool PrepareCityScenarioLoad(City *city, const CityScenarioInput *input) {
  Game *game = city->game;
  CityInterfaceState *ui = game->ui;
  HideCityAuxiliaryWindowsAndStopSound(city);
  ResetCitySimulationState(city);
  if (!InitializeCityDocumentForIdleMode(city)) return false;
  u32 error_serial = ui->command_error_serial;
  bool loaded = LoadCityScenarioMetadata(city, input->path);
  if (!loaded) {
    if (input->from_path) {
      // The codec may already have presented a more specific load failure.
      if (ui->command_error_serial == error_serial)
        ShowCityLoadResourceMessage(city, CITY_NOTICE_LOAD_CITY_FAILED, 0, 0xFFFFFFFFu);
      game->path_open_flag = 0;
    }
    if (!ui->restore_frame_tools) {
      game->idle_state = CITY_IDLE_STARTUP_MENU;
      game->idle_entry = 1;
    }
    if (!input->from_path || !ui->restore_frame_tools) return false;
  }
  NotifyCityLoaded(city);
  ShowCityMapToolWindows(city);
  game->idle_state = CITY_IDLE_PLAY;
  game->idle_entry = 1;
  SetCityTimerMode(city, CITY_TIMER_RUNNING);
  return true;
}

static bool ReadScenarioInstructions(City *city, CityScenarioInput *input, char text[CITY_SCENARIO_INSTRUCTION_BYTES + 1]) {
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

// Retained picker, load, instructions and cleanup.
void OpenCityScenarioFromFileDialog(City *city) {
  Game *game = city->game;
  CityScenarioSelectionDialog dialog;
  ConstructCityScenarioSelectionDialog(&dialog, 0);
  CityScenarioLifetime lifetime = {.dialog = &dialog};
  CityFileCleanup cleanup = {DestroyScenarioLifetime, &lifetime, NULL};
  PushCityFileCleanup(game->file, &cleanup);
  game->ui->save_prompt_active = false;
  game->drawing->palette.direct_animation = 1;
  RunCityScenarioSelectionDialog(city, &dialog);
  game->drawing->palette.direct_animation = 0;
  if (dialog.selected_index == CITY_SCENARIO_NO_SELECTION) {
    if (!game->ui->restore_frame_tools) {
      game->idle_state = CITY_IDLE_STARTUP_MENU;
      game->idle_entry = 1;
    }
  } else {
    lifetime.input.path = dialog.selected_path;
    char text[CITY_SCENARIO_INSTRUCTION_BYTES + 1];
    if (PrepareCityScenarioLoad(city, &lifetime.input) && ReadScenarioInstructions(city, &lifetime.input, text)) {
      ShowCityTextMessageDialog(city, text, 0, 0);
      CompleteCityScenarioLoad(city, false);
    }
  }
  PopCityFileCleanup(game->file, &cleanup);
  DestroyScenarioLifetime(&lifetime);
}

// Retained direct path and its distinct failure/encoding behavior.
void OpenCityScenarioFromPath(City *city, const char *path) {
  CityScenarioSelectionDialog dialog;
  ConstructCityScenarioSelectionDialog(&dialog, 0);
  CityScenarioLifetime lifetime = {.dialog = &dialog,
      .input = {.path = path, .from_path = true}};
  CityFileCleanup cleanup = {DestroyScenarioLifetime, &lifetime, NULL};
  PushCityFileCleanup(city->game->file, &cleanup);
  city->game->ui->save_prompt_active = false;
  char text[CITY_SCENARIO_INSTRUCTION_BYTES + 1];
  if (PrepareCityScenarioLoad(city, &lifetime.input) && ReadScenarioInstructions(city, &lifetime.input, text)) {
    ShowCityTextMessageDialog(city, text, 0, 0);
    CompleteCityScenarioLoad(city, true);
  }
  PopCityFileCleanup(city->game->file, &cleanup);
  DestroyScenarioLifetime(&lifetime);
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
      ReadScenarioInstructions(city, &action->input, file->scenario_instructions);
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
