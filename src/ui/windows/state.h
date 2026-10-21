#ifndef SC2K_UI_WINDOWS_STATE_H
#define SC2K_UI_WINDOWS_STATE_H

#include "city/economy/types.h"
#include "city/infrastructure/types.h"
#include "city/maps/tile_storage.h"
#include "city/engine/commands.h"
#include "city/maps/map_types.h"
#include "city/maps/water.h"
#include "city/types.h"
#include "ui/menus/menu.h"
#include "ui/signs.h"
#include "ui/updates.h"

// Nonnegative dragging values are window indices.
typedef enum CityCanvasDragTarget {
  CITY_CANVAS_DRAG_NONE = -1,
} CityCanvasDragTarget;

// Nonnegative bridge hits are style indices, not Win32 dialog results.
typedef enum CityCanvasSpanHit {
  CITY_CANVAS_SPAN_HIT_NONE = -1, CITY_CANVAS_SPAN_HIT_CANCEL = -2,
  CITY_CANVAS_SPAN_HIT_SORT = -3, CITY_CANVAS_SPAN_HIT_DIRECTION = -4,
  CITY_CANVAS_SPAN_HIT_UP = -5, CITY_CANVAS_SPAN_HIT_DOWN = -6,
  CITY_CANVAS_SPAN_HIT_TRACK = -7, CITY_CANVAS_SPAN_HIT_RESTORE = -8,
} CityCanvasSpanHit;

// Nonnegative ordinance hits are persistent policy indices.
typedef enum CityCanvasPolicyHit {
  CITY_CANVAS_POLICY_HIT_NONE = -1,
} CityCanvasPolicyHit;

// Native label continuation and captured volume-slider domains.
typedef enum CityCanvasLabelContinuation {
  CITY_CANVAS_LABEL_CONTINUATION_NONE = 0,
  CITY_CANVAS_LABEL_CONTINUATION_SIGN = 1,
  CITY_CANVAS_LABEL_CONTINUATION_FACILITY = 2,
} CityCanvasLabelContinuation;

typedef enum CityCanvasVolumeSlider {
  CITY_CANVAS_VOLUME_SLIDER_NONE = 0,
  CITY_CANVAS_VOLUME_SLIDER_MUSIC = 1, CITY_CANVAS_VOLUME_SLIDER_EFFECTS = 2,
} CityCanvasVolumeSlider;

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
typedef struct NativeNewspaperDialog NativeNewspaperDialog;
typedef struct CityConfirmationState CityConfirmationState;
typedef void CityWindowCallback(City *city, void *context);
typedef struct CityWindowCompletion {
  struct CityWindowCompletion *next;
  CityWindowCallback *callback;
  void *context;
} CityWindowCompletion;
typedef struct CityAdvisorWindowState {
  struct CityAdvisorWindowState *next;
  i32 token;
  bool opening;
} CityAdvisorWindowState;

typedef struct {
  u32 window_id;
  CityTileInformation data;
  u16 sprite;
  CityRiverSprites river;
  bool us_currency, sprite_flipped;
  char name[256];
} CityTileInformationSnapshot;

enum { CITY_TILE_INFORMATION_WIDTH = 280, CITY_TILE_INFORMATION_HEIGHT = 128 };

typedef enum CityBudgetRow {
  CITY_BUDGET_ROW_RESIDENTIAL, CITY_BUDGET_ROW_COMMERCIAL, CITY_BUDGET_ROW_INDUSTRIAL,
  CITY_BUDGET_ROW_ORDINANCES, CITY_BUDGET_ROW_BONDS, CITY_BUDGET_ROW_POLICE,
  CITY_BUDGET_ROW_FIRE, CITY_BUDGET_ROW_HEALTH, CITY_BUDGET_ROW_EDUCATION,
  CITY_BUDGET_ROW_TRANSIT, CITY_BUDGET_ROW_COUNT
} CityBudgetRow;

// UI owns window geometry, stacking, controls and pointer gestures.
typedef struct {
  i64 cash;
  i64 loan;
  const char *credit_rating;
  int next_bond_rate;
  i32 year_to_date_cashflow;
  i32 estimated_cashflow;
  i32 current_year;
  i32 estimate_year;
  i32 rate[CITY_BUDGET_ROW_COUNT];
  i32 to_date[CITY_BUDGET_ROW_COUNT];
  i32 estimate[CITY_BUDGET_ROW_COUNT];
  char city_name[32];
} CityBudgetPanel;

