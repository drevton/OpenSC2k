#ifndef SC2K_GAME_OBSERVATION_H
#define SC2K_GAME_OBSERVATION_H

#include "city/engine/commands.h"

typedef struct CityFileRuntime CityFileRuntime;
typedef struct CityFilePickerOptions CityFilePickerOptions;
struct CityScenarioSelectionDialog;
struct CityTextMessageDialog;
struct CityNewspaperDialog;
struct CityNoticeRequest;
struct CityStartupWindow;
struct CityNewCitySetupDialog;
typedef struct CityMapViewConfig CityMapViewConfig;
typedef struct NativeSaveIOOutcome NativeSaveIOOutcome;

typedef enum CityPresentationEvent {
  CITY_PRESENTATION_STADIUM_OPEN, CITY_PRESENTATION_STADIUM_ANSWER,
  CITY_PRESENTATION_BUDGET_OPEN, CITY_PRESENTATION_BUDGET_CLOSE,
  CITY_PRESENTATION_SERVICE_NOTICE, CITY_PRESENTATION_MISSION_NOTICE,
  CITY_PRESENTATION_NAME_OPEN, CITY_PRESENTATION_NAME_ANSWER,
  CITY_PRESENTATION_UNLOCK_OPEN, CITY_PRESENTATION_UNLOCK_ANSWER,
  CITY_PRESENTATION_FILE_PICKER_OPEN, CITY_PRESENTATION_FILE_PICKER_ANSWER,
  CITY_PRESENTATION_QUESTION_OPEN, CITY_PRESENTATION_QUESTION_ANSWER,
  CITY_PRESENTATION_SCENARIO_OPEN, CITY_PRESENTATION_SCENARIO_CLOSE,
  CITY_PRESENTATION_SCENARIO_TEXT_OPEN, CITY_PRESENTATION_SCENARIO_TEXT_CLOSE,
  CITY_PRESENTATION_SCENARIO_TEXT_OBSERVATION,
  CITY_PRESENTATION_SPAN_OPEN, CITY_PRESENTATION_SPAN_ANSWER,
  CITY_PRESENTATION_CONNECTION_OPEN, CITY_PRESENTATION_CONNECTION_ANSWER,
  CITY_PRESENTATION_MISSION_OPEN, CITY_PRESENTATION_MISSION_ANSWER,
  CITY_PRESENTATION_RESOURCE_ALERT_OPEN, CITY_PRESENTATION_RESOURCE_ALERT_ANSWER,
  CITY_PRESENTATION_TEXT_NOTICE_OPEN, CITY_PRESENTATION_TEXT_NOTICE_ANSWER,
} CityPresentationEvent;

typedef struct {
  CityCommand packet;
  const CityPointerInput *input;
  u32 count;
  const u8 *text;
  u32 length;
  const CityCreationOptions *creation;
} CityCommandObservation;

