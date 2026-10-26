#include "city/saveload/state.h"
#include "base/text.h"
#include "city/engine/city_name.h"
#include "ui/city_lifecycle.h"

#include <stdlib.h>
#include <string.h>

#include "assets/state.h"
#include "game/audio.h"
#include "city/engine/initialization.h"
#include "city/engine/simulation.h"
#include "city/events/news.h"
#include "city/maps/trees.h"
#include "city/presentation.h"
#include "city/saveload/file.h"
#include "city/saveload/runtime.h"
#include "city/state.h"
#include "city/terrain/terrain.h"
#include "drawing/maps/map_view.h"
#include "drawing/state.h"
#include "game/city_lifecycle.h"
#include "game/commands.h"
#include "game/errors.h"
#include "game/observation.h"
#include "game/settings.h"
#include "game/state.h"
#include "ui/commands.h"
#include "ui/dialogs/budget.h"
#include "ui/dialogs/dialogs.h"
#include "ui/dialogs/file_dialog.h"
#include "ui/dialogs/map_data.h"
#include "ui/dialogs/messages.h"
#include "ui/dialogs/scenario.h"
#include "ui/dialogs/spans.h"
#include "ui/map.h"
#include "ui/news/newspaper.h"
#include "ui/signs.h"
#include "ui/startup/startup.h"
#include "ui/state.h"
#include "ui/text/status.h"
#include "ui/text/text.h"
#include "ui/tools/tools.h"
#include "ui/tools/types.h"
#include "ui/windows/tooltips.h"
#include "ui/windows/windows.h"

// UI retains accepted choices across picker/setup callbacks and scenario instructions.
// Game owns the actual city transition; each completion uses the live owners before releasing its request.

void ResetCityConstructionToolOptions(Game *game) {
  CityInterfaceState *ui = game->ui;
  for (int group = 0; group < CITY_TOOL_GROUP_BUILD_COUNT; ++group) {
    int variant = group >= CITY_TOOL_GROUP_RESIDENTIAL && group <= CITY_TOOL_GROUP_INDUSTRIAL
        ? CITY_ZONE_DENSE : CITY_TOOL_VARIANT_DEFAULT;
    CopyText(ui->tool_labels[group], sizeof(ui->tool_labels[group]), CityToolName(group, variant));
    ui->tool_indices[group] = variant;
  }
}

// Close auxiliary panels in original order, then stop playback.
void CloseCityAuxiliaryPresentation(City *city) {
  HideCityChartPanel(city, CITY_PANEL_HISTORY);
  HideCityChartPanel(city, CITY_PANEL_DEMOGRAPHICS);
  HideCityMapDataDialog(city);
  HideCityChartPanel(city, CITY_PANEL_NEIGHBORS);
  HideCityChartPanel(city, CITY_PANEL_INDUSTRY);
  SetCityToolsVisible(city->game, 0);
  StopAllCitySound(city->game);
}

// Native replacement follows the user's explicit New/Load/Abandon choices without a save question.
void PrepareCityReplacementPresentation(City *city) {
  city->game->ui->save_prompt_active = false;
  CloseCityAuxiliaryPresentation(city);
}

static int ChooseNewCitySetupAction(City *city, void *context) {
  return ChooseNewCitySetupDialog(city, context);
}

// Apply accepted values and release the dialog's temporary name storage.
static void ConfigureCityFromSetupValues(City *city, CityNewCitySetupDialog *dialog) {
  CaptureCitySetupName(city, dialog);
  AssignCityName(city, (int)strlen(dialog->city_name), (const u8 *)dialog->city_name);
  if (city->name.length == 0)
    AssignCityName(city, dialog->previous_name.length, (const u8 *)CityNameText(&dialog->previous_name));
  ApplyCitySetupValues(city, dialog->era, dialog->start_year, dialog->difficulty);
  InsertRankedCityEvent(city, CITY_NEWS_CITY_FOUNDED, 0);
  DestroyNewCitySetupDialog(dialog);
}

// Whole accepted setup, including actual paused wrapper/unlock/status
// callees. Modal cancellation does not skip setup in the original.
void RunCityConfigurationSetup(City *city) {
  ResetCitySimulationState(city);
  CityNewCitySetupDialog dialog;
  ConstructNewCitySetupDialog(&dialog);
  if (RunPausedCityAction(city, ChooseNewCitySetupAction, &dialog) == CITY_DIALOG_UNANSWERED) {
    DestroyNewCitySetupDialog(&dialog);
    return;
  }
  ConfigureCityFromSetupValues(city, &dialog);
}

