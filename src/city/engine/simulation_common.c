#include "city/infrastructure/infrastructure.h"
#include "audio/types.h"
#include "city/engine/command_types.h"
#include "city/presentation.h"
#include "city/engine/simulation.h"

#include <string.h>

#include "audio/audio.h"
#include "city/engine/city_types.h"
#include "game/state.h"
#include "city/engine/initialization.h"
#include "city/state.h"
#include "city/saveload/file.h"
#include "game/errors.h"
#include "game/observation.h"
#include "game/state.h"
#include "audio/state.h"
#include "assets/state.h"

// Scheduling, pending work and City effects shared by native and comparison dispatch.

// Prepare cursor and music for a due manual annual budget. Retained comparison callers preserve their minimized
// gate; normal City execution consumes the selection draw independently of local window state.
bool PrepareCityAnnualBudget(City *city) {
  if (city->indicators.auto_budget_enabled || (city->game->observation && IsCityPresentationMinimized(city)))
    return false;
  SetCityCursor(city, CITY_CURSOR_REWARDS, 0);
  StopAllCitySound(city->game);
  static const u16 kMusicTracks[] = {
    CITY_MUSIC_TRACK_16, CITY_MUSIC_TRACK_05, CITY_MUSIC_TRACK_02, CITY_MUSIC_TRACK_10,
  };
  PlayCityEventMusicById(city->game, kMusicTracks[NextCityMapRandomModulo(city, 4)]);
  return true;
}

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
    complete = StepCityWaterNetwork(city, kMapBatch);
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
    city->cadence_counter.tick_due = 1;
  u32 result = city->cadence_counter.tick_due;
  if (result != 0 && city->settings.timer_mode == CITY_TIMER_EVENT && speed != CITY_SPEED_UNLIMITED) {
    int parity = cadence->parity;
    result = (result & 0xFFFF0000u) | (u16)(parity + 1);
    cadence->parity = result;
    if (parity & 1)
      city->cadence_counter.tick_due = 0;
  }
  return (i16)result;
}

// Invoke the retained blocking caller through an installed comparison observation adapter. Callback/context are
// borrowed; without that adapter returns CITY_DIALOG_UNANSWERED instead of inventing a native modal reply.
int RunPausedCityAction(City *city, int (*action)(City *city, void *context), void *context) {
  const GameObservation *observer = city->game->observation;
  if (observer && observer->run_paused_action) return observer->run_paused_action(city, action, context);
  // Original blocking callers require an explicit diagnostic adapter.
  return CITY_DIALOG_UNANSWERED;
}
