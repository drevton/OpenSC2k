#include "city/economy/budget.h"
#include "ui/windows/window_internal.h"
#include "ui/dialogs/budget.h"

#include <stdio.h>
#include <string.h>

#include "base/text.h"
#include "city/economy/economy.h"
#include "city/economy/tables.h"
#include "city/economy/types.h"
#include "city/engine/city_types.h"
#include "city/fixes.h"
#include "city/presentation.h"
#include "city/state.h"
#include "drawing/fonts/fonts.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/raster/canvas_raster_internal.h"
#include "game/state.h"
#include "ui/dialogs/messages.h"
#include "ui/sdl.h"
#include "ui/state.h"
#include "ui/text/text.h"
#include "ui/windows/controls.h"
#include "ui/windows/state.h"
#include "ui/windows/windows.h"

// Budget values, shared control geometry, painting, ordinances and the bond window.

enum { CITY_BUDGET_FOOTER_TOP = 229, CITY_BUDGET_BUTTON_HEIGHT = 12,
    CITY_BUDGET_RATE_ARROW_WIDTH = 13, CITY_BUDGET_RATE_VALUE_WIDTH = 40 };

static void DrawBudgetRateArrow(Canvas *canvas, CityRect bounds, bool increase) {
  int pressed = CityCanvasButtonPressed(canvas, bounds);
  int center = (bounds.left + bounds.right) / 2 + pressed;
  int top = (bounds.top + bounds.bottom - 4) / 2 + pressed;
  for (int row = 0; row < 4; ++row) {
    int half = increase ? row : 3 - row;
    CanvasFill(canvas, center - half, top + row, center + half + 1, top + row + 1, canvas_text);
  }
}

static void DrawBudgetRateControl(Canvas *canvas, CityRect bounds, int rate) {
  const u32 face = 0xFFFFB52A, light = 0xFFFFDE80, dark = 0xFFD68A08;
  CityRect down = {bounds.left, bounds.top, bounds.left + CITY_BUDGET_RATE_ARROW_WIDTH, bounds.bottom};
  CityRect up = {bounds.right - CITY_BUDGET_RATE_ARROW_WIDTH, bounds.top, bounds.right, bounds.bottom};
  CityRect value = {down.right, bounds.top, up.left, bounds.bottom};
  Canvas framed = *canvas;
  framed.unscaled_bevels = false;
  DrawCanvasButtonFrame(&framed, down, face, light, dark, true);
  DrawCanvasButtonFrame(&framed, value, face, light, dark, true);
  DrawCanvasButtonFrame(&framed, up, face, light, dark, true);
  DrawBudgetRateArrow(canvas, down, false);
  DrawBudgetRateArrow(canvas, up, true);
  char text[16];
  snprintf(text, sizeof(text), "%d%%", rate);
  DrawCityInterfaceTextCentered(canvas->interface_font, canvas, value, text, canvas_text, false, 10);
}

void Sc2kCanvasBudgetFromCity(City *city, CityBudgetPanel *budget) {
  static const u8 kRows[CITY_ECONOMY_SECTOR_COUNT] = {
    CITY_BUDGET_ROW_RESIDENTIAL, CITY_BUDGET_ROW_COMMERCIAL, CITY_BUDGET_ROW_INDUSTRIAL,
    CITY_BUDGET_ROW_ORDINANCES, CITY_BUDGET_ROW_BONDS, CITY_BUDGET_ROW_POLICE,
    CITY_BUDGET_ROW_FIRE, CITY_BUDGET_ROW_HEALTH, CITY_BUDGET_ROW_EDUCATION,
    CITY_BUDGET_ROW_EDUCATION, CITY_BUDGET_ROW_TRANSIT, CITY_BUDGET_ROW_TRANSIT,
    CITY_BUDGET_ROW_TRANSIT, CITY_BUDGET_ROW_TRANSIT, CITY_BUDGET_ROW_TRANSIT, CITY_BUDGET_ROW_TRANSIT
  };
  CityEconomyMonths *economy = &city->economy;
  const CityBudgetSectorDivisors *divisors = &kBudgetSectorDivisors;
  i32 accumulated_total = 0;
  i32 estimated_total = 0;
  memset(budget, 0, sizeof(*budget));
  budget->cash = city->indicators.cash;
  budget->loan = (i64)city->indicators.bond_count * 10000;
  budget->credit_rating = CityBondCreditRating(city);
  budget->next_bond_rate = city->fixes & (1 << CITY_FIX_BOND_RATING) ? CityBondInterestRate(city) : 5;
  budget->current_year = city->geography.start_year + (i32)(city->clock.tick / 300);
  budget->estimate_year = budget->current_year + (city->indicators.year_flag != 0);
  CopyText(budget->city_name, sizeof(budget->city_name), CityNameText(&city->name));
  for (i32 index = 0; index < CITY_ECONOMY_SECTOR_COUNT; ++index) {
    EconomySector *sector = &economy->sector[index];
    int rate = sector->current_rate;
    int annual_rate = rate * sector->current_amount;
    int projected = city->indicators.year_flag ? 12 * annual_rate :
        sector->accumulated + annual_rate * (11 - city->clock.month);
    i32 divisor = divisors->value[index];
    i32 category = kRows[index];
    budget->to_date[category] += sector->accumulated / (12 * divisor);
    budget->estimate[category] += projected / (12 * divisor);
    accumulated_total += sector->accumulated / divisor;
    estimated_total += projected / divisor;
  }
  budget->year_to_date_cashflow = accumulated_total / 12;
  budget->estimated_cashflow = estimated_total / 12;
  for (int sector = CITY_ECONOMY_RESIDENTIAL; sector <= CITY_ECONOMY_INDUSTRIAL; ++sector)
    budget->rate[sector] = economy->sector[sector].current_rate;
  budget->rate[CITY_BUDGET_ROW_POLICE] = economy->sector[CITY_ECONOMY_POLICE].current_rate;
  budget->rate[CITY_BUDGET_ROW_FIRE] = economy->sector[CITY_ECONOMY_FIRE].current_rate;
  budget->rate[CITY_BUDGET_ROW_HEALTH] = economy->sector[CITY_ECONOMY_HEALTH].current_rate;
  budget->rate[CITY_BUDGET_ROW_EDUCATION] = (economy->sector[CITY_ECONOMY_SCHOOLS].current_rate +
      economy->sector[CITY_ECONOMY_COLLEGES].current_rate) / 2;
  int transit_rates = 3;
  for (int sector = CITY_ECONOMY_ROADS; sector < CITY_ECONOMY_SECTOR_COUNT; ++sector)
    transit_rates += economy->sector[sector].current_rate;
  budget->rate[CITY_BUDGET_ROW_TRANSIT] = transit_rates / 6;
}

