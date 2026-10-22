#ifndef SC2K_GAME_TIMING_H
#define SC2K_GAME_TIMING_H

#include "game/types.h"

typedef struct Game Game;

// Logical delay-clock adapter. Advancing one frame returns whether it crosses a cadence boundary, even if no
// cadence is delivered. The two readers notify time observers; only screen-shake reads advance City by one frame.
bool AdvanceGameDelayFrame(Game *game);
u32 ReadCityScreenShakeTime(City *city);
u32 ReadCityAnimationTime(City *city);
// Poll the retained delay cadence and deliver it once when due.
void PumpGameDelayCadence(Game *game);

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
