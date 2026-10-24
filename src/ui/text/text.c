#include "ui/text/text.h"
#include "base/text.h"
#include "base/utf8.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "city/presentation.h"
#include "city/state.h"
#include "game/errors.h"
#include "game/state.h"

const char *CityNoticeText(int notice) {
  switch (notice) {
  case CITY_NOTICE_ERROR_OPENING_FILE: return "... could not open the file.";
  case CITY_NOTICE_ERROR_READING_FILE: return "... could not read the file.";
  case CITY_NOTICE_ERROR_DECOMPRESSING_FILE: return "... city data could not be decompressed.";
  case CITY_NOTICE_UNRECOGNIZED_CITY_FORMAT: return "... unrecognized city file format.";
  case CITY_NOTICE_INVALID_CITY_CONTENTS: return "... invalid city file contents.";
  case CITY_NOTICE_ERROR_WRITING_FILE: return "... could not write the city file.";
  case CITY_NOTICE_PLACEMENT_AT_MAP_EDGE: return "... cannot build on the map edge.";
  case CITY_NOTICE_PLACEMENT_PROTEST: return "... residents object to this location.";
  case CITY_NOTICE_MARINA_REQUIRES_SHORELINE: return "... marina must span land and water.";
  case CITY_NOTICE_HYDRO_REQUIRES_WATERFALL: return "... hydroelectric dams require a waterfall.";
  case CITY_NOTICE_TUNNEL_CANNOT_CROSS_TUNNELS: return "... another tunnel is in the way.";
  case CITY_NOTICE_TUNNEL_EXCEEDS_CITY_LIMITS: return "... tunnel would extend beyond the map.";
  case CITY_NOTICE_TUNNEL_TOO_DEEP: return "... too deep to build.";
  case CITY_NOTICE_TUNNEL_UNSTABLE_EXIT: return "... exit terrain is unstable.";
  case CITY_NOTICE_TUNNEL_UNDERGROUND_OBSTRUCTION: return "... subway tracks or water pipes block construction.";
  case CITY_NOTICE_TUNNEL_REQUIRES_HILLSIDE: return "... tunnel entrance needs a hillside.";
  case CITY_NOTICE_NATIONAL_GUARD_DEPLOYED:
    return "National emergency units have arrived because local police and fire services are unavailable.";
  case CITY_NOTICE_NO_POLICE_TO_DISPATCH: return "... no police units available.";
  case CITY_NOTICE_CANNOT_DISPATCH_POLICE_HERE: return "... police cannot reach this location.";
  case CITY_NOTICE_NO_FIRETRUCKS_TO_DISPATCH: return "... no fire engines available.";
  case CITY_NOTICE_CANNOT_DISPATCH_FIRETRUCKS_HERE: return "... fire engines cannot reach this location.";
  case CITY_NOTICE_NO_MILITARY_TO_DISPATCH: return "... no military units available.";
  case CITY_NOTICE_CANNOT_DISPATCH_MILITARY_HERE: return "... military units cannot reach this location.";
  case CITY_NOTICE_TREE_REMOVAL_PROTEST: return "Residents are protesting the removal of these trees.";
  case CITY_NOTICE_ERROR_EXTRACTING_SCENARIO_RESOURCE: return "... could not read the scenario data.";
  case CITY_NOTICE_MILITARY_BASE_OFFER:
    return "The military wants land for a base. It may bring in more revenue, but it could also create "
        "problems for the city. Grant the land?";
  case CITY_NOTICE_MILITARY_BASE_ARMY: return "An army base has opened in the city.";
  case CITY_NOTICE_MILITARY_BASE_AIR_FORCE: return "An air base has opened in the city.";
  case CITY_NOTICE_MILITARY_BASE_NAVY: return "A naval base has opened on the coast.";
  case CITY_NOTICE_MILITARY_BASE_MISSILE_SILOS: return "Six defense installations have opened in the city.";
  case CITY_NOTICE_SHORELINE_NEEDS_CLEARING: return "... clear the shoreline first.";
  case CITY_NOTICE_NEEDS_OPPOSITE_SHORE: return "... no opposite shore.";
  case CITY_NOTICE_CROSSING_BLOCKED: return "... another crossing blocks the route.";
  case CITY_NOTICE_INSUFFICIENT_OPERATING_FUNDS: return "... not enough money.";
  case CITY_NOTICE_OPPOSITE_SHORE_TOO_STEEP: return "... opposite shore is too steep.";
  case CITY_NOTICE_SHORELINE_TOO_STEEP: return "... shoreline is too steep.";
  case CITY_NOTICE_CANNOT_CROSS_OCEAN: return "... route cannot cross open ocean.";
  case CITY_NOTICE_SPAN_BLOCKED_BY_CROSSING: return "... another crossing blocks the route.";
  case CITY_NOTICE_SIGN_LIMIT_REACHED: return "... sign limit reached.";
  case CITY_NOTICE_CANNOT_PLACE_LABEL_HERE: return "... cannot place a sign here.";
  case CITY_NOTICE_MAYOR_PARADE: return "Residents are holding a parade in your honor. Try to look modest.";
  case CITY_NOTICE_MISSION_WON:
    return "All scenario goals met! You can keep building. The council is already taking the credit.";
  case CITY_NOTICE_MISSION_FAILED:
    return "The scenario deadline has passed, and the goals are still unmet. The voters have decided to give "
        "someone else a turn at the desk.";
  case CITY_NOTICE_BANKRUPT:
    return "The city can no longer pay its debts. Residents are waiting outside your office with a moving box.";
  case CITY_NOTICE_BOND_LIMIT_REACHED: return "... bond limit reached.";
  case CITY_NOTICE_BOND_REPAYMENT_REQUIRES_FUNDS: return "... repayment requires $10,000 in cash.";
  case CITY_NOTICE_FISCAL_CRISIS:
    return "The treasury is in trouble. Cut city spending sharply; the council's usual talent for optimism "
        "won't cover the bills.";
  case CITY_NOTICE_BOND_CREDIT_TOO_LOW: return "... credit rating must improve before issuing another bond.";
  case CITY_NOTICE_SELECT_VALID_STADIUM_SPORT: return "... select a valid sport.";
  case CITY_NOTICE_MILITARY_BASE_UNSUITABLE:
    return "No suitable site was found for a military base. The request has been withdrawn.";
  case CITY_NOTICE_LOAD_CITY_FAILED: return "... could not load the city.";
  case CITY_NOTICE_ARCOLOGY_EXODUS_BEGUN: return "The launch has started.\n";
  case CITY_NOTICE_ARCOLOGY_EXODUS_COMPENSATION:
    return "The launch communities have departed. Construction compensation has been added to the treasury.";
  case CITY_NOTICE_ACTION_FAILED: return "... action could not be completed.";
  default: return "";
  }
}


