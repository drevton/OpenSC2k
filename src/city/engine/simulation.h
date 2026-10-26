#ifndef SC2K_DOMAIN_SRC_CITY_ENGINE_SIMULATION_H
#define SC2K_DOMAIN_SRC_CITY_ENGINE_SIMULATION_H

#include "city/engine/simulation_state.h"
#include "city/types.h"

// Simulation

// One idle command advances a bounded batch, without advancing the calendar
// again while the current phase is pending. Small cities use the stock path.
bool StepCitySimulationWork(City *city);
void FinishCitySimulationWork(City *city);
// Explicit native tick options. A result >=2 identifies suspended tick work.
enum {
  CITY_TICK_DEFER_DIALOGS = 1,
  CITY_TICK_FROM_IDLE = 2,
  CITY_TICK_ALLOW_TERRAIN_WATER = 4,
  // Retained event-idle tail; not accepted by the public advance-tick command.
  CITY_TICK_FROM_EVENT_IDLE = 8,
  CITY_TICK_EVENT_VIEW_ACTIVE = 16,
};
i64 AdvanceCitySimulationPhaseWithOptions(City *city, u32 options);
i64 AdvanceCityDisasterWithOptions(City *city, u32 options);
void SelectCityNewspaperEdition(City *city, int edition);
i64 BeginCityNewspaperOpening(City *city, int edition);
const CityTickContinuation *FindCityTickContinuation(const City *city, int token);
// Resuming newspaper initialization consumes its token and finishes the City tail only; UI owns page creation.
i64 ResumeCityTick(City *city, int token, bool resume, int answer);
void ReleaseCityTickContinuations(City *city);
// Reply only to a live military-offer token. Requires INVALID_TILE, YES/NO and a zero reserved field.
// TEST leaves the caller suspended; execution resumes it once with live City state and may return another token.
i64 CmdAnswerMilitaryOffer(City *city, u32 flags, TileIndex tile, int token, int answer, int reserved);
// Phase routines operate on City. Presentation uses the Game-owned drawing configuration.
void AdvanceCitySimulationPhase(City *city);

// Set a supported gameplay fix and refresh its derived state. TEST validates fix/Boolean/reserved values only.
i64 CmdSetCityFix(City *city, u32 flags, int fix, int enabled, int reserved);

void SetCityTimerMode(City *city, int mode);

#endif
