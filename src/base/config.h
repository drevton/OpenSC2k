#ifndef SC2K_BASE_CONFIG_H
#define SC2K_BASE_CONFIG_H

#include "base/types.h"

typedef struct ConfigLine ConfigLine;
typedef struct {
  ConfigLine *first, *last;
  bool changed;
} Config;

// Dotted bare keys, signed decimal integers, booleans and quoted UTF-8 strings.
// Retain every line and replace only edited value spans, including inline comments.
// Zero-initialize before first use. A successful load replaces the document; failure leaves it unchanged.
// A missing file counts as success and leaves the document unchanged.
bool LoadConfig(Config *config, const char *path);
// Write changed contents atomically. Success clears changed; failure retains it for retry.
bool WriteConfig(Config *config, const char *path);
// Release owned lines/strings and clear the document. Safe to repeat.
void FreeConfig(Config *config);
// Missing keys or the wrong value type fail without writing value.
bool ReadConfigInteger(const Config *config, const char *key, i64 *value);
// Borrowed until this key is changed or the document is replaced/freed. Missing/wrong-type keys return NULL.
const char *ReadConfigString(const Config *config, const char *key);
bool HasConfigKey(const Config *config, const char *key);
// Copy supplied values. Failure leaves the document unchanged; equal values preserve their original spelling.
bool SetConfigInteger(Config *config, const char *key, i64 value, bool boolean);
bool SetConfigString(Config *config, const char *key, const char *value);

#endif
