#include "base/text.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
