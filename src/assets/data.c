#include "city/economy/facilities.h"
#include "assets/resource_types.h"
#include <stdlib.h>

#include "assets/resources.h"

#include "assets/state.h"

static const char *const kFacilityTemplates[] = {
  "",
  " the entire bus network)",
  " all city parks)",
  " the entire rail network)",
  " the entire subway network)",
  " every city library)",
  " every city marina)",
  " every city museum)",
  " all hydroelectric plants)",
  " all wind turbines)",
  "#T",
  "Capacity used: #3%",
  "(figures cover ",
  "(figures cover the citywide",
  "(combined figures cover",
  "City parkland: #2 acres",
  "Service age: #0 years (max 50)",
  "Alpaca population: #3",
  "Andean llamas: #2",
  "Yearly upkeep: $#0",
  "Yearly upkeep: $#3",
  "Public approval: #2%",
  "Arrests made: #3",
  "Annual attendance: #1",
  "Visitor attendance: #1",
  "Available beds: 1000",
  "Boats berthed: #1",
  "Collection size: #2 books",
  "Year established: #1",
  "Year established: #2",
  "Year established: #3",
  "Bungee visits: #0",
  "Bus stops: #1",
  "Operating buses: #2",
  "Total capacity: 10000",
  "Total capacity: 1500",
  "Total capacity: 25000",
  "Flow capacity: 5,000 cubic feet/min",
  "Total capacity: 5000",
  "Complaints received: #3",
  "Facility condition: #G",
  "Reported offenses: #2",
  "Planned capacity: #1000",
  "Medical staff: #2",
  "Guests received: #0",
  "Staff employed: #1",
  "Staff employed: #2",
  "Staff employed: #3",
  "Escape incidents: #0",
  "Exhibits open: #2",
  "Engines in service: #2",
  "Fire crew: #1",
  "Performance grade: #G",
  "Security staff: #2",
  "Structure height: 65 feet",
  "Hydroelectric units: #1",
  "Prison population: #1",
  "Animals observed: #2",
  "Home sport: #S",
  "Cast in bronze",
  "Peak output: #1 MW",
  "Network peak output: #2 MW",
  "Police officers: #1",
  "Other camelids: #0",
  "Daily passenger trips: #3",
  "People receiving care: #1",
  "Peruvian llamas: #1",
  "Birds roosting: #2",
  "Rail stops: #1",
  "People housed: #2",
  "Typical response: #3 minutes",
  "Capacity used: #0%",
  "Capacity used: #2%",
  "Salt extracted: #1 tons",
  "Enrolled students: #1",
  "Subway stops: #1",
  "Teaching staff: #2",
  "Processed sewage: #2,000 cubic feet",
  "Sewage awaiting treatment: #1,000 cubic feet",
  "Guest visits: #1",
  "Wedding services: #0",
  "Turbines in service: #1",
  "Season record: #W",
};
const char *const kCityNewspaperTitles[] = {
  "Times",
  "Herald",
  "Gazette",
  "Chronicle",
  "Tribune",
  "Journal",
};
const char *const kCityNewspaperWeatherHeadings[] = {
  "Local forecast",
  "Weather desk",
  "Around the city",
  "Skywatch",
  "The outlook",
  "Weather notes",
};
const char *const kCityNewspaperEditorialHeadings[] = {
  "From the editor",
  "Editorial desk",
  "Local commentary",
  "City opinion",
  "Public survey",
  "Advice desk",
};
const char *const kCityNewspaperStatistics[] = {
  "Traffic: %d%%",
  "Pollution: %d%%",
  "Crime: %d%%",
  "Tax rate: %d%%",
  "Unemployment: %d%%",
  "Education: %d%%",
  "Health: %d%%",
};
const char *const kCityNewspaperPrices[3][CITY_NEWSPAPER_PRICE_ERAS] = {
  {"1 cent", "5 cents", "25 cents", "1 dollar", "5 dollars"},
  {"Cover: 1¢", "Cover: 5¢", "Cover: 25¢", "Cover: $1", "Cover: $5"},
  {"Price: 1¢", "Price: 5¢", "Price: 25¢", "Price: $1", "Price: $5"},
};

// Facility Information

