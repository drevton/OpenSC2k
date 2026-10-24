#ifndef SC2K_GAME_TYPES_H
#define SC2K_GAME_TYPES_H

#include "base/types.h"

// Game-owned timing and execution inputs.

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

// Local scheduling permissions for one idle pass. Nested passes restore their parent before event delivery.
typedef struct {
  bool active_view;
  bool simulation_enabled;
  bool terrain_water_allowed;
} GameIdleInputs;

#endif
