#ifndef SC2K_BASE_CONFIG_H
#define SC2K_BASE_CONFIG_H

#include "base/types.h"

typedef struct ConfigLine ConfigLine;
typedef struct {
  ConfigLine *first, *last;
  bool changed;
} Config;

typedef enum ConfigValueType {
  CONFIG_MISSING, CONFIG_INTEGER, CONFIG_STRING
} ConfigValueType;
typedef struct ConfigValue {
  ConfigValueType type;
  union {
    i64 integer;
    const char *string;
  };
} ConfigValue;

// Dotted bare keys, signed decimal integers, booleans and quoted UTF-8 strings.
// Retain every line and replace only edited value spans, including inline comments.
// Zero-initialize before first use. A successful load replaces the document; failure leaves it unchanged.
// A missing file counts as success and leaves the document unchanged.
bool LoadConfig(Config *config, const char *path);
// Write changed contents atomically. Success clears changed; failure retains it for retry.
bool WriteConfig(Config *config, const char *path);
// Release owned lines/strings and clear the document. Safe to repeat.
void FreeConfig(Config *config);
// One lookup distinguishes a missing key from an integer (including booleans) or a string.
// Read only the payload selected by type. Integers are copied; terminated strings are borrowed until
// this key changes or the document is replaced/freed. Reading allocates nothing and leaves Config unchanged.
ConfigValue ReadConfigValue(const Config *config, const char *key);
// Copy supplied values. Failure leaves the document unchanged; equal values preserve their original spelling.
bool SetConfigInteger(Config *config, const char *key, i64 value, bool boolean);
bool SetConfigString(Config *config, const char *key, const char *value);

#endif