// Only the reason is normalized; subsequent context can contain case-sensitive filenames.
char *FormatCityErrorReason(const char *reason) {
  if (!reason || !*reason) reason = "action could not be completed";
  const char *line_end = strchr(reason, '\n');
  if (!line_end) line_end = reason + strlen(reason);
  const char *start = reason, *end = line_end;
  while (start < end && (*start == '.' || *start == ' ' || *start == '\t')) ++start;
  while (end > start && (isspace((u8)end[-1]) || end[-1] == '.' || end[-1] == '!' || end[-1] == '?')) --end;
  char *text = AllocateText(strlen(reason) + 6);
  memcpy(text, "... ", 4);
  size_t length = 4;
  for (const char *p = start; p < end; ++p)
    text[length++] = *p >= 'A' && *p <= 'Z' ? *p + ('a' - 'A') : *p;
  text[length++] = '.';
  memcpy(text + length, line_end, strlen(line_end) + 1);
  return text;
}

static int CityTextPrevious(const char *text, int caret) {
  const char *position = text;
  int previous = 0;
  while (*position && position - text < caret) {
    previous = position - text;
    ReadUtf8Character(&position);
  }
  return previous;
}

int CityTextNext(const char *text, int caret) {
  const char *position = text + caret;
  if (*position) ReadUtf8Character(&position);
  return position - text;
}

void DeleteCityTextSelection(char *text, int *caret, int *anchor) {
  int first = IntMin(*caret, *anchor), last = IntMax(*caret, *anchor);
  memmove(text + first, text + last, strlen(text + last) + 1);
  *caret = *anchor = first;
}

