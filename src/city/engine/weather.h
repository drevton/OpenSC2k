#ifndef SC2K_CITY_ENGINE_WEATHER_H
#define SC2K_CITY_ENGINE_WEATHER_H

#include "city/types.h"

// Choose the next weather from the live quarter/state using exactly one CRT RNG draw, then average temperature,
// wind and rainfall with its properties. Quarter must be 0..3 and state a CityWeather value.
// The monthly caller invokes this after any milestone notice completes.
void AdvanceCityWeather(City *city);

#endif
