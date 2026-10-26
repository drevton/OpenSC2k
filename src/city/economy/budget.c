#include "city/economy/budget.h"

#include "base/integer.h"
#include "city/economy/facilities.h"
#include "city/economy/tables.h"
#include "city/engine/commands.h"
#include "city/maps/buildings.h"
#include "city/maps/kinds.h"
#include "city/maps/map_types.h"
#include "city/presentation.h"
#include "city/state.h"
#include "city/tools/tools.h"
#include "game/observation.h"
#include "game/state.h"


// Refresh every sector's projected annual total from the live rates, amounts and calendar.
// Replaces the forecasts and invalidates finance information without booking income or costs.
void RefreshCityBudgetEstimates(City *city) {
  CityEconomyMonths *economy = &city->economy;
  for (int sector = 0; sector < CITY_ECONOMY_SECTOR_COUNT; ++sector) {
    EconomySector *account = &economy->sector[sector];
    int annual_rate = account->current_rate * account->current_amount;
    account->projected_annual_total = city->indicators.year_flag != 0 ? 12 * annual_rate :
        account->accumulated + annual_rate * (11 - city->clock.month);
  }
  InvalidateCityInformation(city->game, CITY_UPDATE_FINANCES);
}

// Adjust all eleven industry tax baselines and recompute their rounded mean. A zero direction refreshes the mean
// after an individual rate edit; each stored rate is normalized to the 0..20 range.
static void AdjustIndustryTaxRates(City *city, int direction) {
  CityIndustryDemand *baseline = &city->industry_baseline;
  int total = 0;
  for (int index = 0; index < CITY_INDUSTRY_COUNT; ++index) {
    // The addition wraps to signed 16 bits before clamping.
    int value = (i16)(baseline->value[index] + direction);
    value = IntClamp(value, 0, 20);
    baseline->value[index] = value;
    total += value;
  }
  city->economy.sector[CITY_ECONOMY_INDUSTRIAL].current_rate = (total + 6) / CITY_INDUSTRY_COUNT;
}

// Return one policy's signed monthly income/cost from current sector amounts. The caller decides whether the
// policy is active; unsupported policy IDs return zero. This query leaves City state unchanged.
int CityPolicyMonthlyCost(const City *city, int policy) {
  const CityEconomyMonths *economy = &city->economy;
  int residential = economy->sector[CITY_ECONOMY_RESIDENTIAL].current_amount;
  int commercial = economy->sector[CITY_ECONOMY_COMMERCIAL].current_amount;
  int industrial = economy->sector[CITY_ECONOMY_INDUSTRIAL].current_amount;
  switch (policy) {
  case CITY_POLICY_SALES_TAX:
    return commercial;
  case CITY_POLICY_INCOME_TAX:
    return residential;
  case CITY_POLICY_LEGALIZED_GAMBLING:
    return 2 * commercial;
  case CITY_POLICY_PARKING_FINES:
    return residential / 2;
  case CITY_POLICY_VOLUNTEER_FIRE:
  case CITY_POLICY_NEIGHBORHOOD_WATCH:
    return residential / -3;
  case CITY_POLICY_PUBLIC_SMOKING_BAN:
    return commercial / -6;
  case CITY_POLICY_FREE_CLINICS:
  case CITY_POLICY_HOMELESS_SHELTERS:
    return residential / -2;
  case CITY_POLICY_JUNIOR_SPORTS:
  case CITY_POLICY_CITY_BEAUTIFICATION:
    return residential / -4;
  case CITY_POLICY_PRO_READING:
  case CITY_POLICY_CPR_TRAINING:
    return residential / -6;
  case CITY_POLICY_ANTI_DRUG:
    return residential / -5;
  case CITY_POLICY_TOURIST_ADVERTISING:
    return -commercial;
  case CITY_POLICY_BUSINESS_ADVERTISING:
  case CITY_POLICY_POLLUTION_CONTROLS:
    return -industrial;
  case CITY_POLICY_ANNUAL_CARNIVAL:
    return commercial / -3;
  case CITY_POLICY_ENERGY_CONSERVATION:
    return (int)(0 - city->indicators.population - city->indicators.arcology_population);
  case CITY_POLICY_NUCLEAR_FREE_ZONE:
    return 0;
  default:
    return 0;
  }
}

