#include "city/economy/economy.h"

#include <string.h>

#include "base/integer.h"
#include "base/fixed.h"
#include "city/economy/budget.h"
#include "city/economy/facilities.h"
#include "city/economy/history.h"
#include "city/economy/tables.h"
#include "city/economy/types.h"
#include "city/engine/city_types.h"
#include "city/engine/random.h"
#include "city/engine/weather.h"
#include "city/events/news.h"
#include "city/maps/layers.h"
#include "city/maps/map_types.h"
#include "city/maps/water.h"
#include "city/maps/tile_storage.h"
#include "city/maps/trees.h"
#include "city/presentation.h"
#include "city/state.h"
#include "game/audio.h"

// Reset the current economy for new-city setup without changing treasury, neighbor accounts or calendar state.
// Only month zero is initialized; later monthly records retain their existing values until normal bookkeeping.
void InitializeNewCityEconomy(City *city) {
  static const u8 kDefaultRates[CITY_ECONOMY_SECTOR_COUNT] = {
    [CITY_ECONOMY_RESIDENTIAL] = 7,
    [CITY_ECONOMY_COMMERCIAL] = 7,
    [CITY_ECONOMY_INDUSTRIAL] = 7,
    [CITY_ECONOMY_ORDINANCES] = 1,
    [CITY_ECONOMY_BONDS] = 0,
    [CITY_ECONOMY_POLICE] = 100,
    [CITY_ECONOMY_FIRE] = 100,
    [CITY_ECONOMY_HEALTH] = 100,
    [CITY_ECONOMY_SCHOOLS] = 100,
    [CITY_ECONOMY_COLLEGES] = 100,
    [CITY_ECONOMY_ROADS] = 100,
    [CITY_ECONOMY_HIGHWAYS] = 100,
    [CITY_ECONOMY_BRIDGES] = 100,
    [CITY_ECONOMY_RAIL] = 100,
    [CITY_ECONOMY_SUBWAY] = 100,
    [CITY_ECONOMY_TUNNELS] = 100,
  };
  memset(city->economic_tables.sector_rates.offsets.value, 0, sizeof(city->economic_tables.sector_rates.offsets.value));
  for (int sector = 0; sector < CITY_INDUSTRY_COUNT; ++sector) {
    city->industry_demand.value[sector] = 0;
    city->industry_baseline.value[sector] = 7;
    city->industry_accounts.value[sector] = 0;
  }
  memset(city->economic_tables.bond_rates.rate, 0, sizeof(city->economic_tables.bond_rates.rate));
  city->indicators.bond_rate_total = 0;
  city->indicators.bond_count = 0;
  for (int sector = 0; sector < CITY_ECONOMY_SECTOR_COUNT; ++sector) {
    EconomySector *economy = &city->economy.sector[sector];
    u32 default_rate = kDefaultRates[sector];
    economy->monthly_amount[0] = 0;
    economy->monthly_rate[0] = default_rate;
    economy->current_amount = 0;
    economy->current_rate = default_rate;
    economy->accumulated = 0;
    economy->projected_annual_total = 0;
  }
}

// Apply the January settlement gate and prepare the year's budget bookkeeping. Returns false outside the due January
// boundary; a true result means the caller must continue the settlement sequence once.
bool BeginCityAnnualSettlement(City *city) {
  if (city->clock.month_word != CITY_MONTH_JANUARY || !city->indicators.year_flag) return false;
  CityEconomyMonths *economy = &city->economy;
  for (int sector = 0; sector < CITY_ECONOMY_SECTOR_COUNT; ++sector) {
    EconomySector *account = &economy->sector[sector];
    int divisor = 12 * kCityEconomySectorDivisors[sector];
    if (divisor != 0)
      city->indicators.cash = AddInt64Clamped(city->indicators.cash, account->accumulated / divisor);
    account->accumulated = 0;
  }
  city->indicators.year_flag = 0;
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  return true;
}

// Retained monthly bookkeeping, annual settlement and ranked policy event 41. Call once at the monthly boundary;
// warning callbacks retain the caller and completion must use live month/rates afterward.
void UpdateMonthlyEconomyBookkeeping(City *city) {
  if (BeginCityAnnualSettlement(city)) {
    AdvanceAnnualFacilityState(city);
    if (PrepareCityFiscalWarning(city)) ShowCityMessage(city, CITY_NOTICE_FISCAL_CRISIS);
  }
  FinishCityMonthlyEconomy(city);
}

// Disable automatic budgeting when negative cash requires a fiscal warning. Returns true only when that transition
// occurs and requests drawing refresh; presentation and dismissal belong to the caller.
bool PrepareCityFiscalWarning(City *city) {
  if (city->indicators.cash >= 0 || !city->indicators.auto_budget_enabled) return false;
  city->indicators.auto_budget_enabled = 0;
  RefreshCityDrawing(city);
  return true;
}