int CityBudgetRateKindForRow(int row) {
  if (row >= CITY_BUDGET_ROW_RESIDENTIAL && row <= CITY_BUDGET_ROW_INDUSTRIAL) return row;
  return row >= CITY_BUDGET_ROW_POLICE && row < CITY_BUDGET_ROW_COUNT ? row - 2 : -1;
}

int CityBudgetRateMaximum(int row) {
  return row <= CITY_BUDGET_ROW_INDUSTRIAL ? 20 : 100;
}

static int CityBudgetRowTop(int category) {
  return category < CITY_BUDGET_ROW_BONDS ? 51 + category * 12 : 127 + (category - CITY_BUDGET_ROW_BONDS) * 12;
}

CityRect CityBudgetControlBounds(CityRect b, CityBudgetControl control, int category) {
  int y = b.top + CityBudgetRowTop(category);
  int rate_left = b.left + 150;
  int rate_right = rate_left + 2 * CITY_BUDGET_RATE_ARROW_WIDTH + CITY_BUDGET_RATE_VALUE_WIDTH;
  switch (control) {
  case CITY_BUDGET_CONTROL_CATEGORY: return (CityRect){b.left + 16, y, b.left + 134, y + 12};
  case CITY_BUDGET_CONTROL_RATE: return (CityRect){rate_left, y, rate_right, y + 12};
  case CITY_BUDGET_CONTROL_DECREASE:
    return (CityRect){rate_left, y, rate_left + CITY_BUDGET_RATE_ARROW_WIDTH, y + 12};
  case CITY_BUDGET_CONTROL_INCREASE:
    return (CityRect){rate_right - CITY_BUDGET_RATE_ARROW_WIDTH, y, rate_right, y + 12};
  case CITY_BUDGET_CONTROL_ADVISOR: return (CityRect){b.left + 134, y, b.left + 148, y + 12};
  case CITY_BUDGET_CONTROL_TO_DATE: return (CityRect){b.right - 214, y, b.right - 110, y + 12};
  case CITY_BUDGET_CONTROL_ESTIMATE: return (CityRect){b.right - 100, y, b.right - 10, y + 12};
  case CITY_BUDGET_CONTROL_BORROW:
    return (CityRect){b.left, b.bottom - CITY_BUDGET_BUTTON_HEIGHT, b.left + 144, b.bottom};
  case CITY_BUDGET_CONTROL_REPAY:
    return (CityRect){b.left + 144, b.bottom - CITY_BUDGET_BUTTON_HEIGHT, b.left + 288, b.bottom};
  case CITY_BUDGET_CONTROL_SHOW_BONDS:
    return (CityRect){b.left + 288, b.bottom - CITY_BUDGET_BUTTON_HEIGHT, b.right, b.bottom};
  case CITY_BUDGET_CONTROL_TIMER: return (CityRect){b.left + 150, b.top + 18, b.left + 186, b.top + 46};
  case CITY_BUDGET_CONTROL_OPEN_ORDINANCES:
    y = b.top + CityBudgetRowTop(CITY_BUDGET_ROW_ORDINANCES);
    return (CityRect){rate_left, y, rate_right, y + 12};
  default: return (CityRect){0};
  }
}

i32 Sc2kCanvasBudgetStepAt(const Window *window, i32 x, i32 y) {
  for (int category = 0; category < CITY_BUDGET_ROW_COUNT; ++category) {
    if (category == CITY_BUDGET_ROW_ORDINANCES || category == CITY_BUDGET_ROW_BONDS) continue;
    if (CanvasPointInRect(CityBudgetControlBounds(window->bounds,
        CITY_BUDGET_CONTROL_DECREASE, category), x, y)) return category * 2;
    if (CanvasPointInRect(CityBudgetControlBounds(window->bounds,
        CITY_BUDGET_CONTROL_INCREASE, category), x, y)) return category * 2 + 1;
  }
  return -1;
}

int Sc2kCanvasBudgetRateAt(const Window *window, int x, int y) {
  for (int row = 0; row < CITY_BUDGET_ROW_COUNT; ++row) {
    if (CityBudgetRateKindForRow(row) < 0) continue;
    CityRect value = CityBudgetControlBounds(window->bounds, CITY_BUDGET_CONTROL_RATE, row);
    value.left += CITY_BUDGET_RATE_ARROW_WIDTH;
    value.right -= CITY_BUDGET_RATE_ARROW_WIDTH;
    if (CanvasPointInRect(value, x, y)) return row;
  }
  return -1;
}

i32 Sc2kCanvasBudgetAdviceAt(const Window *window, i32 x, i32 y) {
  for (int category = 0; category < CITY_BUDGET_ROW_COUNT; ++category)
    if (CanvasPointInRect(CityBudgetControlBounds(window->bounds,
        CITY_BUDGET_CONTROL_ADVISOR, category), x, y)) return category <= CITY_BUDGET_ROW_INDUSTRIAL ?
            CITY_BUDGET_PROPERTY_TAX : category - 2;
  return -1;
}

CityRect CityPolicyPanelBounds(CityRect bounds, int panel) {
  int width = (bounds.right - bounds.left - 2 * CITY_POLICY_PANEL_MARGIN - CITY_POLICY_PANEL_GAP) / 2;
  int left = bounds.left + CITY_POLICY_PANEL_MARGIN + (panel & 1) * (width + CITY_POLICY_PANEL_GAP);
  int top = bounds.top + 16 + CITY_POLICY_PANEL_MARGIN +
      (panel / 2) * (CITY_POLICY_PANEL_HEIGHT + CITY_POLICY_PANEL_GAP);
  return (CityRect){left, top, left + width, top + CITY_POLICY_PANEL_HEIGHT};
}