// Original facility classes and five resource selections per class.
static const i8 kFacilityInformationTemplates[CITY_FACILITY_INFO_COUNT][CITY_FACILITY_INFO_LINE_COUNT] = {
  {-1, -1, -1, -1, -1},
  {60, 72, 16, -1, -1},
  {45, 29, -1, -1, -1},
  {25, 65, 43, 52, 20},
  {62, 41, 22, -1, 19},
  {51, 50, 70, -1, 19},
  {35, 74, 76, 52, 20},
  {36, 24, 58, 10, 82},
  {34, 56, 53, 48, 11},
  {38, 24, 76, 52, 20},
  {66, 18, 17, 63, -1},
  {54, 59, 28, 67, -1},
  {21, 28, 47, 44, -1},
  {37, -1, 78, 77, -1},
  {71, 73, 46, -1, -1},
  {42, 69, 30, -1, 40},
  {80, 79, 57, 39, 31},
  {32, 33, 64, 12, 1},
  {68, 64, -1, 12, 3},
  {75, 64, -1, 12, 4},
  {81, 61, -1, 13, 9},
  {55, 61, -1, 13, 8},
  {23, 15, 47, 12, 2},
  {23, 49, -1, 14, 7},
  {24, 27, 52, 14, 5},
  {26, -1, -1, 14, 6},
};

static bool IsFacilityLocale(const char *locale, const char *expected) {
  while (*expected) {
    int character = (u8)*locale++;
    if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
    if (character != *expected++) return false;
  }
  return *locale == '\0';
}

// Choose localized facility information templates without changing the borrowed locale. Assets retains the strings;
// these references require no separate allocation or release.
void InitializeCityFacilityInformation(CityAssets *assets, const char *locale) {
  int ids[CITY_FACILITY_INFO_COUNT][CITY_FACILITY_INFO_LINE_COUNT];
  for (int facility = 0; facility < CITY_FACILITY_INFO_COUNT; ++facility)
    for (int line = 0; line < CITY_FACILITY_INFO_LINE_COUNT; ++line)
      ids[facility][line] = kFacilityInformationTemplates[facility][line];
  if (IsFacilityLocale(locale, "aus")) {
    ids[CITY_FACILITY_INFO_LLAMA_DOME][0] = CITY_FACILITY_TEMPLATE_83;
    ids[CITY_FACILITY_INFO_LLAMA_DOME][1] = CITY_FACILITY_TEMPLATE_84;
    ids[CITY_FACILITY_INFO_LLAMA_DOME][2] = CITY_FACILITY_TEMPLATE_23;
    ids[CITY_FACILITY_INFO_LLAMA_DOME][3] = CITY_FACILITY_TEMPLATE_85;
    ids[CITY_FACILITY_INFO_LLAMA_DOME][4] = -1;
  } else if (IsFacilityLocale(locale, "fre")) {
    ids[CITY_FACILITY_INFO_SCHOOL][3] = CITY_FACILITY_TEMPLATE_83;
    ids[CITY_FACILITY_INFO_COLLEGE][3] = CITY_FACILITY_TEMPLATE_83;
    ids[CITY_FACILITY_INFO_WIND_POWER][3] = CITY_FACILITY_TEMPLATE_12;
    ids[CITY_FACILITY_INFO_LIBRARY][2] = CITY_FACILITY_TEMPLATE_83;
    for (int facility_class = CITY_FACILITY_INFO_SUBWAY_STATION; facility_class < CITY_FACILITY_INFO_COUNT;
        ++facility_class) {
      if (facility_class == CITY_FACILITY_INFO_LARGE_PARK)
        facility_class = CITY_FACILITY_INFO_MARINA;
      ids[facility_class][2] = ids[facility_class][3];
      ids[facility_class][3] = ids[facility_class][4];
      ids[facility_class][4] = facility_class == CITY_FACILITY_INFO_MARINA ? CITY_FACILITY_TEMPLATE_85 :
          CITY_FACILITY_TEMPLATE_84;
    }
  } else if (IsFacilityLocale(locale, "ger")) {
    ids[CITY_FACILITY_INFO_COLLEGE][3] = CITY_FACILITY_TEMPLATE_84;
    ids[CITY_FACILITY_INFO_LIBRARY][0] = CITY_FACILITY_TEMPLATE_85;
    ids[CITY_FACILITY_INFO_COLLEGE][1] = CITY_FACILITY_TEMPLATE_83;
    ids[CITY_FACILITY_INFO_LIBRARY][2] = CITY_FACILITY_TEMPLATE_83;
  }
  for (int facility = 0; facility < CITY_FACILITY_INFO_COUNT; ++facility)
    for (int line = 0; line < CITY_FACILITY_INFO_LINE_COUNT; ++line) {
      int id = ids[facility][line];
      assets->facility_information[facility][line] = id == -1 ? NULL :
          id < (int)(sizeof(kFacilityTemplates) / sizeof(kFacilityTemplates[0])) ? kFacilityTemplates[id] : "";
    }
}


// Newspaper

void ReleaseCityNewspaperResources(CityNewspaperResources *resources) {
  free(resources->text);
  free(resources->offsets);
  resources->articles = NULL;
  resources->text = NULL;
  resources->offsets = NULL;
  resources->text_capacity = 0;
  resources->offsets_capacity = 0;
}
