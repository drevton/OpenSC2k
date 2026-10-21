#ifndef SC2K_DOMAIN_SRC_UI_DIALOGS_DIALOG_STATE_H
#define SC2K_DOMAIN_SRC_UI_DIALOGS_DIALOG_STATE_H

#include "city/maps/map_types.h"
#include "city/engine/commands.h"
#include "city/saveload/file.h"
#include "city/types.h"
#include "ui/windows/state.h"

#include "ui/news/newspaper_state.h"

// Budget State

typedef struct { i32 group[5]; } CityBudgetPolicyGroupTotals;

// File State

typedef struct CityCanvasDesktop CityCanvasDesktop;
typedef struct NativeSaveScope NativeSaveScope;
typedef struct NativeFileDialog NativeFileDialog;
typedef struct NativeReplacementPrompt NativeReplacementPrompt;

typedef struct {
  CityCanvasDesktop *desktop;
  NativeSaveScope *scope;
  NativeFileDialog *dialog;
  NativeReplacementPrompt *prompt;
  int next_token;
  int browser_width, browser_height;
  bool browser_sort_date, browser_descending;
  char *destination;
} CityFilePresentation;

// Host State

typedef struct NativeFileNotice {
  struct NativeFileNotice *next;
  u32 window_id;
  char text[];
} NativeFileNotice;

// The caller or window-close callback owns the reply through window dismissal.
typedef struct CityConfirmationState {
  u32 window_id;
  CityCommand command;
  i32 answer;
} CityConfirmationState;

typedef struct {
  u32 window_id;
  CityCommand command;
  char name[CITY_OVERLAY_NAME_BYTES];
  int answer;
  bool retained;
} NativeStadiumDialog;

typedef struct {
  u32 window_id;
  union {
    CityCommand command;
    struct { TileIndex tile; int token; } facility_opening;
  };
  char name[CITY_OVERLAY_NAME_BYTES];
  i32 continuation;
  i32 answer;
  bool opening;
} NativeLabelDialog;

typedef struct NativeNewspaperDialog {
  double close_at;
  double opening_started;
  int opening_frame;
  bool opening_skipped;
  CityCanvasNewspaperPage page;
  struct RunSprite *picture; // Owned reference; grayscale issues share rows with a private palette.
  CityRect filler[2048];
  CityCanvasNewspaperTextRun text_runs[32];
  char text_run_buffers[32][2048];
  struct { char headline[2048], body[2048], heading[64]; } articles[7];
  int reading, scroll, hovered; // -1 selects the front page; reader scroll is in text rows.
  i64 wheel_remainder;
} NativeNewspaperDialog;

// Map Data State
typedef struct {
  struct CityCanvasDesktop *desktop;
  struct CityMapDataWindowDrawing *drawing;
  u32 window_id;
  u8 group, option;
  bool visible;
  bool refresh_enabled;
  bool isometric;
  CityRect refresh_rect;
} CityMapDataState;

// Resource State

typedef struct CityStartupWindow {
  u32 resource_id;
  u32 window_id;
} CityStartupWindow;

// Span State
typedef struct CityCanvasDesktop CityCanvasDesktop;
typedef struct NativeSpanDialog NativeSpanDialog;

// UI borrows the desktop and the currently open operation-local chooser scope.
typedef struct {
  CityCanvasDesktop *desktop;
  NativeSpanDialog *active;
  u32 next_token;
} CitySpanPresentation;

#endif