i32 Sc2kCanvasPolicyAt(const Window *window, i32 x, i32 y) {
  for (int group = 0; group < CITY_POLICY_GROUP_COUNT; ++group) {
    CityRect panel = CityPolicyPanelBounds(window->bounds, group);
    if (!CanvasPointInRect(panel, x, y)) continue;
    int row = y - panel.top - CITY_POLICY_ROWS_TOP;
    if (row >= 0 && row < CITY_POLICIES_PER_GROUP * CITY_POLICY_ROW_PITCH &&
        row % CITY_POLICY_ROW_PITCH < CITY_POLICY_ROW_HEIGHT)
      return group * CITY_POLICIES_PER_GROUP + row / CITY_POLICY_ROW_PITCH;
  }
  return CITY_CANVAS_POLICY_HIT_NONE;
}

bool IsCityTransitUnderfunded(const City *city) {
  i32 total_rate = 0;
  for (i32 index = CITY_ECONOMY_ROADS; index < CITY_ECONOMY_SECTOR_COUNT; ++index)
    total_rate += (i32)city->economy.sector[index].current_rate;
  return total_rate < 600;
}

void CanvasBudgetAmount(char *output, size_t size, i64 value) {
  char digits[24];
  u64 magnitude = value < 0 ? 0 - (u64)value : (u64)value;
  snprintf(digits, sizeof(digits), "%llu", (unsigned long long)magnitude);
  size_t length = strlen(digits);
  size_t position = 0;
  if (value < 0 && position + 1 < size)
    output[position++] = '-';
  for (size_t index = 0; index < length && position + 1 < size; ++index) {
    output[position++] = digits[index];
    if ((length - index - 1) % 3 == 0 && index + 1 < length && position + 1 < size)
      output[position++] = ',';
  }
  output[position] = '\0';
}

static void DrawCityBudgetAmount(Canvas *canvas, int right, int y, i64 value, u32 ink, bool shadow) {
  char amount[CITY_CURRENCY_TEXT_CAPACITY];
  FormatCityCurrencyForCountry(amount, value, "usa");
  DrawCityInterfaceText(canvas->interface_font, canvas,
      right - CityInterfaceTextWidth(canvas->interface_font, amount), y, amount, ink, shadow);
}

static void DrawCityBudgetTotal(Canvas *canvas, CityRect b, int label_x, int y, const char *label,
    i64 to_date, i64 estimate) {
  CityRect current = CityBudgetControlBounds(b, CITY_BUDGET_CONTROL_TO_DATE, 0);
  CityRect forecast = CityBudgetControlBounds(b, CITY_BUDGET_CONTROL_ESTIMATE, 0);
  CanvasFill(canvas, current.left, y - 2, current.right, y - 1, canvas_text);
  CanvasFill(canvas, forecast.left, y - 2, forecast.right, y - 1, canvas_text);
  DrawCityInterfaceText(canvas->interface_font, canvas, label_x, y, label, 0xFFFCFCFC, true);
  DrawCityBudgetAmount(canvas, current.right, y, to_date, 0xFFFCFCFC, true);
  DrawCityBudgetAmount(canvas, forecast.right, y, estimate, 0xFFFCFCFC, true);
}