bool HandleCityTextSelectionKey(char *text, int *caret, int *anchor, CityHotkey key) {
  int length = strlen(text);
  *caret = IntClamp(*caret, 0, length);
  *anchor = IntClamp(*anchor, 0, length);
  if (key == (CITY_HOTKEY_CTRL | 'A')) { *anchor = 0; *caret = length; return true; }
  bool extend = (key & CITY_HOTKEY_SHIFT) != 0;
  key &= ~CITY_HOTKEY_SHIFT;
  if (key == CITY_KEY_LEFT)
    *caret = !extend && *caret != *anchor ? IntMin(*caret, *anchor) : CityTextPrevious(text, *caret);
  else if (key == CITY_KEY_RIGHT)
    *caret = !extend && *caret != *anchor ? IntMax(*caret, *anchor) : CityTextNext(text, *caret);
  else if (key == CITY_KEY_HOME) *caret = 0;
  else if (key == CITY_KEY_END) *caret = length;
  else if (key == CITY_KEY_BACKSPACE || key == CITY_KEY_DELETE) {
    if (*caret == *anchor)
      *anchor = key == CITY_KEY_BACKSPACE ? CityTextPrevious(text, *caret) : CityTextNext(text, *caret);
    DeleteCityTextSelection(text, caret, anchor);
    return true;
  } else return false;
  if (!extend) *anchor = *caret;
  return true;
}

void InsertCityTextSelection(char *text, size_t capacity, int *caret, int *anchor,
    const char *input, bool digits_only) {
  int length = strlen(text);
  *caret = IntClamp(*caret, 0, length);
  *anchor = IntClamp(*anchor, 0, length);
  while (*input) {
    const char *start = input;
    u32 character = ReadUtf8Character(&input);
    if (character < 32 || character == 0x7F || character == 0xFFFD ||
        (digits_only && (character < '0' || character > '9'))) continue;
    int bytes = input - start;
    if (*caret != *anchor) {
      DeleteCityTextSelection(text, caret, anchor);
      length = strlen(text);
    }
    if ((size_t)length + bytes >= capacity) continue;
    memmove(text + *caret + bytes, text + *caret, length - *caret + 1);
    memcpy(text + *caret, start, bytes);
    *caret += bytes;
    *anchor = *caret;
    length += bytes;
  }
}

// Legacy controls expose an all-or-nothing selection flag; the edit engine uses byte ranges.
bool HandleCityTextEditKey(char *text, int *caret, int *select_all, CityHotkey key) {
  if (key & CITY_HOTKEY_SHIFT) {
    *caret = IntClamp(*caret, 0, strlen(text));
    return false;
  }
  int position = *select_all ? strlen(text) : *caret;
  int anchor = *select_all ? 0 : position;
  bool handled = HandleCityTextSelectionKey(text, &position, &anchor, key);
  *caret = handled ? position : IntClamp(*caret, 0, strlen(text));
  if (handled) *select_all = key == (CITY_HOTKEY_CTRL | 'A');
  return handled;
}

void InsertCityTextInput(char *text, size_t capacity, int *caret, int *select_all,
    const char *input, bool digits_only) {
  int anchor = *select_all ? 0 : *caret;
  if (*select_all) *caret = strlen(text);
  InsertCityTextSelection(text, capacity, caret, &anchor, input, digits_only);
  *select_all = *caret != anchor;
}

