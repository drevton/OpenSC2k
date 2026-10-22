#ifndef SC2K_CITY_ENGINE_WEATHER_H
#define SC2K_CITY_ENGINE_WEATHER_H

#include "city/types.h"

// Deterministic weather states; presentation chooses their labels.
typedef enum CityWeather {
  CITY_WEATHER_COLD = 0, CITY_WEATHER_CLEAR = 1, CITY_WEATHER_HOT = 2,
  CITY_WEATHER_FOGGY = 3, CITY_WEATHER_CHILLY = 4, CITY_WEATHER_OVERCAST = 5,
  CITY_WEATHER_SNOW = 6, CITY_WEATHER_RAIN = 7, CITY_WEATHER_WINDY = 8,
  CITY_WEATHER_BLIZZARD = 9, CITY_WEATHER_HURRICANE = 10, CITY_WEATHER_TORNADO = 11,
  CITY_WEATHER_COUNT = 12
} CityWeather;

typedef struct CityWeatherState {
  u8 state;
  u8 temperature;
  u8 wind;
  u8 rainfall;
} CityWeatherState;

_Static_assert(sizeof(CityWeatherState) == 4, "weather state stores four independent bytes");

// Choose the next weather from the live quarter/state using exactly one CRT RNG draw, then average temperature,
// wind and rainfall with its properties. Quarter must be 0..3 and state a CityWeather value.
// The monthly caller invokes this after any milestone notice completes.
void AdvanceCityWeather(City *city);

#endif