// Move one sector rate by a caller-selected step within its directional limits. Direction is positive to increase and
// nonpositive to decrease; the caller supplies a valid existing rate and step.
static void ChangeBudgetRate(EconomySector *sector, int direction, int step, int maximum) {
  int rate = (int)sector->current_rate;
  if (direction > 0) {
    if (rate < maximum)
      rate += step;
  } else if (rate > 0) {
    rate -= step;
  }
  sector->current_rate = (u32)rate;
}

// Clamp a sector's funding rate to zero through the supplied maximum. Requires a live sector and nonnegative maximum;
// only current_rate changes.
static void ClampBudgetRate(EconomySector *sector, int maximum) {
  int rate = (int)sector->current_rate;
  rate = IntClamp(rate, 0, maximum);
  sector->current_rate = (u32)rate;
}

// Apply a directional funding or tax step to the selected budget category. The caller validates the category; property
// tax updates all three tax sectors together.
static void AdjustCityBudgetRate(City *city, int category, int direction) {
  CityEconomyMonths *economy = &city->economy;
  if (category == CITY_BUDGET_PROPERTY_TAX) {
    ChangeBudgetRate(&economy->sector[CITY_ECONOMY_RESIDENTIAL], direction, 1, 20);
    ChangeBudgetRate(&economy->sector[CITY_ECONOMY_COMMERCIAL], direction, 1, 20);
    if ((direction > 0 && (int)economy->sector[CITY_ECONOMY_INDUSTRIAL].current_rate < 20) ||
        (direction < 0 && (int)economy->sector[CITY_ECONOMY_INDUSTRIAL].current_rate > 0))
      AdjustIndustryTaxRates(city, direction);
    ClampBudgetRate(&economy->sector[CITY_ECONOMY_RESIDENTIAL], 20);
  } else if (category >= CITY_BUDGET_POLICE && category <= CITY_BUDGET_HEALTH) {
    ChangeBudgetRate(&economy->sector[category + 2], direction, 5, 100);
    ClampBudgetRate(&economy->sector[category + 2], 100);
  } else if (category == CITY_BUDGET_EDUCATION) {
    ChangeBudgetRate(&economy->sector[CITY_ECONOMY_SCHOOLS], direction, 5, 100);
    ChangeBudgetRate(&economy->sector[CITY_ECONOMY_COLLEGES], direction, 5, 100);
    ClampBudgetRate(&economy->sector[CITY_ECONOMY_COLLEGES], 100);
  } else if (category == CITY_BUDGET_TRANSIT) {
    for (int index = CITY_ECONOMY_ROADS; index < CITY_ECONOMY_SECTOR_COUNT; ++index)
      ChangeBudgetRate(&economy->sector[index], direction, 5, 100);
    if (direction > 0 && city->budget_transit_clamp_rate > 100)
      city->budget_transit_clamp_rate = 100;
    if (direction < 0 && city->budget_transit_clamp_rate < 0)
      city->budget_transit_clamp_rate = 0;
  }
}

// Apply the budget policy to the city. Policy must identify an admitted ordinance; applies its funding effects and
// refreshes the attached budget controls.
static void ToggleCityBudgetPolicy(City *city, int policy) {
  u32 bit = 1u << policy;
  int cost = CityPolicyMonthlyCost(city, policy);
  EconomySector *policy_budget = &city->economy.sector[CITY_ECONOMY_ORDINANCES];
  city->unlocks.ordinance_flags ^= bit;
  if ((city->unlocks.ordinance_flags & bit) != 0) {
    policy_budget->current_amount += cost;
  } else {
    policy_budget->current_amount -= cost;
  }
  if (city->indicators.year_flag != 0)
    policy_budget->projected_annual_total = policy_budget->current_amount * 12;
  else
    policy_budget->projected_annual_total =
        policy_budget->accumulated + policy_budget->current_amount * (11 - city->clock.month);
  UpdateCityPolicyControls(city, policy, cost);
}