typedef enum WindowKind {
  CITY_CANVAS_BUDGET = 1,
  CITY_CANVAS_NOTICE = 2,
  CITY_CANVAS_NEWSPAPER = 3,
  CITY_CANVAS_ADVISOR = 4,
  CITY_CANVAS_CONFIRM = 5,
  CITY_CANVAS_SPAN_STYLE = 6,
  CITY_CANVAS_POLICIES = 7,
  CITY_CANVAS_BONDS = 8,
  CITY_CANVAS_MAP_DATA = 9,
  CITY_CANVAS_LAND_USE = 10,
  CITY_CANVAS_LABEL_EDIT = 11,
  CITY_CANVAS_FACILITY_INFORMATION = 12,
  CITY_CANVAS_TILE_INFORMATION = 13,
  CITY_CANVAS_STADIUM_TEAM = 14,
  CITY_CANVAS_TOOL_STATUS = 15,
  CITY_CANVAS_JUKEBOX = 16,
  CITY_CANVAS_MUSIC_PROGRAMME = 17,
  CITY_CANVAS_GAME_OPTIONS = 18,
  CITY_CANVAS_HISTORY = 19,
  CITY_CANVAS_DEMOGRAPHICS = 20,
  CITY_CANVAS_NEIGHBORS = 21,
  CITY_CANVAS_INDUSTRY = 22,
  CITY_CANVAS_FILE_SAVE = 23,
  CITY_CANVAS_SAVE_QUESTION = 24,
  CITY_CANVAS_FILE_OPEN = 25,
  CITY_CANVAS_FILE_QUESTION = 26,
  CITY_CANVAS_STARTUP_MENU = 27,
  CITY_CANVAS_SCENARIO_LIST = 28,
  CITY_CANVAS_MAP_VIEWPORT = 29,
  CITY_CANVAS_EXIT = 30,
  CITY_CANVAS_SIGN_EDIT = 31,
  CITY_CANVAS_SIGN_LIST = 32,
  CITY_CANVAS_CONSOLE = 33,
  CITY_CANVAS_CONSTRUCTION = 34,
  CITY_CANVAS_ABANDON = 35,
  CITY_CANVAS_COMMAND_ERROR = 36,
  CITY_CANVAS_BUDGET_RATE = 37,
  CITY_CANVAS_WINDOW_KIND_COUNT
} WindowKind;

typedef struct Canvas Canvas;
typedef struct CanvasSDLEvent CanvasSDLEvent;
typedef struct Window Window;
typedef struct CityDropdownSelection CityDropdownSelection;

typedef enum WindowEventKind {
  WINDOW_PAINT, WINDOW_INPUT, WINDOW_DROPDOWN, WINDOW_ZOOM, WINDOW_DESTROY
} WindowEventKind;

// Paint borrows a window-local canvas for this call only. Input keeps the host
// sample alongside logical desktop coordinates; wheel deltas stay in input.
// Procedures may open/close windows: retain IDs, not Window pointers, across callbacks.
typedef struct WindowEvent {
  WindowEventKind kind;
  int x, y; // Logical desktop position for input/zoom; paint uses window-local coordinates.
  union {
    Canvas *canvas;
    const CanvasSDLEvent *input;
    int zoom_steps;
    const CityDropdownSelection *selection;
  };
} WindowEvent;

typedef bool WindowProcedure(Game *game, const Window *window, const WindowEvent *event);

WindowProcedure DefaultWindowProcedure;
WindowProcedure MessageWindowProcedure;
WindowProcedure AdvisorWindowProcedure;
WindowProcedure ConnectionWindowProcedure;
WindowProcedure NoticeWindowProcedure;
WindowProcedure NameEditorWindowProcedure;
WindowProcedure BudgetWindowProcedure;
WindowProcedure BudgetRateWindowProcedure;
WindowProcedure PoliciesWindowProcedure;
WindowProcedure BondsWindowProcedure;
WindowProcedure MapDataWindowProcedure;
WindowProcedure LandUseWindowProcedure;
WindowProcedure InformationWindowProcedure;
WindowProcedure NewspaperWindowProcedure;
WindowProcedure SpanWindowProcedure;
WindowProcedure FileWindowProcedure;
WindowProcedure FileQuestionWindowProcedure;
WindowProcedure SetupWindowProcedure;
WindowProcedure ScenarioWindowProcedure;
WindowProcedure StartupWindowProcedure;
WindowProcedure ConstructionWindowProcedure;
WindowProcedure ChartsWindowProcedure;
WindowProcedure OptionsWindowProcedure;
WindowProcedure MusicWindowProcedure;
WindowProcedure SignWindowProcedure;
WindowProcedure MapViewportWindowProcedure;
WindowProcedure ConsoleWindowProcedure;
WindowProcedure LeaveWindowProcedure;
WindowProcedure ToolStatusWindowProcedure;

