#ifndef SC2K_DOMAIN_SRC_UI_DIALOGS_DIALOGS_H
#define SC2K_DOMAIN_SRC_UI_DIALOGS_DIALOGS_H

#include "city/engine/commands.h"
#include "city/engine/input.h"
#include "city/types.h"

typedef struct Game Game;
typedef struct Window Window;
typedef struct CityInterfaceState CityInterfaceState;

// A close transfers completions from Window to UI. The lifetime module releases each record before invoking
// its callback; the callback owns its context until it runs, after the outer command has unwound.
typedef void CityWindowCallback(City *city, void *context);
typedef struct CityWindowCompletion {
  struct CityWindowCompletion *next;
  CityWindowCallback *callback;
  void *context;
} CityWindowCompletion;
// The caller or window-close callback owns the reply through window dismissal.
typedef struct CityConfirmationState {
  u32 window_id;
  CityCommand command;
  i32 answer;
} CityConfirmationState;

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

void WaitForCityMissionNotice(City *city, u32 window_id, int outcome);
void WaitForCityResourceAlert(City *city, u32 window_id, int bitmap_id, int text_id);
void WaitForCityTextNotice(City *city, u32 window_id, int resource);
// Match both the scope token and notice kind before closing its window.
bool AnswerCityNotice(City *city, int token, int kind);
bool AnswerCityMissionNotice(City *city, int token);
bool AnswerCityResourceAlert(City *city, int token);
bool AnswerCityTextNotice(City *city, int token);
bool CityMissionNoticeWaiting(const CityInterfaceState *ui);

// Window state is released before completion runs, outside any command.
// Attach before any call that can reconcile pauses or run nested UI work. Failure leaves the window unchanged;
// success retains callback delivery and borrows its context until completion. This API never frees the context.
bool AttachCityWindowCallback(Window *window, CityWindowCallback *callback, void *context);
// Open the notice for an existing City continuation. UI retains its reply and pause until completion;
// the callback resumes the live caller once. Unknown tokens do nothing; an open failure cancels that caller.
void OpenCityTickContinuation(City *city, int token);
// Copies text and attaches completion before pause reconciliation. Success transfers callback delivery to UI;
// the window may already have closed on return. Failure retains neither callback nor context.
bool OpenCityNoticeWithCallback(City *city, const char *title, const char *text, u32 flags,
    CityWindowCallback *callback, void *context);
void CompleteCityWindowCallbacks(City *city);

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

i32 ChooseCityCanvasDialog(City *city, void *dialog);

// Leave/abandon choices are local windows; Game applies an accepted shutdown or replacement after callers unwind.
void HandleCityExitCommand(City *city);
void OpenCityAbandonDialog(Game *game);
void AnswerCityLeaveDialog(Game *game, bool accept);
void CompleteCityAbandon(Game *game);

// Local retained callers borrow action/context synchronously. The matching pause wraps the complete call.
int RunPausedCityAction(City *city, int (*action)(City *city, void *context), void *context);
i32 RunPausedCityModalDialog(City *city, void *dialog);

#endif
