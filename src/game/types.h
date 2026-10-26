#ifndef SC2K_GAME_TYPES_H
#define SC2K_GAME_TYPES_H

#include <stddef.h>

#include "base/types.h"
#include "city/types.h"

// Game-owned lifecycle values, timing and execution inputs.

// Host startup and idle delivery states, separate from City's simulation phases.
typedef enum CityApplicationIdleState {
  CITY_IDLE_PLAY = -1,
  CITY_IDLE_TOOL_PREVIEW = 0,
  CITY_IDLE_STARTUP_MENU = 7,
  CITY_IDLE_LOAD_CITY = 10,
  CITY_IDLE_NEW_CITY = 11,
  CITY_IDLE_TERRAIN = 12,
  CITY_IDLE_LOAD_SCENARIO = 13,
  CITY_IDLE_STARTUP_CHOICE = 14,
  CITY_IDLE_COMMAND_LINE_FILE = 15,
} CityApplicationIdleState;

// Application command-line dispatch selects either a city or scenario path.
typedef enum CityPathOpenMode {
  CITY_PATH_OPEN_NONE = 0, CITY_PATH_OPEN_CITY = 1, CITY_PATH_OPEN_SCENARIO = 2
} CityPathOpenMode;
_Static_assert(sizeof(CityApplicationIdleState) == sizeof(i32), "Game idle state remains a signed word");
_Static_assert(sizeof(CityPathOpenMode) == sizeof(u32), "Game path mode remains a word");

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

// Borrowed host input for a synchronous scripted or diagnostic command. City retains only resolved choices;
// UI owns live gestures. The enclosing input scope keeps samples alive and restores its parent cursor.
enum {
  CITY_DRAG_INPUT_RELEASE = 1,
  CITY_DRAG_INPUT_SHIFT_CANCEL = 2,
  CITY_DRAG_INPUT_MASK = 3,
};
typedef struct {
  CityPoint point;
  u32 flags; // CITY_DRAG_INPUT_*
} CityZoneDragInput;
_Static_assert(sizeof(CityZoneDragInput) == 12, "zone drag input payload size");
_Static_assert(offsetof(CityZoneDragInput, flags) == 8, "zone drag input flags offset");
typedef struct CityPointerInput {
  CityPoint point;
  u32 flags;
} CityPointerInput;
_Static_assert(sizeof(CityPointerInput) == 12, "pointer input payload size");
_Static_assert(offsetof(CityPointerInput, flags) == 8, "pointer input flags offset");
// The cursor does not own its points/samples. Each nested command saves and restores its parent's cursor.
typedef struct {
  const CityPoint *points; // Borrowed for the duration of a pointer capture.
  const CityPointerInput *samples;
  u32 count;
  u32 index;
  u32 release_flags;
} CityPointerCommandState;

#endif
