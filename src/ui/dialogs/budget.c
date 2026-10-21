#include "city/economy/types.h"
#include "ui/windows/state.h"
#include "city/presentation.h"
#include "city/economy/tables.h"
#include "drawing/drawing_internal.h"

// Drawing subsystem: budget.

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
  i32 group_total[5] = {0};

  for (int policy = 0; policy < CITY_ORDINANCE_COUNT; ++policy) {
    if ((city->unlocks.ordinance_flags & (1u << policy)) == 0)
      continue;
    int cost = Phase0PolicyCost(city, policy);
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
