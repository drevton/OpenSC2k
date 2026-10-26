#ifndef SC2K_CITY_ECONOMY_HISTORY_H
#define SC2K_CITY_ECONOMY_HISTORY_H

#include "city/economy/types.h"
#include "city/types.h"

// Chart Data

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
bool ReadCityDemographicValues(const City *city, i32 mode, CityDemographicValues *values);
bool ReadCityIndustryValues(const City *city, i32 mode, CityIndustryValues *values);
// Stored side is 0..3; screen order and artwork selection belong to presentation. Invalid sides return zero.
u32 ReadCityNeighborPopulation(const City *city, int side);
u32 ReadCityNeighborCentralPopulation(const City *city);

// History

// Record one monthly sample per series and advance the half-year/five-year bands when due.
void RecordCityMonthlyHistory(City *city);
void RebuildCityHistoryMaxima(City *city);
// Rescale from the recent year, clamp retained samples and invalidate history windows.
void RebaseCityHistoryGraphScales(City *city);

#endif