// Run the retained setup sequence. Generated-preview cancellation releases the setup record before returning.
void RunCityCreationSetup(City *city) {
  Game *game = city->game;
  CityInterfaceState *ui = game->ui;
  CityNewCitySetupDialog setup;
  bool generated_setup = ui->terrain_setup_enabled;
  if (generated_setup) {
    ConstructNewCitySetupDialog(&setup);
    CopyText(setup.city_name, sizeof(setup.city_name), "New City");
    ui->terrain_setup_active = true;
    int answer = RunPausedCityAction(city, ChooseNewCitySetupAction, &setup);
    ui->terrain_setup_active = false;
    if (answer != CITY_DIALOG_OK || GetGameError(game)) {
      DestroyNewCitySetupDialog(&setup);
      return;
    }
    if (!setup.city_name[0]) CopyText(setup.city_name, sizeof(setup.city_name), "New City");
  }
  ui->save_prompt_active = false;
  if (city->geography.settings_preserved == 0)
    PrepareCityReplacementPresentation(city);
  if (generated_setup && GetGameError(game)) {
    DestroyNewCitySetupDialog(&setup);
    return;
  }
  if (generated_setup && !ResizeCityMap(city, ui->terrain_preview->width, ui->terrain_preview->height)) {
    SetGameError(game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    DestroyNewCitySetupDialog(&setup);
    return;
  }
  if (city->geography.settings_preserved != 0)
    SetCityToolsVisible(city->game, 0);
  game->idle_state = CITY_IDLE_NEW_CITY;
  game->idle_entry = 1;
  if (generated_setup) {
    ResetCitySimulationState(city);
    RecalculateCityMapClientOrigins(city->game);
    InitializeNewCityDocument(city);
    ResetCityConstructionToolOptions(game);
  } else if (!InitializeCityDocumentForIdleMode(city)) return;
  city->settings.timer_mode = CITY_TIMER_INITIALIZING;
  ShowCityMapToolWindows(city);
  if (generated_setup) {
    if (!ApplyCityTerrainMap(city, ui->terrain_preview, &ui->terrain_settings)) return;
    ui->desktop.menu.tree_climate = ui->terrain_preview->climate == CITY_TERRAIN_SNOWY ? CLIMATE_ARCTIC :
        ui->terrain_preview->climate == CITY_TERRAIN_DESERT ? CLIMATE_DESERT_50 :
        ui->terrain_preview->climate == CITY_TERRAIN_SIMCITY ? CLIMATE_SIMCITY : CLIMATE_TEMPERATE;
    ui->desktop.menu.tree_species = CITY_TREE_RANDOM;
    CopyText((char *)city->overlay_names.entry[0].character, sizeof(city->overlay_names.entry[0].character),
        ui->terrain_mayor);
    ConfigureCityFromSetupValues(city, &setup);
  } else RunCityConfigurationSetup(city);
  game->idle_state = CITY_IDLE_PLAY;
  game->idle_entry = 1;
  SetCityTimerMode(city, CITY_TIMER_RUNNING);
  city->geography.settings_preserved = 0;
  city->game->city_active = true;
  city->game->ui->replacement_prompt_suppressed = false;
}

// Enter terrain editing with the existing map.
void RunCityTerrainEditorSetup(City *city) {
  city->game->ui->save_prompt_active = false;
  PrepareCityReplacementPresentation(city);

  EnterCityTerrainEditor(city);
}

// Picker cancellation, load failure and success retain distinct UI paths.
void OpenCityFromFileDialog(City *city) {
  Game *game = city->game;
  char *path = NULL;
  city->game->ui->save_prompt_active = false;
  city->drawing->palette.direct_animation = 1;
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
      RebuildCityToolbar(city);
    } else {
      game->idle_state = CITY_IDLE_STARTUP_MENU;
      game->idle_entry = 1;
    }
    city->drawing->palette.direct_animation = 0;
  }
  free(path);
  FreeCityPathStorage(&directory);
}

static void CloseScenarioInput(void *context) {
  CloseCityScenarioInput(context);
}

// Retained picker, load, instructions and cleanup.
void OpenCityScenarioFromFileDialog(City *city) {
  Game *game = city->game;
  CityScenarioSelectionDialog dialog = {.base.resource_id = CITY_SCENARIO_SELECTION_RESOURCE,
      .selected_index = CITY_SCENARIO_NO_SELECTION};
  CityScenarioInput input = {0};
  CityFileCleanup cleanup = {CloseScenarioInput, &input, NULL};
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
    input.path = dialog.selected_path;
    char text[CITY_SCENARIO_INSTRUCTION_BYTES + 1];
    if (PrepareCityScenarioLoad(city, &input) && LoadCityScenarioInstructions(city, &input, text)) {
      ShowCityScenarioTextDialog(city, text);
      CompleteCityScenarioLoad(city, false);
    }
  }
  PopCityFileCleanup(game->file, &cleanup);
  CloseCityScenarioInput(&input);
}

