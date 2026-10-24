#ifndef SC2K_GAME_TIMING_H
#define SC2K_GAME_TIMING_H

#include "game/types.h"

typedef struct Game Game;
typedef struct City City;

// Logical delay-clock adapter. Advancing one frame returns whether it crosses a cadence boundary, even if no
// cadence is delivered. The two readers notify time observers; only screen-shake reads advance City by one frame.
bool AdvanceGameDelayFrame(Game *game);
u32 ReadCityScreenShakeTime(City *city);
u32 ReadCityAnimationTime(City *city);
// Poll the retained delay cadence and deliver it once when due.
void PumpGameDelayCadence(Game *game);

// Begin with an inactive caller-owned record and retain it until completion or cancellation.
// Finish nested delays in reverse begin order to restore the parent's gates.
void BeginGameDelay(Game *game, GameDelay *delay, int milliseconds, bool poll_timers);
// Poll the optional cadence observer, then sample the clock once. False leaves the delay active;
// completion restores its saved gates. An inactive step returns true without polling.
bool StepGameDelay(Game *game, GameDelay *delay);
// Restore saved gates once and leave the record inactive. Cancelling an inactive record is a no-op.
void CancelGameDelay(Game *game, GameDelay *delay);

// Accrue elapsed host time using the prior pause sample, then refresh pause state from current City/UI.
void SampleGameTime(Game *game, double now);
double ReadGameTime(const Game *game, GameTimePolicy policy);
// Host seconds pace delivery; each delivery advances integer City time.
void StartGameCadence(Game *game, double now);
// A due poll advances logical cadence time once; missed host intervals are not replayed.
bool PollGameCadence(Game *game, double now);
// A zero deadline starts the interval. Return elapsed intervals in one operation.
// Host times must be finite and interval positive. A backward jump before the interval start resets its deadline.
// The owner retains its deadline; destroying the owner cancels its timer. The return saturates at UINT32_MAX while
// the deadline advances by the full elapsed count.
u32 PollGameInterval(double now, double interval, double *next);

#endif
