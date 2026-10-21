#include "base/config.h"
#include "base/file.h"
#include "base/utf8.h"
#include "base/text.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ConfigLine {
  ConfigLine *next;
  char *source, *key, *string, *replacement;
  size_t value_start, value_end;
  i64 integer;
  bool is_string;
};

static char *CopyConfigSpan(const char *start, size_t length) {
  char *text = malloc(length + 1);
  if (text) { memcpy(text, start, length); text[length] = 0; }
  return text;
}

static ConfigLine *FindConfigLine(const Config *config, const char *key) {
  for (ConfigLine *line = config->first; line; line = line->next)
    if (line->key && !strcmp(line->key, key)) return line;
  return NULL;
}

bool HasConfigKey(const Config *config, const char *key) { return FindConfigLine(config, key) != NULL; }

bool ReadConfigInteger(const Config *config, const char *key, i64 *value) {
  ConfigLine *line = FindConfigLine(config, key);
  if (!line || line->is_string) return false;
  *value = line->integer;
  return true;
}

const char *ReadConfigString(const Config *config, const char *key) {
  ConfigLine *line = FindConfigLine(config, key);
  return line && line->is_string ? line->string : NULL;
}

static bool ConfigKeyCharacter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

static const char *SkipConfigSpace(const char *text) {
  while (*text == ' ' || *text == '\t') ++text;
  return text;
}

static bool ConfigLineEnd(const char *text) {
  if (*text == '#') while (*text && *text != '\r' && *text != '\n') ++text;
  if (*text == '\r') ++text;
  if (*text == '\n') ++text;
  return !*text;
}

static bool ParseConfigLine(ConfigLine *line) {
  const char *end = line->source + strlen(line->source);
  for (const char *p = line->source; p < end;) {
    u32 character;
    if (!ReadUtf8CharacterBounded(&p, end, &character) ||
        (character < 32 && character != '\t' && character != '\r' && character != '\n') || character == 127)
      return false;
  }
  const char *text = SkipConfigSpace(line->source), *start = text;
  if (!*text || *text == '#' || *text == '\r' || *text == '\n') return ConfigLineEnd(text);
  for (;;) {
    if (!ConfigKeyCharacter(*text)) return false;
    while (ConfigKeyCharacter(*text)) ++text;
    if (*text != '.') break;
    ++text;
  }
  line->key = CopyConfigSpan(start, text - start);
  if (!line->key) return false;
  text = SkipConfigSpace(text);
  if (*text++ != '=') return false;
  text = SkipConfigSpace(text);
  line->value_start = text - line->source;
  if (*text == '"') {
    ++text;
    line->is_string = true;
    line->string = malloc(strlen(text) + 1);
    if (!line->string) return false;
    char *out = line->string;
    while (*text && *text != '"') {
      unsigned char c = *text++;
      if (c < 32 || c == 127) return false;
      if (c == '\\') {
        c = *text++;
        if (c == 'n') c = '\n';
        else if (c == 'r') c = '\r';
        else if (c == 't') c = '\t';
        else if (c != '\\' && c != '"') return false;
      }
      *out++ = c;
    }
    if (*text++ != '"') return false;
    *out = 0;
  } else if (!strncmp(text, "true", 4)) { line->integer = 1; text += 4; }
  else if (!strncmp(text, "false", 5)) { line->integer = 0; text += 5; }
  else {
    const char *digits = text;
    if (*digits == '-' || *digits == '+') ++digits;
    if (*digits < '0' || *digits > '9') return false;
    char *end;
    errno = 0;
    line->integer = strtoll(text, &end, 10);
    if (errno) return false;
    text = end;
  }
  line->value_end = text - line->source;
  text = SkipConfigSpace(text);
  return ConfigLineEnd(text);
}

void FreeConfig(Config *config) {
  ConfigLine *line = config->first;
  while (line) {
    ConfigLine *next = line->next;
    free(line->source); free(line->key); free(line->string); free(line->replacement); free(line);
    line = next;
  }
  *config = (Config){0};
}

