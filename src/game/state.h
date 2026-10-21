#ifndef SC2K_GAME_STATE_H
#define SC2K_GAME_STATE_H

#include <stdio.h>

#include "city/engine/simulation.h"
#include "city/saveload/file.h"
#include "city/fixes.h"
#include "game/timing.h"
#include "city/types.h"


typedef struct City City;
typedef struct CommandRecord CityCommand;
typedef struct CityAudioState CityAudioState;
typedef struct CityInterfaceState CityInterfaceState;
typedef struct Drawing Drawing;
typedef struct CityFileRuntime CityFileRuntime;
typedef struct CityResourceState CityResourceState;
typedef struct CityCursorInputRecord CityCursorInputRecord;

// Command-scoped execution inputs. Nested idle commands restore their parent.
typedef struct {
  bool active_view;
  bool simulation_enabled;
  bool terrain_water_allowed;
} CityIdleInputs;

// Shared services, outside saved city state. Game currently owns one City.
// Observers inspect dirty tiles without consuming them; each viewport keeps
// its own damage. The command pointer is valid only during the callback.
typedef struct Game {
  City *city;
  u64 default_city_fixes;
  bool spread_simulation; // Local execution policy; shared commands finish any partial phase.
  bool debug_city; // Launch option, reapplied after loading or creating a city.
  bool backup_saves, diagnostic_log, crash_diagnostics, save_extended, repair_save_headers;
  struct Diagnostics *diagnostics;
  struct GameSettings *settings;
  u32 path_open_flag;
  u32 idle_blocked;
  u32 idle_entry;
  u32 idle_state;
  u32 idle_input;
  bool city_active;
  TileIndex error_tile; // Presentation only; never saved or part of simulation equality.
  bool exit_requested;
  // Current host file identity; not deterministic City state or an I/O workspace.
  CityPath city_path;
  CityPath city_working_directory;
  CityPath profile_base_directory;
  CityPath command_line_path;
  CityAudioState *audio;
  CityInterfaceState *ui;
  Drawing *drawing;
  // Borrowed from the currently executing file/command scope; NULL between operations.
  CityFileRuntime *file;
  char *file_directory;
  CityResourceState *resources;
  CityIdleInputs idle_inputs;
  double next_cadence_time;
  GameTiming timing;
  bool delay_blocked;
  GameDelay animation_delay;
  int animation_token;
  struct CityPendingCommand *pending_commands;
  struct CityTimedCommand *timed_command;
  u32 animation_options;
  // Host scheduling and teardown, distinct from cancellable exit_requested.
  double host_start_time;
  double close_deadline;
  u32 command_depth;
  struct lua_State *script_state;
  int script_instructions;
  bool script_running;
  FILE *input_script;
  // Borrowed for the active scripted-input run.
  const char *input_script_city_path;
  u8 execution_failed;
  u8 close_requested;
  // The host has already presented this frame before delivering simulation batches.
  void *activation_context;
  int (*poll_activation)(void *context, bool *active);
  int reconstruction_error;
  u32 ranked_events_suppressed;
  // Optional scheduling observation; gameplay never depends on a recorder.
  void (*clock_advanced)(City *city, CityClockChange kind, u32 input, u64 before, u64 after);
  void (*time_sampled)(Game *game, u32 milliseconds);
  void (*cadence_polled)(Game *game, bool due);
  void (*activation_polled)(Game *game, bool present, bool active);
  void (*cursor_observed)(Game *game, CityCursorInputRecord input);
  void (*sound_played)(Game *game, int sound_id, int playback, bool result);
  void (*music_track_played)(Game *game, int track, u32 result);
  const struct GameObservation *observation;
  void *observer;
  void (*command_completed)(void *observer, City *city, const CityCommand *command, i64 result);
} Game;

void UpdateGame(Game *game);
i64 FinishCityIdleSimulation(Game *game, bool terrain_water_allowed);
void CompleteCityIdleSimulation(Game *game, u32 options);

void AdvanceGameCadencePresentation(Game *game, u32 counter, bool animate);
void FinishGameCitySimulationWork(Game *game);

#endif