// Original picture-mask formatting, including negative/zero sections and German separators.
char *FormatCityNumber(Game *game, char *text, const char *mask, int width, double value) {
  char picture[104];
  if (width < 1 || width >= CITY_NUMBER_TEXT_CAPACITY || strlen(mask) >= sizeof(picture)) {
    text[0] = '\0';
    SetGameError(game, SC2K_ERROR_NUMBER_FORMAT);
    return text;
  }
  CopyText(picture, sizeof(picture), mask);
  bool german = strcmp((const char *)game->city->country.character, "ger") == 0;
  if (german)
    for (int i = 0; picture[i] != '\0'; ++i) {
      if (picture[i] == '.') picture[i] = ',';
      else if (picture[i] == ',') picture[i] = '.';
    }
  char *sections[3] = {NULL, NULL, NULL};
  char *section = picture;
  for (int i = 0; i < 3; ++i) {
    section += strspn(section, ";");
    if (*section == '\0')
      break;
    sections[i] = section;
    section += strcspn(section, ";");
    if (*section != '\0')
      *section++ = '\0';
  }
  const char *pattern = sections[0];
  if (pattern == NULL) {
    text[0] = '\0';
    SetGameError(game, SC2K_ERROR_NUMBER_FORMAT);
    return text;
  }
  if (value < 0.0 && sections[1] != NULL) {
    pattern = sections[1];
    value = -value;
  } else if (value == 0.0 && sections[2] != NULL) {
    pattern = sections[2];
  }
  int decimals = 0;
  const char *dot = strchr(pattern, '.');
  if (dot != NULL)
    for (const char *p = dot + 1; *p != '\0'; ++p)
      if (*p == '#' || *p == '0')
        ++decimals;
  int first_digit = (int)strcspn(pattern, "#0");
  int pattern_length = (int)strlen(pattern);
  // Reachable calls fit their fixed output width. Oversized numeric input was a stock
  // buffer overrun; retain a defined error rather than reproducing host undefined behavior.
  int length = snprintf(text, (size_t)width + 1, "%-*.*f", width, decimals, value);
  if (length < 0 || length > width) {
    SetGameError(game, SC2K_ERROR_NUMBER_FORMAT);
    text[0] = '\0';
    return text;
  }
  char *space = strchr(text, ' ');
  int source = space != NULL ? (int)(space - text) - 1 : (int)strlen(text) - 1;
  int output = width;
  bool nonzero_fraction = false;
  bool grouped = false;
  bool fractional = decimals > 0;
  int position = pattern_length;
  while (source >= 0) {
    if (output < 0 || position < 0 || position > pattern_length) {
      SetGameError(game, SC2K_ERROR_NUMBER_FORMAT);
      text[0] = '\0';
      return text;
    }
    char ch = pattern[position];
    switch (ch) {
    case '#':
    case '0':
      if (ch == '0') {
        text[output--] = text[source--];
        nonzero_fraction = true;
      } else if (fractional) {
        if (nonzero_fraction || text[source] != '0') {
          text[output--] = text[source--];
          nonzero_fraction = true;
        } else {
          --source;
        }
      } else if (nonzero_fraction && value > 0.0 && value < 1.0) {
        --source;
      } else {
        text[output--] = text[source--];
      }
      break;
    case ',':
      text[output--] = ch;
      grouped = true;
      break;
    case '.':
      if (nonzero_fraction)
        text[output--] = text[source];
      fractional = false;
      --source;
      break;
    default:
      text[output--] = ch;
      break;
    }
    --position;
    if (position < first_digit) {
      if (source < 0)
        break;
      position = first_digit + (grouped ? 3 : 0);
    }
  }
  while (output >= 0) {
    if (position < 0) {
      text[output--] = ' ';
    } else {
      char ch = pattern[position--];
      if (ch != '#' && ch != ',')
        text[output--] = ch;
    }
  }
  char *negative_comma = strstr(text, "-,");
  if (negative_comma != NULL) {
    negative_comma[0] = ' ';
    negative_comma[1] = '-';
  }
  if (german)
    for (int i = 0; text[i] != '\0'; ++i) {
      if (text[i] == '.') text[i] = ',';
      else if (text[i] == ',') text[i] = '.';
    }
  return text;
}

// Group integer currency without losing precision or separating the sign from its prefix.
void FormatCityCurrencyForCountry(char text[CITY_CURRENCY_TEXT_CAPACITY], i64 value, const char *language) {
  // Format integers directly: double cannot preserve all 64 treasury bits.
  bool french = !strcmp(language, "fre"), german = !strcmp(language, "ger");
  u64 magnitude = value < 0 ? 0 - (u64)value : (u64)value;
  char digits[21];
  snprintf(digits, sizeof(digits), "%llu", (unsigned long long)magnitude);
  int length = strlen(digits), position = 0;
  if (value < 0) text[position++] = '-';
  if (!french && !german) text[position++] = '$';
  for (int index = 0; index < length; ++index) {
    text[position++] = digits[index];
    if (index + 1 < length && (length - index - 1) % 3 == 0) text[position++] = german ? '.' : ',';
  }
  if (french || german) {
    text[position++] = ' ';
    text[position++] = french ? 'F' : 'D';
    text[position++] = french ? 'F' : 'M';
  }
  text[position] = 0;
}

void FormatCityCurrency(Game *game, char text[CITY_CURRENCY_TEXT_CAPACITY], i64 value) {
  FormatCityCurrencyForCountry(text, value, (const char *)game->city->country.character);
}
