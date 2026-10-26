#ifndef SC2K_UI_CITY_LIFECYCLE_H
#define SC2K_UI_CITY_LIFECYCLE_H

#include "city/types.h"

typedef struct Game Game;

// Local choices coordinate file/setup windows and Game's lifecycle services. Paths are borrowed for the call;
// retained requests own copies. Cancellation releases those requests without restarting the operation.
void RequestCityFileOpen(Game *game);
void RequestCityFilePath(Game *game, const char *path);
void RequestNewCity(Game *game);
void CompleteCityTerrainEditing(Game *game);
void RequestCityTerrainEditor(Game *game);
void RequestCityScenarioOpen(Game *game);
void OpenCityScenarioPath(Game *game, const char *path, bool from_path);
bool RequestCityConfiguration(Game *game);

// Retained startup/setup entry points run through synchronous choices and notices. Generated-preview cancellation
// releases its temporary setup record; configuration preserves the retained caller's original reply rules.
void RunCityCreationSetup(City *city);
void RunCityConfigurationSetup(City *city);
void RunCityTerrainEditorSetup(City *city);

// Retained picker callers own their dialog/path cleanup through loading and instructions. Direct scenario paths
// are borrowed for the call; cancellation and file failures keep their existing, separate completion paths.
// A live Game file operation owns the registered cleanup records throughout these calls.
void OpenCityFromFileDialog(City *city);
void OpenCityScenarioFromFileDialog(City *city);
void OpenCityScenarioFromPath(City *city, const char *path);

// Reset local tool variants and labels from immutable Assets; City simulation state is untouched.
void ResetCityConstructionToolOptions(Game *game);
// Close auxiliary panels in order and stop sound before an accepted replacement.
void CloseCityAuxiliaryPresentation(City *city);
void PrepareCityReplacementPresentation(City *city);

// Clear replacement-specific sign, marker, cost and demolition presentation, then queue information refresh.
// This consumes the accepted City's current state without changing simulation or host file state.
void ResetCityReplacementPresentation(City *city);
// Discard the main map cache and damage the default canvas after loading or replacing City.
void InvalidateLoadedCityCanvas(City *city);

// Close old windows and their temporary pauses before Game replaces the city.
void CloseCityWindowsForReplacement(Game *game);

// Resume detached window callbacks, scenario selection, city creation and file-dialog callers in that order.
// Each step keeps its own command-depth/admission rules; this adds no outer guard or repetition.
// Call after input delivery or from unblocked host polling. Shutdown uses its separate cleanup phases below.
void CompleteCityPendingWindows(Game *game);

// Shutdown keeps these UI phases around Game's existing owner teardown boundaries.
// Begin closes content windows and manual budget while presentation is still available.
void BeginCityInterfaceShutdown(Game *game);
// Complete scenario/window callers and budget UI records after host hooks are detached,
// while City continuations and callback contexts are still live.
void CompleteCityInterfaceCallers(Game *game);
// Clear startup/preview state and detach presentation bindings after pending City/Game work
// is released, before Audio, SDL and Drawing teardown.
void ReleaseCityInterfacePresentation(Game *game);

#endif
