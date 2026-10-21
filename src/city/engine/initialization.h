#ifndef SC2K_DOMAIN_SRC_CITY_ENGINE_INITIALIZATION_H
#define SC2K_DOMAIN_SRC_CITY_ENGINE_INITIALIZATION_H

#include "city/types.h"

// Initialization

void AdjustIndustryTaxRates(City *city, int direction);
void InitializeCityRuntimeBuffers(City *city);
void InitializeCityNewspaperAndFacilityState(City *city);
void ResetNewCityOverlayAndEventState(City *city);
void ResetNewCityMapGenerationLayers(City *city);
void InitializeNewCityDocument(City *city);
void InitializeNewCitySimulationState(City *city);
void ResetCitySimulationState(City *city);
bool InitializeCityDocumentForIdleMode(City *city);
bool OpenCitySelectedFile(City *city, const u8 *text, int length, bool from_path);

// Startup

typedef struct City City;
typedef struct CityFileRuntime CityFileRuntime;
// Deferred instructions require a caller-owned file operation that survives command return.
bool OpenCitySelectedScenario(City *city, const u8 *text, int length, bool from_path, bool defer_instructions);
bool FinishCityScenarioLoad(City *city, bool from_path);
void CancelCityScenarioLoad(CityFileRuntime *file);
void CompleteCityScenarioLoad(City *city, bool from_path);
void EnterCityTerrainEditor(City *city);
struct CityCreationOptions;
bool CreateCityFromOptions(City *city, const struct CityCreationOptions *options);
bool ConfigureCityFromOptions(City *city, const struct CityCreationOptions *options);

void InitializeCityDisasterDefaults(City *city);

// Scenario Commands

// Both original application boundaries return void; path text is borrowed.
void OpenCityScenarioFromFileDialog(City *city);
void OpenCityScenarioFromPath(City *city, const char *path);

// Replacement Commands

typedef struct City City;

int ConfirmCitySaveBeforeReplacement(City *city);
void HideCityAuxiliaryWindowsAndStopSound(City *city);

void HideCityMapDataDialog(City *city);

#endif
