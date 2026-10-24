#include "city/economy/budget.h"
#include "city/engine/random.h"
#include "city/economy/types.h"
#include "city/engine/city_types.h"
#include "city/maps/tile_storage.h"
#include "city/presentation.h"
#include "game/audio.h"
#include "city/economy/history.h"
#include "city/engine/questions.h"
#include "game/state.h"
#include "city/state.h"

// Prepare cursor and music for a due manual annual budget. Retained comparison callers preserve their minimized
// gate; normal City execution consumes the selection draw independently of local window state.
bool PrepareCityAnnualBudget(City *city) {
  if (city->indicators.auto_budget_enabled || (city->game->observation && IsCityPresentationMinimized(city)))
    return false;
  SetCityCursor(city, CITY_CURSOR_REWARDS, 0);
  StopAllCitySound(city->game);
  static const u16 kMusicTracks[] = {
    CITY_MUSIC_TRACK_16, CITY_MUSIC_TRACK_05, CITY_MUSIC_TRACK_02, CITY_MUSIC_TRACK_10,
  };
  PlayCityEventMusicById(city->game, kMusicTracks[NextCityMapRandomModulo(city, 4)]);
  return true;
}

// Select advice from current funding and population metrics. Adequate health funding consumes one CRT draw to choose
// a suggested policy; other branches leave RNG unchanged. Presentation owns loading text and opening the advisor.
CityBudgetAdvice SelectCityBudgetAdvisorMessage(City *city, CityBudgetCategory category) {
  CityEconomyMonths *economy = &city->economy;
  u32 population = city->indicators.population;
  u32 policies = city->unlocks.ordinance_flags;
  switch (category) {
  case CITY_BUDGET_PROPERTY_TAX: {
    int growth = city->economic_tables.sector_rates.offsets.value[0] +
        city->economic_tables.sector_rates.offsets.value[1] + city->economic_tables.sector_rates.offsets.value[2];
    CityBudgetAdvice advice = growth < -666   ? CITY_ADVICE_LOWER_PROPERTY_TAX
        : city->indicators.cash >= population ? CITY_ADVICE_NONE
        : growth <= 666                       ? CITY_ADVICE_CUT_SERVICES
                                              : CITY_ADVICE_RAISE_PROPERTY_TAX;
    return advice;
  }
  case CITY_BUDGET_ORDINANCES:
    if (!(policies & CITY_POLICY_FLAG_POLLUTION_CONTROLS) &&
        (i32)ReadHistorySample(city, CITY_HISTORY_POLLUTION, 0) > 30)
      return CITY_ADVICE_POLLUTION_CONTROLS;
    if (!(policies & CITY_POLICY_FLAG_ENERGY_CONSERVATION) && city->infrastructure.power_percent > 98)
      return CITY_ADVICE_ENERGY_CONSERVATION;
    if ((i32)ReadHistorySample(city, CITY_HISTORY_CRIME, 0) > 30) {
      if (!(policies & CITY_POLICY_FLAG_NEIGHBORHOOD_WATCH))
        return CITY_ADVICE_NEIGHBORHOOD_WATCH;
      if (!(policies & CITY_POLICY_FLAG_ANTI_DRUG))
        return CITY_ADVICE_ANTI_DRUG;
      return policies & CITY_POLICY_FLAG_LEGALIZED_GAMBLING ? CITY_ADVICE_DROP_GAMBLING : CITY_ADVICE_NONE;
    }
    if ((policies & CITY_POLICY_FLAG_INCOME_TAX) && city->economic_tables.sector_rates.offsets.value[0] < -666)
      return CITY_ADVICE_DROP_INCOME_TAX;
    if ((policies & CITY_POLICY_FLAG_SALES_TAX) && city->economic_tables.sector_rates.offsets.value[1] < -666)
      return CITY_ADVICE_DROP_SALES_TAX;
    return CITY_ADVICE_NONE;
  case CITY_BUDGET_BONDS: {
    if (city->indicators.cash < -1000)
      return CITY_ADVICE_FLOAT_BOND;
    if (city->indicators.cash < 0)
      return CITY_ADVICE_NEGATIVE_TREASURY;
    u64 divisor = city->indicators.asset_value;
    if (divisor != UINT64_MAX) ++divisor;
    int interest = city->indicators.federal_interest_rate +
        (i16)((25000 * (u64)city->indicators.bond_count) / divisor) + 1;
    if (interest < 4)
      return CITY_ADVICE_LOW_INTEREST;
    int tax = economy->sector[CITY_ECONOMY_RESIDENTIAL].projected_annual_total +
        economy->sector[CITY_ECONOMY_COMMERCIAL].projected_annual_total +
        economy->sector[CITY_ECONOMY_INDUSTRIAL].projected_annual_total;
    return tax >= economy->sector[CITY_ECONOMY_BONDS].projected_annual_total ?
        CITY_ADVICE_NONE : CITY_ADVICE_BOND_PAYMENTS;
  }
  case CITY_BUDGET_POLICE: {
    int crime = (i32)ReadHistorySample(city, CITY_HISTORY_CRIME, 0);
    if (crime > 40)
      return CITY_ADVICE_HIGH_CRIME;
    if (crime > 30 && !(policies & CITY_POLICY_FLAG_NEIGHBORHOOD_WATCH))
      return CITY_ADVICE_NEIGHBORHOOD_WATCH;
    return crime < 20 ? CITY_ADVICE_LOW_CRIME : CITY_ADVICE_AVERAGE_CRIME;
  }
  case CITY_BUDGET_FIRE: {
    u64 capacity = economy->sector[CITY_ECONOMY_FIRE].current_rate *
        (150 * (u64)city->statistics.value[TK_FIRE_STATION] / 9);
    if (capacity < population)
      return CITY_ADVICE_FIRE_SHORTAGE;
    return 2u * capacity / 3 > population ? CITY_ADVICE_FIRE_SURPLUS : CITY_ADVICE_FIRE_ADEQUATE;
  }
  case CITY_BUDGET_HEALTH: {
    u64 capacity =
        economy->sector[CITY_ECONOMY_HEALTH].current_rate * (250 * (u64)city->statistics.value[TK_HOSPITAL] / 9);
    if (capacity < population)
      return CITY_ADVICE_HEALTH_SHORTAGE;
    if (2u * capacity / 3 > population)
      return CITY_ADVICE_HEALTH_SURPLUS;
    switch (Sc2kCrtRandRange(city, 3)) {
    case 0:
      return policies & CITY_POLICY_FLAG_PUBLIC_SMOKING_BAN ? CITY_ADVICE_HEALTH_ADEQUATE : CITY_ADVICE_SMOKING_BAN;
    case 1:
      return policies & CITY_POLICY_FLAG_CPR_TRAINING ? CITY_ADVICE_HEALTH_ADEQUATE : CITY_ADVICE_CPR_TRAINING;
    default:
      return policies & CITY_POLICY_FLAG_FREE_CLINICS ? CITY_ADVICE_HEALTH_ADEQUATE : CITY_ADVICE_FREE_CLINICS;
    }
  }
  case CITY_BUDGET_EDUCATION:
    if (economy->sector[CITY_ECONOMY_SCHOOLS].current_rate * (u32)(15 * city->statistics.value[TK_SCHOOL] / 9) <
        city->age_cohorts.value[1] + city->age_cohorts.value[2])
      return CITY_ADVICE_SCHOOL_SHORTAGE;
    return economy->sector[CITY_ECONOMY_COLLEGES].current_rate *
                (u32)(50 * city->statistics.value[TK_COLLEGE] / 16) <
            city->age_cohorts.value[3]
        ? CITY_ADVICE_COLLEGE_SHORTAGE
        : CITY_ADVICE_EDUCATION_ADEQUATE;
  case CITY_BUDGET_TRANSIT: {
    u32 amount = 0, rates = 0;
    for (int index = CITY_ECONOMY_ROADS; index <= CITY_ECONOMY_TUNNELS; ++index) {
      amount += economy->sector[index].current_amount;
      rates += economy->sector[index].current_rate;
    }
    if ((i32)rates < 600)
      return CITY_ADVICE_TRANSIT_UNDERFUNDED;
    if (population / 100 > amount)
      return CITY_ADVICE_TRANSIT_SHORTAGE;
    return population / 10 < amount ? CITY_ADVICE_TRANSIT_SURPLUS : CITY_ADVICE_NONE;
  }
  default:
    return CITY_ADVICE_NONE;
  }
}

