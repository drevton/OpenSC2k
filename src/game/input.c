#include "ui/tools/tools.h"
#include "ui/input.h"

#include "city/engine/commands.h"
#include "game/state.h"
#include "city/engine/simulation.h"
#include "city/state.h"
#include "game/errors.h"
#include "ui/state.h"

#include "game/observation.h"

// Runtime input state, queue dispatch and optional command-journal recording.

void PumpGameActivation(Game *game) {
  const GameObservation *observer = game->observation;
  if (observer && observer->pump_activation) {
    observer->pump_activation(game);
    return;
  }
  bool active = false;
  int present = game->poll_activation != NULL ? game->poll_activation(game->activation_context, &active) : -1;
  if (present < 0) return;
  if (game->activation_polled != NULL)
    game->activation_polled(game, present != 0, active);
  if (present)
    HandleApplicationActivation(game, active);
}

u32 ReadCityScreenShakeTime(City *city) {
  u32 value = SimulationFrameTime(city);
  AdvanceCityClock(city, CITY_CLOCK_SCREEN_SHAKE, 1);
  if (city->game != NULL && city->game->time_sampled != NULL)
    city->game->time_sampled(city->game, value);
  return value;
}

u32 ReadCityAnimationTime(City *city) {
  u32 value = SimulationFrameTime(city);
  if (city->game != NULL && city->game->time_sampled != NULL)
    city->game->time_sampled(city->game, value);
  return value;
}

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
