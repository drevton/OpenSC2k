#ifndef SC2K_DOMAIN_SRC_CITY_ECONOMY_ECONOMY_H
#define SC2K_DOMAIN_SRC_CITY_ECONOMY_ECONOMY_H

#include "city/economy/types.h"
#include "city/types.h"

// Economy
typedef struct CityAudioState CityAudioState;

void RefreshBudgetDialogEstimates(City *city);
int Phase0PolicyCost(City *city, int index);
void RecalculateCityAssetValue(City *city);
bool CanCityIssueBond(const City *city);
int CityBondInterestRate(const City *city);
const char *CityBondCreditRating(const City *city);
void ApplyCityBondIssue(City *city, int rate);
void ApplyCityBondRedemption(City *city, int rate);
void UpdateMonthlyEconomyBookkeeping(City *city);
bool BeginCityAnnualSettlement(City *city);
bool PrepareCityFiscalWarning(City *city);
void FinishCityMonthlyEconomy(City *city);
void MaybeTriggerPeriodicCityEvent(City *city, CityAudioState *audio, int trigger_mode);
void UpdateCityEconomyPrimary(City *city);
typedef struct {
  int index;
  u8 event, data;
  bool show;
} CityMilestoneNotice;
CityMilestoneNotice BeginCityEconomyPrimary(City *city);
void FinishCityEconomyPrimary(City *city, int milestone);
int ScaleDemand(int value, int percent);
void UpdateCityEconomySecondary(City *city);

// Budget Advisor

// The one active budget's command state; window presentation state belongs to UI.
typedef struct {
  i32 id;
  i32 helper_stage;
  u32 timer_revision;
  bool timer_running;
  bool close_requested;
  i32 advisor_question;
  bool manual;
  u32 saved_auto_budget;
} CityBudgetState;

enum {
  CITY_BUDGET_HELPER_DISABLED = -1,
  CITY_BUDGET_HELPER_LAST_STAGE = 7,
  CITY_BUDGET_HELPER_TIMER = 0x8095,
  CITY_BUDGET_HELPER_INTERVAL_MS = 6000,
  CITY_BUDGET_HELPER_BITMAP_FIRST = 189,
  CITY_BUDGET_ADVISOR_PORTRAIT_FIRST = 197,
  CITY_BUDGET_HELP_CONTEXT = 0x20066,
};

// Choice order used by LoadBudgetAdvisorMessage.
typedef enum CityBudgetAdvice {
  CITY_ADVICE_NONE = 0, CITY_ADVICE_FLOAT_BOND, CITY_ADVICE_LOW_INTEREST,
  CITY_ADVICE_BOND_PAYMENTS, CITY_ADVICE_NEGATIVE_TREASURY,
  CITY_ADVICE_LOW_CRIME, CITY_ADVICE_AVERAGE_CRIME, CITY_ADVICE_HIGH_CRIME,
  CITY_ADVICE_FIRE_SURPLUS, CITY_ADVICE_FIRE_ADEQUATE, CITY_ADVICE_FIRE_SHORTAGE,
  CITY_ADVICE_LOWER_PROPERTY_TAX, CITY_ADVICE_RAISE_PROPERTY_TAX, CITY_ADVICE_CUT_SERVICES,
  CITY_ADVICE_POLLUTION_CONTROLS, CITY_ADVICE_ENERGY_CONSERVATION,
  CITY_ADVICE_NEIGHBORHOOD_WATCH, CITY_ADVICE_ANTI_DRUG, CITY_ADVICE_DROP_GAMBLING,
  CITY_ADVICE_DROP_INCOME_TAX, CITY_ADVICE_DROP_SALES_TAX,
  CITY_ADVICE_HEALTH_SURPLUS, CITY_ADVICE_HEALTH_SHORTAGE, CITY_ADVICE_SMOKING_BAN,
  CITY_ADVICE_CPR_TRAINING, CITY_ADVICE_FREE_CLINICS, CITY_ADVICE_HEALTH_ADEQUATE,
  CITY_ADVICE_EDUCATION_ADEQUATE, CITY_ADVICE_SCHOOL_SHORTAGE, CITY_ADVICE_COLLEGE_SHORTAGE,
  CITY_ADVICE_TRANSIT_UNDERFUNDED, CITY_ADVICE_TRANSIT_SHORTAGE, CITY_ADVICE_TRANSIT_SURPLUS,
  CITY_ADVICE_COUNT,
} CityBudgetAdvice;

CityBudgetAdvice SelectCityBudgetAdvisorMessage(City *city, CityBudgetCategory category);
const CityBudgetState *FindCityBudget(City *city, i32 id);
bool BeginCityBudget(City *city, i32 id, bool manual);
bool EndCityBudget(City *city, i32 id);
bool DeliverCityBudgetHelperTimer(City *city, i32 id, i32 timer);
bool SuspendCityBudgetHelperForAdvisor(City *city, i32 id, i32 question);
bool ResumeCityBudgetHelperAfterAdvisor(City *city, i32 id, i32 question, i32 parent_question, bool helper_enabled);
void RestartCityBudgetHelperAfterRateEdit(City *city, i32 id);

// Chart Data

typedef struct { int x, y; } CityChartPoint;
typedef struct {
  CityChartPoint point[20];
  u32 latest;
  i32 count;
} CityHistoryPlot;
typedef struct {
  int value[20];
  int summary;
} CityDemographicValues;
typedef struct {
  i32 value[11];
  i32 maximum;
} CityIndustryValues;

// City calculations used by chart presentation.
bool BuildCityHistoryPlot(City *city, i32 series, i32 period, i32 width, i32 height, CityHistoryPlot *plot);
bool ReadCityDemographicValues(City *city, i32 mode, CityDemographicValues *values);
bool ReadCityIndustryValues(City *city, i32 mode, CityIndustryValues *values);
u32 ReadCityNeighborPopulation(City *city, i32 neighbor, i32 rotation);
i32 CityNeighborPopulationIconRow(u32 population, i32 neighboring);
u32 ReadCityNeighborCentralPopulation(City *city);

// History

void RebuildCityHistoryMaxima(City *city);
// Rescale from the recent year, clamp retained samples and invalidate history windows.
void RebaseCityHistoryGraphScales(City *city);

// Newspaper

typedef struct {
  u16 scores[8];
  u16 category_order[8];
} CityNewspaperProblemRankings;

typedef struct {
  u32 enabled;
  i16 random_base_seed;
  u16 edition_index;
  CityNewspaperProblemRankings problems;
} CityNewspaperState;

// Rankings

void UpdateCityNewspaperProblemRankings(City *city);
bool BeginCityNewspaperProblemRankings(City *city);
void UpdateCityNewspaperRanksAndF3Records(City *city);
void FinishCityNewspaperRanksAndF3Records(City *city);
void Sc2kPhase21SortFacilities(City *city);
bool RecordRankedCityEvent(City *city, int event_code, u8 event_data);
// Records the event and returns whether its caller must present a newspaper.
bool PrepareRankedCityEvent(City *city, int event_code, u8 event_data);
void InsertRankedCityEvent(City *city, int event_code, u8 event_data);

#endif
