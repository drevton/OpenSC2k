#ifndef SC2K_UI_NAME_EDITOR_H
#define SC2K_UI_NAME_EDITOR_H

#include "city/engine/command_types.h"
#include "city/maps/labels.h"
#include "city/types.h"

typedef struct Game Game;
typedef struct CityInterfaceState CityInterfaceState;

enum { CITY_NAME_EDITOR_CAPACITY = 16 };

// Retained name callers distinguish sign and facility replies within their editor slot.
typedef enum CityCanvasLabelContinuation {
  CITY_CANVAS_LABEL_CONTINUATION_NONE = 0,
  CITY_CANVAS_LABEL_CONTINUATION_SIGN = 1,
  CITY_CANVAS_LABEL_CONTINUATION_FACILITY = 2,
} CityCanvasLabelContinuation;

// Retained editor state lives in UI's fixed slots.
// Name buffers belong to these records; windows borrow them while open.
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

// Per-call snapshot, using the same two bounded reply arrays as pause reconciliation. No retained queue or allocation.
// Collect fills only the counted entries; the caller must reconcile the pause before Cancel submits the commands.
typedef struct CityEditorCancellations {
  CityCommand stadiums[CITY_NAME_EDITOR_CAPACITY], labels[CITY_NAME_EDITOR_CAPACITY];
  int stadium_count, label_count;
} CityEditorCancellations;

// Clear closed editor slots and collect their cancellation replies without commands or callbacks. Returns whether
// any editor window is still open. Labels still opening or retained by a continuation do not receive cancellation.
bool CollectClosedCityNameEditors(CityInterfaceState *ui, CityEditorCancellations *cancelled);
// Execute the collected stadium replies, then label replies. Borrows the snapshot only for this call.
void CancelClosedCityNameEditors(Game *game, const CityEditorCancellations *cancelled);

// Opening an editor collects local choices. Committed names/team choices execute through City commands.
void PresentCityLabelEditor(City *city, TileIndex tile, bool facility);
// Capture reply text before closing/reconciling the editor can reuse its fixed slot.
void AnswerCityLabelEditor(City *city, NativeLabelDialog *pending, bool accept);
// Capture the submitted choice and update/close only the same window after command callbacks return.
void AnswerCityStadiumEditor(City *city, NativeStadiumDialog *pending, bool accept);
void PresentCityStadiumTeamDialog(City *city, TileIndex tile);

#endif
