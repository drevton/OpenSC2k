#ifndef SC2K_UI_INFORMATION_TEXT_H
#define SC2K_UI_INFORMATION_TEXT_H

#include "city/types.h"

typedef struct Canvas Canvas;

// Caller-owned facility text cursor. Writing advances x; the caller positions each line.
typedef struct CityCanvasInformationCursor {
  Canvas *canvas;
  i32 x;
  i32 y;
  u32 color;
} CityCanvasInformationCursor;

// UI borrows the inspector cursor and live City; glyph drawing uses Drawing services directly.
typedef struct CityInformationText {
  CityCanvasInformationCursor *cursor;
  City *city;
} CityInformationText;

// Draw an explicit byte span, including embedded NULs, and advance the borrowed cursor.
bool DrawCityTextBytes(CityInformationText *text, const char *value, int length);
// Expand one localized line using live facility data and borrowed Assets strings.
// Missing lines draw nothing; invalid facility/template substitutions set the Game error.
void DrawCityFacilityInformationTemplate(CityInformationText *text, int facility, int line);

#endif
