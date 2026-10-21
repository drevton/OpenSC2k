#ifndef SC2K_BASE_TEXT_H
#define SC2K_BASE_TEXT_H

#include <stddef.h>
#include <string.h>
#include "base/types.h"
#include "base/utf8.h"

// Buffer

// Copy a terminated string without writing past the destination buffer.
static inline void CopyText(char *destination, size_t capacity, const char *source) {
  if (capacity == 0) return;
  size_t length = Utf8PrefixBytes(source, capacity - 1);
  memmove(destination, source, length);
  destination[length] = 0;
}

bool TextContains(const char *name, const char *filter, bool match_case);

// Owned variable-length text. Setters copy before freeing, so aliases are safe.
// Allocation failure terminates the native process; callers do not branch on OOM.
char *AllocateText(size_t bytes);
char *DuplicateText(const char *text);
void SetText(char **owned, const char *text);
// Source length is in bytes; the destination always receives a trailing NUL.
void SetTextN(char **owned, const char *text, size_t length);
void FormatText(char **owned, const char *format, ...);

#endif