// Window pointers are borrowed until the next callback that can change the stack.
bool SendWindowEvent(Game *game, const Window *window, const WindowEvent *event);
bool DispatchCityWindowInput(Game *game, const CanvasSDLEvent *input);
void BeginCityButtonRepeat(Game *game, const Window *window, const WindowEvent *event);
void HandleCityButtonRepeatInput(Game *game, const CanvasSDLEvent *input);
void CancelCityButtonRepeat(Game *game);
Window *FindCityWindowAtPixels(CityCanvasDesktop *desktop, int x, int y);

enum {
  CITY_WINDOW_SINGLETON = 1, CITY_WINDOW_DONE_BUTTON = 2, CITY_WINDOW_RESIZABLE = 4,
  CITY_WINDOW_SHADEABLE = 8, CITY_WINDOW_NO_KEYBOARD = 16,
};
typedef struct {
  WindowProcedure *procedure;
  u32 update_categories;
  u8 flags;
  u16 minimum_width, minimum_height, default_width, default_height;
} CityWindowTraits;
const CityWindowTraits *ReadCityWindowTraits(WindowKind kind);

typedef enum CityMessageStyle {
  CITY_MESSAGE_INFORMATION = 1, CITY_MESSAGE_WARNING = 2, CITY_MESSAGE_ERROR = 3
} CityMessageStyle;

typedef enum CityMessageButtons {
  CITY_MESSAGE_NO_BUTTONS = 0x10, CITY_MESSAGE_OK = 0x20,
  CITY_MESSAGE_YES_NO = 0x30, CITY_MESSAGE_YES_NO_CANCEL = 0x40
} CityMessageButtons;

enum {
  CITY_VIEWPORT_WIDTH = 300, CITY_VIEWPORT_HEIGHT = 268,
  CITY_VIEWPORT_TOP = 17, CITY_VIEWPORT_BOTTOM = 36
};

typedef enum CityViewportControl {
  CITY_VIEWPORT_CONTROL_NONE,
  CITY_VIEWPORT_ZOOM_IN, CITY_VIEWPORT_ZOOM_OUT,
  CITY_VIEWPORT_ROTATE_CLOCKWISE, CITY_VIEWPORT_ROTATE_COUNTERCLOCKWISE,
  CITY_VIEWPORT_MAIN_CENTER, CITY_VIEWPORT_CENTER,
  CITY_VIEWPORT_SHADE, CITY_VIEWPORT_DEFAULT_SIZE, CITY_VIEWPORT_STICKY
} CityViewportControl;

typedef struct {
  CityRect bounds;
  i16 font_size;
  const char *text;
} CityCanvasNewspaperPanel;
typedef struct {
  CityRect bounds;
  i16 font_size;
  i16 alignment;
  const char *text;
} CityCanvasNewspaperTextRun;

typedef struct {
  CityCanvasNewspaperPanel panel[7];
  CityRect picture_bounds;
  const struct RunSprite *picture;
  u8 picture_visible;
  const CityRect *filler;
  i32 filler_count;
  const CityCanvasNewspaperTextRun *text_runs;
  i32 text_run_count;
  CityRect masthead_bounds;
} CityCanvasNewspaperPage;

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
  u16 land_use_acres[11];
  u16 land_use_percent[11];
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
  const char *stadium_team_labels[5];
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
  CityConfirmationState *confirmation; // Borrowed from a caller or window-close callback context.
  u8 message_style; // CityMessageStyle; explicitly supplied by the message-box caller.
  u8 message_buttons; // CityMessageButtons; explicitly supplied by the message-box caller.
} Window;

