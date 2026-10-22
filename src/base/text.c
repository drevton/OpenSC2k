#include "base/text.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const u16 kWindows1252[32] = {
  0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
  0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
  0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
  0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
};

u16 DecodeWindows1252Byte(u8 byte) {
  return byte >= 128 && byte < 160 ? kWindows1252[byte - 128] : byte;
}

u8 EncodeWindows1252Character(u32 unicode) {
  if (unicode < 128 || (unicode >= 160 && unicode <= 255)) return unicode;
  for (int index = 0; index < 32; ++index)
    if (unicode == kWindows1252[index]) return index + 128;
  return '?';
}

static const u16 kOem437[128] = {
  0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
  0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
  0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
  0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
  0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
  0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
  0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
  0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
  0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
  0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
  0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
  0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
  0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
  0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
  0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
  0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0,
};

u16 DecodeOem437Byte(u8 byte) {
  return byte < 128 ? byte : kOem437[byte - 128];
}

bool TextContains(const char *name, const char *filter, bool match_case) {
  for (int start = 0;; ++start) {
    int index = 0;
    while (filter[index] && name[start + index] &&
        (match_case ? (u8)name[start + index] == (u8)filter[index] :
            tolower((u8)name[start + index]) == tolower((u8)filter[index]))) ++index;
    if (!filter[index]) return true;
    if (!name[start]) return false;
  }
}

char *AllocateText(size_t bytes) {
  char *text = malloc(bytes);
  if (text == NULL) {
    fputs("out of memory allocating text.\n", stderr);
    exit(EXIT_FAILURE);
  }
  return text;
}

char *DuplicateText(const char *text) {
  size_t bytes = strlen(text) + 1;
  char *copy = AllocateText(bytes);
  memcpy(copy, text, bytes);
  return copy;
}

void SetTextN(char **owned, const char *text, size_t length) {
  if (length == SIZE_MAX) {
    fputs("text is too long to allocate.\n", stderr);
    exit(EXIT_FAILURE);
  }
  char *copy = AllocateText(length + 1);
  if (length) memcpy(copy, text, length);
  copy[length] = 0;
  free(*owned);
  *owned = copy;
}

void SetText(char **owned, const char *text) {
  SetTextN(owned, text, strlen(text));
}

void FormatText(char **owned, const char *format, ...) {
  va_list args, measure;
  va_start(args, format);
  va_copy(measure, args);
  int length = vsnprintf(NULL, 0, format, measure);
  va_end(measure);
  if (length < 0) {
    fputs("could not format text.\n", stderr);
    exit(EXIT_FAILURE);
  }
  char *text = AllocateText((size_t)length + 1);
  vsnprintf(text, (size_t)length + 1, format, args);
  va_end(args);
  free(*owned);
  *owned = text;
}
