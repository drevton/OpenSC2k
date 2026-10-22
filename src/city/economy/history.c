#include "city/economy/economy.h"

#include <limits.h>
#include <string.h>

#include "city/economy/tables.h"
#include "city/engine/city_types.h"
#include "city/maps/layers.h"
#include "city/presentation.h"
#include "city/state.h"

// Chart Data
// Calculation blocks in the original chart painters, without window-system drawing.

// Fill caller-owned plot coordinates and scale from a selected history series and period. Rejects invalid dimensions,
// series, periods and NULL output; a successful plot borrows no City storage.
bool BuildCityHistoryPlot(const City *city, int series, int period, int width, int height, CityHistoryPlot *plot) {
  if (plot == NULL || series < 0 || series >= CITY_HISTORY_SERIES_COUNT || period < 0 ||
      period > CITY_HISTORY_PERIOD_LAST || width < 4 || height < 4 || width > 32767 || height > 32767)
    return false;
  int maximum = (int)city->history_maxima.value[series];
  if (maximum == 0)
    return false;
  int first = period == CITY_HISTORY_PERIOD_YEAR ? 11 : period == CITY_HISTORY_PERIOD_TEN_YEARS ? 31 : 51;
  plot->count = period == CITY_HISTORY_PERIOD_YEAR ? 12 : 20;
  int spacing = period == CITY_HISTORY_PERIOD_YEAR ? 66 : 114;
  for (int index = 0; index < plot->count; ++index) {
    u32 sample = ReadHistorySample(city, series, first - index);
    int product = (int)((u32)(height - 3) * sample);
    if (product == INT_MIN && maximum == -1)
      return false; // Reject division overflow in a malformed graph scale.
    int offset = product / maximum;
    plot->point[index] = (CityChartPoint){(width - 1) * 5 * index / spacing, height - 2 - offset};
  }
  plot->latest = ReadHistorySample(city, series, first - plot->count + 1);
  return true;
}

// Calculate population, health or education values for each age group. NULL outputs and unsupported modes return
// false; success fills caller-owned values without changing simulation metrics.
bool ReadCityDemographicValues(const City *city, int mode, CityDemographicValues *values) {
  if (values == NULL || mode < 0 || mode > CITY_DEMOGRAPHIC_MODE_LAST)
    return false;
  memset(values, 0, sizeof(*values));
  if (city->indicators.population == 0)
    return true;
  values->summary = (mode == CITY_DEMOGRAPHIC_POPULATION ? city->indicators.workforce_percent
          : mode == CITY_DEMOGRAPHIC_HEALTH              ? city->indicators.workforce_life_expectancy
                                                         : city->indicators.workforce_education);
  for (int index = 0; index < 20; ++index) {
    u32 count = city->age_cohorts.value[index];
    u32 value;
    if (mode == CITY_DEMOGRAPHIC_POPULATION) {
      value = (600 * count) / city->indicators.population;
      if (count != 0 && value == 0)
        value = 1;
    } else if (count == 0) {
      value = 0;
    } else if (mode == CITY_DEMOGRAPHIC_HEALTH) {
      value = city->age_metric_b.value[index] / count;
    } else {
      u32 denominator = 25 * count;
      if (denominator == 0)
        return false;
      value = (15 * city->age_metric_a.value[index]) / denominator;
    }
    values->value[index] = value;
  }
  return true;
}

// Read industry balances, tax rates or demand and determine the chart scale. NULL outputs and unsupported modes return
// false; success fills caller-owned balances/scale without applying a tax or demand edit.
bool ReadCityIndustryValues(const City *city, int mode, CityIndustryValues *values) {
  if (values == NULL || mode < 0 || mode > CITY_INDUSTRY_MODE_LAST)
    return false;
  values->maximum = kIndustryGraphMinimum[mode];
  for (int index = 0; index < CITY_INDUSTRY_COUNT; ++index) {
    int value = mode == CITY_INDUSTRY_ACCOUNTS ? (int)city->industry_accounts.value[index]
        : mode == CITY_INDUSTRY_TAX            ? city->industry_baseline.value[index]
                                               : city->industry_demand.value[index];
    values->value[index] = value;
    values->maximum = IntMax(values->maximum, value);
  }
  return true;
}

