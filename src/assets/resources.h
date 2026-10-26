#ifndef SC2K_DOMAIN_SRC_ASSETS_RESOURCES_H
#define SC2K_DOMAIN_SRC_ASSETS_RESOURCES_H

#include "city/types.h"

typedef struct CityNewspaperResources CityNewspaperResources;

// Facility Information
// ASCII locale text is borrowed. Case-insensitive selection writes only the Assets-owned template references.
void InitializeCityFacilityInformation(CityAssets *assets, const char *locale);

// Names

extern const char *const kCitySports[5];
extern const char *const kCityTeamNames[5];

// Borrowed static text; unknown categories and indices return an empty string.
const char *ReadCityNameResource(int category, int index);
bool InitializeCityCalendarNames(CityAssets *resources);

// Newspaper

// Bind immutable native articles and ranking inputs; installed template buffers keep their separate ownership.
void InitializeCityNewsResources(CityAssets *assets);

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

// Release owned template buffers and drop borrowed article text; topic tables remain unchanged.
void ReleaseCityNewspaperResources(CityNewspaperResources *resources);

#endif