static void DrawCityBudgetTable(Canvas *canvas, CityRect b, const CityBudgetPanel *budget) {
  static const char *const kCategories[CITY_BUDGET_ROW_COUNT] = {
    "Residential Tax", "Commercial Tax", "Industrial Tax", "City Ordinances", "Bond Payments",
    "Police Department", "Fire Department", "Health & Welfare", "Education", "Transit Authority"
  };
  const u32 heading = 0xFFFCFCFC, category_ink = 0xFFFFC000;
  char text[64];
  CityRect current = CityBudgetControlBounds(b, CITY_BUDGET_CONTROL_TO_DATE, 0);
  CityRect forecast = CityBudgetControlBounds(b, CITY_BUDGET_CONTROL_ESTIMATE, 0);
  DrawCityInterfaceText(canvas->interface_font, canvas, b.left + 12, b.top + 20, "Year", heading, true);
  CanvasFill(canvas, b.left + 12, b.top + 32,
      b.left + 12 + CityInterfaceTextWidth(canvas->interface_font, "Year"), b.top + 33, heading);
  snprintf(text, sizeof(text), "%d", budget->current_year);
  DrawCityInterfaceText(canvas->interface_font, canvas,
      current.right - CityInterfaceTextWidth(canvas->interface_font, text), b.top + 20, text, heading, true);
  snprintf(text, sizeof(text), "%d", budget->estimate_year);
  DrawCityInterfaceText(canvas->interface_font, canvas,
      forecast.right - CityInterfaceTextWidth(canvas->interface_font, text), b.top + 20, text, heading, true);
  CanvasFill(canvas, current.right - 28, b.top + 32, current.right, b.top + 33, heading);
  CanvasFill(canvas, forecast.right - 28, b.top + 32, forecast.right, b.top + 33, heading);
  CanvasTextRight(canvas, current.right, b.top + 36, "To date", canvas_text);
  CanvasTextRight(canvas, forecast.right, b.top + 36, "Full year", canvas_text);
  DrawCityInterfaceText(canvas->interface_font, canvas,
      b.left + 12, b.top + 39, "Taxes & Ordinances", heading, true);
  DrawCityInterfaceText(canvas->interface_font, canvas,
      b.left + 12, b.top + 115, "Operating Expenses", heading, true);
  i64 income_to_date = 0, income_estimate = 0, expense_to_date = 0, expense_estimate = 0;
  for (int category = 0; category < CITY_BUDGET_ROW_COUNT; ++category) {
    CityRect label = CityBudgetControlBounds(b, CITY_BUDGET_CONTROL_CATEGORY, category);
    CityRect advisor = CityBudgetControlBounds(b, CITY_BUDGET_CONTROL_ADVISOR, category);
    DrawCityInterfaceText(canvas->interface_font, canvas,
        label.left + 4, label.top + 1, kCategories[category], category_ink, true);
    DrawCanvasButtonFrame(canvas, advisor, canvas_face, canvas_light, canvas_dark, true);
    DrawCityInterfaceTextCentered(canvas->interface_font, canvas, advisor, "?", heading, true, 10);
    if (category == CITY_BUDGET_ROW_ORDINANCES) {
      CityRect button = CityBudgetControlBounds(b, CITY_BUDGET_CONTROL_OPEN_ORDINANCES, 0);
      Canvas framed = *canvas;
      framed.unscaled_bevels = false;
      DrawCanvasButtonFrame(&framed, button, 0xFFFFB52A, 0xFFFFDE80, 0xFFD68A08, true);
      DrawCityInterfaceTextCentered(canvas->interface_font, canvas, button, "Open", canvas_text, false, 10);
    } else if (category != CITY_BUDGET_ROW_BONDS) {
      CityRect rate = CityBudgetControlBounds(b, CITY_BUDGET_CONTROL_RATE, category);
      DrawBudgetRateControl(canvas, rate, budget->rate[category]);
    }
    DrawCityBudgetAmount(canvas, current.right, label.top + 1, budget->to_date[category], canvas_text, false);
    DrawCityBudgetAmount(canvas, forecast.right, label.top + 1, budget->estimate[category], canvas_text, false);
    if (category < CITY_BUDGET_ROW_BONDS) {
      income_to_date += budget->to_date[category];
      income_estimate += budget->estimate[category];
    } else {
      expense_to_date += budget->to_date[category];
      expense_estimate += budget->estimate[category];
    }
  }
  DrawCityBudgetTotal(canvas, b, b.left + 100, b.top + 101, "Total", income_to_date, income_estimate);
  DrawCityBudgetTotal(canvas, b, b.left + 100, b.top + 201, "Total", expense_to_date, expense_estimate);
  DrawCityBudgetTotal(canvas, b, b.left + 12, b.top + 217, "Profit",
      budget->year_to_date_cashflow, budget->estimated_cashflow);
  Canvas framed = *canvas;
  framed.unscaled_bevels = false;
  CityRect footer = {b.left, b.top + CITY_BUDGET_FOOTER_TOP, b.right,
      b.bottom - CITY_BUDGET_BUTTON_HEIGHT};
  // Adjacent panels retain both the dark bottom edge and the following light top edge.
  DrawCanvasFrame(&framed, footer, canvas_face, canvas_light, canvas_dark, true);
  DrawCityInterfaceText(canvas->interface_font, canvas,
      b.left + 2, b.top + 231, "Own Funds", heading, true);
  DrawCityInterfaceText(canvas->interface_font, canvas,
      b.left + 2, b.top + 241, "Loan", heading, true);
  DrawCityInterfaceText(canvas->interface_font, canvas,
      b.left + 2, b.top + 253, "Bank Balance", heading, true);
  // Treasury already includes bond proceeds; subtract principal to show the city's own funds.
  i64 own_funds = AddInt64Clamped(budget->cash, -budget->loan);
  int balance_right = b.left + 208;
  DrawCityBudgetAmount(canvas, balance_right, b.top + 231, own_funds,
      own_funds < 0 ? 0xFFFF0000 : canvas_text, own_funds < 0);
  DrawCityBudgetAmount(canvas, balance_right, b.top + 241, budget->loan, canvas_text, false);
  CanvasFill(canvas, b.left + 104, b.top + 252, balance_right, b.top + 253, canvas_text);
  DrawCityBudgetAmount(canvas, balance_right, b.top + 253, budget->cash, heading, true);
  DrawCityInterfaceText(canvas->interface_font, canvas,
      b.left + 232, b.top + 231, "Next bond:", heading, true);
  snprintf(text, sizeof(text), "%d%%", budget->next_bond_rate);
  CanvasTextAt(canvas, b.left + 316, b.top + 231, text, canvas_text);
  DrawCityInterfaceText(canvas->interface_font, canvas,
      b.left + 232, b.top + 241, "Credit rating:", heading, true);
  CanvasTextAt(canvas, b.left + 316, b.top + 241, budget->credit_rating, canvas_text);
  DrawCityInterfaceText(canvas->interface_font, canvas,
      b.left + 232, b.top + 253, "Year end:", heading, true);
  i64 year_end_balance = AddInt64Clamped(budget->cash, budget->estimated_cashflow);
  DrawCityBudgetAmount(canvas, b.right - 10, b.top + 253, year_end_balance, heading, true);
  static const char *const kBondButtons[] = {"Borrow $10,000", "Repay $10,000", "Show bonds"};
  for (int control = CITY_BUDGET_CONTROL_BORROW; control <= CITY_BUDGET_CONTROL_SHOW_BONDS; ++control) {
    CityRect button = CityBudgetControlBounds(b, control, 0);
    DrawCanvasButtonFrame(&framed, button, canvas_face, canvas_light, canvas_dark, true);
    DrawCityInterfaceTextCentered(canvas->interface_font, canvas, button,
        kBondButtons[control - CITY_BUDGET_CONTROL_BORROW], canvas_text, false, 10);
  }
}

void Sc2kCanvasDrawBudgetPanel(Canvas *canvas, CityRect bounds, const CityBudgetPanel *budget) {
  char title[64];
  snprintf(title, sizeof(title), "%s Finances", budget->city_name[0] ? budget->city_name : "City");
  CityRect body = bounds;
  body.top += 14;
  body.bottom = bounds.top + CITY_BUDGET_FOOTER_TOP;
  Canvas framed = *canvas;
  framed.unscaled_bevels = false;
  DrawCanvasFrame(&framed, body, canvas_face, canvas_light, canvas_dark, true);
  DrawCityWindowCaption(canvas, bounds, title, 0xFF247CC9, 0xFF78B4E4, 0xFF124C83, 0);
  DrawCityBudgetTable(canvas, bounds, budget);
}