// Counter groups can fund more than one network: crossings and tunnel mouths
// intentionally occur in separate sectors. Ranges within each sector are disjoint.
static const struct { u8 sector, first, last; } kMaintenanceRanges[] = {
  {CITY_ECONOMY_ROADS, TSTAT_ROAD, TSTAT_ROAD_LAST},
  {CITY_ECONOMY_ROADS, TSTAT_TUNNEL_ENTRANCE, TSTAT_ROAD_RAIL_CROSSING_LAST},
  {CITY_ECONOMY_ROADS, TSTAT_HIGHWAY_ROAD, TSTAT_HIGHWAY_ROAD_LAST},
  {CITY_ECONOMY_ROADS, TSTAT_HIGHWAY_ENTRANCE, TSTAT_HIGHWAY_ENTRANCE_LAST},
  {CITY_ECONOMY_RAIL, TSTAT_RAIL, TSTAT_RAIL_LAST},
  {CITY_ECONOMY_RAIL, TSTAT_ROAD_RAIL_CROSSING, TSTAT_RAIL_POWER_CROSSING_LAST},
  {CITY_ECONOMY_RAIL, TSTAT_HIGHWAY_RAIL, TSTAT_HIGHWAY_RAIL_LAST},
  {CITY_ECONOMY_RAIL, TSTAT_SUBWAY_TO_RAIL, TSTAT_SUBWAY_TO_RAIL_LAST},
  {CITY_ECONOMY_BRIDGES, TSTAT_ROAD_BRIDGE, TSTAT_WATER_POWER_LINE},
  {CITY_ECONOMY_BRIDGES, TSTAT_HIGHWAY_BRIDGE, TSTAT_HIGHWAY_BRIDGE_LAST},
  {CITY_ECONOMY_HIGHWAYS, TSTAT_HIGHWAY, TSTAT_HIGHWAY_LAST},
  {CITY_ECONOMY_HIGHWAYS, TSTAT_HIGHWAY_INCLINE, TSTAT_HIGHWAY_BRIDGE_LAST},
  {CITY_ECONOMY_TUNNELS, TSTAT_TUNNEL_ENTRANCE, TSTAT_TUNNEL_ENTRANCE_LAST},
};

// Reload live month, rates and amounts after any settlement warning. Requires current settlement state; updates
// monthly records once and must not replay earlier annual effects.
void FinishCityMonthlyEconomy(City *city) {
  int month = city->clock.month_word;
  CityEconomyMonths *economy = &city->economy;
  const CityStatistics *statistics = &city->statistics;
  for (int sector = 0; sector < CITY_ECONOMY_SECTOR_COUNT; ++sector) {
    EconomySector *account = &economy->sector[sector];
    int amount = account->current_amount;
    int rate = account->current_rate;
    account->monthly_amount[month] = amount;
    account->monthly_rate[month] = rate;
    account->accumulated += rate * amount;
  }
  if (month == CITY_MONTH_DECEMBER)
    city->indicators.year_flag = 1;
  economy->sector[CITY_ECONOMY_BONDS].current_amount = city->indicators.bond_count;
  // Each service is funded per complete facility footprint, independent of sector or tile-kind ordering.
  static const struct { u8 sector, kind, tiles_per_facility; } kServiceMaintenance[] = {
    {CITY_ECONOMY_POLICE, TK_POLICE_STATION, 9},
    {CITY_ECONOMY_FIRE, TK_FIRE_STATION, 9},
    {CITY_ECONOMY_HEALTH, TK_HOSPITAL, 9},
    {CITY_ECONOMY_SCHOOLS, TK_SCHOOL, 9},
    {CITY_ECONOMY_COLLEGES, TK_COLLEGE, 16},
  };
  for (size_t i = 0; i < sizeof(kServiceMaintenance) / sizeof(*kServiceMaintenance); ++i) {
    int count = statistics->value[kServiceMaintenance[i].kind];
    economy->sector[kServiceMaintenance[i].sector].current_amount = count / kServiceMaintenance[i].tiles_per_facility;
  }
  for (int sector = CITY_ECONOMY_ROADS; sector < CITY_ECONOMY_SECTOR_COUNT; ++sector)
    economy->sector[sector].current_amount = 0;
  for (size_t range = 0; range < sizeof(kMaintenanceRanges) / sizeof(*kMaintenanceRanges); ++range) {
    EconomySector *account = &economy->sector[kMaintenanceRanges[range].sector];
    for (int statistic = kMaintenanceRanges[range].first; statistic <= kMaintenanceRanges[range].last; ++statistic)
      account->current_amount += statistics->value[statistic];
  }
  economy->sector[CITY_ECONOMY_SUBWAY].current_amount =
      (int)city->infrastructure.subway_tiles + statistics->value[TK_SUBWAY_STATION];
  economy->sector[CITY_ECONOMY_RAIL].current_amount += statistics->value[TK_RAIL_STATION];
  int bus_station_tiles = statistics->value[TK_BUS_STATION];
  economy->sector[CITY_ECONOMY_ROADS].current_amount += 250 * (bus_station_tiles / 4);
  economy->sector[CITY_ECONOMY_ORDINANCES].current_amount = 0;
  u32 options = city->unlocks.ordinance_flags;
  for (int index = 0; index < CITY_ORDINANCE_COUNT; ++index) {
    if (options & (1u << index))
      economy->sector[CITY_ECONOMY_ORDINANCES].current_amount += CityPolicyMonthlyCost(city, index);
  }
  if (city->indicators.event_flag == 0) {
    u32 draw = Sc2kCrtRand(city);
    if ((draw & 7) == 0) {
      draw = Sc2kCrtRand(city) + 50000;
      if ((int)draw < city->indicators.cash) {
        u32 policy = Sc2kCrtRandRange(city, CITY_ORDINANCE_COUNT);
        city->unlocks.ordinance_flags |= 1u << policy;
        InsertRankedCityEvent(city, CITY_NEWS_ORDINANCE_PASSED, policy);
      }
    }
  }
  InvalidateCityInformation(city->game, CITY_UPDATE_FINANCES | CITY_UPDATE_POLICIES);
}