// Neighbor painter uses this rotation order for its four surrounding icons. Neighbor must be 0 through 3 for a nonzero
// result and rotation supported; does not change saved neighbor values.
u32 ReadCityNeighborPopulation(const City *city, int neighbor, int rotation) {
  static const i8 direction[4] = {2, -1, 0, 1};
  if (neighbor < 0 || neighbor > 3)
    return 0;
  int index = ((u32)rotation + (u32)direction[neighbor]) & 3;
  return city->primary_accounts.value[index];
}

// Exact population thresholds and empty neighboring-city row. Returns a stage index only; the neighboring flag
// controls the special empty-city row without changing population.
int CityNeighborPopulationIconRow(u32 population, int neighboring) {
  if (population == 0 && neighboring != 0)
    return 0;
  if (population < 2000)
    return 1;
  if (population < 10000)
    return 2;
  if (population < 50000)
    return 3;
  return population < 100000 ? 4 : 5;
}

// Compute the local-city population used by the neighbor display, including special-building contributions. Requires
// current tile statistics; the result is a read-only presentation value.
u32 ReadCityNeighborCentralPopulation(const City *city) {
  int special = city->statistics.value[TK_PLYMOUTH_ARCOLOGY] +
      city->statistics.value[TK_FOREST_ARCOLOGY] + city->statistics.value[TK_DARCO_ARCOLOGY] +
      city->statistics.value[TK_LAUNCH_ARCOLOGY];
  int extra = special / 16 > 140 ? 4000 * (5 * (special / 16) - 700) : 0;
  return city->indicators.population + (u32)extra + city->indicators.arcology_population;
}

// History

// Rebuild every history maximum from the defaults and 52 saved samples. Requires initialized complete series; replaces
// cached maxima and leaves saved sample values intact.
void RebuildCityHistoryMaxima(City *city) {
  CityHistoryMaxima *maxima = &city->history_maxima;
  const CityHistoryMaxima *defaults = &kCityHistoryMaximumDefaults;
  for (int series = 0; series < CITY_HISTORY_SERIES_COUNT; ++series) {
    u32 maximum = defaults->value[series];
    for (int sample = 0; sample < CITY_HISTORY_SAMPLE_COUNT; ++sample) {
      u32 value = ReadHistorySample(city, series, sample);
      if ((int)maximum < (int)value)
        maximum = value;
    }
    maxima->value[series] = maximum;
  }
  for (int series = CITY_HISTORY_RESIDENTS; series <= CITY_HISTORY_INDUSTRY; ++series)
    maxima->value[series] = maxima->value[CITY_HISTORY_CITY_SIZE];
}

// Menu rescaling uses the recent year, then clips every retained sample.
// Recompute graph maxima from the stored history and default minimum scales. Requires initialized history arrays;
// replaces cached maxima rather than changing recorded samples.
void RebaseCityHistoryGraphScales(City *city) {
  CityHistoryMaxima *maxima = &city->history_maxima;
  const CityHistoryMaxima *defaults = &kCityHistoryMaximumDefaults;
  for (int series = 0; series < CITY_HISTORY_SERIES_COUNT; ++series) {
    u32 maximum = defaults->value[series];
    for (int sample = 0; sample < CITY_MONTH_COUNT; ++sample) {
      u32 value = ReadHistorySample(city, series, sample);
      if ((int)value > (int)maximum)
        maximum = value;
    }
    maxima->value[series] = maximum;
    for (int sample = 0; sample < CITY_HISTORY_SAMPLE_COUNT; ++sample) {
      u32 value = ReadHistorySample(city, series, sample);
      if ((int)value > (int)maximum)
        WriteHistorySample(city, series, sample, maximum);
    }
  }
  for (int series = CITY_HISTORY_RESIDENTS; series <= CITY_HISTORY_INDUSTRY; ++series)
    maxima->value[series] = maxima->value[CITY_HISTORY_CITY_SIZE];
  InvalidateCityInformation(city->game, CITY_UPDATE_HISTORY);
}

