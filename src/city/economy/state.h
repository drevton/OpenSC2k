#ifndef SC2K_DOMAIN_SRC_CITY_ECONOMY_STATE_H
#define SC2K_DOMAIN_SRC_CITY_ECONOMY_STATE_H

#include "city/economy/types.h"
#include "city/maps/tile_storage.h"
#include "city/economy/facilities.h"
#include "city/types.h"

// The one active budget's command state; window presentation state belongs to UI.
typedef struct CityBudgetState {
  i32 id;
  i32 helper_stage;
  u32 timer_revision;
  bool timer_running;
  bool close_requested;
  i32 advisor_question;
  bool manual;
  u32 saved_auto_budget;
} CityBudgetState;

// Accounts

typedef struct {
  u32 value[CITY_ECONOMY_TAX_SECTOR_COUNT];
} CityIndustrySectors;
typedef struct {
  u32 value[4];
} CityAccountQuartet;
typedef struct {
  u16 value[4];
} CityAccountNameIndices;
typedef struct {
  u32 value[CITY_INDUSTRY_COUNT];
} CityIndustryAccounts;
// Saved signed industry demand and tax baseline words promote directly to int.
typedef struct {
  i16 value[CITY_INDUSTRY_COUNT];
} CityIndustryDemand;

// Indicators

// Saved economic totals and derived working state. Natural native layout;
// original addresses and byte transport belong to the comparison backend.
typedef struct {
  i64 cash;
  u32 bond_count;
  i32 bond_rate_total;
  u32 population;
  u32 city_land_value;
  u64 asset_value;
  u32 unemployment;
  u32 zone_count;
  u32 workforce_life_expectancy;
  u32 workforce_education;
  u32 city_pollution;
  u32 city_crime;
  u32 arcology_population;
  u32 history_initialization_state;
  u32 national_value;
  u32 national_population;
  i16 federal_interest_rate; // Federal interest rate, in percent.
  // Shared by the tile-decay scan and traffic history.
  u32 city_traffic;
  i16 national_economy_trend;
  // -1 means the industry capacity ratio is below the threshold.
  i16 industrial_pollution_bonus;
  i16 industrial_mix_bonus;
  u32 sector_population_total;
  u32 population_cumulative;
  u32 birth_count;
  u32 attrition_count;
  u32 workforce_percent;
  u32 event_flag;
  u32 year_flag;
  u32 ranked_disaster_gate;
  u32 auto_budget_enabled;
  u16 difficulty;
} CityEconomicIndicators;

// Months

// Amounts and financial totals are signed; funding rates are nonnegative.
typedef struct {
  i32 monthly_amount[12];
  u32 monthly_rate[12];
  union {
    i32 mission_metric; // +0x60
    i32 current_amount;
  };
  u32 current_rate;
  i32 accumulated;
  i32 projected_annual_total;
} EconomySector;
typedef struct {
  EconomySector sector[CITY_ECONOMY_SECTOR_COUNT];
} CityEconomyMonths;

typedef struct {
  u32 value[CITY_POPULATION_BUCKET_COUNT];
} CityPopulationSectors;

typedef struct {
  u32 value[CITY_AGE_COHORT_COUNT];
} CityAgeCohorts;
_Static_assert(sizeof(CityAgeCohorts) == 80 && offsetof(CityAgeCohorts, value) == 0,
    "age cohort row stores twenty 32-bit values");

// Unlock State

// Reward and policy availability are shared words in the tool-group table,
// not independent copies. Native layout does not depend on executable addresses.
typedef union {
  u32 slot[18];
  struct {
    u32 groups_0_4[5];
    u32 special_facility_mask;
    u32 groups_6_7[2];
    u32 policy_availability_flags;
    u32 groups_9_17[9];
  } shared;
} CityUnlockTable;

// Executable startup availability, before date/population rebuilding.
extern const CityUnlockTable kCityInitialToolAvailability;

_Static_assert(sizeof(CityUnlockTable) == 18 * sizeof(u32), "unlock table width");

typedef struct {
  CityUnlockTable tools;
  u32 ordinance_flags;
  i16 disaster_level;
  u16 power_mask;
  u32 arcology_count;
  u8 military_base;
} CityUnlockState;

#endif
