#include "game/timing.h"

// Host clock sampling and presentation deadlines, separate from City time.

#include <math.h>

#include "city/engine/city_types.h"
#include "city/engine/clock.h"
#include "city/state.h"
#include "game/observation.h"
#include "game/state.h"
#include "ui/state.h"

static u32 ReadGameDelayClock(Game *game, bool poll_timers) {
  const GameObservation *observer = game->observation;
  if (observer && observer->delay_clock) return observer->delay_clock(game, poll_timers);
  return (u32)(u64)(ReadGameTime(game, GAME_TIME_REALTIME) * 1000);
}

// Advance the City's logical clock by one presentation-delay frame. Returns whether that advance crossed the cadence
// counter boundary; this function advances even when its Boolean result is false.
bool AdvanceGameDelayFrame(Game *game) {
  u64 before = SimulationFrameCount(game->city);
  AdvanceCityClock(game->city, CITY_CLOCK_DELAY, 1);
  return before / CITY_CADENCE_INTERVAL_MS != SimulationFrameCount(game->city) / CITY_CADENCE_INTERVAL_MS;
}

// Observe logical time before advancing a screen-shake frame, then publish that sample to the observer.
u32 ReadCityScreenShakeTime(City *city) {
  u32 value = SimulationFrameTime(city);
  AdvanceCityClock(city, CITY_CLOCK_SCREEN_SHAKE, 1);
  Game *game = city->game;
  if (game != NULL && game->time_sampled != NULL)
    game->time_sampled(game, value);
  return value;
}

// Observe animation time without advancing City or sampling the host clock.
u32 ReadCityAnimationTime(City *city) {
  u32 value = SimulationFrameTime(city);
  Game *game = city->game;
  if (game != NULL && game->time_sampled != NULL)
    game->time_sampled(game, value);
  return value;
}

// Deliver at most one due cadence from the retained delay poll; ordinary Game delays use the outer cadence loop.
void PumpGameDelayCadence(Game *game) {
  const GameObservation *observer = game->observation;
  bool due;
  if (observer && observer->poll_delay_cadence) due = observer->poll_delay_cadence(game);
  else {
    due = AdvanceGameDelayFrame(game);
    if (game->cadence_polled != NULL) game->cadence_polled(game, due);
  }
  if (due)
    UpdateCityCadence(game->city);
}

void BeginGameDelay(Game *game, GameDelay *delay, int milliseconds, bool poll_timers) {
  delay->saved_idle_gate = game->delay_blocked;
  CityInterfaceState *ui = game->ui;
  delay->saved_input_gate = ui->input_loop_active;
  game->delay_blocked = true;
  if (poll_timers) ui->input_loop_active = 1;
  delay->active = true;
  delay->poll_timers = poll_timers;
  delay->deadline = ReadGameDelayClock(game, poll_timers) + (u32)milliseconds;
}

void CancelGameDelay(Game *game, GameDelay *delay) {
  if (!delay->active) return;
  game->delay_blocked = delay->saved_idle_gate != 0;
  game->ui->input_loop_active = delay->saved_input_gate;
  delay->active = false;
}

bool StepGameDelay(Game *game, GameDelay *delay) {
  if (!delay->active) return true;
  const GameObservation *observer = game->observation;
  // Normal cadence is already delivered by the outer game loop. The adapter
  // supplies only the independently recorded timer message at this boundary.
  if (delay->poll_timers && observer && observer->poll_delay_cadence && observer->poll_delay_cadence(game))
    UpdateCityCadence(game->city);
  // ReadGameDelayClock selects the live observer after the cadence callback.
  // Unsigned subtraction followed by a signed comparison also handles a wrapped deadline.
  if ((i32)(ReadGameDelayClock(game, delay->poll_timers) - delay->deadline) < 0) return false;
  CancelGameDelay(game, delay);
  return true;
}

void SampleGameTime(Game *game, double now) {
  GameTiming *time = &game->timing;
  if (!time->initialized) {
    time->unpaused = now;
    time->initialized = true;
  } else if (!time->paused && now > time->realtime) {
    time->unpaused += now - time->realtime;
  }
  time->realtime = now;
  time->paused = game->city->settings.speed == CITY_SPEED_PAUSED || game->ui->dialog_pause_depth;
}

double ReadGameTime(const Game *game, GameTimePolicy policy) {
  return policy == GAME_TIME_UNPAUSED ? game->timing.unpaused : game->timing.realtime;
}

u32 PollGameInterval(double now, double interval, double *next) {
  if (!*next || now < *next - interval) {
    *next = now + interval;
    return 0;
  }
  if (now < *next) return 0;
  double count = floor((now - *next) / interval) + 1;
  *next += count * interval;
  if (*next <= now) *next = now + interval;
  return count >= UINT32_MAX ? UINT32_MAX : (u32)count;
}

void StartGameCadence(Game *game, double now) {
  SampleGameTime(game, now);
  game->next_cadence_time = now + CITY_CADENCE_INTERVAL_MS / 1000.0;
}

bool PollGameCadence(Game *game, double now) {
  if (now < game->next_cadence_time) return false;
  // Delayed presentation delivers one cadence, without catching up missed wall time.
  StartGameCadence(game, now);
  AdvanceCityClock(game->city, CITY_CLOCK_CADENCE, CITY_CADENCE_INTERVAL_MS / CITY_SCHEDULER_FRAME_MS);
  return true;
}
