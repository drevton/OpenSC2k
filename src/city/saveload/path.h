#ifndef SC2K_FILE_PATH_H
#define SC2K_FILE_PATH_H

#include <stddef.h>

#include "city/types.h"

typedef struct Game Game;

enum { CITY_FILE_DIALOG_PATH_LIMIT = 1023, CITY_PATH_CAPACITY = CITY_FILE_DIALOG_PATH_LIMIT + 1 };
typedef struct CityPath {
  char text[CITY_PATH_CAPACITY];
  int length;
  char *overflow;
  int overflow_capacity;
} CityPath;

// Borrow active text until a mutating path call or release. A mutator may relocate it even when spelling is unchanged.
static inline const char *CityPathText(const CityPath *path) {
  return path->overflow != NULL ? path->overflow : path->text;
}

// Reserve room for capacity text bytes plus the terminator, preserving existing text and never shrinking storage.
// Return borrowed writable storage; negative capacity returns NULL and records an error on Game.
char *ReserveCityPath(Game *game, CityPath *path, int capacity);
// Copy length readable bytes and terminate them. Input may overlap the active text and may be NULL for zero length.
// Negative length returns false and records an error without replacing the path; successful writes own any overflow.
bool SetCityPathBytes(Game *game, CityPath *path, int length, const char *text);
// Append a missing suffix, ignoring case. Extension is borrowed terminated text without a dot, three or four bytes;
// path must be initialized and may grow its owned storage. False reports invalid lengths through Game's error.
bool EnsureCitySaveExtension(Game *game, CityPath *path, const char *extension);
// Release overflow and clear initialized path storage. Safe to repeat; no Game or file-operation context is needed.
void FreeCityPathStorage(CityPath *path);
// Release Game's four path values and relative-file directory. Does not close streams or change City.
void ReleaseCityFilePaths(Game *game);

#endif
