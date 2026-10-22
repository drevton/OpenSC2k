#ifndef SC2K_DOMAIN_SRC_UI_TEXT_TEXT_H
#define SC2K_DOMAIN_SRC_UI_TEXT_TEXT_H

#include "city/disasters/types.h"
#include "city/engine/city_types.h"
#include "city/engine/weather.h"
#include "city/presentation.h"

#include "ui/input.h"
#include <stddef.h>
#include <string.h>

#include "city/types.h"
#include "base/utf8.h"

#include "base/text.h"

enum {
  CITY_STATUS_COLUMN_TOOL_COST = 0, CITY_STATUS_COLUMN_ISSUE = 1,
  CITY_STATUS_COLUMN_WEATHER = 2, CITY_STATUS_COLUMN_COUNT = 3,
};

// Owned error reason; any newline-separated context retains its spelling and case.
char *FormatCityErrorReason(const char *reason);

// Native single-line editors use decoded character boundaries in byte strings.

int CityTextPrevious(const char *text, int caret);
int CityTextNext(const char *text, int caret);
void DeleteCityTextSelection(char *text, int *caret, int *anchor);
bool HandleCityTextSelectionKey(char *text, int *caret, int *anchor, CityHotkey key);
void InsertCityTextSelection(char *text, size_t capacity, int *caret, int *anchor,
    const char *input, bool digits_only);
bool HandleCityTextEditKey(char *text, int *caret, int *select_all, CityHotkey key);
void InsertCityTextInput(char *text, size_t capacity, int *caret, int *select_all,
    const char *input, bool digits_only);

// Status Text

enum { CITY_DISASTER_STATUS_RESOURCE_COUNT = CITY_DISASTER_HURRICANE + 1 };

extern const char *const kCityDisasterText[CITY_DISASTER_STATUS_RESOURCE_COUNT];
extern const char *const kCityWeatherText[CITY_WEATHER_COUNT];

// Queued notices and command/file errors retain their numerical codes independently of text.

// Status State

enum { CITY_STATUS_TEXT_CAPACITY = 256 };

typedef struct {
  char text[CITY_STATUS_COLUMN_COUNT][CITY_STATUS_TEXT_CAPACITY];
  u32 color[CITY_STATUS_COLUMN_COUNT];
} CityStatusLegend;

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

void SetCityProblemDescription(CityStatusState *status, const char *text);
void ClearCityProblemDescription(CityStatusState *status);

// Status

void SetCityStatusPresentation(CityStatusState *status, struct CityCanvasDesktop *desktop, int width);
void SetCityStatusMinimized(CityStatusState *status, bool minimized);
bool IsCityStatusAvailable(const CityStatusState *status);
bool InvalidateCityStatus(CityStatusState *status);

// Text

typedef struct City City;
typedef struct Game Game;
typedef struct CityResourceState CityResourceState;

// One calendar day per simulation phase, with 25 days in every month.
int CityDayOfMonth(const City *city);

// Publish the current city title and status through native UI.
void UpdateCityStatus(City *city);

enum { CITY_NUMBER_TEXT_CAPACITY = 64, CITY_CURRENCY_TEXT_CAPACITY = 40 };

// The caller supplies width+1 bytes. No allocation or RTL object is involved.
char *FormatCityNumber(Game *game, char *text, const char *mask, int width, double value);
void TrimCityNumberText(char *text);
void FormatCityCurrencyForCountry(char text[CITY_CURRENCY_TEXT_CAPACITY], i64 value, const char *language);
void FormatCityCurrency(Game *game, char text[CITY_CURRENCY_TEXT_CAPACITY], i64 value);
bool SetCityMapLegendColumn(Game *game, CityStatusState *status, int column, const char *text, u32 color);

void RefreshCityStatusPanel(City *city, CityStatusState *status, int speed, int force);

#endif