CityRect CityBudgetQueryBounds(CityRect b, CityBudgetQueryControl control) {
  switch (control) {
  case CITY_BUDGET_QUERY_INPUT: return (CityRect){b.left + 8, b.top + 24, b.right - 24, b.top + 38};
  case CITY_BUDGET_QUERY_CLEAR: return (CityRect){b.right - 24, b.top + 24, b.right - 8, b.top + 38};
  case CITY_BUDGET_QUERY_DEFAULT: return (CityRect){b.left + 8, b.bottom - 24, b.left + 76, b.bottom - 6};
  case CITY_BUDGET_QUERY_CANCEL: return (CityRect){b.left + 80, b.bottom - 24, b.right - 80, b.bottom - 6};
  case CITY_BUDGET_QUERY_OK: return (CityRect){b.right - 76, b.bottom - 24, b.right - 8, b.bottom - 6};
  default: return (CityRect){0};
  }
}

void DrawCityBudgetRateEditor(Canvas *canvas, const Window *window) {
  CityRect b = window->bounds;
  CanvasPanelFrame(canvas, b, window->title);
  DrawCityTextBox(canvas, CityBudgetQueryBounds(b, CITY_BUDGET_QUERY_INPUT), window->budget_rate_input,
      window->text_caret, window->text_scroll, true, window->text_select_all, true);
  char hint[64];
  snprintf(hint, sizeof(hint), "Enter a percentage (0 to %d).", CityBudgetRateMaximum(window->budget_rate_row));
  CanvasTextAt(canvas, b.left + 8, b.top + 44, hint, canvas_text);
  static const char *const kLabels[] = {"X", "Default", "Cancel", "OK"};
  for (int control = CITY_BUDGET_QUERY_CLEAR; control <= CITY_BUDGET_QUERY_OK; ++control) {
    CityRect button = CityBudgetQueryBounds(b, control);
    DrawCanvasButtonFrame(canvas, button, canvas_face, canvas_light, canvas_dark, true);
    DrawCityInterfaceTextCentered(canvas->interface_font, canvas, button, kLabels[control - 1],
        0xFFFCFCFC, true, 10);
  }
}

// Initialize ordinance checks, policy totals, and annual estimates. Requires attached UI/economy owners; rebuilds
// estimates and checks from the live policies rather than applying a new player choice.
void InitializeCityOrdinancesDialog(City *city) {
  CityInterfaceState *ui = city->game->ui;
  EconomySector *policy_budget = &city->economy.sector[CITY_ECONOMY_ORDINANCES];
  CityBudgetPolicyGroupTotals *group_totals = &ui->budget_policy_totals;
  int policy_total = 0;
  i32 group_total[CITY_POLICY_GROUP_COUNT] = {0};

  for (int policy = 0; policy < CITY_ORDINANCE_COUNT; ++policy) {
    if ((city->unlocks.ordinance_flags & (1u << policy)) == 0)
      continue;
    int cost = CityPolicyMonthlyCost(city, policy);
    policy_total += cost;
    group_total[policy / 4] += cost;
  }

  memcpy(group_totals->group, group_total, sizeof(group_total));
  if (!city->game->observation) return;
  policy_budget->current_amount = policy_total;
  if (city->indicators.year_flag != 0)
    policy_budget->projected_annual_total = policy_total * 12;
  else
    policy_budget->projected_annual_total = policy_budget->accumulated + policy_total * (11 - city->clock.month);
}

void UpdateCityPolicyControls(City *city, int policy, int cost) {
  CityInterfaceState *ui = city->game->ui;
  if (!ui) return;
  int *total = &ui->budget_policy_totals.group[policy / 4];
  *total += city->unlocks.ordinance_flags & (1u << policy) ? cost : -cost;
  ui->policy_dialog_value = *total / 75;
}

void Sc2kCanvasDrawPolicies(Game *game, Canvas *canvas, const Window *window) {
  static const char *const headings[CITY_POLICY_GROUP_COUNT] = {
    "Finance", "Safety & Health", "Education", "Promotional", "Other"
  };
  static const char *const policies[CITY_ORDINANCE_COUNT] = {
    "1% Sales Tax", "1% Income Tax", "Legalized Gambling", "Parking Fines",
    "Volunteer Fire Dept.", "Public Smoking Ban", "Free Clinics", "Junior Sports",
    "Pro-Reading Campaign", "Anti-Drug Campaign", "CPR Training", "Neighborhood Watch",
    "Tourist Advertising", "Business Advertising", "City Beautification", "Annual Carnival",
    "Energy Conservation", "Nuclear Free Zone", "Homeless Shelters", "Pollution Controls"
  };
  CityRect bounds = window->bounds;
  CanvasBeveledBox(canvas, bounds.left, bounds.top + 14, bounds.right, bounds.bottom, canvas_face, 1);
  DrawCityWindowCaption(canvas, bounds, window->title, 0xFF247CC9, 0xFF78B4E4, 0xFF124C83, 0);
  char amount[CITY_CURRENCY_TEXT_CAPACITY];
  i64 annual_total = 0;

  for (i32 group = 0; group < CITY_POLICY_GROUP_COUNT; ++group) {
    CityRect panel = CityPolicyPanelBounds(bounds, group);
    i32 left = panel.left, top = panel.top, right = panel.right;
    CanvasBeveledBox(canvas, left, top, right, panel.bottom, canvas_face, 0);
    DrawCityInterfaceText(canvas->interface_font, canvas, left + 4, top + 3, headings[group], 0xFFFCFCFC, true);
    i64 annual_cost = window->policy_group_cost[group] / 75;
    annual_total += annual_cost;
    FormatCityCurrency(game, amount, annual_cost);
    CanvasTextRight(canvas, right - 4, top + 3, amount, canvas_text);
    for (i32 item = 0; item < 4; ++item) {
      i32 policy = group * CITY_POLICIES_PER_GROUP + item;
      i32 row_top = top + CITY_POLICY_ROWS_TOP + item * CITY_POLICY_ROW_PITCH;
      i32 row_height = CITY_POLICY_ROW_HEIGHT;
      i32 check_left = left + 4;
      i32 check_right = check_left + 16;
      bool checked = (window->policy_flags & (1 << policy)) != 0;
      DrawCityCheckbox(canvas, (CityRect){check_left, row_top + 1, check_right, row_top + 11},
          checked, canvas_face, canvas_light, canvas_dark);
      CanvasTextAt(canvas, check_right + 4, row_top + (row_height - 10) / 2,
          policies[policy], canvas_text);
      if (checked) {
        FormatCityCurrency(game, amount, window->policy_cost[policy] / 75);
        CanvasTextRight(canvas, right - 4, row_top + (row_height - 10) / 2, amount, canvas_text);
      }
    }
  }

  CityRect totals = CityPolicyPanelBounds(bounds, CITY_POLICY_GROUP_COUNT);
  CanvasBeveledBox(canvas, totals.left, totals.top, totals.right, totals.bottom, canvas_face, 0);
  DrawCityInterfaceText(canvas->interface_font, canvas, totals.left + 4, totals.top + 3,
      "Ordinance Totals", 0xFFFCFCFC, true);
  int cost_left = totals.left, cost_right = totals.right;
  int total_y = totals.top + CITY_POLICY_ROWS_TOP + 4;
  DrawCityInterfaceText(canvas->interface_font, canvas, cost_left + 8, total_y, "Total", 0xFFFCFCFC, true);
  FormatCityCurrency(game, amount, annual_total);
  DrawCityInterfaceText(canvas->interface_font, canvas,
      cost_right - 4 - CityInterfaceTextWidth(canvas->interface_font, amount), total_y, amount, 0xFFFCFCFC, true);

  DrawCityInterfaceText(canvas->interface_font, canvas, cost_left + 8, total_y + 12, "YTD", 0xFFFCFCFC, true);
  FormatCityCurrency(game, amount, window->policy_ytd_total / 900);
  CanvasTextRight(canvas, cost_right - 4, total_y + 12, amount, canvas_text);
  DrawCityInterfaceText(canvas->interface_font, canvas, cost_left + 8, total_y + 24,
      "Year-end estimate", 0xFFFCFCFC, true);
  FormatCityCurrency(game, amount, window->policy_estimated_total / 900);
  CanvasTextRight(canvas, cost_right - 4, total_y + 24, amount, canvas_text);
}

