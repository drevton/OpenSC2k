#include "city/saveload/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "base/text.h"
#include "game/errors.h"
#include "game/state.h"

char *ReserveCityPath(Game *game, CityPath *path, int capacity) {
  if (capacity < 0) {
    SetGameError(game, SC2K_ERROR_GUEST_BUFFER_TOO_SMALL);
    return NULL;
  }
  if (path->overflow != NULL && capacity <= path->overflow_capacity) return path->overflow;
  if (path->overflow == NULL && capacity < CITY_PATH_CAPACITY) return path->text;
  char *text = AllocateText((size_t)capacity + 1);
  memcpy(text, CityPathText(path), (size_t)path->length + 1);
  free(path->overflow);
  path->overflow = text;
  path->overflow_capacity = capacity;
  return text;
}

bool SetCityPathBytes(Game *game, CityPath *path, int length, const char *text) {
  if (length < 0) {
    SetGameError(game, SC2K_ERROR_GUEST_BUFFER_TOO_SMALL);
    return false;
  }
  // Copy before releasing overflow because text may borrow the path's existing storage.
  if (length < CITY_PATH_CAPACITY) {
    if (length != 0) memmove(path->text, text, (size_t)length);
    path->text[length] = 0;
    free(path->overflow);
    path->overflow = NULL;
    path->overflow_capacity = 0;
  } else if (path->overflow != NULL && length <= path->overflow_capacity) {
    memmove(path->overflow, text, (size_t)length);
    path->overflow[length] = 0;
  } else {
    char *replacement = AllocateText((size_t)length + 1);
    memcpy(replacement, text, (size_t)length);
    replacement[length] = 0;
    free(path->overflow);
    path->overflow = replacement;
    path->overflow_capacity = length;
  }
  path->length = length;
  return true;
}

bool EnsureCitySaveExtension(Game *game, CityPath *path, const char *extension) {
  size_t suffix_length = strlen(extension) + 1;
  if ((suffix_length != 4 && suffix_length != 5) || path->length > INT32_MAX - 6) {
    SetGameError(game, SC2K_ERROR_GUEST_BUFFER_TOO_SMALL);
    return false;
  }
  char suffix[6]; snprintf(suffix, sizeof(suffix), ".%s", extension);
  int length = path->length;
  char *text = ReserveCityPath(game, path, length + suffix_length);
  if (text == NULL) return false;
  if (length < (int)suffix_length || strcasecmp(text + length - suffix_length, suffix) != 0) {
    memcpy(text + length, suffix, suffix_length + 1);
  }
  path->length = (int)strlen(text);
  return true;
}

void FreeCityPathStorage(CityPath *path) {
  free(path->overflow);
  *path = (CityPath){0};
}

void ReleaseCityFilePaths(Game *game) {
  FreeCityPathStorage(&game->city_path);
  free(game->file_directory);
  game->file_directory = NULL;
  FreeCityPathStorage(&game->city_working_directory);
  FreeCityPathStorage(&game->profile_base_directory);
  FreeCityPathStorage(&game->command_line_path);
}
