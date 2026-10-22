#ifndef SC2K_CITY_ENGINE_SIMULATION_STATE_H
#define SC2K_CITY_ENGINE_SIMULATION_STATE_H

#include "city/infrastructure/types.h"
#include "city/maps/map_types.h"
#include "city/types.h"

// City owns cadence counters independently of host clock reads or presentation frames.
typedef struct {
  u32 counter;
  u16 parity;
  u32 tick_due;
} TimerCadenceCounter;

// Pending algorithm state belongs to City. Zero phase means no suspended work.
// Order-sensitive scans retain the original scan, queue and random draw ordering.
typedef struct CitySimulationWork {
  u32 phase;
  // Decay and utility stage 0 count sections; other scans count tile positions.
  int cursor;
  u8 stage;
  bool large_city_effect, running;
  CityUtilityTraversal network;
  CityEnvironmentScan environment;
} CitySimulationWork;

#endif
