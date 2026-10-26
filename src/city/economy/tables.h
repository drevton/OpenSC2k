#ifndef SC2K_DOMAIN_SRC_CITY_ECONOMY_TABLES_H
#define SC2K_DOMAIN_SRC_CITY_ECONOMY_TABLES_H

#include "city/economy/types.h"

// Base unlock years, before initialization adds its random delay.

extern const u16 kCityMilestoneBaseYears[CITY_MILESTONE_COUNT];

// Coefficients

enum {
  CITY_ECONOMY_POPULATION_CENTER = 5000000,
  CITY_ECONOMY_VALUE_CENTER = 3500000
};

// Convert sector rate/activity totals into money; negative factors denote expenses.
// Annual amounts use 12 times the factor. Each caller preserves its own accumulation and rounding order.
extern const i16 kCityEconomySectorDivisors[CITY_ECONOMY_SECTOR_COUNT];
extern const CityHistoryMaxima kCityHistoryMaximumDefaults;
// Ten population thresholds followed by a zero sentinel.
extern const u32 kDisasterUnlockPopulationThresholds[11];

// Successive era weights, with columns in CityIndustryCategory order. The last era remains in effect afterward.
enum {
  CITY_INDUSTRY_WEIGHT_BASE_YEAR = 1900,
  CITY_INDUSTRY_WEIGHT_INTERVAL_YEARS = 50,
  CITY_INDUSTRY_WEIGHT_ERA_COUNT = 5,
};
extern const u8 kIndustryYearWeights[CITY_INDUSTRY_WEIGHT_ERA_COUNT][CITY_INDUSTRY_COUNT];
extern const i16 kEconomyRateAdjustments[23];
extern const u8 kDifficultyPolicyPercent[4];
extern const i8 kPolicyDriftByPolicy[4];

enum {
  CITY_DEMAND_GROWTH_PERCENT = 90,
  CITY_DEMAND_MODERATE_PERCENT = 110,
  CITY_DEMAND_HIGH_PERCENT = 120,
  CITY_DEMAND_LOW_PERCENT = 80
};

#endif
