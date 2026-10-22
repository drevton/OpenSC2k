#ifndef SC2K_DOMAIN_SRC_CITY_ECONOMY_TABLES_H
#define SC2K_DOMAIN_SRC_CITY_ECONOMY_TABLES_H

#include "city/economy/types.h"
#include "city/economy/state.h"
#include "city/types.h"

// Tables

static inline int CityMilestoneDefaultYear(int index) {
  static const u16 city_milestone_base_years[CITY_MILESTONE_COUNT] = {
      1940, 1950, 1980, 1970, 2020, 2050, 1920, 1920, 1910, 1900, 1925, 1980, 1990, 2040, 2090, 2140, 2190};
  return city_milestone_base_years[index];
}

// Fixed native storage for saved economy records. The three demand offsets
// alias the first three signed words of the eight-word transport-bonus table.
typedef struct {
  i16 rate[50];
} CityBondRates;

typedef struct {
  i16 due_year[17];
} CityMilestoneYears;

typedef struct {
  i16 value[3];
} CitySectorRateOffsets;

typedef union {
  CitySectorRateOffsets offsets;
  i16 value[8];
} CitySectorRates;

typedef struct {
  CityBondRates bond_rates;
  CityMilestoneYears milestone_years;
  CitySectorRates sector_rates;
} CityEconomicTables;

// Coefficients

typedef struct {
  u8 statistic;
  u8 divisor;
  u16 multiplier;
} CityAssetValueRule;
enum {
  CITY_ECONOMY_POPULATION_CENTER = 5000000,
  CITY_ECONOMY_VALUE_CENTER = 3500000
};

typedef struct { i16 factor[16]; } CityAnnualSettlementFactors;
typedef struct { i16 value[16]; } CityBudgetSectorDivisors;
typedef struct { i16 value[16]; } CityDefaultEconomyRates;
typedef struct { u32 multiplier[4]; } CityAssetTailWeights;

extern const CityAnnualSettlementFactors kAnnualSettlementFactors;
extern const CityBudgetSectorDivisors kBudgetSectorDivisors;
extern const CityDefaultEconomyRates kCityDefaultEconomyRates;
extern const CityAssetTailWeights kCityAssetTailWeights;
extern const CityHistoryMaxima kCityHistoryMaximumDefaults;
// Ten population thresholds followed by a zero sentinel.
extern const u32 kDisasterUnlockPopulationThresholds[11];

typedef struct {
  u8 value[11];
} CityIndustryYearWeights;

extern const CityIndustryYearWeights kIndustryYearWeights[5];
extern const i16 kEconomyRateAdjustments[23];
extern const u8 kDifficultyPolicyPercent[4];
extern const i8 kPolicyDriftByPolicy[4];
extern const u8 kIndustryGraphMinimum[3];

enum {
  CITY_DEMAND_GROWTH_PERCENT = 90,
  CITY_DEMAND_MODERATE_PERCENT = 110,
  CITY_DEMAND_HIGH_PERCENT = 120,
  CITY_DEMAND_LOW_PERCENT = 80
};

#endif
