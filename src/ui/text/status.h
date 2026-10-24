#ifndef SC2K_UI_TEXT_STATUS_H
#define SC2K_UI_TEXT_STATUS_H

#include "city/disasters/types.h"
#include "city/engine/weather.h"
#include "city/types.h"

typedef struct Game Game;

enum {
  CITY_STATUS_COLUMN_TOOL_COST = 0, CITY_STATUS_COLUMN_ISSUE = 1,
  CITY_STATUS_COLUMN_WEATHER = 2, CITY_STATUS_COLUMN_COUNT = 3,
};

// Status Text

enum { CITY_DISASTER_STATUS_RESOURCE_COUNT = CITY_DISASTER_HURRICANE + 1 };

extern const char *const kCityDisasterText[CITY_DISASTER_STATUS_RESOURCE_COUNT];
extern const char *const kCityWeatherText[CITY_WEATHER_COUNT];

// Status State

enum { CITY_STATUS_TEXT_CAPACITY = 256 };

typedef struct {
  char text[CITY_STATUS_COLUMN_COUNT][CITY_STATUS_TEXT_CAPACITY];
  u32 color[CITY_STATUS_COLUMN_COUNT];
} CityStatusLegend;

// UI owns the text arrays and borrows the desktop bound by SetCityStatusPresentation.
struct CityCanvasDesktop;
typedef struct {
  CityStatusLegend legend;
  char problem_description[CITY_STATUS_TEXT_CAPACITY];
  char tool_text[CITY_STATUS_TEXT_CAPACITY];
  char city_title[CITY_STATUS_TEXT_CAPACITY];
  struct CityCanvasDesktop *desktop;
  int width;
  bool minimized;
} CityStatusState;

// Copy/truncate borrowed problem text; NULL clears it and overlapping sources are allowed.
void SetCityProblemDescription(CityStatusState *status, const char *text);
void ClearCityProblemDescription(CityStatusState *status);

// Status

// Bind a borrowed desktop and logical width. Unbinding clears width/minimized state and retains text.
void SetCityStatusPresentation(CityStatusState *status, struct CityCanvasDesktop *desktop, int width);
void SetCityStatusMinimized(CityStatusState *status, bool minimized);
bool IsCityStatusAvailable(const CityStatusState *status);
bool InvalidateCityStatus(CityStatusState *status);

// Copy the title into UI status text and keep the host caption at the application name. Text is borrowed for the call.
void PublishCityTitle(City *city, const char *title);
bool IsCityTitleMinimized(const City *city);

// Publish the current city title and status through native UI.
void UpdateCityStatus(City *city);

// Store a bounded legend string/color; a change damages the status area. An unchanged value reports availability.
bool SetCityMapLegendColumn(Game *game, CityStatusState *status, int column, const char *text, u32 color);

// Reads the selected UI tool and City weather/problem state; force bypasses minimized-status suppression.
void RefreshCityStatusPanel(City *city, CityStatusState *status, int speed, int force);

#endif
