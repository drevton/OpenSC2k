#ifndef SC2K_UI_SPANS_H
#define SC2K_UI_SPANS_H

#include "city/engine/commands.h"
#include "city/infrastructure/types.h"
#include "city/types.h"

typedef struct Canvas Canvas;
typedef struct Window Window;
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

// Chooser geometry is in logical canvas pixels. Opening helpers return desktop array indices, or -1 on failure.
i32 Sc2kCanvasDesktopOpenSpanChoices(CityCanvasDesktop *desktop, CityRect bounds,
    const char *title, const char *message, u32 styles, const u32 *prices);
enum CitySpanChoiceLayout { CITY_SPAN_CHOICE_WIDTH = 220, CITY_SPAN_LIST_TOP = 27, CITY_SPAN_ROW_HEIGHT = 24,
       CITY_SPAN_FOOTER_HEIGHT = 14 };
enum CitySpanSort { CITY_SPAN_SORT_COST, CITY_SPAN_SORT_NAME };
// Nonnegative controls are visible row numbers; negative values are header/scroll controls.
CityRect CitySpanControlBounds(const Window *window, int control);
int ReadCitySpanList(const Window *window, u8 order[CITY_SPAN_STYLE_COUNT]);
int CitySpanVisibleRows(const Window *window);
CityRect CitySpanScrollThumb(const Window *window);
// Returns a style index or a CityCanvasSpanHit sentinel.
i32 CitySpanChoiceAt(const Window *window, i32 x, i32 y);
void Sc2kCanvasDrawSpanStyle(Canvas *canvas, CityRect bounds, const char *title, const char *message);
void CanvasDrawSpanChoices(Canvas *canvas, const Window *window);

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