// Rebuild the active policy total, then refresh city unlock availability. Requires initialized economy/unlock state;
// recomputes derived availability without toggling an ordinance.
void RebuildCityPolicyAvailability(City *city) {
  RestoreCityCursorAfterDialog(city);
  EconomySector *policy_budget = &city->economy.sector[CITY_ECONOMY_ORDINANCES];
  policy_budget->current_amount = 0;
  for (int policy = 0; policy < CITY_ORDINANCE_COUNT; ++policy)
    if ((city->unlocks.ordinance_flags & (1u << policy)) != 0)
      policy_budget->current_amount += CityPolicyMonthlyCost(city, policy);
  RebuildCityUnlockState(city);
}

// Apply one directional step to an admitted budget category. Rejects unsupported direction/category and reserved
// input; TEST does not change rates, while execution updates the matching shared sector values.
i64 CmdBudgetRate(City *city, u32 flags, TileIndex location, int parameter1, int parameter2, int parameter3) {
  (void)location;
  int invalid = CITY_COMMAND_COST_INVALID;
  const CityBudgetState *session = FindCityBudget(city, parameter3);
  if (parameter1 < 0 || parameter1 >= CITY_BUDGET_CATEGORY_COUNT ||
      (parameter2 != -1 && parameter2 != 1) ||
      (parameter3 != 0 && (!session || session->close_requested)))
    return invalid;
  if (flags & CITY_COMMAND_TEST) return 0;
  RestartCityBudgetHelperAfterRateEdit(city, parameter3);
  AdjustCityBudgetRate(city, parameter1, parameter2);
  RefreshCityBudgetEstimates(city);
  return 1;
}

// Direct values are shared City input; no local budget window or sound is involved. CITY_COMMAND_TEST queries
// admission without applying effects; execution returns the action result or a negative error.
i64 CmdSetBudgetRate(City *city, u32 flags, TileIndex location, int kind, int rate, int options) {
  (void)location;
  int maximum = kind <= CITY_BUDGET_RATE_INDUSTRIAL ? 20 : 100;
  if (kind < 0 || kind >= CITY_BUDGET_RATE_COUNT || rate < 0 || rate > maximum || options)
    return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) return 0;
  CityEconomyMonths *economy = &city->economy;
  switch (kind) {
  case CITY_BUDGET_RATE_RESIDENTIAL:
  case CITY_BUDGET_RATE_COMMERCIAL:
    economy->sector[kind].current_rate = rate;
    break;
  case CITY_BUDGET_RATE_INDUSTRIAL:
    for (int industry = 0; industry < CITY_INDUSTRY_COUNT; ++industry) city->industry_baseline.value[industry] = rate;
    AdjustIndustryTaxRates(city, 0);
    break;
  case CITY_BUDGET_RATE_POLICE:
  case CITY_BUDGET_RATE_FIRE:
  case CITY_BUDGET_RATE_HEALTH:
    economy->sector[CITY_ECONOMY_POLICE + kind - CITY_BUDGET_RATE_POLICE].current_rate = rate;
    break;
  case CITY_BUDGET_RATE_EDUCATION:
    economy->sector[CITY_ECONOMY_SCHOOLS].current_rate = rate;
    economy->sector[CITY_ECONOMY_COLLEGES].current_rate = rate;
    break;
  case CITY_BUDGET_RATE_TRANSIT:
    for (int sector = CITY_ECONOMY_ROADS; sector < CITY_ECONOMY_SECTOR_COUNT; ++sector)
      economy->sector[sector].current_rate = rate;
    break;
  default: break;
  }
  RefreshCityBudgetEstimates(city);
  return 1;
}

