#ifndef SC2K_GAME_UPDATE_H
#define SC2K_GAME_UPDATE_H

#include "game/types.h"

typedef struct Game Game;

// Signal real input to the waiting startup dispatcher; ordinary play leaves the idle-input latch unchanged.
// Callers classify native input first. This does not filter framework messages or produce their handled result.
void RecordGameInput(Game *game);

// Apply focus changes to suppression, UI, Audio and palette presentation without advancing City phases.
// Exit requests suppress activation handling; active values 0/1 keep their existing deactivation/activation order.
void HandleApplicationActivation(Game *game, int active);

// Poll and apply one pending activation input through the installed Game input source.
void PumpGameActivation(Game *game);

void UpdateGame(Game *game);
i64 FinishCityIdleSimulation(Game *game, bool terrain_water_allowed);
void CompleteCityIdleSimulation(Game *game, u32 options);

void FinishGameCitySimulationWork(Game *game);

// Run one idle pass with explicit view/simulation permissions, restoring the parent inputs before delivering events.
// Permissions apply to UpdateGame only; event callbacks observe the restored parent scope.
void RunGameIdle(Game *game, GameIdleInputs inputs);
// Host deadline limits the number of batches; every started deterministic batch finishes before returning.
// Attempt one batch even when the deadline has passed. Stop checks use live City after idle callbacks return.
void RunCitySimulationFrame(Game *game, double deadline);

#endif
