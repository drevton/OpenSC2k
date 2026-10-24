#include "city/engine/simulation.h"
#include "city/engine/clock.h"
#include "city/maps/environment.h"

#include <assert.h>

#include "city/economy/budget.h"
#include "city/engine/commands.h"
#include "city/engine/simulation_state.h"
#include "city/growth/growth.h"
#include "city/infrastructure/transport.h"
#include "city/infrastructure/utilities.h"
#include "city/maps/map.h"
#include "city/presentation.h"
#include "city/state.h"

// Scheduling and bounded City work shared by native and comparison dispatch.

// Run one bounded batch of the currently scheduled simulation pass. Returns true when no work remains and false when
// suspended or already running; the cursor and scratch state remain owned by City between calls.
bool StepCitySimulationWork(City *city) {
  CitySimulationWork *work = &city->simulation_work;
  if (!work->phase) return true;
  if (work->running) return false;
  work->running = true;
  bool complete = false;
  enum { kSectionsPerBatch = 16, kMapBatch = kSectionsPerBatch * CITY_MAP_SECTION_CELL_COUNT,
      kGrowthBatch = kMapBatch / 16 };
  if (work->phase == CITY_PHASE_POWER) {
    complete = StepCityPowerGrid(city, work, kMapBatch);
  } else if (work->phase == CITY_PHASE_WATER_NETWORK) {
    complete = StepCityWaterNetwork(city, work, kMapBatch);
  } else if (work->phase == CITY_PHASE_ENVIRONMENT) {
    complete = StepCityEnvironment(city, &work->environment, 4 * kMapBatch / city->map_width);
  } else if (work->phase == CITY_PHASE_TRAFFIC_DECAY) {
    if (!work->cursor) {
      InvalidateMapLayer(city, CITY_MAP_CHANGE_TRAFFIC_DENSITY);
      city->indicators.city_traffic = 0;
    }
    const int sections = city->map_cell_count / CITY_MAP_SECTION_CELL_COUNT;
    int end = IntMin(sections, work->cursor + kSectionsPerBatch);
    u32 total = city->indicators.city_traffic;
    for (int section = work->cursor; section < end; ++section)
      total += DecayTrafficDensitySection(city, section);
    city->indicators.city_traffic = total;
    work->cursor = end;
    complete = end == sections;
  } else if (work->phase >= CITY_PHASE_TILE_SCAN_FIRST && work->phase <= CITY_PHASE_TILE_SCAN_LAST) {
    int quarter = work->phase - CITY_PHASE_TILE_SCAN_FIRST;
    int count = IntMin(kGrowthBatch, city->map_cell_count / 16 - work->cursor);
    ScanCityTileBatch(city, quarter % 4, quarter / 4,
        work->cursor, count, work->large_city_effect);
    work->cursor += count;
    complete = work->cursor == city->map_cell_count / 16;
  }
  work->running = false;
  if (complete) *work = (CitySimulationWork){0};
  return complete;
}

// Drain the current simulation pass using its existing working state. Requires a caller outside the pass's running
// callback; completion clears the pass without scheduling a new one.
void FinishCitySimulationWork(City *city) {
  if (city->simulation_work.running) return;
  while (city->simulation_work.phase) StepCitySimulationWork(city);
}

// Advance the deterministic cadence counter, palette due flags and sound-delay completion. Requires attached
// Audio/Drawing owners; returns the retained tick/parity projection and does not sample the host clock or execute a
// simulation phase.
i16 UpdateCityCadence(City *city) {
  TimerCadenceCounter *cadence = &city->cadence_counter;
  u32 counter = cadence->counter + 1;
  bool animate = counter != 0;
  cadence->counter = counter;
  if (counter != 0 && counter % 8 == 0) {
    counter = 0;
    cadence->counter = counter;
  }
  AdvanceGameCadencePresentation(city->game, counter, animate);
  int speed = city->settings.speed;
  bool tick_due = (speed == CITY_SPEED_PAUSED && counter == 0) || (speed == CITY_SPEED_SLOW && counter % 4 == 0) ||
      (speed == CITY_SPEED_NORMAL && counter % 2 == 0) || speed == CITY_SPEED_FAST || speed == CITY_SPEED_UNLIMITED;
  if (tick_due)
    cadence->tick_due = 1;
  u32 result = cadence->tick_due;
  if (result != 0 && city->settings.timer_mode == CITY_TIMER_EVENT && speed != CITY_SPEED_UNLIMITED) {
    int parity = cadence->parity;
    result = (result & 0xFFFF0000u) | (u16)(parity + 1);
    cadence->parity = result;
    if (parity & 1)
      cadence->tick_due = 0;
  }
  return (i16)result;
}

// Store an agreed simulation speed and refresh its status presentation. The caller validates the speed enum; this does
// not advance simulation or persist a local preference.
void SelectCitySimulationSpeed(City *city, int speed) {
  assert(speed >= CITY_SPEED_PAUSED && speed <= CITY_SPEED_UNLIMITED);
  city->settings.speed = speed;
  RefreshCitySpeedStatus(city, 0);
}

// Set an agreed valid City speed independently of local dialog pauses. Rejects unsupported speed/reserved values; TEST
// validates only and execution refreshes speed presentation after the shared change.
i64 CmdSetSimulationSpeed(City *city, u32 flags, int speed, int reserved1, int reserved2) {
  if (speed < CITY_SPEED_PAUSED || speed > CITY_SPEED_UNLIMITED ||
      reserved1 != 0 || reserved2 != 0)
    return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) return 0;
  SelectCitySimulationSpeed(city, speed);
  return 1;
}

// Enable or disable one gameplay-affecting runtime fix. Rejects presentation-only fixes, invalid indices and reserved
// arguments; TEST has no effects and execution updates City deterministically.
i64 CmdSetCityFix(City *city, u32 flags, int fix, int enabled, int reserved) {
  if ((u32)fix >= CITY_FIX_COUNT ||
      !(CITY_FIX_SIMULATION_MASK & (1ull << fix)) || (u32)enabled > 1 || reserved)
    return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) return 0;
  if (enabled) city->fixes |= 1ull << fix;
  else city->fixes &= ~(1ull << fix);
  if (fix == CITY_FIX_ASSET_VALUES || fix == CITY_FIX_TREATMENT_VALUE ||
      fix == CITY_FIX_DAILY_VALUATION)
    RecalculateCityAssetValue(city);
  if (fix == CITY_FIX_LOADED_CONNECTIONS && enabled)
    RecountCityIndustryConnections(city);
  return 1;
}