// Set the agreed industry-tax baseline within the supported percentage range. Rejects reserved payload and
// out-of-range values; TEST has no effects and execution updates shared tax calculations.
i64 CmdSetIndustryTaxBaseline(City *city, u32 flags, TileIndex location,
    int parameter1, int parameter2, int parameter3) {
  (void)location;
  int invalid = CITY_COMMAND_COST_INVALID;
  int industry = parameter1;
  int rate = parameter2;
  if ((industry < 0 && industry != -1) || industry >= CITY_INDUSTRY_COUNT || rate < 0 || rate > 20 ||
      parameter3 != 0)
    return invalid;
  if (flags & CITY_COMMAND_TEST) return 0;
  if (industry == -1) {
    for (int index = 0; index < CITY_INDUSTRY_COUNT; ++index)
      city->industry_baseline.value[index] = rate;
    AdjustIndustryTaxRates(city, 0);
  } else if (city->industry_baseline.value[industry] != rate) {
    city->industry_baseline.value[industry] = rate;
    AdjustIndustryTaxRates(city, 0);
  }
  return 1;
}

// Toggle an admitted city ordinance and rebuild its availability/cost effects. Payload selects a valid policy and
// contains no local window identity; TEST validates without changing rates, unlocks or policy state.
i64 CmdToggleCityPolicy(City *city, u32 flags, TileIndex location,
    int parameter1, int parameter2, int parameter3) {
  (void)location;
  int invalid = CITY_COMMAND_COST_INVALID;
  int policy = parameter1;
  if (policy < 0 || policy >= CITY_ORDINANCE_COUNT || parameter2 != 0 || parameter3 != 0)
    return invalid;
  if (flags & CITY_COMMAND_TEST) return 0;
  ToggleCityBudgetPolicy(city, policy);
  if (!city->game->observation) {
    RebuildCityPolicyAvailability(city);
    InitializeCityOrdinancesDialog(city);
  }
  return 1;
}

// Toggle automatic budget handling through the shared action path. All payload fields are reserved and must be zero;
// TEST reports admission without changing the shared flag or local preferences.
i64 CmdToggleAutoBudget(City *city, u32 flags, TileIndex location,
    int parameter1, int parameter2, int parameter3) {
  (void)location;
  if (parameter1 || parameter2 || parameter3) return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) return 0;
  city->indicators.auto_budget_enabled = city->indicators.auto_budget_enabled == 0;
  return 1;
}

