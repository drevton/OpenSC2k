#ifndef SC2K_GAME_CITY_LIFECYCLE_H
#define SC2K_GAME_CITY_LIFECYCLE_H

#include "city/saveload/types.h"
#include "city/terrain/types.h"
#include "city/types.h"

// Accepted local creation choices, independent of any window or preview buffer.
// start_year is a calendar year; difficulty is the zero-based setup choice.
typedef struct CityCreationOptions {
  char city_name[33], mayor_name[33];
  int start_year, difficulty;
  CityTerrainSettings terrain;
  bool edit_terrain;
} CityCreationOptions;

typedef struct Game Game;
typedef struct CityFileRuntime CityFileRuntime;
typedef struct CityLoadStream CityLoadStream;

// Initialize supplied owners and load a terminated host path; an empty path creates the initial city.
// Failure preserves completed initialization and partial load writes. Assets and presentation owners must be ready.
bool InitializeGameCity(Game *game, const char *path);
// Close scripting before releasing Assets, then invalidate Drawing's cursor preparation and sprite metrics.
void ReleaseGameCityAssets(Game *game);

// Host transitions coordinate Game, City and presentation. These are local lifecycle actions,
// not CityCommand handlers: callers supply accepted setup values or borrowed host paths.
bool CreateCityFromOptions(City *city, const CityCreationOptions *options);
bool CreateCityFromTerrainFile(City *city, const CityCreationOptions *options, const char *path);
bool ConfigureCityFromOptions(City *city, const CityCreationOptions *options);
void EnterCityTerrainEditor(City *city);

// Install terrain after a City reset, then reset/repaint its main viewport. Failure preserves partial City writes
// and returns before the viewport reset. Map dimensions must match allocated City storage.
bool ApplyCityTerrainMap(City *city, const CityTerrainMap *map, const CityTerrainSettings *settings);
// Apply accepted setup control values, optional launch debug policy and final status in that order.
// era/difficulty are zero-based choices; a zero start_year uses the selected era's year.
void ApplyCitySetupValues(City *city, int era, int start_year, int difficulty);

// Accepted direct paths share the host load completion used by retained UI callers.
void OpenCityFromPath(City *city, const char *path);

// Reset replacement UI, apply launch debug policy, invalidate the canvas and present import warnings, in order.
// The load caller retains admission and continuation decisions, including metadata-error recovery.
void CompleteCityLoad(City *city);

// Reset the old city before reading; complete with the captured pre-read error serial so a specific load error
// is not replaced by a generic one. The caller retains a live Game file operation through both phases.
// Failure preserves partial codec writes; completion uses live City state.
void PrepareCityFileOpen(City *city);
void FinishCityFileOpen(City *city, bool loaded, u32 error_serial);

// Selected paths are borrowed for the call. Failed loads preserve partial City writes.
bool OpenCitySelectedFile(City *city, const u8 *text, int length, bool from_path);
// A scenario caller borrows path and owns stream until CloseCityScenarioInput or an explicit transfer to its file
// operation. Zero-initialize stream. Close consumes it even if the underlying file close throws.
typedef struct CityScenarioInput {
  const char *path;
  CityLoadStream *stream;
  bool from_path;
} CityScenarioInput;
void CloseCityScenarioInput(CityScenarioInput *input);
// True permits instruction loading. Direct-path callers restoring tools keep the original continuation after
// metadata failure. The caller retains its file operation and earlier City writes.
bool PrepareCityScenarioLoad(City *city, const CityScenarioInput *input);
// Open/read/normalize TEXT into a buffer of CITY_SCENARIO_INSTRUCTION_BYTES + 1 bytes. Failure leaves any
// opened stream owned by input; the caller closes it through its existing cleanup path.
bool LoadCityScenarioInstructions(City *city, CityScenarioInput *input,
    char text[CITY_SCENARIO_INSTRUCTION_BYTES + 1]);
// Run after instructions return or are dismissed, using live City state; stream closure is a separate step.
void CompleteCityScenarioLoad(City *city, bool from_path);

// Deferred instructions require a caller-owned file operation that remains alive until Finish or Cancel.
bool OpenCitySelectedScenario(City *city, const u8 *text, int length, bool from_path, bool defer_instructions);
bool FinishCityScenarioLoad(City *city, bool from_path);
void CancelCityScenarioLoad(CityFileRuntime *file);

// Replacement resets affect windows, tools, playback and host paths as well as City fields.
// Initialize follows Game's selected idle mode; false rejects unsupported setup before later steps run.
void ResetCitySimulationState(City *city);
bool InitializeCityDocumentForIdleMode(City *city);

#endif