enum { kBondRowHeight = 12, kBondScrollbarWidth = 12, kBondScrollArrowHeight = 10, kBondHeaderRows = 4 };

CityRect CityBondResizeGripBounds(const Window *window) {
  CityRect b = window->bounds;
  return (CityRect){b.right - kBondScrollbarWidth, b.bottom - kBondScrollbarWidth, b.right, b.bottom};
}

static CityRect CityBondContentBounds(const Window *window) {
  CityRect b = window->bounds;
  return (CityRect){b.left + 1, b.top + 15, b.right - kBondScrollbarWidth - 1, b.bottom - 1};
}

int CityBondContentRows(int count) {
  // Summary, headings, bonds (or the empty message), a gap and the yearly total share one scroll range.
  return kBondHeaderRows + IntMax(1, count) + 2;
}

int CityBondVisibleRows(const Window *window) {
  CityRect list = CityBondContentBounds(window);
  return (list.bottom - list.top) / kBondRowHeight;
}

static CityRect CityBondScrollTrack(const Window *window) {
  CityRect b = window->bounds;
  return (CityRect){b.right - kBondScrollbarWidth, b.top + 14 + kBondScrollArrowHeight,
      b.right, b.bottom - kBondScrollbarWidth - kBondScrollArrowHeight};
}

static CityRect CityBondScrollThumb(const Window *window, int count) {
  CityRect track = CityBondScrollTrack(window);
  return ReadCityScrollbar(track, CityBondContentRows(count), CityBondVisibleRows(window),
      window->bond_scroll, kBondScrollbarWidth).thumb;
}

static void ScrollCityBonds(Game *game, Window *window, int delta) {
  CityCanvasDesktop *desktop = &game->ui->desktop;
  int last = IntMax(0, CityBondContentRows(game->city->indicators.bond_count) - CityBondVisibleRows(window));
  int first = IntClamp(IntClamp(window->bond_scroll, 0, last) + delta, 0, last);
  if (first != window->bond_scroll) {
    window->bond_scroll = first;
    Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(window));
  }
}

static CityRect CityBondControlBounds(const Window *window, int first, CityBondAction action) {
  CityRect content = CityBondContentBounds(window);
  int y = content.top + 1 + (2 - first) * kBondRowHeight;
  switch (action) {
  case CITY_BOND_ACTION_DECREASE_RATE:
    return (CityRect){content.right - 90, y, content.right - 66, y + kBondRowHeight};
  case CITY_BOND_ACTION_INCREASE_RATE:
    return (CityRect){content.right - 32, y, content.right - 8, y + kBondRowHeight};
  default: return (CityRect){0};
  }
}

static void DrawCityBondScrollArrow(Canvas *canvas, CityRect b, bool up) {
  DrawCanvasButtonFrame(canvas, b, canvas_face, canvas_light, canvas_dark, true);
  int pressed = CityCanvasButtonPressed(canvas, b);
  int center = (b.left + b.right) / 2 + pressed, top = (b.top + b.bottom - 4) / 2 + pressed;
  for (int row = 0; row < 4; ++row) {
    int half = up ? row : 3 - row;
    CanvasFill(canvas, center - half, top + row, center + half + 1, top + row + 1, canvas_text);
  }
}

