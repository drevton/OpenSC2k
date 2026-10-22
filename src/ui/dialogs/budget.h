#ifndef SC2K_UI_BUDGET_H
#define SC2K_UI_BUDGET_H

#include "city/economy/types.h"
#include "city/types.h"

typedef struct Game Game;
typedef struct Canvas Canvas;
typedef struct Window Window;
typedef struct CityInterfaceState CityInterfaceState;
typedef struct CityCanvasDesktop CityCanvasDesktop;
typedef struct CityBudgetState CityBudgetState;

typedef struct { i32 group[CITY_POLICY_GROUP_COUNT]; } CityBudgetPolicyGroupTotals;

typedef enum CityBudgetRow {
  CITY_BUDGET_ROW_RESIDENTIAL, CITY_BUDGET_ROW_COMMERCIAL, CITY_BUDGET_ROW_INDUSTRIAL,
  CITY_BUDGET_ROW_ORDINANCES, CITY_BUDGET_ROW_BONDS, CITY_BUDGET_ROW_POLICE,
  CITY_BUDGET_ROW_FIRE, CITY_BUDGET_ROW_HEALTH, CITY_BUDGET_ROW_EDUCATION,
  CITY_BUDGET_ROW_TRANSIT, CITY_BUDGET_ROW_COUNT
} CityBudgetRow;

// Retained values shown by the budget window. The credit rating text is borrowed.
typedef struct {
  i64 cash;
  i64 loan;
  const char *credit_rating;
  int next_bond_rate;
  i32 year_to_date_cashflow;
  i32 estimated_cashflow;
  i32 current_year;
  i32 estimate_year;
  i32 rate[CITY_BUDGET_ROW_COUNT];
  i32 to_date[CITY_BUDGET_ROW_COUNT];
  i32 estimate[CITY_BUDGET_ROW_COUNT];
  char city_name[32];
} CityBudgetPanel;

enum { CITY_ADVISOR_BUTTON_SIZE = 22, CITY_ADVISOR_WINDOW_WIDTH = 280,
    CITY_ADVISOR_CONTENT_MARGIN = 10, CITY_ADVISOR_PORTRAIT_GAP = 6 };

const char *CityAdvisorName(CityBudgetCategory category);
CityRect CityAdvisorButtonBounds(const Window *window, CityBudgetCategory category);
int CityAdvisorButtonAt(const Window *window, int x, int y);

// Open helpers return a desktop array index, or -1 on failure. Refresh helpers update retained window data.
i32 Sc2kCanvasDesktopOpenPolicies(CityCanvasDesktop *desktop, CityRect bounds, u32 policy_flags);
i32 Sc2kCanvasDesktopOpenBonds(CityCanvasDesktop *desktop, CityRect bounds, int issue_rate);
void UpdateCityPolicyWindows(City *city, CityCanvasDesktop *desktop, u32 policy_flags);
void Sc2kCanvasDesktopUpdateBudget(CityCanvasDesktop *desktop, const CityBudgetPanel *budget);

// Nonnegative ordinance hits are persistent policy indices.
typedef enum CityCanvasPolicyHit {
  CITY_CANVAS_POLICY_HIT_NONE = -1,
} CityCanvasPolicyHit;

