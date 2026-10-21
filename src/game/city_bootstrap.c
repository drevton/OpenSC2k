#include "city/economy/facilities.h"
#include "drawing/maps/map_view.h"
#include "game/application.h"
#include "assets/assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/application_internal.h"
#include "city/saveload/runtime.h"
#include "audio/state.h"
#include "city/economy/economy.h"
#include "game/state.h"
#include "city/engine/initialization.h"
#include "city/engine/simulation.h"
#include "city/state.h"
#include "city/saveload/file.h"
#include "game/errors.h"
#include "assets/catalog.h"
#include "drawing/drawing.h"
#include "drawing/state.h"
#include "assets/resources.h"
#include "assets/state.h"
#include "ui/state.h"
#include "ui/text/text.h"
#include "ui/tools/tools.h"

// Host file context

bool InitializeCityFileDirectory(Game *game) {
  char *directory = ReadWorkingDirectory();
  if (directory == NULL) return false;
  free(game->file_directory);
  game->file_directory = directory;
  return true;
}

char *CityFileDirectory(const CityFileRuntime *file) { return file->game->file_directory; }
void SetCityFileDirectory(CityFileRuntime *file, const char *path) { SetText(&file->game->file_directory, path); }
bool ShouldBackupCitySave(const CityFileRuntime *file) { return file->game->backup_saves; }

static bool LoadInitialGameCity(Game *game, const char *path) {
  City *city = game->city;
  Drawing *drawing = game->drawing;
  CityResourceState *resources = game->resources;
  CityAudioState *audio = game->audio;
  // Initialize City independently of the comparison image's initial values.
  InitializeCityDefaults(city);
  InitializeDrawingDefaults(drawing);
  game->city->construction = (CityConstructionState){0};
  InitializeCityInteractionState(game->ui);
  InitializeCityToolbars(game->ui);
  // Seed both random generators before entering city play.
  SeedCityMapRandomState(city, SimulationFrameTime(city) | 1);
  SeedCityEventRandomState(city, SimulationFrameTime(city) | 1);
  memcpy(city->country.character, "usa", sizeof(city->country.character));
  InitializeCityAudioState(audio);
  FreeCityPathStorage(&game->city_working_directory);
  // Resource text is owned native storage; reference descriptors are diagnostic transport.
  if (!InitializeCityResourceNames(resources)) {
    SetGameError(game, SC2K_ERROR_GUEST_BUFFER_TOO_SMALL);
    return false;
  }
  InitializeCityFacilityInformation(city);
  drawing->map_view_storage = (CityMapViewConfig){0};
  drawing->drag_view_storage = (CityMapViewConfig){0};
  drawing->default_map_view = &drawing->map_view_storage;
  drawing->drag_map_view = &drawing->drag_view_storage;
  drawing->default_map_view->zoom = CITY_MAP_ZOOM_LARGE;
  SetSimulationFrameTime(city, 0);
  InitializeCityNewsResources(resources);
  InitializeCityRuntimeBuffers(city);
  // Initialize the pre-load flag before saved MISC replaces it.
  InitializeCityDisasterDefaults(city);
  InitializeCityStorage(city);
  if (!path[0]) {
    InitializeNewCityDocument(city);
    SetCityNameText(game, &city->name, 8, (const u8 *)"New City");
    game->idle_state = CITY_IDLE_PLAY;
    game->city_active = false;
    drawing->map_state.sprite_catalog = resources->sprites;
    return !GetGameError(game);
  }
  CityFile *file = CityFileRead(path);
  if (file == NULL)
    return false;
  SetCityPathBytes(game, &game->city_path, (int)strlen(path), path);
  bool scenario = CityFileHasChunk(file, "SCEN");
  bool loaded = scenario || LoadCityArchive(city, file);
  CityFileFree(file);
  if (!loaded)
    return false;
  drawing->map_state.sprite_catalog = resources->sprites;
  Sc2kSetCityMapClientRect(drawing, (CityRect){0, 0, 600, 490});
  // Pointer input can arrive before the first framebuffer allocation/draw.
  drawing->map_width = drawing->client_rect.right - drawing->client_rect.left;
  drawing->map_height = drawing->client_rect.bottom - drawing->client_rect.top;
  if (scenario) {
    // Scenario metadata already goes through LoadCityFile and its original completion calls.
    if (!LoadCityScenarioMetadata(city, path))
      return false;
  } else {
    // Restore the saved view, then rebuild city unlocks.
    // The native chunk-first boot needs rendering resources ready before these existing routines run.
    CenterCityMapViewAndResetOverlays(city, drawing->map_state.saved_view.x, drawing->map_state.saved_view.y,
        drawing->map_state.saved_view.mode);
    RebuildCityUnlockState(city);
  }
  game->idle_state = CITY_IDLE_PLAY;
  if (GetGameError(game) != 0)
    return false;
  // Activate play only after the complete city load succeeds.
  game->city_active = true;
  return true;
}

bool InitializeGameCity(Game *game, const char *path) {
  if (!InitializeCityFileDirectory(game)) return false;
  CityFileRuntime operation;
  BeginCityFileOperation(game, &operation);
  bool result = LoadInitialGameCity(game, path);
  if (result && game->debug_city)
    result = DoCityCommand(game->city, CITY_COMMAND_ENABLE_DEBUG_CITY, 0, INVALID_TILE, 0, 0, 0) >= 0;
  EndCityFileOperation(&operation);
  return result;
}

void ReleaseGameCityAssets(Game *game) {
  CloseCityScripting(game);
  ReleaseCityAssets(game->resources);
  game->drawing->cursors.initialized = false;
  game->drawing->map_state.sprite_catalog = NULL;
}
