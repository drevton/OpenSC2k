#ifndef SC2K_GAME_UPDATE_H
#define SC2K_GAME_UPDATE_H

#include "game/types.h"

typedef struct Game Game;

// Signal real input to the waiting startup dispatcher; ordinary play leaves the idle-input latch unchanged.
void RecordGameInput(Game *game);

// Poll and apply one pending activation input through the installed Game input source.
void PumpGameActivation(Game *game);

void UpdateGame(Game *game);
i64 FinishCityIdleSimulation(Game *game, bool terrain_water_allowed);
void CompleteCityIdleSimulation(Game *game, u32 options);

void AdvanceGameCadencePresentation(Game *game, u32 counter, bool animate);
void FinishGameCitySimulationWork(Game *game);

// Run one idle pass with explicit view/simulation permissions, restoring the parent inputs before delivering events.
void RunGameIdle(Game *game, CityIdleInputs inputs);
// Host deadline limits the number of batches; every started deterministic batch finishes before returning.
void RunCitySimulationFrame(Game *game, double deadline);

#endif