void Sc2kCanvasBudgetFromCity(City *city, CityBudgetPanel *budget);
enum { CITY_BUDGET_WINDOW_WIDTH = 432, CITY_BUDGET_WINDOW_HEIGHT = 276 };
typedef enum CityBudgetControl {
  CITY_BUDGET_CONTROL_CATEGORY, CITY_BUDGET_CONTROL_RATE,
  CITY_BUDGET_CONTROL_DECREASE, CITY_BUDGET_CONTROL_INCREASE,
  CITY_BUDGET_CONTROL_ADVISOR, CITY_BUDGET_CONTROL_TO_DATE, CITY_BUDGET_CONTROL_ESTIMATE,
  CITY_BUDGET_CONTROL_BORROW, CITY_BUDGET_CONTROL_REPAY, CITY_BUDGET_CONTROL_SHOW_BONDS,
  CITY_BUDGET_CONTROL_TIMER, CITY_BUDGET_CONTROL_OPEN_ORDINANCES
} CityBudgetControl;
CityRect CityBudgetControlBounds(CityRect bounds, CityBudgetControl control, int category);
int CityBudgetRateKindForRow(int row);
int CityBudgetRateMaximum(int row);
int Sc2kCanvasBudgetRateAt(const Window *window, int x, int y);
typedef enum CityBudgetQueryControl {
  CITY_BUDGET_QUERY_INPUT, CITY_BUDGET_QUERY_CLEAR, CITY_BUDGET_QUERY_DEFAULT,
  CITY_BUDGET_QUERY_CANCEL, CITY_BUDGET_QUERY_OK
} CityBudgetQueryControl;
CityRect CityBudgetQueryBounds(CityRect bounds, CityBudgetQueryControl control);
void DrawCityBudgetRateEditor(Canvas *canvas, const Window *window);
// Return -1 outside an arrow, otherwise budget row * 2 plus one for increase.
i32 Sc2kCanvasBudgetStepAt(const Window *window, i32 x, i32 y);
i32 Sc2kCanvasBudgetAdviceAt(const Window *window, i32 x, i32 y);
enum {
  CITY_POLICY_WINDOW_WIDTH = 460, CITY_POLICY_WINDOW_HEIGHT = 236,
  CITY_POLICY_PANEL_MARGIN = 4, CITY_POLICY_PANEL_GAP = 4, CITY_POLICY_PANEL_HEIGHT = 68,
  CITY_POLICY_ROWS_TOP = 14, CITY_POLICY_ROW_PITCH = 12, CITY_POLICY_ROW_HEIGHT = 12
};
CityRect CityPolicyPanelBounds(CityRect bounds, int panel);
// Returns a policy index or a CityCanvasPolicyHit sentinel.
i32 Sc2kCanvasPolicyAt(const Window *window, i32 x, i32 y);
bool IsCityTransitUnderfunded(const City *city);
void Sc2kCanvasDrawBudgetPanel(Canvas *canvas, CityRect bounds, const CityBudgetPanel *budget);
void Sc2kCanvasDrawPolicies(Game *game, Canvas *canvas, const Window *window);

// Bond painting, scrolling and rate controls share the window's current geometry.
enum { CITY_BOND_WINDOW_WIDTH = 360, CITY_BOND_WINDOW_HEIGHT = 204, CITY_BOND_WINDOW_MIN_HEIGHT = 132 };
void DrawCityBonds(const City *city, Canvas *canvas, const Window *window);
CityRect CityBondResizeGripBounds(const Window *window);
int CityBondVisibleRows(const Window *window);
int CityBondContentRows(int count);

// Window lifetime and local controls. Rate and policy choices execute through City commands.
// Returned budget state is borrowed; refresh it after callbacks that may close the window.
// Finish the detached manual caller before city replacement or host teardown.
void CloseCityManualBudget(City *city);
void OpenCityBudget(City *city);
const CityBudgetState *ReadCityBudgetWindowState(const City *city);
const CityBudgetState *FindCityBudgetWindowState(const City *city, int id);
void AdvanceCityBudgetHelper(City *city, int window);
// Returns a desktop array index, or -1 on allocation failure; does not begin the retained budget caller.
int OpenCityBudgetWindow(City *city);
void ApplicationBudgetBegin(City *city, u32 window, bool manual);
void ApplicationBudgetEnd(City *city, u32 window);
void ApplicationBudgetAdvisor(City *city, u32 window, CityBudgetCategory category);
void OpenCityAdvisor(City *city, CityBudgetCategory category);
void ApplyCityBudgetWindowRate(City *city, u32 window, int row, int rate);
void OpenCityBudgetRateEditor(City *city, u32 window, int row);
void ApplicationBudgetSync(City *city);
void ReleaseCityBudgetPresentation(CityInterfaceState *ui);
void ApplicationBudgetPoll(City *city);

// Budget presentation. The annual entry retains its preparation and paused-host lifetime; manual opening uses UI.
void ShowAnnualBudgetDialog(City *city);
void ShowBudgetDialogManually(City *city);

// Format a signed amount with grouped thousands into the caller's bounded buffer.
void CanvasBudgetAmount(char *output, size_t size, i64 value);

// Returns borrowed hover help for these controls, or null when no help applies.
const char *CityBudgetTooltipAt(const Window *window, int x, int y);

#endif