// Update the primary economy accounts. Runs the primary economy path through any milestone notice and its real tail;
// this is a simulation update, not a preview.
void UpdateCityEconomyPrimary(City *city) {
  CityMilestoneNotice notice = BeginCityEconomyPrimary(city);
  if (notice.show) ShowRankedCityEventModal(city, notice.event, notice.data);
  FinishCityEconomyPrimary(city, notice.milestone);
}

// Apply primary population/economy updates and select any due milestone newspaper. Returns the captured milestone
// notice for the caller's continuation; this is a simulation effect and must not be repeated after presentation.
CityMilestoneNotice BeginCityEconomyPrimary(City *city) {
  MaybePlayCityMusic(city->game, false);
  CityPopulationSectors *population_state = &city->population;
  CityIndustrySectors *sector_state = &city->industry_sectors;
  CityEconomyMonths *economy_state = &city->economy;
  CityTileCounts *tile_counts = &city->statistics;
  u32 old_residential = city->indicators.sector_population_total;
  u32 total = 0;
  for (int i = CITY_POPULATION_RESIDENTIAL_LOW; i <= CITY_POPULATION_INDUSTRIAL_HIGH; ++i)
    total += population_state->value[i];
  population_state->value[CITY_POPULATION_TOTAL] = total;
  u32 old_total = city->indicators.population;
  u32 new_total = 10 * total;
  city->indicators.birth_count = 0;
  city->indicators.attrition_count = 0;
  if (new_total >= old_total)
    city->indicators.attrition_count = new_total - old_total;
  else
    city->indicators.birth_count = old_total - new_total;
  city->indicators.population = new_total;
  city->indicators.population_cumulative += new_total;
  for (int i = 0; i < CITY_ECONOMY_TAX_SECTOR_COUNT; ++i)
    sector_state->value[i] = population_state->value[2 * i + 1] + population_state->value[2 * i + 2];
  city->indicators.sector_population_total = sector_state->value[CITY_ECONOMY_RESIDENTIAL];
  for (int i = 0; i < CITY_ECONOMY_TAX_SECTOR_COUNT; ++i)
    economy_state->sector[i].current_amount = 10 * sector_state->value[i];
  int arcology_population = (int)city->indicators.arcology_population;
  economy_state->sector[CITY_ECONOMY_RESIDENTIAL].current_amount += arcology_population / 6;
  for (int i = 1; i < CITY_ECONOMY_TAX_SECTOR_COUNT; ++i)
    economy_state->sector[i].current_amount += arcology_population / 12;
  i64 jobs = 0;
  for (int i = CITY_POPULATION_COMMERCIAL_LOW; i <= CITY_POPULATION_INDUSTRIAL_HIGH; ++i)
    jobs += population_state->value[i];
  Fixed demand[CITY_ECONOMY_TAX_SECTOR_COUNT];
  u32 industrial_population = sector_state->value[CITY_ECONOMY_INDUSTRIAL];
  demand[CITY_ECONOMY_RESIDENTIAL] = FixedFromInt(
      (int)sector_state->value[CITY_ECONOMY_RESIDENTIAL] / 50 + jobs);
  i64 commercial_base = (i64)(new_total + 50000) * (int)old_residential;
  commercial_base = MulDiv(commercial_base, FIXED_ONE, 150000);
  demand[CITY_ECONOMY_COMMERCIAL] = MulDiv(commercial_base, industrial_population, jobs + 1);
  int policy_percent = city->indicators.industrial_mix_bonus +
      kDifficultyPolicyPercent[city->indicators.difficulty];
  demand[CITY_ECONOMY_INDUSTRIAL] = MulDiv(
      FixedFromInt((int)old_residential) * policy_percent, industrial_population, (jobs + 1) * 100);
  demand[CITY_ECONOMY_INDUSTRIAL] = FixedMax(demand[CITY_ECONOMY_INDUSTRIAL], 15 * FIXED_ONE);
  int amenities = tile_counts->count[TK_LARGE_PARK] / 3 + tile_counts->count[TK_STADIUM] +
      tile_counts->count[TK_ZOO] + tile_counts->count[TK_MARINA] + 10;
  i64 capacity[CITY_ECONOMY_TAX_SECTOR_COUNT];
  capacity[CITY_ECONOMY_RESIDENTIAL] = (int)(150 * (u32)amenities);
  i64 commercial_capacity = 4 * sector_state->value[CITY_ECONOMY_COMMERCIAL] + 500;
  if (capacity[CITY_ECONOMY_RESIDENTIAL] > commercial_capacity)
    capacity[CITY_ECONOMY_RESIDENTIAL] = commercial_capacity;
  int airports = (tile_counts->count[TK_RUNWAY_FIRST] + tile_counts->count[TK_RUNWAY_LAST] +
                     (int)city->infrastructure.service_bonus_b) /
      5;
  capacity[CITY_ECONOMY_COMMERCIAL] = (int)(375 * (4 * (u32)airports + 4));
  int seaports = tile_counts->count[TK_CRANE] + (int)city->infrastructure.service_bonus_a + 1;
  capacity[CITY_ECONOMY_INDUSTRIAL] = (int)(1500 * (u32)seaports);
  for (int i = 0; i < CITY_ECONOMY_TAX_SECTOR_COUNT; ++i) {
    demand[i] = FixedMin(demand[i], FixedFromInt(capacity[i]));
  }
  u32 options = city->unlocks.ordinance_flags;
  for (int i = 0; i < CITY_ECONOMY_TAX_SECTOR_COUNT; ++i) {
    u32 rate_bits = economy_state->sector[i].current_rate;
    if (i == CITY_ECONOMY_RESIDENTIAL) {
      rate_bits += (options & CITY_POLICY_FLAG_INCOME_TAX) != 0;
      rate_bits -= (options & CITY_POLICY_FLAG_CITY_BEAUTIFICATION) != 0;
    } else if (i == CITY_ECONOMY_COMMERCIAL) {
      rate_bits += (options & CITY_POLICY_FLAG_SALES_TAX) != 0;
      rate_bits -= (options & CITY_POLICY_FLAG_TOURIST_ADVERTISING) != 0;
      rate_bits -= (options & CITY_POLICY_FLAG_ANNUAL_CARNIVAL) != 0;
      rate_bits -= (options & CITY_POLICY_FLAG_HOMELESS_SHELTERS) != 0;
    } else {
      rate_bits -= (options & CITY_POLICY_FLAG_BUSINESS_ADVERTISING) != 0;
      rate_bits += (options & CITY_POLICY_FLAG_POLLUTION_CONTROLS) != 0;
    }
    int adjustment = kEconomyRateAdjustments[IntMax((int)rate_bits, 0)];
    i64 denominator = ((i64)sector_state->value[i] + 1) * FIXED_ONE;
    int scaled = (600 * demand[i] + (adjustment - 600) * denominator) / denominator;
    // Stock adds in AX and clamps the signed stored word.
    city->economic_tables.sector_rates.offsets.value[i] += (u32)scaled;
    int new_rate = city->economic_tables.sector_rates.offsets.value[i];
    city->economic_tables.sector_rates.offsets.value[i] = IntClamp(new_rate, -2000, 2000);
  }

  int y = Sc2kCrtRand(city) & (city->map_height - 1);
  int x = Sc2kCrtRand(city) & (city->map_width - 1);
  TileIndex tile_index = MapTileXY(city, x, y);
  TileInfo *tp = &city->tile[tile_index];
  u32 old_tile = tp->kind;
  if (old_tile == TK_RADIOACTIVE_WASTE && (Sc2kCrtRand(city) & 0xF) == 0) {
    ReplaceCityTileAndStatistics(city, tile_index,
        ReadCityWaterGroundKind(tp));
  }
  GrowCityStockTrees(city, tile_index);
  DecayCityNewsRankings(city);
  InsertRankedCityEvent(city, CITY_NEWS_LOCAL_NEWS, 0);
  {
    u32 selection = Sc2kCrtRandRange(city, 6);
    if (selection == 0) {
      if ((Sc2kCrtRand(city) & 3) == 0) {
        InsertRankedCityEvent(city, CITY_NEWS_INTERNATIONAL_NEWS, 0);
      }
      if ((Sc2kCrtRand(city) & 3) == 0) {
        InsertRankedCityEvent(city, CITY_NEWS_MARKET, city->indicators.national_economy_trend);
      }
    } else {
      InsertRankedCityEvent(city, selection + CITY_NEWS_RANDOM_ARTICLE_OFFSET, 0);
    }
  }
  {
    if (city->statistics.count[TK_STADIUM] > 0) {
      u32 category = Sc2kCrtRandRange(city, CITY_TEAM_NAME_COUNT);
      if (city->infrastructure.stadium_team_mask & (1u << category)) {
        InsertRankedCityEvent(city, CITY_NEWS_SPORTS, category);
      }
    }
    static const struct { u8 series, high, low; } kHistoryMetricNews[] = {
      {CITY_HISTORY_TRAFFIC, CITY_NEWS_HIGH_TRAFFIC, CITY_NEWS_LOW_TRAFFIC},
      {CITY_HISTORY_POLLUTION, CITY_NEWS_HIGH_POLLUTION, CITY_NEWS_LOW_POLLUTION},
      {CITY_HISTORY_CRIME, CITY_NEWS_HIGH_CRIME, CITY_NEWS_LOW_CRIME},
    };
    for (size_t i = 0; i < sizeof(kHistoryMetricNews) / sizeof(*kHistoryMetricNews); ++i) {
      int threshold = (int)city->history[kHistoryMetricNews[i].series].sample[0];
      if ((int)(Sc2kCrtRand(city) & 0x7F) < threshold) {
        InsertRankedCityEvent(city, kHistoryMetricNews[i].high, 0);
      }
      if ((int)(Sc2kCrtRand(city) & 0xF) > threshold) {
        InsertRankedCityEvent(city, kHistoryMetricNews[i].low, 0);
      }
    }
    int unemployment = (int)city->indicators.unemployment;
    if ((int)(Sc2kCrtRand(city) & 0x3F) < unemployment) {
      InsertRankedCityEvent(city, CITY_NEWS_HIGH_UNEMPLOYMENT, 0);
    }
    if ((int)(Sc2kCrtRand(city) & 3) > unemployment) {
      InsertRankedCityEvent(city, CITY_NEWS_LOW_UNEMPLOYMENT, 0);
    }
  }
  {
    u32 metric = city->indicators.workforce_education;
    u32 draw = Sc2kCrtRandRange(city, 80);
    if (metric < 80 ? draw > metric : draw < metric - 80) {
      InsertRankedCityEvent(city, metric < 80 ? CITY_NEWS_LOW_EDUCATION : CITY_NEWS_HIGH_EDUCATION, 0);
    }
    metric = city->indicators.workforce_life_expectancy;
    draw = Sc2kCrtRandRange(city, 60);
    if (metric < 60 ? draw > metric : draw < metric - 60) {
      InsertRankedCityEvent(city, metric < 60 ? CITY_NEWS_POOR_HEALTH : CITY_NEWS_GOOD_HEALTH, 0);
    }
  }
  if ((Sc2kCrtRand(city) & 7) == 0) {
    int current_year = city->geography.start_year + (int)(city->clock.tick / CITY_TICKS_PER_YEAR);
    for (int milestone = 0; milestone < CITY_MILESTONE_COUNT; ++milestone) {
      int due = city->economic_tables.milestone_years.due_year[milestone];
      if (due != 0 && current_year >= due) {
        u32 event = milestone < CITY_MILESTONE_BUILDING_FIRST ? CITY_NEWS_INVENTION : CITY_NEWS_BUILDING_AVAILABLE;
        u32 event_arg =
            milestone < CITY_MILESTONE_BUILDING_FIRST ? milestone : milestone - CITY_MILESTONE_BUILDING_FIRST;
        bool show = PrepareRankedCityEvent(city, event, event_arg);
        return (CityMilestoneNotice){.milestone = milestone, .event = event, .data = event_arg, .show = show};
      }
    }
  }
  return (CityMilestoneNotice){.milestone = -1};
}

