#ifndef SC2K_UI_INFORMATION_TEXT_H
#define SC2K_UI_INFORMATION_TEXT_H

#include "city/types.h"

typedef struct Canvas Canvas;

// Caller-owned inspector text layout; no window or global drawing context is retained.
typedef struct CityCanvasInformationCursor {
  Canvas *canvas;
  i32 x;
  i32 y;
  i32 origin_x;
  i32 origin_y;
  u32 alignment;
  u32 color;
  i32 compact_tile;
  i32 currency_part;
} CityCanvasInformationCursor;

// UI borrows the inspector cursor and live City; glyph drawing uses Drawing services directly.
typedef struct CityInformationText {
  CityCanvasInformationCursor *cursor;
  City *city;
} CityInformationText;

bool DrawCityTextLiteral(CityInformationText *text, const char *value);
bool DrawCityTextBytes(CityInformationText *text, const char *value, int length);
bool DrawCityTextCharacter(CityInformationText *text, char value);
bool DrawCityTextUnsignedWord(CityInformationText *text, u16 value);
bool DrawCityTextInteger(CityInformationText *text, i32 value);
void DrawCityFacilityInformationTemplate(CityInformationText *text, int facility, int line);

bool WriteCityInformationText(CityInformationText *text, const char *value, i32 length);
i32 MeasureCityInformationText(CityInformationText *text, const char *value);
void ShiftCityInformationText(CityInformationText *text, i32 dx);
void PositionCityInformationText(CityInformationText *text, i32 x, i32 y, u32 alignment);
u32 ColorCityInformationText(CityInformationText *text, u32 color);

#endif