enum { CITY_ADVISOR_BUTTON_SIZE = 22, CITY_ADVISOR_WINDOW_WIDTH = 280,
    CITY_ADVISOR_CONTENT_MARGIN = 10, CITY_ADVISOR_PORTRAIT_GAP = 6 };

const char *CityAdvisorName(CityBudgetCategory category);
CityRect CityAdvisorButtonBounds(const Window *window, CityBudgetCategory category);
int CityAdvisorButtonAt(const Window *window, int x, int y);

CityRect CityViewportContentBounds(const Window *window);
CityRect CityViewportControlBounds(const Window *window, CityViewportControl control);
CityViewportControl CityViewportControlAt(const Window *window, int x, int y);
CityRect CitySignControlBounds(const Window *window, CitySignControl control);
CityRect CitySignListRows(const Window *window);
CitySignControl CitySignControlAt(const Window *window, int x, int y);
int CitySignVisibleRows(const Window *window);

// Shared scrollbar geometry in logical UI coordinates; callers own capture and damage.
typedef struct {
  CityRect track, thumb;
  int maximum;
} CityScrollbar;
CityScrollbar ReadCityScrollbar(CityRect track, int count, int visible, int first, int minimum_thumb);
typedef enum {
  CITY_SCROLLBAR_DRAG, CITY_SCROLLBAR_DRAG_NEAREST, CITY_SCROLLBAR_JUMP,
} CityScrollbarMapping;
int CityScrollbarPosition(const CityScrollbar *scrollbar, int pointer, int grab, CityScrollbarMapping mapping);

typedef struct {
  CityRect rects[64];
  i32 count;
} CityDirtyRegions;

typedef struct {
  TileIndex tile;
} CityCanvasPlacementCell;

typedef struct {
  CityCanvasPlacementCell *cells, *scratch; // Committed cells are sorted by TileIndex.
  int count, capacity;
  CityRect bounds;
  int origin_x, origin_y;
  u32 zoom, magnified, underground;
  bool point;
} CityCanvasPlacementSelection;

typedef struct {
  u32 id;
  i32 logical_x, logical_y, x, y;
} CanvasPixelPosition;

typedef struct {
  u16 scale;
  u8 map_scale;
  i32 width, height;
  CanvasPixelPosition positions[16];
  u32 capture;
  i32 grab_x, grab_y;
} CityCanvasWindowPixelState;

typedef struct {
  char text[256], lines[8][96];
  CityRect bounds;
  double show_at;
  u32 window_id;
  int pointer_x, pointer_y, line_count;
  bool pointer_valid, visible, shown_for_target;
  u8 buttons;
} CityTooltip;

typedef enum CityDropdownItemKind {
  CITY_DROPDOWN_PLAIN, CITY_DROPDOWN_RADIO, CITY_DROPDOWN_CHECKBOX, CITY_DROPDOWN_SEPARATOR
} CityDropdownItemKind;
typedef struct CityDropdownItem {
  const char *text; // Borrowed for the lifetime of an open dropdown.
  int value;
  u8 kind;
} CityDropdownItem;
typedef struct {
  u32 face, light, dark, pressed_face, text;
  u32 disabled_face, disabled_light, disabled_dark, disabled_text, disabled_item_text;
  u32 selected_face, selected_text;
} CityDropdownColors;
typedef struct {
  const CityDropdownItem *items;
  const CityDropdownColors *colors; // Borrowed for the lifetime of an open dropdown.
  CityRect anchor; // Relative to the owning window, not the screen.
  u32 window_id;
  int control, count, selected, hovered;
  u32 checked, disabled; // Item-index bitmasks for a persistent dropdown (up to 32 rows).
  const char *summary; // Borrowed live caption; non-NULL enables multiple selection.
} CityDropdown;
typedef struct CityDropdownSelection { u32 window_id; int control, value; } CityDropdownSelection;
typedef enum CityDropdownPointer { CITY_DROPDOWN_MOVE, CITY_DROPDOWN_DOWN, CITY_DROPDOWN_UP } CityDropdownPointer;

typedef struct CityCanvasDesktop {
  CityInterfaceState *interface;
  Game *game; // Borrowed coordinator for window event delivery.
  const struct CityInterfaceFont *interface_font; // Borrowed Assets metrics for dropdown layout.
  CityMenuState menu;
  Window windows[16];
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
