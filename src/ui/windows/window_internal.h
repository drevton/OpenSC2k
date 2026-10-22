#ifndef SC2K_UI_WINDOW_INTERNAL_H
#define SC2K_UI_WINDOW_INTERNAL_H

#include "ui/windows/windows.h"

// Class policy and registered procedures belong to window implementations.
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

// Internal input housekeeping shared by dispatch and window lifetime.
void HandleCityButtonRepeatInput(Game *game, const CanvasSDLEvent *input);
void CancelCityButtonRepeat(Game *game);

// Focus a visible window and preserve the drag index when reordering the stack.
// Explicit navigation requests highlight it; pointer input passes false to clear that highlight.
int FocusCanvasWindow(CityCanvasDesktop *desktop, u32 id, bool highlight);

// Common chrome hit tests and geometry shared by the desktop and its physical-pixel adapter.
bool CityWindowCloseAt(const Window *window, int x, int y);
CityRect CityWindowResizeGrip(const Window *window);
CityRect CanvasPixelMapCover(const CityCanvasDesktop *desktop, u32 id, CityRect logical);
void CanvasSnapCandidate(i32 target, i32 original, i32 *snapped, i32 *radius);
void CanvasSnapToRect(CityRect other, i32 width, i32 height, i32 x, i32 y, i32 *left,
    i32 *top, i32 *horizontal, i32 *vertical);

#endif
