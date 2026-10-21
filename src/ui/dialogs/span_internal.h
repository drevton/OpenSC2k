#ifndef SC2K_UI_SPAN_INTERNAL_H
#define SC2K_UI_SPAN_INTERNAL_H

#include "ui/dialogs/dialogs.h"

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

struct CityConnectionDialog {
  CityConnectionDialog *parent;
  u32 window_id;
  int token, answer;
  bool answered;
  char message[128];
};

enum { NOTICE_MISSION, NOTICE_RESOURCE, NOTICE_TEXT };

struct CityMissionNotice {
  CityMissionNotice *parent;
  u32 window_id;
  int token;
  u8 kind;
  bool answered;
};

typedef struct CityNoticeRequest {
  u32 window;
  int detail, text, kind;
} CityNoticeRequest;

int OpenCitySpanWindow(City *city, NativeSpanDialog *dialog, int distance, int styles);

bool AnswerCityNotice(City *city, int token, int kind);

#endif
