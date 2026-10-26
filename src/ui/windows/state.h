#ifndef SC2K_UI_WINDOWS_STATE_H
#define SC2K_UI_WINDOWS_STATE_H

#include "city/economy/types.h"
#include "city/maps/information.h"
#include "city/engine/command_types.h"
#include "city/infrastructure/types.h"
#include "city/maps/map_types.h"
#include "city/maps/tile_storage.h"
#include "city/types.h"
#include "ui/windows/types.h"
#include "ui/dialogs/budget.h"
#include "ui/dialogs/messages.h"
#include "ui/tools/placement.h"
#include "ui/menus/menu.h"
#include "ui/news/newspaper_state.h"
#include "ui/signs.h"
#include "ui/windows/controls.h"
#include "ui/windows/tooltips.h"

// Nonnegative dragging values are window indices.
typedef enum CityCanvasDragTarget {
  CITY_CANVAS_DRAG_NONE = -1,
} CityCanvasDragTarget;

typedef struct CityButtonFeedback {
  u32 window_id;
  CityRect bounds;
  double raise_at; // Host real-time seconds; independent of simulation and mouse release.
  double repeat_at; // Host real-time seconds; only opted-in numeric buttons repeat.
  bool active, beeped; // One beep per click or key press, including release callbacks.
  bool repeating, shift;
} CityButtonFeedback;

typedef struct CityInterfaceState CityInterfaceState;
typedef struct CityFileBrowser CityFileBrowser;
typedef struct CityTileInformationSnapshot CityTileInformationSnapshot;

typedef struct CityAdvisorWindowState {
  struct CityAdvisorWindowState *next;
  i32 token;
  bool opening;
} CityAdvisorWindowState;

typedef struct Window {
  WindowKind kind;
  u32 update_categories;
  WindowProcedure *procedure; // Borrowed class procedure; retained prompts may override the default.
  i32 visible;
  CityRect bounds;
  CityBudgetPanel budget;
  u32 budget_rate_parent;
  u8 budget_rate_row;
  char budget_rate_input[4];
  const char *title;
  const char *message;
  CityWindowCompletion *completion; // Owned until close, then queued by UI.
  char *owned_message; // Optional dynamic text; released when this window closes.
  bool command_error; // Red summary/detail presentation; notices may retain their original caller.
  const CityFileBrowser *file_browser;
  const struct RunSprite *portrait;
  struct CitySpriteDrawing *picture_drawing; // Drawing-owned prepared sprite, released on close.
  u32 policy_flags;
  int bond_issue_rate;
  u8 bond_scroll; // First content row, including the summary, headings and yearly total.
  bool bond_scroll_dragging;
  int bond_scroll_grab;
  i32 policy_cost[20];
  i32 policy_group_cost[5];
  i32 policy_ytd_total;
  i32 policy_estimated_total;
  i32 data_map_selection;
  const u32 *map_preview;
  u32 land_use_acres[CITY_LAND_USE_COUNT];
  u16 land_use_percent[CITY_LAND_USE_COUNT];
  CityCanvasNewspaperPage newspaper_page;
  NativeNewspaperDialog *newspaper; // Owned page storage; released with the window.
  u32 span_styles;
  u32 span_prices[CITY_SPAN_STYLE_COUNT];
  u8 span_sort, span_scroll, span_selected;
  bool span_descending, span_rail;
  u32 window_id;
  const CityTileInformationSnapshot *information;
  City *city;
  u32 stadium_team_choices;
  i32 stadium_team_selection;
  int text_caret, text_select_all, text_scroll;
  const char *stadium_team_labels[CITY_TEAM_NAME_COUNT];
  const u32 *viewport_pixels;
  int viewport_width, viewport_height, viewport_stride;
  int restore_height;
  u8 viewport_zoom, viewport_pressed;
  bool shaded, sticky;
  u8 toolbar_selection; // Previewed building; committed only by the chooser confirmation.
  u8 toolbar_scroll; // First visible row in a construction list.
  u8 toolbar_category; // One based CityTopbarButton for construction windows.
  bool toolbar_group; // Construction category toolbar, rather than a building chooser.
  bool highlighted, highlight_pending;
  u32 highlighted_at;
  const struct CityCanvasStartupWindow *startup_layout;
  double budget_helper_deadline; // UI real-time seconds; continues while City is paused.
  u32 budget_helper_revision;
  CityAdvisorWindowState *advisor_question; // Owned until closure; then transferred to UI cancellation.
  u8 advisor_category; // Category + 1; zero for generic portrait notices.
  CityCommand confirmed_command; // UI owns this command until the user accepts or closes the window.
  CityConfirmationState *confirmation; // Optional borrowed reply sink; the command lives above.
  u8 message_style; // CityMessageStyle; explicitly supplied by the message-box caller.
  u8 message_buttons; // CityMessageButtons; explicitly supplied by the message-box caller.
} Window;

typedef struct CityDirtyRegions {
  CityRect rects[64];
  i32 count;
} CityDirtyRegions;

typedef struct {
  u32 id;
  i32 logical_x, logical_y, x, y;
} CanvasPixelPosition;

typedef struct {
  u16 scale;
  u8 map_scale;
  i32 width, height;
  CanvasPixelPosition positions[CITY_CANVAS_WINDOW_CAPACITY];
  u32 capture;
  i32 grab_x, grab_y;
} CityCanvasWindowPixelState;

typedef struct CityCanvasDesktop {
  CityInterfaceState *interface;
  Game *game; // Borrowed coordinator for window event delivery.
  const struct CityInterfaceFont *interface_font; // Borrowed Assets metrics for dropdown layout.
  CityMenuState menu;
  Window windows[CITY_CANVAS_WINDOW_CAPACITY];
  u32 next_window_id;
  u32 information_updates; // Coalesced semantic changes awaiting UI preparation.
  u32 input_capture_window; // Pointer release returns to the window that received the press.
  u32 hover_window;
  u8 input_capture_button;
  i32 count;
  i32 dragging;
  i32 drag_x;
  i32 drag_y;
  i32 selected_tool;
  i32 canvas_width;
  i32 canvas_height;
  CityDirtyRegions dirty;
  CityDirtyRegions window_damage;
  CityCanvasPlacementSelection placement;
  int map_zoom;
  CityCanvasWindowPixelState pixel_state;
  u16 ui_scale_numerator, ui_scale_denominator;
  u8 snap_radius;
  bool resizing;
  CityTooltip tooltip;
  CityDropdown dropdown;
} CityCanvasDesktop;

#endif
