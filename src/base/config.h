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
bool LoadConfig(Config *config, const char *path);
bool WriteConfig(Config *config, const char *path);
void FreeConfig(Config *config);
bool ReadConfigInteger(const Config *config, const char *key, i64 *value);
const char *ReadConfigString(const Config *config, const char *key);
bool HasConfigKey(const Config *config, const char *key);
bool SetConfigInteger(Config *config, const char *key, i64 value, bool boolean);
bool SetConfigString(Config *config, const char *key, const char *value);

#endif