// Retained direct path and its distinct failure/encoding behavior.
void OpenCityScenarioFromPath(City *city, const char *path) {
  CityScenarioInput input = {.path = path, .from_path = true};
  CityFileCleanup cleanup = {CloseScenarioInput, &input, NULL};
  PushCityFileCleanup(city->game->file, &cleanup);
  city->game->ui->save_prompt_active = false;
  char text[CITY_SCENARIO_INSTRUCTION_BYTES + 1];
  if (PrepareCityScenarioLoad(city, &input) && LoadCityScenarioInstructions(city, &input, text)) {
    ShowCityScenarioTextDialog(city, text);
    CompleteCityScenarioLoad(city, true);
  }
  PopCityFileCleanup(city->game->file, &cleanup);
  CloseCityScenarioInput(&input);
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
  CityFilePickerOptions options = {.name = "*.sc2", .directory = CityPathText(&directory), .title = "Open City",
      .open = true, .callback = OpenCityFileChoice, .context = request};
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
      CloseCityAuxiliaryPresentation(city);
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
          AreCityNewsResourcesAvailable(game->city)) ScheduleCityStartupNewspaper(city);
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
  if (!game->ranked_events_suppressed && AreCityNewsResourcesAvailable(game->city))
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
      AreCityNewsResourcesAvailable(city)) ScheduleCityStartupNewspaper(city);
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

void ResetCityReplacementPresentation(City *city) {
  Game *game = city->game;
  ResetCitySignEditor(city);
  if (game->ui->signs.indexed) RebuildCitySigns(city);
  SetCityErrorTile(game, INVALID_TILE);
  game->ui->error_tile_window = 0;
  memset(game->drawing->cost_texts, 0, sizeof(game->drawing->cost_texts));
  game->drawing->cost_texts_active = false;
  memset(game->drawing->demolition_effects, 0, sizeof(game->drawing->demolition_effects));
  game->drawing->demolition_active = false;
  InvalidateCityInformation(game, CITY_UPDATE_ALL);
}

void InvalidateLoadedCityCanvas(City *city) {
  city->drawing->main_cache.valid = 0;
  Sc2kDirtyMarkRect(&city->game->ui->desktop.dirty,
      (CityRect){0, 0, kDefaultCityCanvasWidth, kDefaultCityCanvasHeight});
}

// Keep ordinary input and host polling on the same completion order. Each step retains its own admission rules.
void CompleteCityPendingWindows(Game *game) {
  City *city = game->city;
  CompleteCityWindowCallbacks(game);
  CompleteCityScenarioSelection(city);
  CompleteCityCreationWindow(city);
  CompleteCityFileDialogs(city);
}

void BeginCityInterfaceShutdown(Game *game) {
  City *city = game->city;
  CityInterfaceState *ui = game->ui;
  ClearCityResourceNotices(ui);
  WindowIds windows = SnapshotCityWindowIds(&ui->desktop);
  for (int index = windows.count - 1; index >= 0; --index) {
    Window *window = FindCityCanvasWindow(&ui->desktop, windows.ids[index]);
    if (window && (window->newspaper || window->owned_message || window->completion))
      Sc2kCanvasDesktopDismissWindow(&ui->desktop, window->window_id);
  }
  CloseCityManualBudget(city);
}

void CompleteCityInterfaceCallers(Game *game) {
  City *city = game->city;
  CityInterfaceState *ui = game->ui;
  ui->command_display = NULL;
  ClearCityScenarioSelection(city);
  CompleteCityWindowCallbacks(game);
  ReleaseCityBudgetPresentation(ui);
}

void ReleaseCityInterfacePresentation(Game *game) {
  City *city = game->city;
  CityInterfaceState *ui = game->ui;
  ClearCityStartupMenuPresentation(city);
  ReleaseCityTerrainMap(ui->terrain_preview);
  free(ui->terrain_preview); ui->terrain_preview = NULL;
  SetCitySpanPresentation(ui, NULL);
  ShutdownCityFileDialogPresentation(city);
  UnbindCityChartPanels(city);
  SetCityStatusPresentation(&ui->status, NULL, 0);
  SetCityMapDataPresentation(ui, NULL);
  SetCityMapNavigationPresentation(game, false, false);
}
