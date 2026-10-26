#include "ui/text/information.h"

#include <stdio.h>
#include <string.h>

#include "assets/resources.h"
#include "assets/state.h"
#include "city/economy/facilities.h"
#include "city/maps/labels.h"
#include "city/state.h"
#include "drawing/fonts/fonts.h"
#include "game/errors.h"
#include "game/state.h"

enum CityFacilityTextError {
  CITY_FACILITY_TEXT_INVALID_GRADE = -150,
  CITY_FACILITY_TEXT_INVALID_TEAM_NAME = -151,
  CITY_FACILITY_TEXT_INVALID_TEMPLATE = -152,
  CITY_FACILITY_TEXT_INVALID_OFFSET = -153,
  CITY_FACILITY_TEXT_INVALID_SLOT = -154,
};

// Each byte keeps the existing canvas glyph interpretation, including an embedded NUL.
static void WriteCityInformationText(CityInformationText *text, const char *value, int length) {
  CityCanvasInformationCursor *cursor = text->cursor;
  for (int index = 0; index < length; ++index) {
    char character[2] = {value[index], '\0'};
    CanvasTextAt(cursor->canvas, cursor->x, cursor->y, character, cursor->color);
    cursor->x += CanvasTextWidth(character);
  }
}

static int MeasureCityInformationText(const char *value) {
  int width = 0;
  while (*value != '\0') {
    char character[2] = {*value++, '\0'};
    width += CanvasTextWidth(character);
  }
  return width;
}

// Draw terminated text at the cursor's current position.
static void DrawCityTextLiteral(CityInformationText *text, const char *value) {
  WriteCityInformationText(text, value, (int)strlen(value));
}

// Draw an explicit byte span, including embedded NULs.
void DrawCityTextBytes(CityInformationText *text, const char *value, int length) {
  WriteCityInformationText(text, value, length);
}

// A single character, including NUL, has an explicit length of one.
static void DrawCityTextCharacter(CityInformationText *text, char value) {
  WriteCityInformationText(text, &value, 1);
}

// Signed decimal without thousands separators.
static void DrawCityTextInteger(CityInformationText *text, int value) {
  char number[20];
  snprintf(number, sizeof(number), "%d", value);
  DrawCityTextLiteral(text, number);
}

// Zero-extend the input word before decimal formatting.
static void DrawCityTextUnsignedWord(CityInformationText *text, u16 value) {
  DrawCityTextInteger(text, value);
}

// Expand one of the five localized facility-information lines.
void DrawCityFacilityInformationTemplate(CityInformationText *text, int facility, int line) {
  static const char *const kGrades[13] = {
    "F", "D-", "D", "D+", "C-", "C", "C+", "B-", "B", "B+", "A-", "A", "A+"
  };
  City *city = text->city;
  if (facility < 0 || facility >= CityFacilityRecordLimit(city)) {
    SetGameError(city->game, CITY_FACILITY_TEXT_INVALID_SLOT);
    return;
  }
  const CityAnnualFacilityRecord *record = CityFacilityRecordAt(city, facility);
  int facility_class = kFacilityClassByTile[record->type];
  if (facility_class >= CITY_FACILITY_INFO_COUNT || line < 0 || line >= CITY_FACILITY_INFO_LINE_COUNT) {
    SetGameError(city->game, CITY_FACILITY_TEXT_INVALID_TEMPLATE);
    return;
  }
  const char *format = city->game->resources->facility_information[facility_class][line];
  if (format == NULL)
    return;
  int length = strlen(format);
  for (int index = 0; index < length; ++index) {
    // Substitutions also change facility_class; the next character uses that updated class.
    if (facility_class >= CITY_FACILITY_INFO_COUNT) {
      SetGameError(city->game, CITY_FACILITY_TEXT_INVALID_TEMPLATE);
      return;
    }
    format = city->game->resources->facility_information[facility_class][line];
    if (format == NULL || (size_t)index > strlen(format)) {
      SetGameError(city->game, CITY_FACILITY_TEXT_INVALID_OFFSET);
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
        SetGameError(city->game, CITY_FACILITY_TEXT_INVALID_GRADE);
      break;
    case 'S':
      DrawCityTextLiteral(text,
          record->stadium_team < CITY_TEAM_NAME_COUNT ? kCitySports[record->stadium_team] : "");
      break;
    case 'T': {
      if (record->stadium_team_name_slot >= CITY_OVERLAY_NAME_COUNT) {
        SetGameError(city->game, CITY_FACILITY_TEXT_INVALID_TEAM_NAME);
        break;
      }
      const char *name = (const char *)ReadCityOverlayName(city, record->stadium_team_name_slot)->character;
      facility_class = record->stadium_team_name_slot;
      text->cursor->x += MeasureCityInformationText(name) / 2;
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