// Record the current monthly metrics and advance longer-period series when the calendar requires it.
// Call once at the scheduled economy boundary; repeated calls would insert extra samples.
void RecordCityMonthlyHistory(City *city) {
  CityStatistics *stats = &city->statistics;
  CityEconomyMonths *economy = &city->economy;
  CityPopulationSectors *population = &city->population;
  CityIndustrySectors *sectors = &city->industry_sectors;
  CityHistoryMaxima *maxima = &city->history_maxima;

  for (int series = 0; series < CITY_HISTORY_SERIES_COUNT; ++series) {
    CityHistoryRow *row = GetCityHistoryRow(city, series);
    memmove(row->sample + 1, row->sample, (CITY_HISTORY_MONTHLY_COUNT - 1) * sizeof(row->sample[0]));
  }

  int total = stats->value[TK_DARCO_ARCOLOGY] + stats->value[TK_FOREST_ARCOLOGY];
  total += stats->value[TK_PLYMOUTH_ARCOLOGY];
  total += stats->value[TK_LAUNCH_ARCOLOGY];
  int total_sixteenth = total / 16;
  int extra = 0;
  if (total_sixteenth > 140) {
    int scale = 5 * total_sixteenth - 700;
    extra = 4000 * scale;
  }
  int arcology_population = (int)city->indicators.arcology_population;
  int common = extra + arcology_population;
  int population_sample = common + 10 * (int)population->value[CITY_POPULATION_TOTAL];
  int residential_sample = common / 2 + 10 * (int)sectors->value[0];
  int commercial_sample = common / 4 + 10 * (int)sectors->value[1];
  int industrial_sample = common / 4 + 10 * (int)sectors->value[2];
  WriteHistorySample(city, CITY_HISTORY_CITY_SIZE, 0, (u32)population_sample);
  WriteHistorySample(city, CITY_HISTORY_RESIDENTS, 0, (u32)residential_sample);
  WriteHistorySample(city, CITY_HISTORY_COMMERCE, 0, (u32)commercial_sample);
  WriteHistorySample(city, CITY_HISTORY_INDUSTRY, 0, (u32)industrial_sample);

  int largest = (int)maxima->value[CITY_HISTORY_CITY_SIZE];
  int sample = (int)(ReadHistorySample(city, CITY_HISTORY_CITY_SIZE, 0));
  largest = IntMax(largest, sample);
  for (int series = CITY_HISTORY_CITY_SIZE; series <= CITY_HISTORY_INDUSTRY; ++series)
    maxima->value[series] = (u32)largest;

  u32 denominator = economy->sector[CITY_ECONOMY_ROADS].current_amount +
      economy->sector[CITY_ECONOMY_HIGHWAYS].current_amount + economy->sector[CITY_ECONOMY_BRIDGES].current_amount + 1;
  WriteHistorySample(city, CITY_HISTORY_TRAFFIC, 0, UintDiv(city->indicators.city_traffic, denominator));

  u32 zone_count = (i16)city->indicators.zone_count / 4 + 1;
  WriteHistorySample(city, CITY_HISTORY_POLLUTION, 0, UintDiv(city->indicators.city_pollution, zone_count));
  WriteHistorySample(city, CITY_HISTORY_LAND_VALUE, 0, UintDiv(city->indicators.city_land_value, zone_count));
  WriteHistorySample(city, CITY_HISTORY_CRIME, 0, UintDiv(city->indicators.city_crime, zone_count));
  largest = 0;
  for (int series = CITY_HISTORY_TRAFFIC; series <= CITY_HISTORY_CRIME; ++series) {
    int prior = (int)maxima->value[series];
    int current = (int)(ReadHistorySample(city, series, 0));
    largest = IntMax(largest, prior);
    largest = IntMax(largest, current);
  }
  for (int series = CITY_HISTORY_TRAFFIC; series <= CITY_HISTORY_CRIME; ++series)
    maxima->value[series] = (u32)largest;

  WriteHistorySample(city, CITY_HISTORY_POWER_PERCENT, 0, 100 - city->infrastructure.power_percent);
  WriteHistorySample(city, CITY_HISTORY_WATER_PERCENT, 0, 100 - city->infrastructure.water_percent);
  WriteHistorySample(city, CITY_HISTORY_EDUCATION, 0, city->indicators.workforce_education);
  WriteHistorySample(city, CITY_HISTORY_HEALTH, 0, city->indicators.workforce_life_expectancy);
  u32 active_population = population->value[CITY_POPULATION_TOTAL];
  u32 abandoned_population = population->value[CITY_POPULATION_ABANDONED];
  denominator = abandoned_population + active_population + 1;
  u32 proportion = UintDiv(100 * abandoned_population, denominator);
  city->indicators.unemployment = proportion;
  WriteHistorySample(city, CITY_HISTORY_UNEMPLOYMENT, 0, proportion);
  WriteHistorySample(city, CITY_HISTORY_GNP, 0, city->indicators.national_value);
  WriteHistorySample(city, CITY_HISTORY_NATIONAL_POPULATION, 0, city->indicators.national_population);
  WriteHistorySample(city, CITY_HISTORY_FEDERAL_RATE, 0, city->indicators.federal_interest_rate);
  for (int series = CITY_HISTORY_POWER_PERCENT; series < CITY_HISTORY_SERIES_COUNT; ++series) {
    int prior = (int)maxima->value[series];
    int current = (int)(ReadHistorySample(city, series, 0));
    if (prior < current)
      maxima->value[series] = (u32)current;
  }
  if ((int)maxima->value[CITY_HISTORY_GNP] < (int)maxima->value[CITY_HISTORY_NATIONAL_POPULATION])
    maxima->value[CITY_HISTORY_GNP] = maxima->value[CITY_HISTORY_NATIONAL_POPULATION];

  int month = city->clock.month;
  if (month == CITY_MONTH_JANUARY || month == CITY_MONTH_JULY) {
    for (int series = 0; series < CITY_HISTORY_SERIES_COUNT; ++series) {
      CityHistoryRow *row = GetCityHistoryRow(city, series);
      memmove(row->sample + CITY_HISTORY_HALF_YEAR_FIRST + 1, row->sample + CITY_HISTORY_HALF_YEAR_FIRST,
          (CITY_HISTORY_HALF_YEAR_COUNT - 1) * sizeof(row->sample[0]));
      row->sample[CITY_HISTORY_HALF_YEAR_FIRST] = row->sample[CITY_HISTORY_MONTHLY_FIRST];
    }
  }
  if (month == CITY_MONTH_JANUARY && (i16)city->clock.year % 5 == 0) {
    for (int series = 0; series < CITY_HISTORY_SERIES_COUNT; ++series) {
      CityHistoryRow *row = GetCityHistoryRow(city, series);
      memmove(row->sample + CITY_HISTORY_FIVE_YEAR_FIRST + 1, row->sample + CITY_HISTORY_FIVE_YEAR_FIRST,
          (CITY_HISTORY_FIVE_YEAR_COUNT - 1) * sizeof(row->sample[0]));
      row->sample[CITY_HISTORY_FIVE_YEAR_FIRST] = row->sample[CITY_HISTORY_MONTHLY_FIRST];
    }
  }
  InvalidateCityInformation(city->game, CITY_UPDATE_HISTORY);
}