// A selected milestone clears after its newspaper, even if nested commands changed its due year. Milestone must be the
// captured admitted notice index; subsequent weather/economy effects read live state after dismissal.
void FinishCityEconomyPrimary(City *city, int milestone) {
  if (milestone >= 0) {
    city->economic_tables.milestone_years.due_year[milestone] = 0;
    RebuildCityUnlockState(city);
  }
  AdvanceCityWeather(city);
}

// Scale a signed demand by an integer percentage with a widened intermediate. Division truncates toward zero; callers
// must keep the resulting demand within int range.
static int ScaleDemand(int value, int percent) { return (i64)value * percent / 100; }

// Compute the rate-scaled capacity of complete facility groups. Divisor must be nonzero; the unsigned product
// intentionally retains the simulation's fixed-width arithmetic before signed division.
static int AgeFacilityCapacity(u32 rate, int tile_count, int divisor, int scale) {
  int product = (int)(rate * (u32)(tile_count / divisor) * scale);
  return product / 100;
}

// Apply policy, education and population-growth modifiers to industry deficits. Deficits must contain the complete
// industry array; edits are in place and consume no random values.
static void AdjustIndustryDemand(int *deficits, u32 flags, u32 education, bool population_growth) {
  if ((flags & CITY_POLICY_FLAG_POLLUTION_CONTROLS) != 0)
    for (int i = 0; i < 4; ++i) {
      static const u8 kPollutingIndustries[] = {
          CITY_INDUSTRY_STEEL_MINING, CITY_INDUSTRY_TEXTILES, CITY_INDUSTRY_PETROCHEMICAL, CITY_INDUSTRY_AUTOMOTIVE};
      int industry = kPollutingIndustries[i];
      deficits[industry] = ScaleDemand(deficits[industry], CITY_DEMAND_GROWTH_PERCENT);
    }
  if (population_growth)
    deficits[CITY_INDUSTRY_CONSTRUCTION] =
        ScaleDemand(deficits[CITY_INDUSTRY_CONSTRUCTION], CITY_DEMAND_MODERATE_PERCENT);
  if (education > 130 || education < 60) {
    int factor = education > 130 ? CITY_DEMAND_HIGH_PERCENT : CITY_DEMAND_LOW_PERCENT;
    deficits[CITY_INDUSTRY_AEROSPACE] = ScaleDemand(deficits[CITY_INDUSTRY_AEROSPACE], factor);
    deficits[CITY_INDUSTRY_ELECTRONICS] = ScaleDemand(deficits[CITY_INDUSTRY_ELECTRONICS], factor);
  } else if (education > 100) {
    for (int j = 0; j < 6; ++j) {
      static const u8 kEducationIndustries[6] = {CITY_INDUSTRY_PETROCHEMICAL, CITY_INDUSTRY_AUTOMOTIVE,
          CITY_INDUSTRY_FINANCE, CITY_INDUSTRY_MEDIA, CITY_INDUSTRY_AEROSPACE, CITY_INDUSTRY_ELECTRONICS};
      deficits[kEducationIndustries[j]] = ScaleDemand(deficits[kEducationIndustries[j]], CITY_DEMAND_MODERATE_PERCENT);
    }
  }
}

