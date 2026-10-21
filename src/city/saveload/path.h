#ifndef SC2K_FILE_PATH_H
#define SC2K_FILE_PATH_H

#include <stddef.h>

#include "city/types.h"

typedef struct Game Game;

// Native paths are host UTF-8; comparison paths retain their recorded Windows encoding.
// Normal dialog paths fit inline. Longer command-line paths may use overflow storage.
enum { CITY_FILE_DIALOG_PATH_LIMIT = 1023, CITY_PATH_CAPACITY = CITY_FILE_DIALOG_PATH_LIMIT + 1 };
typedef struct {
  char text[CITY_PATH_CAPACITY];
  int length;
  char *overflow;
  int overflow_capacity;
} CityPath;

static inline const char *CityPathText(const CityPath *path) {
  return path->overflow != NULL ? path->overflow : path->text;
}

char *ReserveCityPath(Game *game, CityPath *path, int capacity);
bool SetCityPathBytes(Game *game, CityPath *path, int length, const char *text);
void FreeCityPathStorage(CityPath *path);
void ReleaseCityFilePaths(Game *game);

#endif
