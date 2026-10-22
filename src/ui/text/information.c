#include "ui/text/information.h"

#include "city/maps/labels.h"

#include <stdio.h>
#include <string.h>

#include "assets/resources.h"
#include "assets/state.h"
#include "city/economy/facilities.h"
#include "city/maps/layers.h"
#include "city/state.h"
#include "drawing/fonts/fonts.h"
#include "drawing/state.h"
#include "game/errors.h"
#include "game/state.h"

// Draw terminated text at the cursor's current position.
bool DrawCityTextLiteral(CityInformationText *text, const char *value) {
  return WriteCityInformationText(text, value, (int)strlen(value));
}

// Draw an explicit byte span, including embedded NULs.
bool DrawCityTextBytes(CityInformationText *text, const char *value, int length) {
  return WriteCityInformationText(text, value, length);
}

// A single character, including NUL, has an explicit length of one.
bool DrawCityTextCharacter(CityInformationText *text, char value) {
  return WriteCityInformationText(text, &value, 1);
}

// Signed decimal without thousands separators.
bool DrawCityTextInteger(CityInformationText *text, int value) {
  char number[20];
  snprintf(number, sizeof(number), "%d", value);
  return DrawCityTextLiteral(text, number);
}

// Zero-extend the input word before decimal formatting.
bool DrawCityTextUnsignedWord(CityInformationText *text, u16 value) {
  return DrawCityTextInteger(text, value);
}

// Expand one of the five localized facility-information lines.
void DrawCityFacilityInformationTemplate(CityInformationText *text, int facility, int line) {
  static const char *const kGrades[13] = {
    "F", "D-", "D", "D+", "C-", "C", "C+", "B-", "B", "B+", "A-", "A", "A+"
  };
  City *city = text->city;
  if (facility < 0 || facility >= CityFacilityRecordLimit(city)) {
    SetGameError(city->game, -154);
    return;
  }
  CityAnnualFacilityRecord *record = CityFacilityRecordAt(city, facility);
  int facility_class = kFacilityClassByTile[record->type];
  if (facility_class >= CITY_FACILITY_INFO_COUNT || line < 0 || line >= CITY_FACILITY_INFO_LINE_COUNT) {
    SetGameError(city->game, -152);
    return;
  }
  const char *format = city->game->resources->facility_information[facility_class][line];
  if (format == NULL)
    return;
  int length = strlen(format);
  for (int index = 0; index < length; ++index) {
    // Substitutions also change facility_class; the next character uses that updated class.
    if (facility_class >= CITY_FACILITY_INFO_COUNT) {
      SetGameError(city->game, -152);
      return;
    }
    format = city->game->resources->facility_information[facility_class][line];
    if (format == NULL || (size_t)index > strlen(format)) {
      SetGameError(city->game, -153);
      return;
    }
    char value = format[index];
    if (value != '#') {
      DrawCityTextCharacter(text, value);
      continue;
    }
    switch (format[++index]) {
    case '0': DrawCityTextInteger(text, record->age); break;
    case '1': DrawCityTextInteger(text, record->value_02); break;
    case '2': DrawCityTextInteger(text, record->value_04); break;
    case '3': DrawCityTextInteger(text, record->value_06); break;
    case 'G':
      facility_class = record->age;
      if (record->age < 13)
        DrawCityTextLiteral(text, kGrades[record->age]);
      else
        SetGameError(city->game, -150);
      break;
    case 'S':
      DrawCityTextLiteral(text,
          record->value_04 >= 0 && record->value_04 < 5 ? kCitySports[record->value_04] : "");
      break;
    case 'T': {
      if (record->value_06 >= 256) {
        SetGameError(city->game, -151);
        break;
      }
      const char *name = (const char *)ReadCityOverlayName(city, record->value_06)->character;
      facility_class = record->value_06;
      ShiftCityInformationText(text, MeasureCityInformationText(text, name) / 2);
      DrawCityTextLiteral(text, name);
      break;
    }
    case 'W':
      facility_class = record->age;
      if (record->age != 0) {
        DrawCityTextUnsignedWord(text, record->age);
        DrawCityTextCharacter(text, '-');
        DrawCityTextUnsignedWord(text, (u16)(40 - record->age));
      }
      break;
    default: break;
    }
  }
}

// Information Text

bool WriteCityInformationText(CityInformationText *text, const char *value, i32 length) {
  CityCanvasInformationCursor *cursor = text->cursor;
  if (cursor->compact_tile && cursor->y == cursor->origin_y + 8) {
    i32 width = CanvasFontTextWidth(value, kCanvasHeadingFont);
    CanvasTextFont(cursor->canvas, cursor->x - (width - CanvasTextWidth(value)) / 2,
        cursor->y - 1, value, cursor->color, 1, kCanvasHeadingFont);
    return true;
  }
  if (cursor->currency_part == 1) {
    CanvasTextAt(cursor->canvas, cursor->x, cursor->y, "$", cursor->color);
    cursor->x += CanvasTextWidth("$");
    cursor->currency_part = 0;
  }
  i32 x = cursor->x;
  if (cursor->alignment == CITY_TEXT_RIGHT_UPDATE_POSITION)
    for (i32 index = 0; index < length; ++index) {
      char character[2] = {value[index], '\0'};
      x -= CanvasTextWidth(character);
    }
  for (i32 index = 0; index < length; ++index) {
    char character[2] = {value[index], '\0'};
    CanvasTextAt(cursor->canvas, x, cursor->y, character, cursor->color);
    x += CanvasTextWidth(character);
    cursor->x += CanvasTextWidth(character);
  }
  return true;
}

i32 MeasureCityInformationText(CityInformationText *text, const char *value) {
  i32 width = 0;
  while (*value != '\0') {
    char character[2] = {*value++, '\0'};
    width += CanvasTextWidth(character);
  }
  return width;
}

void ShiftCityInformationText(CityInformationText *text, i32 dx) {
  CityCanvasInformationCursor *cursor = text->cursor;
  cursor->x += dx;
}

void PositionCityInformationText(CityInformationText *text, i32 x, i32 y, u32 alignment) {
  CityCanvasInformationCursor *cursor = text->cursor;
  const Drawing *drawing = text->city->game->drawing;
  int line_height = drawing->map_state.tile_information_line_height;
  const char *locale = (const char *)text->city->country.character;
  cursor->x = cursor->origin_x + x;
  cursor->y = cursor->origin_y + (cursor->compact_tile && y >= 40 && line_height > 0
      ? 24 + (y - 40) * 12 / line_height : cursor->compact_tile && y == 16 ? 8 : y);
  cursor->alignment = alignment;
  cursor->currency_part = cursor->compact_tile && alignment == CITY_TEXT_UPDATE_POSITION &&
      y == 40 + 3 * line_height && strcmp(locale, "usa") == 0;
}

u32 ColorCityInformationText(CityInformationText *text, u32 color) {
  CityCanvasInformationCursor *cursor = text->cursor;
  u32 previous = cursor->color;
  cursor->color = 0xFF000000u | ((color & 255) << 16) | (color & 0xFF00) | ((color >> 16) & 255);
  return (previous & 0xFF00) | ((previous >> 16) & 255) | ((previous & 255) << 16);
}
