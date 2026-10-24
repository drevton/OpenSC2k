#ifndef SC2K_CITY_ENGINE_SIMULATION_STATE_H
#define SC2K_CITY_ENGINE_SIMULATION_STATE_H

#include "city/infrastructure/types.h"
#include "city/maps/environment.h"
#include "city/types.h"

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
