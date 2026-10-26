#include "city/engine/clock.h"

#include <assert.h>

#include "city/engine/commands.h"
#include "city/presentation.h"
#include "game/state.h"
#include "city/state.h"

// Logical time and host delivery are independent of simulation and UI dispatch. Returns the complete deterministic
// scheduler count without sampling host time or advancing a phase.
u64 SimulationFrameCount(const City *city) { return city->scheduler_frame; }

// Convert the logical frame count to milliseconds and return its low 32 bits. This is a deterministic projection with
// intentional wraparound, not a sample of the host clock.
u32 SimulationFrameTime(const City *city) { return (u32)(SimulationFrameCount(city) * CITY_SCHEDULER_FRAME_MS); }

// Add frames to the deterministic scheduler counter and notify the observer with before/after counts. Kind records the
// advance's provenance; it does not select a different counter or dispatch simulation phases itself.
void AdvanceCityClock(City *city, CityClockChange kind, u32 frames) {
  u64 before = city->scheduler_frame;
  city->scheduler_frame += frames;
  if (city->game != NULL && city->game->clock_advanced != NULL)
    city->game->clock_advanced(city, kind, frames, before, city->scheduler_frame);
}
// Advance the deterministic frame counter by the supplied number of frames. Callers control cadence; this does not
// sample wall-clock time or dispatch arbitrary UI events.
void AdvanceSimulationFrames(City *city, u32 frames) {
  AdvanceCityClock(city, CITY_CLOCK_EXPLICIT_ADVANCE, frames);
}

// Set the logical frame counter from milliseconds, truncating incomplete frames. Notifies the observer with an
// external-time input; callers explicitly control this reset rather than synchronizing to wall time.
void SetSimulationFrameTime(City *city, u32 milliseconds) {
  u64 before = city->scheduler_frame;
  city->scheduler_frame = milliseconds / CITY_SCHEDULER_FRAME_MS;
  if (city->game != NULL && city->game->clock_advanced != NULL)
    city->game->clock_advanced(city, CITY_CLOCK_EXTERNAL_TIME, milliseconds, before, city->scheduler_frame);
}

int UpdateCityCalendar(City *city, int tick) {
  int months = tick / CITY_PHASE_COUNT;
  int month = months % CITY_MONTH_COUNT;
  city->clock.month = month;
  city->clock.quarter = ((month + 1) % CITY_MONTH_COUNT) / 3;
  city->clock.year = tick / CITY_TICKS_PER_YEAR;
  return months;
}

// Each of the simulation's 25 monthly phases is one calendar day.
int CityDayOfMonth(const City *city) {
  return 1 + city->clock.tick % CITY_PHASE_COUNT;
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
