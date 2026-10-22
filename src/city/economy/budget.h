#ifndef SC2K_CITY_ECONOMY_BUDGET_H
#define SC2K_CITY_ECONOMY_BUDGET_H

#include "city/economy/types.h"
#include "city/types.h"

typedef struct CityBudgetState CityBudgetState;

// Forecasts replace projected totals; tax adjustment normalizes baselines and refreshes their rounded mean.
void RefreshBudgetDialogEstimates(City *city);
void AdjustIndustryTaxRates(City *city, int direction);

// Signed monthly income/cost, independent of whether the policy is enabled. Unsupported policies cost zero.
int CityPolicyMonthlyCost(const City *city, int policy);
void RebuildCityPolicyAvailability(City *city);

// Valuation and bond queries are read-only except for the explicitly named recalculation.
void RecalculateCityAssetValue(City *city);
bool CanCityIssueBond(const City *city);
int CityBondInterestRate(const City *city);
const char *CityBondCreditRating(const City *city);

// Effects require prior admission and a captured rate. They apply exactly once; retained callers own confirmation.
void ApplyCityBondIssue(City *city, int rate);
void ApplyCityBondRedemption(City *city, int rate);

// Shared budget commands validate their reserved fields and rates before changing City. TEST returns admission
// without restarting a helper, changing rates, consuming RNG or notifying presentation.
// CmdBudgetRate accepts a category, direction (-1/+1) and optional retained budget identity (zero means none).
i64 CmdBudgetRate(City *city, u32 flags, TileIndex location, int parameter1, int parameter2, int parameter3);
i64 CmdSetBudgetRate(City *city, u32 flags, TileIndex location, int kind, int rate, int options);
i64 CmdSetIndustryTaxBaseline(City *city, u32 flags, TileIndex location,
    int parameter1, int parameter2, int parameter3);
i64 CmdToggleCityPolicy(City *city, u32 flags, TileIndex location,
    int parameter1, int parameter2, int parameter3);
i64 CmdToggleAutoBudget(City *city, u32 flags, TileIndex location,
    int parameter1, int parameter2, int parameter3);

// Bond ASK queries return CITY_COMMAND_NEEDS_CONFIRMATION after live admission. Execution requires CONFIRMED;
// issue uses the supplied rate unless the rating fix selects a live rate, redemption consumes the oldest rate.
i64 CmdIssueBond(City *city, u32 flags, TileIndex location,
    int parameter1, int parameter2, int parameter3);
i64 CmdRedeemBond(City *city, u32 flags, TileIndex location,
    int parameter1, int parameter2, int parameter3);

// Retained budget interaction. Returned state pointers are borrowed until the interaction is ended or replaced.
enum {
  CITY_BUDGET_HELPER_DISABLED = -1,
  CITY_BUDGET_HELPER_LAST_STAGE = 7,
  CITY_BUDGET_HELPER_TIMER = 0x8095,
  CITY_BUDGET_HELPER_INTERVAL_MS = 6000,
  CITY_BUDGET_HELPER_BITMAP_FIRST = 189,
  CITY_BUDGET_ADVISOR_PORTRAIT_FIRST = 197,
  CITY_BUDGET_HELP_CONTEXT = 0x20066,
};

// Prepare annual-budget presentation once before entering its retained interaction.
bool PrepareCityAnnualBudget(City *city);
// Selecting health advice can advance the CRT RNG.
CityBudgetAdvice SelectCityBudgetAdvisorMessage(City *city, CityBudgetCategory category);
const CityBudgetState *FindCityBudget(City *city, i32 id);
bool BeginCityBudget(City *city, i32 id, bool manual);
bool EndCityBudget(City *city, i32 id);
bool DeliverCityBudgetHelperTimer(City *city, i32 id, i32 timer);
bool SuspendCityBudgetHelperForAdvisor(City *city, i32 id, i32 question);
bool ResumeCityBudgetHelperAfterAdvisor(City *city, i32 id, i32 question, i32 parent_question, bool helper_enabled);
void RestartCityBudgetHelperAfterRateEdit(City *city, i32 id);

#endif
