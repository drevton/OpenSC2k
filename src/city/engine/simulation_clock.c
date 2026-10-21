#include "city/engine/simulation.h"

#include "game/state.h"
#include "city/state.h"
// Logical time and host delivery are independent of simulation and UI dispatch. Returns the complete deterministic
// scheduler count without sampling host time or advancing a phase.
u64 SimulationFrameCount(City *city) { return city->scheduler_frame; }

// Convert the logical frame count to milliseconds and return its low 32 bits. This is a deterministic projection with
// intentional wraparound, not a sample of the host clock.
u32 SimulationFrameTime(City *city) { return (u32)(SimulationFrameCount(city) * CITY_SCHEDULER_FRAME_MS); }

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

// Advance the City's logical clock by one presentation-delay frame. Returns whether that advance crossed the cadence
// counter boundary; this function advances even when its Boolean result is false.
bool AdvanceGameDelayFrame(Game *game) {
  u64 before = SimulationFrameCount(game->city);
  AdvanceCityClock(game->city, CITY_CLOCK_DELAY, 1);
  return before / CITY_CADENCE_INTERVAL_MS != SimulationFrameCount(game->city) / CITY_CADENCE_INTERVAL_MS;
}

