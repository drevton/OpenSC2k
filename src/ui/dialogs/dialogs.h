#ifndef SC2K_DOMAIN_SRC_UI_DIALOGS_DIALOGS_H
#define SC2K_DOMAIN_SRC_UI_DIALOGS_DIALOGS_H

#include "city/presentation.h"

#include "ui/input.h"
#include "city/engine/commands.h"
#include "city/engine/input.h"
#include "city/types.h"
#include "ui/windows/state.h"

bool HasCityDialogPause(const CityInterfaceState *ui, int token);

// Budget presentation. The annual entry retains its preparation and paused-host lifetime; manual opening uses UI.
void ShowAnnualBudgetDialog(City *city);
void ShowBudgetDialogManually(City *city);

// Connection Host

typedef struct City City;
typedef struct CityInterfaceState CityInterfaceState;
typedef struct CityConnectionDialog CityConnectionDialog;

typedef struct {
  CityConnectionDialog *active;
  int next_token;
} CityConnectionPresentation;

bool AnswerCityConnectionQuestion(City *city, int token, bool accept);
bool CityConnectionQuestionWaiting(const CityInterfaceState *ui);


// Mission Host

typedef struct City City;
typedef struct CityInterfaceState CityInterfaceState;
typedef struct CityMissionNotice CityMissionNotice;

typedef struct {
  CityMissionNotice *active;
  int next_token;
} CityMissionPresentation;

void WaitForCityMissionNotice(City *city, u32 window_id, int outcome);
void WaitForCityResourceAlert(City *city, u32 window_id, int bitmap_id, int text_id);
void WaitForCityTextNotice(City *city, u32 window_id, int resource);
bool AnswerCityMissionNotice(City *city, int token);
bool AnswerCityResourceAlert(City *city, int token);
bool AnswerCityTextNotice(City *city, int token);
bool CityMissionNoticeWaiting(const CityInterfaceState *ui);


// Name

// Canvas name presentation; command execution owns name-table writes.
i32 ChooseCityOverlayNameDialog(City *city, int slot, CityNameCommandInput *state);
bool AnswerCityOverlayNameDialog(City *city, i32 token, i32 answer, const u8 *text, u32 length);


// Window state is released before completion runs, outside any command.
bool AttachCityWindowCallback(Window *window, CityWindowCallback *callback, void *context);
void OpenCityTickContinuation(City *city, int token);
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


// Span Dialog

// Span Host

// Bridge choices return to the main loop before executing their saved command.
void SetCitySpanPresentation(CityInterfaceState *ui, CityCanvasDesktop *desktop);
bool OpenCitySpanCommand(City *city, CityCommand command, int distance, int styles);
void CancelCitySpanCommands(City *city);
bool NativeSpanDialogWaiting(const CityInterfaceState *ui);

void PresentCityLabelEditor(City *city, TileIndex tile, bool facility);

void PresentCityStadiumTeamDialog(City *city, TileIndex tile);

enum { CITY_NAME_EDITOR_INPUT, CITY_NAME_EDITOR_OK, CITY_NAME_EDITOR_CANCEL };
CityRect CityNameEditorBounds(const Window *window, int control);

#endif
