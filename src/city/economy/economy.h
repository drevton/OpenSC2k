#ifndef SC2K_DOMAIN_SRC_CITY_ECONOMY_ECONOMY_H
#define SC2K_DOMAIN_SRC_CITY_ECONOMY_ECONOMY_H

#include "city/economy/types.h"
#include "city/types.h"

// Economy

// Initialize industry demand/baselines/accounts, sector-rate offsets, bonds and current sector defaults for a new
// city. Only monthly slot zero changes; treasury, later monthly slots, calendar and neighbor accounts are retained.
// Requires initialized City storage. No RNG, allocation or presentation; the caller owns setup sequencing.
void InitializeNewCityEconomy(City *city);

void UpdateMonthlyEconomyBookkeeping(City *city);
// Apply the due January settlement once. After any facility/fiscal notices, Finish records the live month and rates.
bool BeginCityAnnualSettlement(City *city);
bool PrepareCityFiscalWarning(City *city);
void FinishCityMonthlyEconomy(City *city);
void UpdateCityEconomyPrimary(City *city);
typedef struct CityMilestoneNotice {
  int milestone; // Captured due milestone, or -1 when none was selected.
  u8 event, data;
  bool show;
} CityMilestoneNotice;
// BeginCityEconomyPrimary applies updates once and returns a notice to retain through any newspaper.
// Call FinishCityEconomyPrimary once with its milestone, including -1, after optional presentation.
// The show flag never suppresses finishing; weather advances from live City state.
CityMilestoneNotice BeginCityEconomyPrimary(City *city);
void FinishCityEconomyPrimary(City *city, int milestone);
// Advance national/neighbor accounts, industry demand/allocation and age cohorts in that order.
// Uses live City state and its deterministic random streams; an empty city still performs the economy phases.
void UpdateCityEconomySecondary(City *city);

#endif
