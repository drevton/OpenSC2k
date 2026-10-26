#ifndef SC2K_UI_DIALOGS_H
#define SC2K_UI_DIALOGS_H

#include "city/types.h"

typedef struct Game Game;
typedef struct CityNameCommandInput CityNameCommandInput;
typedef struct CityInterfaceState CityInterfaceState;

// Operation-local retained notice records. The UI owner borrows the active innermost scope.
typedef struct CityConnectionDialog CityConnectionDialog;
typedef struct CityMissionNotice CityMissionNotice;

struct CityConnectionDialog {
  CityConnectionDialog *parent;
  u32 window_id;
  int token, answer;
  bool answered;
  char message[128];
};

typedef enum CityNoticeKind { NOTICE_MISSION, NOTICE_RESOURCE, NOTICE_TEXT } CityNoticeKind;

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

// Retained dialog presentation boundary. Native UI saves/restores toolbar styles; canvas-free diagnostics
// can observe the palette-dialog request separately. The caller owns the matching restore.
void SetCityPaletteDialogState(City *city, bool paused);
void SetCityCanvasPaletteDialogState(City *city, bool paused);

// Reconcile the requested pause after the outermost command returns.
void ApplicationSyncDialogPause(City *city);
// Reconcile open windows, retained questions and local pause requests; this may submit cancellation commands.
void ApplicationUpdateDialogPause(City *city);

// Connection questions

typedef struct {
  CityConnectionDialog *active;
  int next_token;
} CityConnectionPresentation;

bool AnswerCityConnectionQuestion(City *city, int token, bool accept);
bool CityConnectionQuestionWaiting(const CityInterfaceState *ui);

// Mission and resource notices

typedef struct {
  CityMissionNotice *active;
  int next_token;
} CityMissionPresentation;

// For a supported low-word outcome, play its cue and retain the caller through the notice, then finish the mission.
// Requires the attached UI owner; each notice keeps its existing pause and dismissal lifetime.
void FailRadio(City *city, i32 outcome);
void WaitForCityMissionNotice(City *city, u32 window_id, int outcome);
void WaitForCityResourceAlert(City *city, u32 window_id, int bitmap_id, int text_id);
void WaitForCityTextNotice(City *city, u32 window_id, int resource);
// Match both the scope token and notice kind before closing its window.
bool AnswerCityNotice(City *city, int token, int kind);
bool AnswerCityMissionNotice(City *city, int token);
bool AnswerCityResourceAlert(City *city, int token);
bool AnswerCityTextNotice(City *city, int token);
bool CityMissionNoticeWaiting(const CityInterfaceState *ui);

// Open the notice for an existing City continuation. UI retains its reply and pause until completion;
// the callback resumes the live caller once. Unknown tokens do nothing; an open failure cancels that caller.
void OpenCityTickContinuation(City *city, int token);

// Requests

// Native canvas requests carry a kind and only the data their chooser needs.
// The pause wrapper passes them unchanged; no framework object is required.
typedef enum CityCanvasDialogKind {
  CITY_CANVAS_DIALOG_NONE = 0,
  CITY_CANVAS_STADIUM_DIALOG_REQUEST = 0x53435354,
  CITY_CANVAS_NAME_DIALOG_REQUEST = 0x53434E4D,
  CITY_CANVAS_TILE_DIALOG_REQUEST = 0x53435449,
  CITY_CANVAS_BUDGET_DIALOG_REQUEST = 0x53434244,
} CityCanvasDialogKind;
typedef struct {
  CityCanvasDialogKind kind;
  int slot;
  CityNameCommandInput *state;
} CityCanvasNameRequest;

typedef struct {
  CityCanvasDialogKind kind;
} CityCanvasBudgetRequest;

typedef struct {
  CityCanvasDialogKind kind;
  TileIndex tile;
} CityCanvasTileInformationRequest;

typedef struct {
  CityCanvasDialogKind kind;
  TileIndex tile;
} CityCanvasStadiumRequest;

// Leave/abandon choices are local windows; Game applies an accepted shutdown or replacement after callers unwind.
void HandleCityExitCommand(City *city);
void OpenCityAbandonDialog(Game *game);
void CompleteCityAbandon(Game *game);

// Local retained callers borrow action/context synchronously. The matching pause wraps the complete call.
int RunPausedCityAction(City *city, int (*action)(City *city, void *context), void *context);
i32 RunPausedCityModalDialog(City *city, void *dialog);

#endif
