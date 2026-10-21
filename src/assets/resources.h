#ifndef SC2K_DOMAIN_SRC_ASSETS_RESOURCES_H
#define SC2K_DOMAIN_SRC_ASSETS_RESOURCES_H

#include "assets/state.h"

// Data

void InitializeCityNewsResources(CityAssets *assets);

// Facility Information
void InitializeCityFacilityInformation(City *city);

// Names

extern const char *const kCitySports[5];
extern const char *const kCityTeamNames[5];

// Borrowed static text; unknown categories and indices return an empty string.
const char *ReadCityNameResource(int category, int index);
bool InitializeCityResourceNames(CityResourceState *resources);
bool GenerateCityName(u32 *random, const char *previous, char *name, size_t capacity);

// Newspaper

enum { CITY_NEWSPAPER_PRICE_ERAS = 5 };

extern const char *const kCityNewspaperTitles[6];
extern const char *const kCityNewspaperWeatherHeadings[6];
extern const char *const kCityNewspaperEditorialHeadings[6];
extern const char *const kCityNewspaperStatistics[7];
extern const char *const kCityNewspaperPrices[3][CITY_NEWSPAPER_PRICE_ERAS];

static inline const char *CityNewspaperPriceText(int kind, int era) {
  i64 index = (i64)CITY_NEWSPAPER_PRICE_ERAS * kind + era;
  return index >= 0 && index < 3 * CITY_NEWSPAPER_PRICE_ERAS ?
      kCityNewspaperPrices[index / CITY_NEWSPAPER_PRICE_ERAS][index % CITY_NEWSPAPER_PRICE_ERAS] : "";
}

void ReleaseCityNewspaperResources(CityNewspaperResources *resources);

#endif