// Estimate city assets from tile statistics for bond limits and rates. Requires consistent tile counters; returns the
// live unsigned valuation and leaves its cached City field unchanged.
static u64 CalculateCityAssetValue(const City *city) {
  static const u32 kStockArcologyValues[TK_LAUNCH_ARCOLOGY - TK_PLYMOUTH_ARCOLOGY + 1] = {
    [TK_PLYMOUTH_ARCOLOGY - TK_PLYMOUTH_ARCOLOGY] = 100000,
    [TK_FOREST_ARCOLOGY - TK_PLYMOUTH_ARCOLOGY] = 120000,
    [TK_DARCO_ARCOLOGY - TK_PLYMOUTH_ARCOLOGY] = 150000,
    [TK_LAUNCH_ARCOLOGY - TK_PLYMOUTH_ARCOLOGY] = 200000,
  };
  typedef struct {
    u8 statistic;
    u8 divisor;
    u16 multiplier;
  } CityAssetValueRule;
  static const CityAssetValueRule kAssetValues[] = {{TK_HYDRO_POWER, 1, 400},
      {TK_WIND_POWER, 1, 6600},
      {TK_GAS_POWER_PLANT, 16, 6600}, {TK_OIL_POWER_PLANT, 16, 2000},
      {TK_NUCLEAR_POWER_PLANT, 16, 15000}, {TK_SOLAR_POWER_PLANT, 16, 100},
      {TK_MICROWAVE_POWER_PLANT, 16, 1300}, {TK_FUSION_POWER_PLANT, 16, 28000},
      {TK_COAL_POWER_PLANT, 16, 40000}, {TK_HOSPITAL, 9, 500},
      {TK_POLICE_STATION, 9, 500}, {TK_FIRE_STATION, 9, 500}, {TK_MUSEUM, 9, 1000},
      {TK_LARGE_PARK, 9, 150}, {TK_SCHOOL, 9, 250}, {TK_STADIUM, 16, 5000},
      {TK_PRISON, 16, 3000}, {TK_COLLEGE, 16, 1000}, {TK_ZOO, 16, 3000},
      {TK_WATER_PUMP, 1, 100}, {TK_RUNWAY_FIRST, 1, 250}, {TK_RUNWAY_LAST, 1, 250},
      {TK_PIER, 1, 150}, {TK_CRANE, 1, 150}, {TK_CONTROL_TOWER_1X1_1, 1, 250},
      {TK_WAREHOUSE_1X1_4, 1, 150}, {TK_BUILDING_1X1_1, 1, 250},
      {TK_BUILDING_1X1_2, 1, 250}, {TK_TARMAC, 1, 250}, {TK_SUBWAY_STATION, 1, 250},
      {TK_RADAR, 1, 250}, {TK_WATER_TOWER, 4, 250}, {TK_BUS_STATION, 4, 250},
      {TK_RAIL_STATION, 4, 500}, {TK_PARKING_LOT_2X2_1, 1, 250}, {TK_LOADING_BAY, 1, 150},
      {TK_CARGO_YARD, 1, 150}, {TK_WATER_TREATMENT_PLANT, 9, 500}, {TK_LIBRARY, 4, 500},
      {TK_HANGAR_3X3, 1, 250}, {TK_MARINA, 9, 1000}, {TK_DESALINATION_PLANT, 9, 1000}};
  const CityStatistics *statistics = &city->statistics;
  bool repaired = city->fixes & (1ull << CITY_FIX_ASSET_VALUES);
  u64 asset_value = repaired ?
      (u64)kCityToolCosts[CITY_TOOL_GROUP_RAIL * CITY_TOOL_VARIANT_CAPACITY + CITY_RAIL_SUBWAY] *
          city->infrastructure.subway_tiles : 0 - (u32)(i16)city->infrastructure.subway_tiles;

  for (int statistic = TSTAT_POWER_LINE; statistic <= TSTAT_SUBWAY_TO_RAIL_LAST; ++statistic) {
    int count = statistics->value[statistic];
    u32 multiplier;
    if (statistic < TSTAT_ROAD)
      multiplier = kCityToolCosts[CITY_TOOL_GROUP_POWER * CITY_TOOL_VARIANT_CAPACITY + CITY_POWER_LINES];
    else if (statistic < TSTAT_RAIL)
      multiplier = kCityToolCosts[CITY_TOOL_GROUP_ROAD * CITY_TOOL_VARIANT_CAPACITY + CITY_ROAD_ROAD];
    else if (statistic < TSTAT_TUNNEL_ENTRANCE)
      multiplier = kCityToolCosts[CITY_TOOL_GROUP_RAIL * CITY_TOOL_VARIANT_CAPACITY + CITY_RAIL_RAIL];
    else if (statistic < TSTAT_ROAD_BRIDGE)
      multiplier = 15;
    else if (statistic < TSTAT_HIGHWAY_INCLINE)
      multiplier = 100;
    else if (statistic < TSTAT_SUBWAY_TO_RAIL)
      multiplier = repaired ?
          kCityToolCosts[CITY_TOOL_GROUP_ROAD * CITY_TOOL_VARIANT_CAPACITY + CITY_ROAD_HIGHWAY] : 100;
    else
      multiplier = 250;
    asset_value += (u64)count * multiplier;
  }

  for (int i = 0; i < (int)(sizeof(kAssetValues) / sizeof(kAssetValues[0])); ++i) {
    const CityAssetValueRule *rule = &kAssetValues[i];
    int statistic = rule->statistic;
    int divisor = statistic == TK_WATER_TREATMENT_PLANT &&
        (city->fixes & (1ull << CITY_FIX_TREATMENT_VALUE)) ? 4 : rule->divisor;
    u32 multiplier = rule->multiplier;
    if (repaired && IsTileKindPowerPlant(statistic))
      multiplier = kCityBuildings[statistic].cost;
    int count = statistics->value[statistic] / divisor;
    asset_value += (u64)count * multiplier;
  }
  for (int statistic = TK_PLYMOUTH_ARCOLOGY; statistic <= TK_LAUNCH_ARCOLOGY; ++statistic) {
    int count = statistics->value[statistic] / 16;
    u32 cost = repaired ? kCityBuildings[statistic].cost :
        kStockArcologyValues[statistic - TK_PLYMOUTH_ARCOLOGY];
    asset_value += (u64)count * cost;
  }

  // Disabling corrected asset values retains the stock 32-bit result.
  return repaired ? asset_value : (u32)asset_value;
}

