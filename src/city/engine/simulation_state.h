#ifndef SC2K_CITY_ENGINE_SIMULATION_STATE_H
#define SC2K_CITY_ENGINE_SIMULATION_STATE_H

#include "city/economy/state.h"
#include "city/infrastructure/types.h"
#include "city/maps/map_types.h"
#include "city/types.h"

// Pending algorithm state belongs to City. Zero phase means no suspended work.
// Order-sensitive scans retain the original scan, queue and random draw ordering.
typedef struct CitySimulationWork {
  u32 phase;
  // The phase selects utility progress or a plain growth/decay cursor; environment keeps its own scan below.
  union {
    CityUtilityScan utility; // Power/water phases own their cursor, stage and active component.
    int cursor; // Growth/decay phases use the same cursor storage without utility state.
  };
  bool large_city_effect, running;
  CityEnvironmentScan environment;
} CitySimulationWork;

// City owns suspended tick callers independently of the UI notice that presents each boundary.
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

#endif
