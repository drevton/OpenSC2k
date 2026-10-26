#ifndef SC2K_INTERFACE_STATE_H
#define SC2K_INTERFACE_STATE_H

#include "city/economy/state.h"
#include "city/engine/input.h"
#include "game/types.h"
#include "city/presentation.h"
#include "city/terrain/types.h"
#include "city/tools/types.h"
#include "city/types.h"
#include "ui/charts/charts.h"
#include "ui/dialogs/budget.h"
#include "ui/dialogs/dialogs.h"
#include "ui/dialogs/file_dialog.h"
#include "ui/dialogs/game_options.h"
#include "ui/dialogs/information.h"
#include "ui/dialogs/map_data.h"
#include "ui/dialogs/name_editor.h"
#include "ui/dialogs/spans.h"
#include "ui/input.h"
#include "ui/music/jukebox.h"
#include "ui/signs.h"
#include "ui/startup/startup.h"
#include "ui/text/status.h"
#include "ui/tools/types.h"
#include "ui/windows/state.h"

// Tool choices and interaction state belong to the interface, not a saved city.
// The signed tool readers and unsigned reset aliases share the same storage.
enum { CITY_TOOL_LABEL_CAPACITY = 32, CITY_TOOL_BUTTON_CAPACITY = 64, CITY_DIALOG_PAUSE_TOKEN_CAPACITY = 32 };
enum { CITY_CONSOLE_HISTORY_CAPACITY = 32 };

// Resource control IDs fit in a word; six native toolbar flags fit in a byte.
typedef struct { u16 command; u8 style; } CityToolbarButton;
_Static_assert(sizeof(CityToolbarButton) == 4, "compact toolbar button");

typedef struct {
  u32 window_id;
  struct CityTreePickerDrawing *drawing;
} CityTreePickerWindow;

typedef struct CommandRecord CityCommand;

