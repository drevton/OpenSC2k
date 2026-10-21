#include "city/economy/tables.h"

#define SECTOR_DIVISORS {75, 75, 75, 75, -100, -1, -1, -2, -4, -1, -1000, -500, -400, -250, -250, -250}
const CityAnnualSettlementFactors kAnnualSettlementFactors = {SECTOR_DIVISORS};
const CityBudgetSectorDivisors kBudgetSectorDivisors = {SECTOR_DIVISORS};
#undef SECTOR_DIVISORS
const CityDefaultEconomyRates kCityDefaultEconomyRates = {
  {7, 7, 7, 1, 0, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100}
};
const CityAssetTailWeights kCityAssetTailWeights = {{100000, 120000, 150000, 200000}};
const CityHistoryMaxima kCityHistoryMaximumDefaults = {
  {1, 1, 1, 1, 1, 1, 1, 1, 100, 100, 150, 90, 15, 1, 1, 15}
};
const u32 kDisasterUnlockPopulationThresholds[11] = {
  2000, 10000, 30000, 60000, 90000, 120000, 500000, 1000000, 5000000, 10000000, 0
};

const CityIndustryYearWeights kIndustryYearWeights[5] = {
  {{20, 20, 10, 10, 15, 5, 0, 15, 8, 0, 10}},
  {{30, 20, 40, 10, 20, 40, 20, 20, 16, 10, 20}},
  {{35, 20, 30, 10, 25, 40, 30, 25, 24, 80, 30}},
  {{20, 50, 20, 10, 20, 30, 40, 30, 40, 80, 40}},
  {{10, 20, 10, 10, 20, 20, 50, 30, 20, 80, 50}},
};

const i16 kEconomyRateAdjustments[23] = {
  200, 160, 120, 100, 75, 50, 25, 0, -25, -50, -100, -150,
  -200, -250, -300, -350, -400, -450, -500, -550, -600, -650, -700,
};

const u8 kDifficultyPolicyPercent[4] = {0, 120, 110, 95};
const i8 kPolicyDriftByPolicy[4] = {6, 3, 0, -3};
const u8 kIndustryGraphMinimum[3] = {70, 30, 100};