// Borrowed callbacks; Game owns execution and all input lifetimes. Observers
// keep their own optional state, and each nested command gets its own scope.
typedef struct GameObservation {
  // Stock fixture IDs are translated only by optional diagnostic input adapters.
  u32 (*stock_command_id)(u32 id);
  bool (*script_action)(Game *game, const char *line);
  i64 (*execute_command)(Game *game, CityCommand packet, const CityPointerInput *input, u32 count,
      const u8 *text, u32 length, const CityCreationOptions *creation, const CityCommandArguments *arguments,
      CityCommandResult *result);
  u32 (*delay_clock)(Game *game, bool poll_timers);
  void (*pump_activation)(Game *game);
  bool (*poll_delay_cadence)(Game *game);
  void (*prepare_command)(Game *game);
  // Runs before the native handler; diagnostic replay prepares its own original-game scope here.
  bool (*begin_command)(Game *game, const CityCommandObservation *input, void **scope);
  void (*command_executed)(void *scope);
  void (*end_command)(Game *game, void *scope, i64 result, int error, u32 before_tick);
  void (*file_operation_finished)(Game *game, const CityFileRuntime *file);
  void (*file_io)(Game *game, const NativeSaveIOOutcome *outcome);
  void (*presentation)(Game *game, CityPresentationEvent event, int token,
      int first, int second, int answer, const char *text);
  void (*startup_choice)(Game *game, u32 kind, int token, int choice);
  void (*span_choice)(Game *game, int distance, int styles, int choice);
  void *(*prepare_newspaper)(Game *game, int picture_id, bool attempted);
  void (*newspaper_opened)(Game *game, void *scope, int edition);
  void (*newspaper_closed)(Game *game, void *scope, int edition, u32 reason);
  // Diagnostic music adapters preserve original multimedia outcomes outside native playback.
  u32 (*play_music_track)(Game *game, int track);
  void (*stop_music)(Game *game);
  int (*complete_music)(Game *game, int notification, int token);
  // Diagnostic presentation overrides retain the audited original caller stacks.
  i64 (*execute_bond_command)(City *city, const CityCommand *command);
  i64 (*execute_save_command)(City *city, u32 id, u32 flags);
  i64 (*execute_open_command)(City *city, u32 flags);
  i64 (*execute_new_city_command)(City *city, u32 flags);
  i64 (*configure_city)(City *city, u32 flags);
  i64 (*execute_replacement_command)(City *city, u32 flags);
  int (*choose_file)(City *city, const CityFilePickerOptions *options);
  int (*file_question)(City *city, const char *text, u32 flags);
  int (*choose_scenario)(City *city, struct CityScenarioSelectionDialog *dialog);
  int (*scenario_text)(struct CityTextMessageDialog *dialog, const char *text);
  int (*select_span)(City *city, int distance, int styles);
  int (*choose_connection)(City *city, int kind, int cost);
  void (*wait_notice)(City *city, const struct CityNoticeRequest *request);
  void (*show_message)(City *city, u32 resource);
  int (*choose_canvas_dialog)(City *city, void *dialog);
  bool (*choose_newspaper)(City *city, struct CityNewspaperDialog *dialog);
  int (*choose_startup_window)(City *city, struct CityStartupWindow *dialog);
  int (*choose_setup_window)(City *city, struct CityNewCitySetupDialog *dialog);
  void (*present_unlock_offer)(City *city);
  int (*choose_startup_menu)(City *city);
  void (*show_manual_budget)(City *city);
  void (*show_annual_budget)(City *city);
  void (*monthly_bookkeeping)(City *city);
  void (*mission_outcome)(City *city, i32 outcome);
  void (*show_ranked_event)(City *city, u32 event_code, u32 event_data);
  void (*military_offer)(City *city);
  i64 (*process_disaster)(City *city, bool terrain_water_allowed);
  void (*primary_economy)(City *city);
  // Captured undefined stock routing input; absent in normal gameplay.
  u8 (*transport_root_cost)(Game *game);
  void (*edit_map_label)(City *city, TileIndex tile);
  void (*edit_facility_label)(City *city, TileIndex tile);
  void (*show_tile_information)(City *city, TileIndex tile);
  bool (*place_road_tunnel)(City *city, TileIndex tile);
  void (*show_stadium_placement)(City *city, TileIndex tile);
  void (*accept_stadium_team)(City *city, TileIndex tile, int team, const char *name);
  bool (*begin_facility_name)(City *city, int id, TileIndex tile);
  bool (*before_dialog_pause)(City *city);
  int (*run_paused_action)(City *city, int (*action)(City *city, void *context), void *context);
  void (*open_manual_budget)(City *city);
  void (*open_manual_newspaper)(City *city, int edition);
  void (*open_save_dialog)(City *city);
  void (*open_file_dialog)(City *city);
  void (*open_new_city_setup)(City *city);
  void (*open_scenario_dialog)(City *city);
  void (*open_bond_confirmation)(City *city, u32 command, int rate);
} GameObservation;

void ObserveCityPresentation(Game *game, CityPresentationEvent event, int token,
    int first, int second, int answer, const char *text);
void ObserveCityFileIO(CityFileRuntime *file, int operation, u32 requested, u32 result, int error,
    const void *bytes, u32 size);

#endif
