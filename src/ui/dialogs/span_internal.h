#ifndef SC2K_UI_SPAN_INTERNAL_H
#define SC2K_UI_SPAN_INTERNAL_H

#include "ui/dialogs/spans.h"

// Shared canvas models; normal UI completion owns command execution.
typedef struct NativeSpanDialog {
  struct NativeSpanDialog *parent;
  struct CityBridgeWindowDrawing *drawing;
  u32 window_id;
  int token, choice;
  u8 answered;
  bool scroll_dragging;
  int scroll_drag_offset;
  int distance;
  int styles;
  char message[80];
  CityCommand command; // Nonzero for a native command collected before placement.
} NativeSpanDialog;

// Build a chooser for a caller-owned record and bind it as active, without pause/observation hooks. Returns a
// borrowed desktop index or -1; native callers must attach completion before presenting it.
int CreateCitySpanWindow(City *city, NativeSpanDialog *dialog, int distance, int styles);
// Reconcile pause, refresh charts and notify the opening observer in order. Arguments are captured opening values;
// hooks may close the window or complete/free a native caller. Only a retained caller may access its own record
// afterward; no caller may reuse the old desktop index.
void PresentCitySpanWindow(City *city, int token, int distance, int styles);

#endif