// Look up the currently open budget by its positive identity. Returns a borrowed pointer only for the matching live
// budget, otherwise NULL; callers must not keep it across budget replacement.
const CityBudgetState *FindCityBudget(City *city, i32 id) {
  return id > 0 && city->budget.id == id ? &city->budget : NULL;
}

// Reserve the City's single budget interaction and initialize its helper state. Requires a positive unused identity;
// false leaves an existing interaction in place, while manual opening also prepares its presentation.
bool BeginCityBudget(City *city, i32 id, bool manual) {
  if (id <= 0 || city->budget.id)
    return false;
  city->budget = (CityBudgetState){.id = id,
      .helper_stage =
          city->indicators.year_flag && city->clock.month == CITY_MONTH_JANUARY ? 0 : CITY_BUDGET_HELPER_DISABLED,
      .timer_running = true,
      .timer_revision = 1,
      .manual = manual,
      .saved_auto_budget = manual ? city->indicators.auto_budget_enabled : 0};
  if (manual) {
    city->indicators.auto_budget_enabled = 0;
    if (!PrepareCityAnnualBudget(city)) {
      city->indicators.auto_budget_enabled = city->budget.saved_auto_budget;
      city->budget = (CityBudgetState){0};
      return false;
    }
  }
  return true;
}

