#ifndef SC2K_DOMAIN_SRC_CITY_ECONOMY_STATE_H
#define SC2K_DOMAIN_SRC_CITY_ECONOMY_STATE_H

#include "city/economy/types.h"
#include "city/maps/tile_storage.h"
#include "city/maps/map_types.h"
#include "city/types.h"
#include <stddef.h>

// Facility records and sparse association storage belong to City.
// Facility slots 0..9 are fixed; CreateFacilityInstance allocates from slot 10 onward.
// Slot60 (overlay111) triggers the newspaper F3/ranking refresh in label editors.
enum {
  CITY_FACILITY_RECORD_RESERVED_COUNT = 10,
  CITY_FACILITY_RECORD_FIRST_DYNAMIC = CITY_FACILITY_RECORD_RESERVED_COUNT,
  CITY_FACILITY_RECORD_COUNT = CITY_OVERLAY_FACILITY_COUNT,
  CITY_OVERLAY_RESERVED_FACILITY_LAST = CITY_OVERLAY_FACILITY_FIRST + CITY_FACILITY_RECORD_RESERVED_COUNT - 1,
  CITY_OVERLAY_NEWSPAPER_RANKING_FACILITY = 111,
};

// XMIC records: tracked widths retain aging and service-metric wrap behavior.
typedef struct CityAnnualFacilityRecord {
  u8 type;
  u8 age;
  // Primary metric selected by type: officer count, fire crew, establishment year, etc.
  u16 value_02;
  // Named aliases share the existing metric unions with the raw codec fields.
  union {
    u16 value_04;
    u16 stadium_team;
    u16 police_offenses;
    u16 fire_engines;
    u16 mayors_house_approval;
  };
  union {
    u16 value_06;
    u16 stadium_team_name_slot;
    u16 police_arrests;
    u16 fire_response_minutes;
    u16 mayors_house_staff;
  };
} CityAnnualFacilityRecord;
_Static_assert(sizeof(CityAnnualFacilityRecord) == 8 && offsetof(CityAnnualFacilityRecord, value_02) == 2 &&
    offsetof(CityAnnualFacilityRecord, stadium_team) == 4 &&
    offsetof(CityAnnualFacilityRecord, stadium_team_name_slot) == 6, "XMIC facility record layout");
_Static_assert(offsetof(CityAnnualFacilityRecord, police_offenses) == 4 &&
    offsetof(CityAnnualFacilityRecord, police_arrests) == 6 &&
    offsetof(CityAnnualFacilityRecord, fire_engines) == 4 &&
    offsetof(CityAnnualFacilityRecord, fire_response_minutes) == 6 &&
    offsetof(CityAnnualFacilityRecord, mayors_house_approval) == 4 &&
    offsetof(CityAnnualFacilityRecord, mayors_house_staff) == 6, "facility metric aliases use their saved words");

typedef struct {
  TileIndex tile;
  u32 next, years;
  CityOverlayName name;
} CityFacilityDetails;

typedef struct {
  // The stock prefix is also the bounded SC2 projection. Extra slots use the same simulation record layout.
  CityAnnualFacilityRecord record[CITY_FACILITY_RECORD_COUNT];
  CityAnnualFacilityRecord *extra;
  CityFacilityDetails *details;
  u32 *buckets;
  // Sparse capacity is zero or a power of two and covers details/buckets plus extra records after the fixed prefix.
  // used is an exclusive high-water slot with holes; free_slot is the next allocation-search hint.
  int capacity, used, free_slot;
} CityAnnualFacilities;

_Static_assert(offsetof(CityAnnualFacilities, record) == 0 && sizeof(CityAnnualFacilityRecord) == 8,
    "stock microsim record width");

// The annual caller retains this scan state across aging notices before committing its totals.
typedef struct CityAnnualFacilityWork {
  u32 arcology_population;
  int average, ratio_divisor;
  u32 index;
  u16 value_sum, arcology_count;
  i16 prior_ratio_flag;
  u8 low_school_quality, high_prison_load, resume_power, notice_data;
} CityAnnualFacilityWork;

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
