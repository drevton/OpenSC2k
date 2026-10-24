#ifndef SC2K_CITY_ECONOMY_HISTORY_H
#define SC2K_CITY_ECONOMY_HISTORY_H

#include "city/economy/types.h"
#include "city/state.h"

// Accessors require initialized history and valid series/sample indices. They allocate nothing.
static inline CityHistoryRow *GetCityHistoryRow(City *city, int series) { return &city->history[series]; }

static inline u32 ReadHistorySample(const City *city, int series, int index) {
  return city->history[series].sample[index];
}

static inline void WriteHistorySample(City *city, int series, int index, u32 value) {
  city->history[series].sample[index] = value;
}

// Chart Data

typedef struct { int x, y; } CityChartPoint;
typedef struct {
  CityChartPoint point[20];
  u32 latest;
  i32 count;
} CityHistoryPlot;
typedef struct {
  int value[CITY_AGE_COHORT_COUNT];
  int summary;
} CityDemographicValues;
typedef struct {
  i32 value[11];
  i32 maximum;
} CityIndustryValues;

// Read-only City calculations used by chart presentation. Results live in caller storage; no window or cache is
// updated, and drawing a chart cannot advance the simulation or consume RNG.
// Failed calculations may leave partial output; consume the result only on success.
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
