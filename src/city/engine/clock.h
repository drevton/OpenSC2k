#ifndef SC2K_CITY_ENGINE_CLOCK_H
#define SC2K_CITY_ENGINE_CLOCK_H

#include "city/engine/city_types.h"
#include "city/types.h"

// Scheduler

// One logical scheduling frame represents one millisecond of original game
// time. This counter advances only when the runner steps it, never on a clock
// read. Simulation phases and presentation frames have separate counters.
enum { CITY_SCHEDULER_FRAME_MS = 1, CITY_CADENCE_INTERVAL_MS = 200 };

// Read the deterministic counter without sampling host time or changing City. Milliseconds wrap at 32 bits.
u64 SimulationFrameCount(const City *city);
u32 SimulationFrameTime(const City *city);
// Advance the existing counter and notify Game's clock observer. The caller controls simulation phase dispatch.
void AdvanceSimulationFrames(City *city, u32 frames);
void AdvanceCityClock(City *city, CityClockChange kind, u32 frames);
// Set absolute logical milliseconds and report an external-time change; incomplete logical frames are truncated.
void SetSimulationFrameTime(City *city, u32 milliseconds);

// Cadence

// City owns cadence counters independently of host clock reads or presentation frames.
typedef struct {
  u32 counter;
  u16 parity;
  u32 tick_due;
} TimerCadenceCounter;

// Advance one cadence callback, update presentation and mark simulation work due for the agreed speed.
// Returns the retained tick/parity projection. Requires attached Game/Drawing/Audio owners; no host time is sampled
// and no simulation phase is executed.
i16 UpdateCityCadence(City *city);

// Calendar

// Project the given signed simulation tick onto the stored month, quarter and year.
// Return elapsed months before the twelve-month wrap. Only the derived calendar fields are written.
int UpdateCityCalendar(City *city, int tick);

// One calendar day per simulation phase, with 25 days in every month.
int CityDayOfMonth(const City *city);

#endif