// Close the matching budget interaction and perform its closing effects. Returns false for a missing identity or a
// still-open advisor question; callers must dismiss that child before closing its parent.
bool EndCityBudget(City *city, i32 id) {
  CityBudgetState *budget = (CityBudgetState *)FindCityBudget(city, id);
  if (!budget || budget->advisor_question)
    return false;
  if (budget->manual) {
    SetCityCursor(city, CITY_CURSOR_ARROW, 0);
    city->indicators.auto_budget_enabled = budget->saved_auto_budget;
  }
  *budget = (CityBudgetState){0};
  return true;
}

// Deliver one helper-timer event to the matching open budget. Rejects unrelated timers and closing budgets; a queued
// event may still be accepted after the timer itself has stopped.
bool DeliverCityBudgetHelperTimer(City *city, i32 id, i32 timer) {
  CityBudgetState *budget = (CityBudgetState *)FindCityBudget(city, id);
  if (!budget || timer != CITY_BUDGET_HELPER_TIMER || budget->close_requested)
    return false;
  // Accepts an already-delivered timer even after an advisor stopped generation.
  // Inactive helpers receive the timer but have no stage/close effect.
  if (budget->helper_stage == CITY_BUDGET_HELPER_DISABLED)
    return true;
  if (budget->helper_stage == CITY_BUDGET_HELPER_LAST_STAGE) {
    budget->timer_running = false;
    budget->close_requested = true;
  } else {
    ++budget->helper_stage;
  }
  return true;
}

// Associate an advisor question with the live budget and suspend its helper. Returns false for a missing or closing
// budget; the question identity is retained until the matching resume.
bool SuspendCityBudgetHelperForAdvisor(City *city, i32 id, i32 question) {
  CityBudgetState *budget = (CityBudgetState *)FindCityBudget(city, id);
  if (!budget || budget->close_requested)
    return false;
  budget->advisor_question = question;
  if (budget->helper_stage != CITY_BUDGET_HELPER_DISABLED)
    budget->timer_running = false;
  return true;
}

// Restore the parent advisor identity and helper after a matching child question closes. Returns false for a stale
// budget or question; the caller supplies the captured parent and helper choice.
bool ResumeCityBudgetHelperAfterAdvisor(City *city, i32 id, i32 question, i32 parent_question, bool helper_enabled) {
  CityBudgetState *budget = (CityBudgetState *)FindCityBudget(city, id);
  if (!budget || budget->advisor_question != question)
    return false;
  budget->advisor_question = parent_question;
  // Each original advisor caller keeps its own stage-enabled branch across the wait.
  if (helper_enabled) {
    budget->helper_stage = 0;
    budget->timer_running = true;
    ++budget->timer_revision;
  }
  return true;
}

// Restart an enabled budget helper after a funding edit. Missing budgets and permanently disabled helpers are left
// alone; this updates the timer generation rather than delivering a timer event.
void RestartCityBudgetHelperAfterRateEdit(City *city, i32 id) {
  CityBudgetState *budget = (CityBudgetState *)FindCityBudget(city, id);
  if (budget && budget->helper_stage != CITY_BUDGET_HELPER_DISABLED) {
    budget->helper_stage = 0;
    budget->timer_running = true;
    ++budget->timer_revision;
  }
}
