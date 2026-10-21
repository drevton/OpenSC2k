#ifndef SC2K_UTF8_H
#define SC2K_UTF8_H

#include <stddef.h>
#include "base/types.h"

// Native text is UTF-8. Invalid input advances one byte and returns U+FFFD.
u32 ReadUtf8Character(const char **text);
// A bounded byte span may contain no terminator. Invalid input consumes one byte;
// an empty span consumes none. The validity result distinguishes a literal U+FFFD.
bool ReadUtf8CharacterBounded(const char **text, const char *end, u32 *character);
// Reject invalid input and embedded terminators; return -1 on failure.
int CountUtf8Characters(const void *text, size_t length);
int WriteUtf8Character(char text[4], u32 character);
// Largest complete UTF-8 prefix fitting in the byte limit, excluding the terminator.
size_t Utf8PrefixBytes(const char *text, size_t limit);

#endif