void DrawCityBonds(const City *city, Canvas *canvas, const Window *window) {
  CityRect b = window->bounds, content = CityBondContentBounds(window);
  int count = city->indicators.bond_count, rows = CityBondContentRows(count);
  int visible = CityBondVisibleRows(window);
  int first = IntClamp(window->bond_scroll, 0, IntMax(0, rows - visible));
  Canvas framed = *canvas;
  framed.unscaled_bevels = false;
  DrawCanvasFrame(&framed, (CityRect){b.left, b.top + 14, b.right - kBondScrollbarWidth, b.bottom},
      canvas_face, canvas_light, canvas_dark, true);
  DrawCityWindowCaption(canvas, b, "City Bonds", canvas_title, 0xFFD3BB93, 0xFF674F2B, 0);

  Canvas text = framed;
  text.clip = IntersectCityRects(canvas->clip, content);
  bool rated = (city->fixes & (1 << CITY_FIX_BOND_RATING)) != 0;
  int right = content.right - 6;
  // Draw only visible content rows; the clip also handles a partially visible final row.
  for (int row = first; row < IntMin(rows, first + visible + 1); ++row) {
    int y = content.top + 1 + (row - first) * kBondRowHeight;
    char line[128], amount[CITY_CURRENCY_TEXT_CAPACITY];
    if (row == 0) {
      u64 city_value = city->indicators.asset_value;
      FormatCityCurrency(city->game, amount, city_value > INT64_MAX ? INT64_MAX : city_value);
      snprintf(line, sizeof(line), "Credit: %s    City value: %s", CityBondCreditRating(city), amount);
      CanvasTextAt(&text, content.left + 3, y, line, canvas_text);
    } else if (row == 1) {
      snprintf(line, sizeof(line), "Bonds: %d    Next bond: %d%%    Bank rate: %d%%", count,
          rated ? CityBondInterestRate(city) : window->bond_issue_rate, city->indicators.federal_interest_rate);
      CanvasTextAt(&text, content.left + 3, y, line, canvas_text);
    } else if (row == 2 && !rated) {
      CanvasTextAt(&text, content.left + 3, y, "Next bond rate:", canvas_text);
      snprintf(line, sizeof(line), "%d%%", window->bond_issue_rate);
      DrawCityInterfaceTextCentered(text.interface_font, &text,
          (CityRect){content.right - 66, y, content.right - 32, y + kBondRowHeight},
          line, canvas_text, false, 10);
      DrawCityMessageButton(&text, CityBondControlBounds(window, first, CITY_BOND_ACTION_DECREASE_RATE), "-",
          CITY_MESSAGE_INFORMATION);
      DrawCityMessageButton(&text, CityBondControlBounds(window, first, CITY_BOND_ACTION_INCREASE_RATE), "+",
          CITY_MESSAGE_INFORMATION);
    } else if (row == kBondHeaderRows - 1) {
      DrawCityInterfaceText(text.interface_font, &text, content.left + 4, y, "Bond", 0xFFFCFCFC, true);
      DrawCityInterfaceText(text.interface_font, &text, right - 122, y, "Interest", 0xFFFCFCFC, true);
      DrawCityInterfaceText(text.interface_font, &text, right - 47, y, "Per year", 0xFFFCFCFC, true);
    } else if (row >= kBondHeaderRows && row < kBondHeaderRows + IntMax(1, count)) {
      if (!count) {
        CanvasTextAt(&text, content.left + 4, y, "No outstanding bonds.", canvas_text);
        continue;
      }
      int index = row - kBondHeaderRows;
      snprintf(line, sizeof(line), index == 0 ? "Bond %d (oldest)" : "Bond %d", index + 1);
      CanvasTextAt(&text, content.left + 4, y, line, canvas_text);
      int rate = city->economic_tables.bond_rates.rate[index];
      snprintf(line, sizeof(line), "%d%%", rate);
      CanvasTextRight(&text, right - 75, y, line, canvas_text);
      FormatCityCurrency(city->game, line, (i64)rate * 100);
      CanvasTextRight(&text, right, y, line, canvas_text);
    } else if (row == rows - 1) {
      i64 yearly_total = 0;
      for (int index = 0; index < count; ++index)
        yearly_total += (i64)city->economic_tables.bond_rates.rate[index] * 100;
      FormatCityCurrency(city->game, amount, yearly_total);
      snprintf(line, sizeof(line), "Yearly total: %s", amount);
      CanvasTextAt(&text, content.left + 4, y, line, canvas_text);
    }
  }

  CityRect track = CityBondScrollTrack(window);
  DrawCanvasFrame(&framed, track, canvas_dark, canvas_light, canvas_dark, false);
  DrawCanvasFrame(&framed, CityBondScrollThumb(window, count), canvas_face, canvas_light, canvas_dark, true);
  DrawCityBondScrollArrow(&framed,
      (CityRect){track.left, b.top + 14, track.right, track.top}, true);
  DrawCityBondScrollArrow(&framed,
      (CityRect){track.left, track.bottom, track.right, b.bottom - kBondScrollbarWidth}, false);
  CityRect grip = CityBondResizeGripBounds(window);
  DrawCanvasFrame(&framed, grip, canvas_face, canvas_light, canvas_dark, true);
  for (int offset = 3; offset <= 9; offset += 3)
    DrawCanvasLine(canvas, grip.right - offset, grip.bottom - 2,
        grip.right - 2, grip.bottom - offset, canvas_text, 1);
}

