#ifndef SC2K_MAP_DATA_H
#define SC2K_MAP_DATA_H

#include "city/types.h"

// Map categories; fire protection and education have independent tabs.
typedef enum CityMapDataGroup {
  CITY_DATA_GROUP_SURFACE = 0, CITY_DATA_GROUP_TRANSPORT = 1,
  CITY_DATA_GROUP_POWER = 2, CITY_DATA_GROUP_WATER = 3,
  CITY_DATA_GROUP_POPULATION = 4, CITY_DATA_GROUP_POLICE = 5,
  CITY_DATA_GROUP_POLLUTION = 6, CITY_DATA_GROUP_LAND_VALUE = 7,
  CITY_DATA_GROUP_FIRE = 8, CITY_DATA_GROUP_EDUCATION = 9, CITY_DATA_GROUP_COUNT = 10,
  CITY_DATA_SELECTION_STRIDE = 10
} CityMapDataGroup;

// Data-layer selections use group * CITY_DATA_SELECTION_STRIDE + option.
typedef enum CityMapDataSelection {
  CITY_DATA_SELECTION_STRUCTURES = 0,
  CITY_DATA_SELECTION_ZONES = 1,
  CITY_DATA_SELECTION_ROADS = 10,
  CITY_DATA_SELECTION_RAIL = 11,
  CITY_DATA_SELECTION_TRAFFIC = 12,
  CITY_DATA_SELECTION_POWER = 20,
  CITY_DATA_SELECTION_WATER = 30,
  CITY_DATA_SELECTION_DENSITY = 40,
  CITY_DATA_SELECTION_GROWTH = 41,
  CITY_DATA_SELECTION_CRIME = 50,
  CITY_DATA_SELECTION_POLICE_COVERAGE = 51,
  CITY_DATA_SELECTION_POLICE_STATIONS = 52,
  CITY_DATA_SELECTION_POLLUTION = 60,
  CITY_DATA_SELECTION_LAND_VALUE = 70,
  CITY_DATA_SELECTION_FIRE_COVERAGE = 80,
  CITY_DATA_SELECTION_FIRE_STATIONS = 81,
  CITY_DATA_SELECTION_SCHOOLS = 90,
  CITY_DATA_SELECTION_COLLEGES = 91
} CityMapDataSelection;

// Groups range from zero to CITY_DATA_GROUP_COUNT - 1. UI admission and rendering share these bounds.
static inline int CityMapDataOptionCount(int group) {
  static const u8 kCounts[CITY_DATA_GROUP_COUNT] = {2, 3, 1, 1, 2, 3, 1, 1, 2, 2};
  return kCounts[group];
}

static inline bool IsCityMapDataSelection(int selection) {
  return selection >= 0 && selection < CITY_DATA_GROUP_COUNT * CITY_DATA_SELECTION_STRIDE &&
      selection % CITY_DATA_SELECTION_STRIDE < CityMapDataOptionCount(selection / CITY_DATA_SELECTION_STRIDE);
}

// Rebuild the caller's 128 by 128 ARGB preview, returning whether any pixel changed.
// The preview samples the full map; its horizontal axis is world y and its vertical axis is world x.
bool BuildCityMapDataPreview(const City *city, int selection, u32 *pixels);

#endif
