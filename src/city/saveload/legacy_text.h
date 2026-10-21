#ifndef SC2K_LEGACY_TEXT_H
#define SC2K_LEGACY_TEXT_H

#include "base/utf8.h"

typedef enum { CITY_TEXT_WINDOWS_1252, CITY_TEXT_OEM_437, CITY_TEXT_SCENARIO } CityLegacyTextEncoding;
u16 DecodeCityWindows1252Byte(u8 byte);
u8 EncodeCityWindows1252Character(u32 unicode);
u32 DecodeLegacyCityCharacter(u8 byte, CityLegacyTextEncoding encoding);
// Decode stock bytes at import. Output truncates only between complete UTF-8 characters.
size_t DecodeLegacyCityText(char *output, size_t capacity, const void *bytes, size_t length,
    CityLegacyTextEncoding encoding);
// Stock exports substitute '?' for code points the file cannot represent.
size_t EncodeLegacyCityText(u8 *output, size_t capacity, const char *text);
bool CanEncodeLegacyCityText(const char *text, size_t limit);
void DecodeCityScenarioText(char *text, size_t capacity);

#endif