// Bond scrolling, scrollbar capture and rate controls belong to this window.
// Geometry is shared with painting; input changes only this window's presentation.
bool BondsWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    DrawCityBonds(game->city, event->canvas, window);
    return true;
  }
  if (event->kind != WINDOW_INPUT) return false;
  const CanvasSDLEvent *input = event->input;
  CityCanvasDesktop *desktop = &game->ui->desktop;
  Window *target = FindCityCanvasWindow(desktop, window->window_id);
  switch (input->kind) {
  case CANVAS_SDL_POINTER_DOWN: {
    if (input->button != CANVAS_SDL_BUTTON_LEFT) return false;
    int x = event->x, y = event->y;
    CityRect b = window->bounds;
    CityRect scrollbar = {b.right - kBondScrollbarWidth, b.top + 14, b.right, b.bottom - kBondScrollbarWidth};
    if (CanvasPointInRect(scrollbar, x, y)) {
      CityRect track = CityBondScrollTrack(window);
      CityRect thumb = CityBondScrollThumb(window, game->city->indicators.bond_count);
      if (y < track.top) ScrollCityBonds(game, target, -1);
      else if (y >= track.bottom) ScrollCityBonds(game, target, 1);
      else if (y < thumb.top) ScrollCityBonds(game, target, -CityBondVisibleRows(window));
      else if (y >= thumb.bottom) ScrollCityBonds(game, target, CityBondVisibleRows(window));
      else {
        target->bond_scroll_dragging = true;
        target->bond_scroll_grab = y - thumb.top;
      }
      return true;
    }
    if ((game->city->fixes & (1 << CITY_FIX_BOND_RATING)) ||
        !CanvasPointInRect(CityBondContentBounds(window), x, y)) return false;
    int last = IntMax(0, CityBondContentRows(game->city->indicators.bond_count) - CityBondVisibleRows(window));
    int first = IntClamp(window->bond_scroll, 0, last);
    for (CityBondAction action = CITY_BOND_ACTION_DECREASE_RATE; action <= CITY_BOND_ACTION_INCREASE_RATE; ++action) {
      if (!CanvasPointInRect(CityBondControlBounds(window, first, action), x, y)) continue;
      BeginCityButtonRepeat(game, window, event);
      if (action == CITY_BOND_ACTION_DECREASE_RATE && target->bond_issue_rate > -100) --target->bond_issue_rate;
      if (action == CITY_BOND_ACTION_INCREASE_RATE && target->bond_issue_rate < 100) ++target->bond_issue_rate;
      Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(target));
      return true;
    }
    return false;
  }
  case CANVAS_SDL_POINTER_MOVE: {
    if (!window->bond_scroll_dragging) return false;
    CityScrollbar scrollbar = ReadCityScrollbar(CityBondScrollTrack(window),
        CityBondContentRows(game->city->indicators.bond_count), CityBondVisibleRows(window),
        window->bond_scroll, kBondScrollbarWidth);
    int first = CityScrollbarPosition(&scrollbar, event->y, window->bond_scroll_grab, CITY_SCROLLBAR_DRAG_NEAREST);
    ScrollCityBonds(game, target, first - IntClamp(window->bond_scroll, 0, scrollbar.maximum));
    return true;
  }
  case CANVAS_SDL_POINTER_UP:
  case CANVAS_SDL_FOCUS_LOST: {
    bool dragging = window->bond_scroll_dragging;
    target->bond_scroll_dragging = false;
    return dragging;
  }
  case CANVAS_SDL_SCROLL:
    ScrollCityBonds(game, target, input->y > 0 ? -3 : input->y < 0 ? 3 : 0);
    return true;
  case CANVAS_SDL_KEY_DOWN: {
    int page = CityBondVisibleRows(window);
    int delta = input->key == CITY_KEY_UP ? -1 : input->key == CITY_KEY_DOWN ? 1 :
        input->key == CITY_KEY_PAGE_UP ? -page : input->key == CITY_KEY_PAGE_DOWN ? page : 0;
    if (!delta) return false;
    ScrollCityBonds(game, target, delta);
    return true;
  }
  default: return false;
  }
}

const char *CityBudgetTooltipAt(const Window *window, int x, int y) {
  CityRect b = window->bounds;
  switch (window->kind) {
  case CITY_CANVAS_POLICIES: {
    if (CanvasPointInRect(CityPolicyPanelBounds(b, CITY_POLICY_GROUP_COUNT), x, y))
      return "Total is the annual cost of current ordinances. YTD is the amount recorded so far this year. "
          "Year-end estimate includes the remaining months.";
    if (Sc2kCanvasPolicyAt(window, x, y) >= 0)
      return "Enable or disable this city ordinance. The amount shows its estimated annual income or cost.";
    return "Each category heading shows its estimated annual income or cost. Changes take effect immediately.";
  }
  case CITY_CANVAS_BUDGET_RATE:
    return "Enter a percentage, then press Enter or OK. Escape or Cancel keeps the previous value.";
  case CITY_CANVAS_BUDGET: {
    if (CanvasPointInRect(CityBudgetControlBounds(b, CITY_BUDGET_CONTROL_BORROW, 0), x, y))
      return "Borrow $10,000. Review the offered annual interest rate before confirming.";
    if (CanvasPointInRect(CityBudgetControlBounds(b, CITY_BUDGET_CONTROL_REPAY, 0), x, y))
      return "Repay the oldest $10,000 bond. Review its interest rate before confirming.";
    if (CanvasPointInRect(CityBudgetControlBounds(b, CITY_BUDGET_CONTROL_SHOW_BONDS, 0), x, y))
      return "List every outstanding bond, oldest first, with its interest rate and annual payment.";
    if (y >= b.top + 229) {
      if (x < b.left + 232) return "Own funds plus loans equals the bank balance.";
      if (y >= b.top + 253) return "Year-end balance equals the current bank balance plus estimated annual profit.";
      return "The interest rate offered for a new bond and the city's debt-to-assets credit grade.";
    }
    for (int category = 0; category < CITY_BUDGET_ROW_COUNT; ++category) {
      CityRect row = CityBudgetControlBounds(b, CITY_BUDGET_CONTROL_CATEGORY, category);
      if (y < row.top || y >= row.bottom) continue;
      if (CanvasPointInRect(CityBudgetControlBounds(b, CITY_BUDGET_CONTROL_ADVISOR, category), x, y))
        return "Ask the advisor about this budget category.";
      if (x >= b.right - 100) return "Estimated income or expense for the full year.";
      if (x >= b.right - 214) return "Income or expense accumulated so far this year.";
      if (category == CITY_BUDGET_ROW_ORDINANCES) {
        if (CanvasPointInRect(CityBudgetControlBounds(b, CITY_BUDGET_CONTROL_OPEN_ORDINANCES, 0), x, y))
          return "Open city ordinances.";
        return "Ordinance income and expenses. Use Open to manage city ordinances.";
      }
      if (category == CITY_BUDGET_ROW_BONDS)
        return "Bond interest payments. Use Show bonds to review the bond list.";
      if (x >= b.left + 150) return category <= CITY_BUDGET_ROW_INDUSTRIAL ?
          "Set this property tax rate. Click the value to type; arrows change by 1." :
          "Set service funding. Click the value to type; arrows change by 5, or 1 with Shift.";
      return "City budget category. Use the question mark to ask its advisor.";
    }
    return "Manage city taxes, service funding and financial forecasts.";
  }
  case CITY_CANVAS_BONDS:
    if (CanvasPointInRect(CityBondResizeGripBounds(window), x, y))
      return "Drag this corner to resize the bond list.";
    return "Scroll all bond information using the bar, wheel or arrow keys. Bonds are listed oldest first.";
  default: return NULL;
  }
}
