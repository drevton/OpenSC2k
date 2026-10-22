#include "city/saveload/state.h"
#include "city/engine/city_name.h"
#include "city/engine/clock.h"
#include "game/city_lifecycle.h"

#include <string.h>

#include "assets/assets.h"
#include "assets/catalog.h"
#include "assets/resources.h"
#include "assets/state.h"
#include "audio/state.h"
#include "city/economy/economy.h"
#include "city/economy/facilities.h"
#include "city/engine/commands.h"
#include "city/engine/initialization.h"
#include "city/engine/random.h"
#include "city/events/news.h"
#include "city/presentation.h"
#include "city/saveload/file.h"
#include "city/saveload/runtime.h"
#include "city/state.h"
#include "drawing/maps/map_view.h"
#include "drawing/state.h"
#include "game/application.h"
#include "game/errors.h"
#include "game/state.h"
#include "ui/console.h"
#include "ui/state.h"
#include "ui/text/text.h"
#include "ui/tools/tools.h"


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
  InitializeCityFacilityInformation(resources, (const char *)city->country.character);
  drawing->map_view_storage = (CityMapViewConfig){0};
  drawing->drag_view_storage = (CityMapViewConfig){0};
  drawing->default_map_view = &drawing->map_view_storage;
  drawing->drag_map_view = &drawing->drag_view_storage;
  drawing->default_map_view->zoom = CITY_MAP_ZOOM_LARGE;
  SetSimulationFrameTime(city, 0);
  InitializeCityNewsResources(resources);
  InitializeCityNews(city);
  // Initialize the pre-load flag before saved MISC replaces it.
  InitializeCityDisasterDefaults(city);
  InitializeCityStorage(city);
  if (!path[0]) {
    InitializeNewCityDocument(city);
    SetCityNameText(game, &city->name, 8, (const u8 *)"New City");
    game->idle_state = CITY_IDLE_PLAY;
    game->city_active = false;
    drawing->map_state.sprite_metrics = resources->map_sprite_metrics;
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
  drawing->map_state.sprite_metrics = resources->map_sprite_metrics;
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
    RestoreCityLoadedView(city);
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
  ++game->drawing->cursors.revision;
  game->drawing->map_state.sprite_metrics = NULL;
}
