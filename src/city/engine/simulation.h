#ifndef SC2K_DOMAIN_SRC_CITY_ENGINE_SIMULATION_H
#define SC2K_DOMAIN_SRC_CITY_ENGINE_SIMULATION_H

#include "city/economy/facilities.h"
#include "city/military.h"
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
typedef enum CityTickDialog {
  CITY_TICK_DIALOG_BUDGET, CITY_TICK_DIALOG_ECONOMY_NOTICE,
  CITY_TICK_DIALOG_MONTHLY_NEWSPAPER, CITY_TICK_DIALOG_MISSION,
  CITY_TICK_DIALOG_REWARD_NEWSPAPER,
  CITY_TICK_DIALOG_MILITARY_OFFER, CITY_TICK_DIALOG_MILITARY_RESULT,
  CITY_TICK_DIALOG_DISASTER_NEWSPAPER,
  CITY_TICK_DIALOG_MILESTONE_NEWSPAPER,
  CITY_TICK_DIALOG_AGING_NEWSPAPER,
  CITY_TICK_DIALOG_NEWSPAPER_INITIALIZATION,
  CITY_TICK_ANIMATION,
} CityTickDialog;
enum {
  CITY_NEWSPAPER_INITIALIZATION_ENDED = 1,
  CITY_NEWSPAPER_INITIALIZATION_SKIP_TAIL = 2,
};
typedef struct CityTickContinuation {
  struct CityTickContinuation *next;
  int token;
  u32 options;
  u8 dialog, edition, outcome;
  u16 notice;
  union {
    int arcology_count;
    int milestone;
    CityMilitaryPlacement military;
    CityAnnualFacilityWork annual;
  };
} CityTickContinuation;
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

// Store a valid agreed speed and refresh its presentation. Does not advance simulation or save a local preference.
void SelectCitySimulationSpeed(City *city, int speed);
// Validate speed and zero reserved fields. TEST has no effects; the command's map location is ignored.
i64 CmdSetSimulationSpeed(City *city, u32 flags, int speed, int reserved1, int reserved2);

// Set a supported gameplay fix and refresh its derived state. TEST validates fix/Boolean/reserved values only.
i64 CmdSetCityFix(City *city, u32 flags, int fix, int enabled, int reserved);

// Timer
i16 UpdateCityCadence(City *city);

void SetCityTimerMode(City *city, int mode);

#endif
