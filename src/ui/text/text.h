#ifndef SC2K_UI_TEXT_TEXT_H
#define SC2K_UI_TEXT_TEXT_H

#include <stddef.h>

#include "city/types.h"
#include "ui/input.h"

// Owned error reason; any newline-separated context retains its spelling and case.
char *FormatCityErrorReason(const char *reason);

// Native single-line editors use UTF-8 byte offsets at character boundaries.
// Mutable text is terminated; capacities include the terminator. Editing retains no borrowed input.

int CityTextNext(const char *text, int caret);
void DeleteCityTextSelection(char *text, int *caret, int *anchor);
bool HandleCityTextSelectionKey(char *text, int *caret, int *anchor, CityHotkey key);
void InsertCityTextSelection(char *text, size_t capacity, int *caret, int *anchor,
    const char *input, bool digits_only);
bool HandleCityTextEditKey(char *text, int *caret, int *select_all, CityHotkey key);
void InsertCityTextInput(char *text, size_t capacity, int *caret, int *select_all,
    const char *input, bool digits_only);

// Numbers

typedef struct Game Game;

enum { CITY_NUMBER_TEXT_CAPACITY = 64, CITY_CURRENCY_TEXT_CAPACITY = 40 };

// The caller supplies width+1 bytes. No allocation or RTL object is involved.
char *FormatCityNumber(Game *game, char *text, const char *mask, int width, double value);
void FormatCityCurrencyForCountry(char text[CITY_CURRENCY_TEXT_CAPACITY], i64 value, const char *language);
void FormatCityCurrency(Game *game, char text[CITY_CURRENCY_TEXT_CAPACITY], i64 value);

#endif
