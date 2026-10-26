#include "city/economy/tables.h"

const u16 kCityMilestoneBaseYears[CITY_MILESTONE_COUNT] = {
  1940, 1950, 1980, 1970, 2020, 2050, 1920, 1920, 1910, 1900, 1925, 1980, 1990, 2040, 2090, 2140, 2190
};

const i16 kCityEconomySectorDivisors[CITY_ECONOMY_SECTOR_COUNT] = {
  [CITY_ECONOMY_RESIDENTIAL] = 75,
  [CITY_ECONOMY_COMMERCIAL] = 75,
  [CITY_ECONOMY_INDUSTRIAL] = 75,
  [CITY_ECONOMY_ORDINANCES] = 75,
  [CITY_ECONOMY_BONDS] = -100,
  [CITY_ECONOMY_POLICE] = -1,
  [CITY_ECONOMY_FIRE] = -1,
  [CITY_ECONOMY_HEALTH] = -2,
  [CITY_ECONOMY_SCHOOLS] = -4,
  [CITY_ECONOMY_COLLEGES] = -1,
  [CITY_ECONOMY_ROADS] = -1000,
  [CITY_ECONOMY_HIGHWAYS] = -500,
  [CITY_ECONOMY_BRIDGES] = -400,
  [CITY_ECONOMY_RAIL] = -250,
  [CITY_ECONOMY_SUBWAY] = -250,
  [CITY_ECONOMY_TUNNELS] = -250,
};

const CityHistoryMaxima kCityHistoryMaximumDefaults = {
  {1, 1, 1, 1, 1, 1, 1, 1, 100, 100, 150, 90, 15, 1, 1, 15}
};
const u32 kDisasterUnlockPopulationThresholds[11] = {
  2000, 10000, 30000, 60000, 90000, 120000, 500000, 1000000, 5000000, 10000000, 0
};

const u8 kIndustryYearWeights[CITY_INDUSTRY_WEIGHT_ERA_COUNT][CITY_INDUSTRY_COUNT] = {
  {20, 20, 10, 10, 15, 5, 0, 15, 8, 0, 10}, // 1900
  {30, 20, 40, 10, 20, 40, 20, 20, 16, 10, 20}, // 1950
  {35, 20, 30, 10, 25, 40, 30, 25, 24, 80, 30}, // 2000
  {20, 50, 20, 10, 20, 30, 40, 30, 40, 80, 40}, // 2050
  {10, 20, 10, 10, 20, 20, 50, 30, 20, 80, 50}, // 2100
};

const i16 kEconomyRateAdjustments[23] = {
  200, 160, 120, 100, 75, 50, 25, 0, -25, -50, -100, -150,
  -200, -250, -300, -350, -400, -450, -500, -550, -600, -650, -700,
};

const u8 kDifficultyPolicyPercent[4] = {0, 120, 110, 95};
const i8 kPolicyDriftByPolicy[4] = {6, 3, 0, -3};