typedef struct CityInterfaceState {
  // Live SDL canvas; the host loop attaches it after startup and SDL detaches it before destruction.
  struct CanvasSDL *display;
  // The same window while command entry may poll minimized state; detached before shutdown callbacks.
  struct CanvasSDL *command_display;
  // Borrowed while the command runs; nested commands restore the parent.
  const CityCommand *active_command;
  struct CityCommandFeedback *command_feedback;
  struct CityResourceNotice *resource_notices, *resource_notice_tail;
  TileIndex error_tile; // Local command blocker; its window controls visibility.
  u32 error_tile_window;
  bool command_errors_enabled;
  u32 command_error_depth, command_error_serial;
  double command_error_close_at; // Host presentation time; retained notices never expire.
  CityCanvasDesktop desktop;
  u32 annual_budget_window;
  u32 scenario_instructions_window;
  char scenario_instructions[769];
  bool file_dialog_redraw_pending;
  int next_label_transaction;
  // Nested newspaper callers save and restore these borrowed dialog pointers.
  CityNewspaperDialog *initializing_newspaper;
  CityNewspaperDialog *newspaper_modal_owner;
  CityNewspaperDialog *waiting_newspaper;
  u32 waiting_newspaper_window;
  bool newspaper_closed_during_initialization;
  bool waiting_newspaper_timer_active;
  bool profile_newspaper;
  u32 newspaper_lifetimes;
  u32 newspaper_timer_deliveries;
  double newspaper_timer_seconds;
  // Negative until the new map is presented, then a real-time opening deadline.
  double startup_newspaper_open_at;
  int startup_newspaper_edition;
  int diagnostic_nested_newspaper;
  NativeStadiumDialog stadium_dialogs[CITY_NAME_EDITOR_CAPACITY];
  NativeLabelDialog label_dialogs[CITY_NAME_EDITOR_CAPACITY];
  CityTileInformationSnapshot information_window;
  // Borrowed display state; pointer gestures and momentary feedback are UI-owned.
  i32 music_collapsed, music_pinned;
  const Jukebox *jukebox_display;
  CityMusicPress music_press;
  CityButtonFeedback button_feedback;
  CityMusicTitle music_title;
  CitySignState signs;
  CitySettingsBrowser settings;
  bool floating_status, advanced_query, middle_center;
  bool terrain_point_edit, terrain_no_seawater;
  bool auto_goto_disabled, newspaper_delivery_disabled, extra_newspaper_disabled;
  CityBudgetState budget_window;
  int newspaper_month;
  u8 scrollwheel_mode;
  bool developer_enabled, lua_enabled;
  struct {
    char input[512], output[8192], draft[512];
    char *history[CITY_CONSOLE_HISTORY_CAPACITY];
    int caret, scroll, select_all;
    int output_scroll, output_wheel, output_scroll_grab;
    u8 history_count, history_cursor;
    bool output_dragging;
    u8 binding_count;
    CityKeyBinding bindings[CITY_KEY_BINDING_CAPACITY];
  } console;
  u32 viewport_pressed_window, viewport_release_time, map_view_focus_window;
  bool viewport_press_released;
  u32 options_slider_window, jukebox_slider_window, jukebox_pressed_window;
  u16 options_slider;
  u8 jukebox_slider;
  CityChartState charts;
  CityToolbarButton city_toolbar[CITY_TOOL_BUTTON_CAPACITY];
  CityToolbarButton terrain_toolbar[CITY_TOOL_BUTTON_CAPACITY];
  u8 saved_toolbar_styles[CITY_TOOLBAR_BUTTON_COUNT];
  char tool_labels[CITY_TOOL_GROUP_BUILD_COUNT][CITY_TOOL_LABEL_CAPACITY];
  int tool_indices[CITY_TOOL_GROUP_BUILD_COUNT];
  // Interface policy and restoration choices do not change saved city state.
  i32 periodic_year_interval;
  bool replacement_prompt_suppressed;
  bool restore_frame_tools;
  bool exit_cancel_enabled;
  bool save_prompt_active;
  i8 startup_choice;
  u32 menu_removal_blocked;
  u32 tools_menu_remove_pending;
  u32 extra_menu_enabled;
  struct { u16 value[18]; } selection;
  i16 tool_group;
  i16 terrain_tool;
  u32 cursor_handle[30];
  int selected_cursor;
  u32 installed_cursor_handle;
  CityPoint map_cursor_position;
  bool map_cursor_position_set;
  u32 wait_cursor_active;
  i16 cursor_id;
  u32 input_loop_active;
  u8 key_modifiers;
  u8 border_connection_answer;
  i16 span_choice;
  CitySpanPresentation span;
  CityConnectionPresentation connection;
  CityMissionPresentation mission_notice;
  u32 pointer_mode;
  i32 policy_dialog_value;
  i32 budget_scroll_direction;
  u32 budget_scroll_latch;
  u16 reset_word;
  u32 outcome_dialog_active;
  i16 dialog_pause_saved_speed; // Full City speed word, restored on final dialog close.
  i32 dialog_pause_tokens[CITY_DIALOG_PAUSE_TOKEN_CAPACITY];
  u32 dialog_pause_depth;
  u32 dialog_input_blocked;
  i16 resume_speed; // Selected running speed, retained while paused; never CITY_SPEED_PAUSED.
  u8 dialog_pause_requested, dialog_pause_active, dialog_pause_syncing, dialog_pause_sync_enabled;
  bool abandon_requested;
  CityBudgetPolicyGroupTotals budget_policy_totals;
  u32 budget_question_sequence;
  // Dismissed advisors whose command cancellation is still pending.
  CityAdvisorWindowState *closed_advisor_questions;
  i32 file_preview_open_result;
  CityPointerInput pointer_stroke[2]; // Start and latest endpoint; never a mouse history.
  u8 pointer_stroke_count;
  TileIndex pointer_terrain_tile;
  u8 pointer_terrain_height, pointer_terrain_pixels, pointer_terrain_last_height;
  bool pointer_terrain_stretch, pointer_terrain_level;
  CityPoint pointer_level_pointer;
  CityRect pointer_level_bounds;
  u8 placement_rotation;
  bool placement_rotation_set;
  i32 pointer_stroke_tools, pointer_stroke_flags;
  CityPoint placement_hover;
  u8 placement_hover_shift, placement_hover_ctrl;
  u8 placement_hover_valid, placement_dirty, pointer_query_capture;
  i64 scroll_remainder_x, scroll_remainder_y;
  struct CityExtraMapView *panning_view; // NULL selects the main map while pan_button is held.
  CityPoint pan_pointer;
  u8 pan_button;
  int wheel_zoom_remainder;
  int pinch_remainder;
  CityStatusState status;
  bool tools_visible;
  bool map_controls_visible;
  bool main_window_iconic;
  bool main_window_available;
  CityMapDataState map_data;
  CityTreePickerWindow tree_picker;
  CityFilePresentation file_dialog;
  struct CityCanvasDesktop *startup_desktop;
  CityStartupResourceWindow startup_windows[CITY_STARTUP_WINDOW_LIMIT];
  struct CityStartupMenuState *startup_menu;
  struct CitySetupWindowState *setup_window;
  bool terrain_setup_enabled, terrain_setup_active;
  CityTerrainSettings terrain_settings;
  u32 city_name_random;
  CityTerrainMap *terrain_preview;
  char terrain_mayor[33];
  CityWindowCompletion *closed_windows; // Owned deferred callbacks, drained outside commands by the event module.
  struct ScenarioListModel *scenario_window;
  int scenario_token;
  int startup_token;
  bool startup_waiting;
} CityInterfaceState;

// Fresh interaction state only; preserve host configuration, windows and resources.
void InitializeCityInteractionState(CityInterfaceState *ui);
// Release interaction collections and console history after Drawing teardown; the caller owns UI storage.
void ReleaseCityInteractionState(CityInterfaceState *ui);

#endif