// Store the valuation derived from the City's current tile statistics. Requires initialized statistics; read-only
// credit queries use CalculateCityAssetValue instead of changing this cached value.
void RecalculateCityAssetValue(City *city) {
  city->indicators.asset_value = CalculateCityAssetValue(city);
}

// Read live asset values without changing City during command admission or UI inspection. Returns the score derived
// from live debt/assets without refreshing the cached valuation or changing borrowing state.
static int CityBondCreditScore(const City *city) {
  u64 assets = CalculateCityAssetValue(city);
  if (assets != UINT64_MAX) ++assets;
  return (2500 * (u64)city->indicators.bond_count) / assets;
}

// Check the live bond count and credit score without updating the City's valuation cache. Returns admission only;
// issuing the bond and its cash effects require a command.
static bool CanCityIssueBond(const City *city) {
  return city->indicators.bond_count < 50 && CityBondCreditScore(city) < 6;
}

// sc2kfix debt-to-assets premium. A zero valuation still has a denominator of one. Returns an integer rate premium
// without mutation; a zero asset valuation uses the explicit minimum denominator.
static int CityBondRatePremium(const City *city) {
  u64 assets = city->indicators.asset_value;
  if (assets != UINT64_MAX) ++assets;
  return (25000 * (u64)city->indicators.bond_count) / assets;
}

// Compute the current borrowing rate from the federal rate and credit premium. Returns a percentage clamped to 1
// through 100 without mutating City.
int CityBondInterestRate(const City *city) {
  return IntClamp(city->indicators.federal_interest_rate + CityBondRatePremium(city) + 1, 1, 100);
}

// Return the displayed credit grade for the same live score used by bond admission. The result is a borrowed immutable
// literal and must not be freed or modified.
const char *CityBondCreditRating(const City *city) {
  // The displayed grade and borrowing eligibility use the same credit score.
  static const char *const kGrades[] = {"AAA", "AA", "A", "BBB", "BB", "B", "CCC", "CC", "C", "D"};
  return kGrades[IntClamp(CityBondCreditScore(city), 0, 9)];
}

// Compute the aggregate rate metric for outstanding bonds with stock fixed-width arithmetic. Requires at least one
// bond so the bond-count division is defined.
static int BondAverageRate(const City *city) {
  u32 bits = 10000 * (u32)city->indicators.bond_rate_total;
  return (int)bits / (int)city->indicators.bond_count;
}

// Validated command effects, also shared by the retained diagnostic callers. Caller must establish admission and
// available bond capacity; rate is the agreed captured percentage and the cash/debt effects run once.
void ApplyCityBondIssue(City *city, int rate) {
  city->indicators.cash = AddInt64Clamped(city->indicators.cash, 10000);
  city->economic_tables.bond_rates.rate[city->indicators.bond_count] = rate;
  city->indicators.bond_rate_total += rate;
  ++city->indicators.bond_count;
  city->economy.sector[CITY_ECONOMY_BONDS].current_amount = city->indicators.bond_count;
  city->economy.sector[CITY_ECONOMY_BONDS].current_rate = BondAverageRate(city);
}

