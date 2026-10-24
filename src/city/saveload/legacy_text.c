#include "city/saveload/legacy_text.h"
#include "base/utf8.h"

#include <stdlib.h>
#include <string.h>
#include "base/text.h"

static const u8 kCityCp437ToAnsi[128] = {
  0xC7, 0xFC, 0xE9, 0xE2, 0xE4, 0xE0, 0xE5, 0xE7, 0xEA, 0xEB, 0xE8, 0xEF, 0xEE, 0xEC, 0xC4, 0xC5,
  0xC9, 0xE6, 0xC6, 0xF4, 0xF6, 0xF2, 0xFB, 0xF9, 0xFF, 0xD6, 0xDC, 0xA2, 0xA3, 0xA5, 0x3F, 0x83,
  0xE1, 0xED, 0xF3, 0xFA, 0xF1, 0xD1, 0xAA, 0xBA, 0xBF, 0x3F, 0xAC, 0xBD, 0xBC, 0xA1, 0xAB, 0xBB,
  0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
  0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
  0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
  0x3F, 0xDF, 0x3F, 0x3F, 0x3F, 0x3F, 0xB5, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
  0x3F, 0xB1, 0x3F, 0x3F, 0x3F, 0x3F, 0xF7, 0x3F, 0xB0, 0x3F, 0xB7, 0x3F, 0x3F, 0xB2, 0x3F, 0xA0,
};

// Decode one byte using the selected stock text encoding, including scenario display substitutions. Returns a Unicode
// code point; native runtime strings remain UTF-8 outside this codec boundary.
static u32 DecodeLegacyCityCharacter(u8 byte, CityLegacyTextEncoding encoding) {
  if (encoding == CITY_TEXT_WINDOWS_1252) return DecodeWindows1252Byte(byte);
  if (encoding == CITY_TEXT_OEM_437) return DecodeOem437Byte(byte);
  static const u8 kDisplaySubstitutions[256] = {
    [0x83] = 0xFC, [0xA3] = 0xFA, [0xA6] = 0xC3, [0xBA] = 0xA7,
    [0xC4] = 0xE9, [0xC5] = 0xE8, [0xC6] = 0xED, [0xC7] = 0xC4,
    [0xC9] = 0xEA, [0xD6] = 0xF4, [0xDC] = 0xF6, [0xE0] = 0xD6,
    [0xE2] = 0xC9, [0xE4] = 0xD1, [0xE5] = 0xDC, [0xE6] = 0xEB,
    [0xE7] = 0xE1, [0xE8] = 0xE4, [0xE9] = 0xC7, [0xEA] = 0xE0,
    [0xEB] = 0xE2, [0xEC] = 0xE7, [0xEE] = 0xE5, [0xEF] = 0xE3,
    [0xF2] = 0xEF, [0xF4] = 0xEC, [0xF6] = 0xEE, [0xF9] = 0xF3,
    [0xFB] = 0xF1, [0xFC] = 0xC5, [0xFF] = 0xF2,
  };
  u8 converted = byte < 128 ? byte : kCityCp437ToAnsi[byte - 128];
  if (kDisplaySubstitutions[converted]) converted = kDisplaySubstitutions[converted];
  return DecodeWindows1252Byte(converted);
}

// Convert a bounded stock byte sequence to terminated UTF-8 without splitting a character. Output must hold capacity
// bytes; stops at input NUL or the byte limit and returns bytes written excluding the terminator.
size_t DecodeLegacyCityText(char *output, size_t capacity, const void *bytes, size_t length,
    CityLegacyTextEncoding encoding) {
  const u8 *source = bytes;
  size_t used = 0;
  for (size_t index = 0; index < length && source[index]; ++index) {
    char text[4];
    int count = WriteUtf8Character(text, DecodeLegacyCityCharacter(source[index], encoding));
    if (used + count >= capacity) break;
    memcpy(output + used, text, count);
    used += count;
  }
  if (capacity) output[used] = 0;
  return used;
}

// Project terminated UTF-8 to at most capacity Windows-1252 bytes. Output is a fixed record payload and is not
// automatically terminated; returns bytes written, substituting '?' for unrepresentable code points.
size_t EncodeLegacyCityText(u8 *output, size_t capacity, const char *text) {
  size_t used = 0;
  while (*text && used < capacity) output[used++] = EncodeWindows1252Character(ReadUtf8Character(&text));
  return used;
}

// Convert a terminated scenario string in place through a temporary source copy and normalize stock punctuation.
// Buffer must hold capacity bytes; the UTF-8 result is bounded and the temporary copy is released.
void DecodeCityScenarioText(char *text, size_t capacity) {
  char *source = DuplicateText(text);
  DecodeLegacyCityText(text, capacity, source, strlen(source), CITY_TEXT_SCENARIO);
  free(source);
  for (char *at = text; *at; ++at)
    if (*at == '-' && strchr("!:?;,.", at[1]) && at[1]) memmove(at, at + 1, strlen(at));
}

// Check whether terminated UTF-8 round-trips exactly through Windows-1252 within a character limit. Returns false for
// lossy or overlong text; does not encode into or modify caller storage.
bool CanEncodeLegacyCityText(const char *text, size_t limit) {
  size_t count = 0;
  while (*text) {
    u32 character = ReadUtf8Character(&text);
    if (DecodeWindows1252Byte(EncodeWindows1252Character(character)) != character || ++count > limit)
      return false;
  }
  return true;
}
