#include "base/utf8.h"

bool ReadUtf8CharacterBounded(const char **text, const char *end, u32 *character) {
  const u8 *input = (const u8 *)*text;
  *character = 0;
  if (*text == end) return false;
  u32 code = input[0];
  int count = code < 128 ? 1 : code >= 0xC2 && code <= 0xDF ? 2 :
      code >= 0xE0 && code <= 0xEF ? 3 : code >= 0xF0 && code <= 0xF4 ? 4 : 0;
  if ((size_t)(end - *text) < (size_t)count) count = 0;
  if (count > 1) {
    code &= (1 << (7 - count)) - 1;
    for (int index = 1; index < count; ++index) {
      if ((input[index] & 0xC0) != 0x80) { count = 0; break; }
      code = code << 6 | (input[index] & 63);
    }
    if ((count == 2 && code < 0x80) || (count == 3 && code < 0x800) ||
        (count == 4 && code < 0x10000) || (code >= 0xD800 && code <= 0xDFFF) || code > 0x10FFFF)
      count = 0;
  }
  *text += count ? count : 1;
  *character = count ? code : 0xFFFD;
  return count != 0;
}

u32 ReadUtf8Character(const char **text) {
  if (!**text) return 0;
  size_t length = 1;
  while (length < 4 && (*text)[length]) ++length;
  u32 character;
  ReadUtf8CharacterBounded(text, *text + length, &character);
  return character;
}

int CountUtf8Characters(const void *bytes, size_t length) {
  const char *text = bytes, *end = text + length;
  int count = 0;
  while (text < end) {
    u32 character;
    if (!ReadUtf8CharacterBounded(&text, end, &character) || !character || count == INT32_MAX) return -1;
    ++count;
  }
  return count;
}

int WriteUtf8Character(char text[4], u32 code) {
  if (code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) code = 0xFFFD;
  int count = code < 0x80 ? 1 : code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
  if (count == 1) text[0] = code;
  else {
    for (int index = count - 1; index > 0; --index) {
      text[index] = 0x80 | (code & 63);
      code >>= 6;
    }
    text[0] = (0xFF << (8 - count)) | code;
  }
  return count;
}

size_t Utf8PrefixBytes(const char *text, size_t limit) {
  const char *at = text;
  size_t length = 0;
  while (*at && length < limit) {
    ReadUtf8Character(&at);
    if ((size_t)(at - text) > limit) break;
    length = at - text;
  }
  return length;
}