// Convert a yearly percentage product to a monthly signed change. The intermediate is widened before division by 1200;
// callers supply values whose result fits i32.
static i32 EconomyChange(u32 value, int multiplier) { return (i32)((i64)value * multiplier / 1200); }

// Advance accounts, industry demand/allocation and age cohorts from current population and annual metrics.
// Keep the phases in order: later work observes the live state and random streams left by earlier phases.
void UpdateCityEconomySecondary(City *city) {
  {
    int economy_trend = city->indicators.national_economy_trend;
    u32 population = city->indicators.national_population;
    i32 change = EconomyChange(population, economy_trend);
    city->indicators.national_population = population > CITY_ECONOMY_POPULATION_CENTER ?
        population - change : population + change;
    u32 value = city->indicators.national_value;
    change = EconomyChange(value, kPolicyDriftByPolicy[economy_trend]);
    city->indicators.national_value = value > CITY_ECONOMY_VALUE_CENTER ? value - change : value + change;
  }
  if (Sc2kCrtRandRange(city, 10) == 0) {
    i64 score = (u64)city->indicators.national_value * 100 / ((u64)city->indicators.national_population + 1);
    if (Sc2kCrtRandRange(city, 5) < 2) {
      int divisor = 25 * city->indicators.federal_interest_rate;
      int remainder = Sc2kCrtRandRange(city, divisor);
      if (remainder < score) {
        int next = ++city->indicators.federal_interest_rate;
        InsertRankedCityEvent(city, CITY_NEWS_INTEREST_RATE_INCREASE, next);
      }
      divisor = 25 * city->indicators.federal_interest_rate;
      remainder = Sc2kCrtRandRange(city, divisor);
      if (remainder > score) {
        int next = --city->indicators.federal_interest_rate;
        if (next == 0)
          city->indicators.federal_interest_rate = 1;
        else
          InsertRankedCityEvent(city, CITY_NEWS_INTEREST_RATE_DECREASE, next);
      }
    }
    if (Sc2kCrtRandRange(city, 3) == 0) {
      int next = score < 45 ? 0 : score < 60 ? 1 : score < 75 ? 2 : 3;
      if (city->indicators.national_economy_trend != next) {
        city->indicators.national_economy_trend = next;
        InsertRankedCityEvent(city, CITY_NEWS_MARKET, next);
      }
    }
  }
  {
    int economy_trend = city->indicators.national_economy_trend;
    CityAccountQuartet *primary_accounts = &city->primary_accounts;
    CityAccountQuartet *secondary_accounts = &city->secondary_accounts;
    for (int i = 0; i < 4; ++i) {
      u32 population = primary_accounts->value[i];
      if (population == 0)
        continue;
      int multiplier = economy_trend + (int)Sc2kCrtRandRange(city, 3);
      i32 delta = EconomyChange(population, multiplier);
      population = population > CITY_ECONOMY_POPULATION_CENTER ? population - delta : population + delta;
      primary_accounts->value[i] = population;
      if (delta == 0)
        primary_accounts->value[i] = population + (Sc2kCrtRand(city) & 1);
      population = primary_accounts->value[i];
      u32 value = secondary_accounts->value[i];
      i64 ratio = population ? (u64)value * 100 / population : 0;
      int category = ratio < 45 ? 0 : ratio < 60 ? 1 : ratio < 75 ? 2 : 3;
      int coefficient = kPolicyDriftByPolicy[economy_trend] + (int)Sc2kCrtRandRange(city, 5) -
          city->indicators.federal_interest_rate - category;
      i32 value_change = EconomyChange(value, coefficient);
      value = value > CITY_ECONOMY_VALUE_CENTER ? value - value_change : value + value_change;
      secondary_accounts->value[i] = value;
    }
    if ((Sc2kCrtRand(city) & 0x3F) == 0) {
      u32 selected = Sc2kCrtRand(city) & 3;
      u32 population = primary_accounts->value[selected];
      u32 value = secondary_accounts->value[selected];
      primary_accounts->value[selected] = (u64)population * 3 / 4;
      secondary_accounts->value[selected] = value / 2;
    }
  }
  {
    int start_year = city->geography.start_year;
    int elapsed_years = city->clock.tick / CITY_TICKS_PER_YEAR;
    // The start year selects the initial era; only elapsed years supply progress within an era.
    int era = (start_year - CITY_INDUSTRY_WEIGHT_BASE_YEAR) / CITY_INDUSTRY_WEIGHT_INTERVAL_YEARS +
        elapsed_years / CITY_INDUSTRY_WEIGHT_INTERVAL_YEARS;
    int years_into_era = elapsed_years % CITY_INDUSTRY_WEIGHT_INTERVAL_YEARS;
    u32 weights[CITY_INDUSTRY_COUNT];
    for (int industry = 0; industry < CITY_INDUSTRY_COUNT; ++industry) {
      if (era < CITY_INDUSTRY_WEIGHT_ERA_COUNT - 1) {
        u32 before = kIndustryYearWeights[era][industry];
        u32 after = kIndustryYearWeights[era + 1][industry];
        u32 weighted = before * (CITY_INDUSTRY_WEIGHT_INTERVAL_YEARS - years_into_era) + after * years_into_era;
        weights[industry] = weighted / CITY_INDUSTRY_WEIGHT_INTERVAL_YEARS;
      } else {
        weights[industry] = kIndustryYearWeights[CITY_INDUSTRY_WEIGHT_ERA_COUNT - 1][industry];
      }
    }
    for (int entry = 0; entry < CITY_INDUSTRY_COUNT; ++entry) {
      u32 sum = 0;
      for (int i = 0; i < 4; ++i)
        sum += NextCityMapRandomModulo(city, 128);
      int product = sum * weights[entry];
      city->industry_demand.value[entry] = (3 * city->industry_demand.value[entry] + product / 256) / 4;
    }
  }
  {
    CityIndustryAccounts *account_state = &city->industry_accounts;
    CityIndustryDemand *demand_state = &city->industry_demand;
    CityIndustryDemand *baseline_state = &city->industry_baseline;
    u32 total = 0;
    u32 deficit_sum = 0;
    int deficits[CITY_INDUSTRY_COUNT];
    for (int i = 0; i < CITY_INDUSTRY_COUNT; ++i) {
      total += account_state->value[i];
      deficits[i] = demand_state->value[i];
    }
    AdjustIndustryDemand(deficits, city->unlocks.ordinance_flags, city->indicators.workforce_education,
        city->indicators.attrition_count != 0);
    for (int i = 0; i < CITY_INDUSTRY_COUNT; ++i) {
      deficits[i] = IntMax(deficits[i] - baseline_state->value[i], 0);
      deficit_sum += deficits[i];
    }
    u32 target = city->industry_sectors.value[CITY_ECONOMY_INDUSTRIAL];
    if (total <= target) {
      if (deficit_sum != 0) {
        u64 excess = 100 * (u64)(target - total);
        for (int i = 0; i < CITY_INDUSTRY_COUNT; ++i) {
          if (deficits[i] == 0)
            continue;
          u64 scaled = deficits[i] * excess / deficit_sum;
          u32 value = account_state->value[i] + scaled / 100;
          account_state->value[i] = value;
          if (Sc2kCrtRandRange(city, 100) < scaled % 100)
            account_state->value[i] = value + 1;
        }
      }
    } else {
      u64 excess = 100 * (u64)(total - target);
      for (int i = 0; i < CITY_INDUSTRY_COUNT; ++i) {
        u32 value = account_state->value[i];
        u64 scaled = value * excess / total;
        value -= scaled / 100;
        account_state->value[i] = value;
        if (Sc2kCrtRandRange(city, 100) < scaled % 100)
          account_state->value[i] = value - 1;
      }
    }
  }
  {
    u32 denominator = city->industry_sectors.value[CITY_ECONOMY_INDUSTRIAL] + 1;
    CityIndustryAccounts *account_state = &city->industry_accounts;
    u32 aggregate = account_state->value[CITY_INDUSTRY_STEEL_MINING] +
        account_state->value[CITY_INDUSTRY_PETROCHEMICAL] + account_state->value[CITY_INDUSTRY_TEXTILES] +
        account_state->value[CITY_INDUSTRY_AUTOMOTIVE];
    int ratio = (int)UintDiv(100 * aggregate, denominator);
    city->indicators.industrial_pollution_bonus = ratio >= 20 ? (ratio - 20) / 30 : -1;
    u32 peak = 0;
    for (int i = 0; i < CITY_INDUSTRY_COUNT; ++i) {
      u32 current = UintDiv(100 * account_state->value[i], denominator);
      if (current > peak)
        peak = current;
    }
    city->indicators.industrial_mix_bonus = (int)peak >= 20 ? ((int)peak - 20) / 5 : 0;
  }

  CityAgeCohorts *counts = &city->age_cohorts;
  CityAgeCohorts *education = &city->age_metric_a;
  CityAgeCohorts *life_expectancy = &city->age_metric_b;
  if (city->indicators.population == 0) {
    memset(counts, 0, sizeof(*counts));
    memset(education, 0, sizeof(*education));
    memset(life_expectancy, 0, sizeof(*life_expectancy));
    return;
  }
  const CityEconomyMonths *economy = &city->economy;
  const CityTileCounts *tiles = &city->statistics;

  int school_capacity = AgeFacilityCapacity(
      economy->sector[CITY_ECONOMY_SCHOOLS].current_rate, tiles->value[TK_SCHOOL], 9, 15);
  int college_capacity = AgeFacilityCapacity(
      economy->sector[CITY_ECONOMY_COLLEGES].current_rate, tiles->value[TK_COLLEGE], 16, 50);
  int health_capacity = AgeFacilityCapacity(
      economy->sector[CITY_ECONOMY_HEALTH].current_rate, tiles->value[TK_HOSPITAL], 9, 25);
  int newborn_life_expectancy = (city->unlocks.ordinance_flags & CITY_POLICY_FLAG_ANTI_DRUG) ? 90 : 85;
  if (city->unlocks.ordinance_flags & CITY_POLICY_FLAG_CPR_TRAINING)
    newborn_life_expectancy += 5;
  if (city->unlocks.ordinance_flags & CITY_POLICY_FLAG_PUBLIC_SMOKING_BAN)
    newborn_life_expectancy += 5;
  if (city->unlocks.ordinance_flags & CITY_POLICY_FLAG_FREE_CLINICS)
    health_capacity += economy->sector[CITY_ECONOMY_RESIDENTIAL].current_amount / 400;

  for (int index = 1, age = 5; index < CITY_AGE_COHORT_COUNT; ++index, age += 5) {
    u32 count = counts->value[index];
    if (count == 0)
      continue;
    int survival_ratio = (int)(100 * (life_expectancy->value[index] / count)) / age;
    if (survival_ratio >= 100)
      continue;
    u32 scaled = count * (100 - survival_ratio) / 24;
    u32 deaths = scaled / 100;
    if (Sc2kCrtRandRange(city, 100) < scaled % 100)
      ++deaths;
    deaths = UintMin(deaths, count);
    if (deaths == 0)
      continue;
    u32 cohort_education = education->value[index];
    u32 education_loss = deaths * cohort_education / count;
    education->value[index] = cohort_education - UintMin(education_loss, cohort_education);
    life_expectancy->value[index] -= deaths * life_expectancy->value[index] / count;
    counts->value[index] -= deaths;
    city->indicators.attrition_count += deaths;
  }

  u32 pollution_denominator = city->indicators.population + 10 * city->population.value[CITY_POPULATION_ABANDONED] + 1;
  int pollution_penalty = IntMin(UintDiv(city->indicators.city_pollution, pollution_denominator), 3);
  for (int source = CITY_AGE_COHORT_COUNT - 2; source >= 0; --source) {
    u32 count = counts->value[source];
    if (count == 0)
      continue;
    u32 moving = count / 60;
    if (Sc2kCrtRandRange(city, 60) < count % 60)
      ++moving;
    moving = UintMin(moving, count);
    if (moving == 0)
      continue;
    u32 education_transfer = moving * education->value[source] / count;
    education->value[source] -= education_transfer;
    if (source < 2)
      education_transfer += 35 * UintMin((u32)school_capacity, moving);
    else if (source == 2)
      education_transfer += (education_transfer * UintMin((u32)college_capacity, moving) / moving) >> 1;
    if (!(city->unlocks.ordinance_flags & CITY_POLICY_FLAG_PRO_READING))
      education_transfer -= moving;
    education->value[source + 1] += education_transfer;
    u32 life_expectancy_transfer = moving * life_expectancy->value[source] / count;
    life_expectancy->value[source] -= life_expectancy_transfer;
    life_expectancy->value[source + 1] += life_expectancy_transfer - pollution_penalty;
    counts->value[source] -= moving;
    counts->value[source + 1] += moving;
  }

  u32 fertile_population = counts->value[4] + counts->value[5] + counts->value[6] + counts->value[7] + counts->value[8];
  u32 births = fertile_population / 300;
  if ((int)Sc2kCrtRandRange(city, 300) < (int)(fertile_population - 300 * (u32)pollution_penalty))
    ++births;
  if (births != 0) {
    u32 protected_births = UintMin((u32)health_capacity, births);
    life_expectancy->value[0] += 35 * births + protected_births * (newborn_life_expectancy - 35);
    education->value[0] += city->indicators.workforce_education * births / 5;
    counts->value[0] += births;
    city->indicators.birth_count += births;
  }

  // Balance the cohort population against the city population after deaths and births.
  if (city->indicators.birth_count < city->indicators.attrition_count) {
    city->indicators.attrition_count -= city->indicators.birth_count;
    u32 step = (city->indicators.attrition_count >> 4) + 1;
    while (city->indicators.attrition_count != 0) {
      for (int index = 4; index < 8 && city->indicators.attrition_count != 0; ++index) {
        u32 added = UintMin(step, city->indicators.attrition_count);
        life_expectancy->value[index] += added * (65 - index);
        education->value[index] += added * (90 - index);
        counts->value[index] += added;
        city->indicators.attrition_count -= added;
      }
      for (int index = 0; index < 12 && city->indicators.attrition_count != 0; ++index) {
        u32 added = UintMin(step, city->indicators.attrition_count);
        life_expectancy->value[index] += added * (65 - index);
        education->value[index] += added * (index < 3 ? 17 + 35 * index : 90 - index);
        counts->value[index] += added;
        city->indicators.attrition_count -= added;
      }
    }
  } else if (city->indicators.birth_count > city->indicators.attrition_count) {
    city->indicators.birth_count -= city->indicators.attrition_count;
    while (city->indicators.birth_count != 0) {
      u32 pass_emigrants = city->indicators.birth_count;
      for (int index = 0; index < CITY_AGE_COHORT_COUNT && city->indicators.birth_count != 0; ++index) {
        u32 count = counts->value[index];
        if (count == 0)
          continue;
        u32 denominator = city->indicators.population + city->indicators.birth_count;
        u32 removed = (u32)((u64)pass_emigrants * count / denominator);
        if (removed == 0)
          removed = (Sc2kCrtRand(city) & 3) == 0;
        removed = UintMin(UintMin(removed, count), city->indicators.birth_count);
        if (removed == 0)
          continue;
        life_expectancy->value[index] -= (u32)((u64)life_expectancy->value[index] * removed / count);
        education->value[index] -= (u32)((u64)education->value[index] * removed / count);
        counts->value[index] -= removed;
        city->indicators.birth_count -= removed;
      }
    }
  }

  u32 working_count = 0;
  u32 workforce_education = 0;
  u32 workforce_life_expectancy = 0;
  for (int index = 4; index < 11; ++index) {
    working_count += counts->value[index];
    workforce_education += education->value[index];
    workforce_life_expectancy += life_expectancy->value[index];
  }
  city->indicators.workforce_life_expectancy = UintDiv(workforce_life_expectancy, working_count);
  city->indicators.workforce_education = UintDiv(workforce_education, working_count);
  city->indicators.workforce_percent = UintDiv(100 * working_count, city->indicators.population + 1);
  InvalidateCityInformation(city->game, CITY_UPDATE_NEIGHBORS | CITY_UPDATE_INDUSTRY |
      CITY_UPDATE_POPULATION | CITY_UPDATE_FINANCES);
}