bool LoadConfig(Config *config, const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) return errno == ENOENT;
  Config next = {0};
  char *source = NULL;
  size_t length = 0, capacity = 0, total = 0, number = 1;
  int c;
  bool ok = true;
  while ((c = fgetc(file)) != EOF) {
    if (!c || total++ == 1024 * 1024) { ok = false; break; }
    if (length + 2 > capacity) {
      size_t size = capacity ? capacity * 2 : 256;
      char *allocation = realloc(source, size);
      if (!allocation) { ok = false; break; }
      source = allocation; capacity = size;
    }
    source[length++] = c;
    if (c != '\n') continue;
    source[length] = 0;
    ConfigLine *line = calloc(1, sizeof(*line));
    if (!line) { ok = false; break; }
    line->source = source; source = NULL; length = capacity = 0;
    if (next.last) next.last->next = line; else next.first = line;
    next.last = line;
    if (!ParseConfigLine(line)) { ok = false; break; }
    for (ConfigLine *other = next.first; other != line; other = other->next)
      if (line->key && other->key && !strcmp(line->key, other->key)) { ok = false; break; }
    if (!ok) break;
    ++number;
  }
  if (ok && length) {
    source[length] = 0;
    ConfigLine *line = calloc(1, sizeof(*line));
    if (!line) ok = false;
    else {
      line->source = source; source = NULL;
      if (next.last) next.last->next = line; else next.first = line;
      next.last = line;
      ok = ParseConfigLine(line);
      for (ConfigLine *other = next.first; ok && other != line; other = other->next)
        if (line->key && other->key && !strcmp(line->key, other->key)) ok = false;
    }
  }
  free(source);
  if (ferror(file)) ok = false;
  if (fclose(file)) ok = false;
  if (!ok) {
    fprintf(stderr, "Cannot parse configuration at %s:%zu\n", path, number);
    FreeConfig(&next);
    return false;
  }
  FreeConfig(config);
  *config = next;
  return true;
}

static bool ReplaceConfigValue(Config *config, const char *key, char *encoded, const char *string, i64 integer) {
  if (!encoded) return false;
  ConfigLine *line = FindConfigLine(config, key);
  if (line && line->is_string == (string != NULL) &&
      (string ? !strcmp(line->string, string) : line->integer == integer)) { free(encoded); return true; }
  char *copy = string ? strdup(string) : NULL;
  if (string && !copy) { free(encoded); return false; }
  if (!line) {
    line = calloc(1, sizeof(*line));
    if (line) { line->key = strdup(key); line->source = strdup(""); }
    if (!line || !line->key || !line->source) {
      if (line) { free(line->key); free(line->source); free(line); }
      free(copy); free(encoded); return false;
    }
    if (config->last) config->last->next = line; else config->first = line;
    config->last = line;
  }
  free(line->replacement); line->replacement = encoded;
  free(line->string); line->string = copy;
  line->is_string = string != NULL;
  line->integer = integer;
  config->changed = true;
  return true;
}

bool SetConfigInteger(Config *config, const char *key, i64 value, bool boolean) {
  ConfigLine *line = FindConfigLine(config, key);
  if (line && !line->is_string && line->integer == value) return true;
  char text[32];
  if (boolean) snprintf(text, sizeof(text), "%s", value ? "true" : "false");
  else snprintf(text, sizeof(text), "%" PRId64, value);
  return ReplaceConfigValue(config, key, strdup(text), NULL, value);
}

bool SetConfigString(Config *config, const char *key, const char *value) {
  ConfigLine *line = FindConfigLine(config, key);
  if (line && line->is_string && !strcmp(line->string, value)) return true;
  size_t length = strlen(value);
  if (length > (SIZE_MAX - 3) / 2) return false;
  char *text = malloc(2 * length + 3);
  if (!text) return false;
  char *out = text;
  *out++ = '"';
  for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
    if (*p == '"' || *p == '\\') { *out++ = '\\'; *out++ = *p; }
    else if (*p == '\n' || *p == '\r' || *p == '\t') {
      *out++ = '\\'; *out++ = *p == '\n' ? 'n' : *p == '\r' ? 'r' : 't';
    } else if (*p < 32 || *p == 127) { free(text); return false; }
    else *out++ = *p;
  }
  *out++ = '"'; *out = 0;
  return ReplaceConfigValue(config, key, text, value, 0);
}

static bool WriteConfigContents(FILE *file, void *context) {
  Config *config = context;
  bool ok = true;
  bool newline = true;
  for (ConfigLine *line = config->first; ok && line; line = line->next) {
    if (!line->replacement) ok = fputs(line->source, file) >= 0;
    else if (!line->source[0]) {
      if (!newline) ok = fputc('\n', file) != EOF;
      if (ok) ok = fprintf(file, "%s = %s\n", line->key, line->replacement) >= 0;
    } else {
      ok = fwrite(line->source, 1, line->value_start, file) == line->value_start &&
          fputs(line->replacement, file) >= 0 && fputs(line->source + line->value_end, file) >= 0;
    }
    size_t length = strlen(line->source);
    newline = !length || line->source[length - 1] == '\n';
  }
  return ok;
}

bool WriteConfig(Config *config, const char *path) {
  if (!config->changed) return true;
  if (!WriteFileAtomically(path, WriteConfigContents, config)) return false;
  config->changed = false;
  return true;
}
