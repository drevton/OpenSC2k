#ifndef SC2K_GAME_TIMING_H
#define SC2K_GAME_TIMING_H

#include "city/types.h"

// Original application and newspaper timer identifiers.
typedef enum CityTimerId {
  CITY_TIMER_ID_SIMULATION = 1, CITY_TIMER_ID_NEWSPAPER_CLOSE = 0x8094,
} CityTimerId;

typedef struct Game Game;

// Host clocks are presentation inputs, never sources of deterministic City ticks.
typedef enum GameTimePolicy {
  GAME_TIME_REALTIME, // UI deadlines continue during pause.
  GAME_TIME_UNPAUSED, // World presentation freezes during pause, without a resume jump.
} GameTimePolicy;

typedef struct {
  double realtime, unpaused;
  bool initialized, paused;
} GameTiming;

// One delay boundary. Each suspended caller retains its own instance. Durations and polling gaps must stay below
// 2^31 milliseconds so signed modular deadline comparisons remain unambiguous across u32 clock wrap.
typedef struct {
  u32 deadline, saved_idle_gate, saved_input_gate;
  bool active, poll_timers;
} GameDelay;

void BeginGameDelay(Game *game, GameDelay *delay, int milliseconds, bool poll_timers);
// Poll once, then return. False leaves the caller suspended until another frame.
bool StepGameDelay(Game *game, GameDelay *delay);
void CancelGameDelay(Game *game, GameDelay *delay);

void SampleGameTime(Game *game, double now);
double ReadGameTime(const Game *game, GameTimePolicy policy);
// Host seconds pace delivery; each delivery advances integer City time.
void StartGameCadence(Game *game, double now);
bool PollGameCadence(Game *game, double now);
// A zero deadline starts the interval. Return elapsed intervals in one operation.
// The owner retains its deadline; destroying the owner cancels its timer.
u32 PollGameInterval(double now, double interval, double *next);

#endif
