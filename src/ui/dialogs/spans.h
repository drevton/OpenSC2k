#ifndef SC2K_UI_SPANS_H
#define SC2K_UI_SPANS_H

#include "city/engine/command_types.h"
#include "city/infrastructure/types.h"
#include "city/types.h"

typedef struct CityInterfaceState CityInterfaceState;

typedef struct CityCanvasDesktop CityCanvasDesktop;
typedef struct NativeSpanDialog NativeSpanDialog;

// UI borrows the desktop and the currently open operation-local chooser scope.
typedef struct {
  CityCanvasDesktop *desktop;
  NativeSpanDialog *active;
  u32 next_token;
} CitySpanPresentation;

// Nonnegative bridge hits are style indices; negative values name chooser controls.
typedef enum CityCanvasSpanHit {
  CITY_CANVAS_SPAN_HIT_NONE = -1, CITY_CANVAS_SPAN_HIT_CANCEL = -2,
  CITY_CANVAS_SPAN_HIT_SORT = -3, CITY_CANVAS_SPAN_HIT_DIRECTION = -4,
  CITY_CANVAS_SPAN_HIT_UP = -5, CITY_CANVAS_SPAN_HIT_DOWN = -6,
  CITY_CANVAS_SPAN_HIT_TRACK = -7, CITY_CANVAS_SPAN_HIT_RESTORE = -8,
} CityCanvasSpanHit;

// Shared chooser geometry is in logical canvas pixels.
enum CitySpanChoiceLayout { CITY_SPAN_CHOICE_WIDTH = 220, CITY_SPAN_LIST_TOP = 27, CITY_SPAN_ROW_HEIGHT = 24,
       CITY_SPAN_FOOTER_HEIGHT = 14 };

// Native bridge choices execute their saved command from deferred window completion.
void SetCitySpanPresentation(CityInterfaceState *ui, CityCanvasDesktop *desktop);
// Success transfers the caller to deferred window completion before presentation hooks run. The chooser may
// already have closed on return. Failure retains no completion callback. The resolved command is copied.
bool OpenCitySpanCommand(City *city, CityCommand command, int distance, int styles);
void CancelCitySpanCommands(City *city);
bool NativeSpanDialogWaiting(const CityInterfaceState *ui);

// Only the matching active chooser may answer; accepting closes its window before the retained caller resumes.
bool AnswerCitySpanDialog(City *city, int token, int style);

#endif
