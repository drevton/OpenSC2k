#ifndef SC2K_DOMAIN_SRC_CITY_ECONOMY_ECONOMY_H
#define SC2K_DOMAIN_SRC_CITY_ECONOMY_ECONOMY_H

#include "city/economy/types.h"
#include "city/types.h"

// Economy

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
void UpdateCityEconomySecondary(City *city);

#endif