// Debit one bond principal, remove the supplied oldest rate and compact the rate queue. Admission, sufficient funds
// and the captured rate must already be established by the caller.
void ApplyCityBondRedemption(City *city, int rate) {
  city->indicators.cash = AddInt64Clamped(city->indicators.cash, -10000);
  city->indicators.bond_rate_total -= rate;
  --city->indicators.bond_count;
  for (int index = 0; index < (int)city->indicators.bond_count; ++index)
    city->economic_tables.bond_rates.rate[index] = city->economic_tables.bond_rates.rate[index + 1];
  city->economy.sector[CITY_ECONOMY_BONDS].current_amount = city->indicators.bond_count;
  city->economy.sector[CITY_ECONOMY_BONDS].current_rate =
      city->indicators.bond_count != 0 ? BondAverageRate(city) : 0;
}

// Issue one admitted bond using the resolved confirmation policy. TEST checks live eligibility without changing
// valuation, cash or debt; execution preserves rejection and the original bond effect contract.
i64 CmdIssueBond(City *city, u32 flags, TileIndex location,
    int parameter1, int parameter2, int parameter3) {
  CityCommand recorded_command = EncodeCityCommand(city, CITY_COMMAND_ISSUE_BOND, location,
      parameter1, parameter2, parameter3);
  int invalid = CITY_COMMAND_COST_INVALID;
  if (parameter1 < -32768 || parameter1 > 32767 ||
      (parameter2 != CITY_BOND_CONFIRMED && parameter2 != CITY_BOND_ASK) ||
      parameter3 != 0)
    return invalid;
  if (!(flags & CITY_COMMAND_TEST) && city->game->observation && city->game->observation->execute_bond_command)
    return city->game->observation->execute_bond_command(city, &recorded_command);
  if (!CanCityIssueBond(city)) return CITY_COMMAND_COST_BLOCKED;
  if (parameter2 == CITY_BOND_ASK)
    return flags & CITY_COMMAND_TEST ? CITY_COMMAND_NEEDS_CONFIRMATION : CITY_COMMAND_COST_REJECTED;
  if (flags & CITY_COMMAND_TEST) return 0;
  ApplyCityBondIssue(city, city->fixes & (1ull << CITY_FIX_BOND_RATING) ? CityBondInterestRate(city) : parameter1);
  return 1;
}

// Repay the oldest admitted bond using the resolved confirmation policy. TEST checks funds and availability without
// mutation; execution retains the captured rate across any supported confirmation continuation.
i64 CmdRedeemBond(City *city, u32 flags, TileIndex location,
    int parameter1, int parameter2, int parameter3) {
  CityCommand recorded_command = EncodeCityCommand(city, CITY_COMMAND_REDEEM_BOND, location,
      parameter1, parameter2, parameter3);
  int invalid = CITY_COMMAND_COST_INVALID;
  if (parameter1 != 0 ||
      (parameter2 != CITY_BOND_CONFIRMED && parameter2 != CITY_BOND_ASK) || parameter3 != 0 ||
      city->indicators.bond_count == 0)
    return invalid;
  if (!(flags & CITY_COMMAND_TEST) && city->game->observation && city->game->observation->execute_bond_command)
    return city->game->observation->execute_bond_command(city, &recorded_command);
  if (city->indicators.cash < 10000) return CITY_COMMAND_COST_TOO_EXPENSIVE;
  if (parameter2 == CITY_BOND_ASK)
    return flags & CITY_COMMAND_TEST ? CITY_COMMAND_NEEDS_CONFIRMATION : CITY_COMMAND_COST_REJECTED;
  if (flags & CITY_COMMAND_TEST) return 10000;
  ApplyCityBondRedemption(city, city->economic_tables.bond_rates.rate[0]);
  return 1;
}
