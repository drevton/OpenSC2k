#ifndef SC2K_DOMAIN_SRC_CITY_ECONOMY_ECONOMY_H
#define SC2K_DOMAIN_SRC_CITY_ECONOMY_ECONOMY_H

#include "city/economy/types.h"
#include "city/types.h"

// Economy

void UpdateMonthlyEconomyBookkeeping(City *city);
bool BeginCityAnnualSettlement(City *city);
bool PrepareCityFiscalWarning(City *city);
void FinishCityMonthlyEconomy(City *city);
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

// Read-only City calculations used by chart presentation. Results live in caller storage; no window or cache is
// updated, and drawing a chart cannot advance the simulation or consume RNG.
bool BuildCityHistoryPlot(const City *city, i32 series, i32 period, i32 width, i32 height, CityHistoryPlot *plot);
bool ReadCityDemographicValues(const City *city, i32 mode, CityDemographicValues *values);
bool ReadCityIndustryValues(const City *city, i32 mode, CityIndustryValues *values);
u32 ReadCityNeighborPopulation(const City *city, i32 neighbor, i32 rotation);
i32 CityNeighborPopulationIconRow(u32 population, i32 neighboring);
u32 ReadCityNeighborCentralPopulation(const City *city);

// History

// Record one monthly sample per series and advance the half-year/five-year bands when due.
void RecordCityMonthlyHistory(City *city);
void RebuildCityHistoryMaxima(City *city);
// Rescale from the recent year, clamp retained samples and invalidate history windows.
void RebaseCityHistoryGraphScales(City *city);

#endif
