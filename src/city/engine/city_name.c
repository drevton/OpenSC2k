#include "city/engine/city_name.h"
#include "game/state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/errors.h"

#include "city/state.h"
#include "assets/resources.h"
#include "base/text.h"

// City Name

// Replace the City's name with the supplied byte sequence. Text is borrowed for this call; SetCityNameText owns any
// overflow allocation and records allocation errors on Game.
void AssignCityName(City *city, int length, const u8 *text) {
  SetCityNameText(city->game, &city->name, length, text);
}

// Clear the City's name through the normal name-storage path. Existing capacity may be reused; the City remains
// responsible for releasing its name storage.
void EmptyCityName(City *city) {
  SetCityNameText(city->game, &city->name, 0, NULL);
}

// Saved neighbor indices select locally generated labels, with zero reserved for Ocean. Hash the city's name bytes
// explicitly so the result is independent of host byte order. Slot must be 0 through 3; writes a terminated fixed
// label without advancing City's simulation RNG streams.
void GenerateCityNeighborName(City *city, int slot) {
  char *name = (char *)city->geography.neighbor_names.name[slot];
  size_t capacity = sizeof(city->geography.neighbor_names.name[slot]);
  int index = city->account_names.value[slot];
  if (!index) {
    CopyText(name, capacity, "Ocean");
    return;
  }
  u32 seed = 2166136261;
  const char *city_name = CityNameText(&city->name);
  for (int character = 0; character < city->name.length; ++character)
    seed = (seed ^ (u8)city_name[character]) * 16777619;
  seed = (seed ^ (index & 255)) * 16777619;
  seed = (seed ^ (index >> 8)) * 16777619;
  // Retain distinct neighboring names even if two indices produce the same first candidate.
  for (int attempt = 0; attempt < 8; ++attempt) {
    if (!GenerateCityName(&seed, "", name, capacity)) continue;
    bool duplicate = false;
    for (int previous = 0; previous < slot; ++previous)
      if (!strcmp(name, (const char *)city->geography.neighbor_names.name[previous])) duplicate = true;
    if (!duplicate) return;
  }
  snprintf(name, capacity, "Neighbor %d", index);
}

// Regenerate all four neighboring-city labels from the current City name. The generated labels occupy City-owned fixed
// slots and require no caller cleanup.
void RefreshCityNeighborNames(City *city) {
  for (int slot = 0; slot < 4; ++slot) GenerateCityNeighborName(city, slot);
}

// Name Storage

// Copy length bytes into a terminated name, using inline storage where it fits and owned overflow otherwise. Negative
// lengths and allocation failure return false and set Game's error; nonempty input must provide length readable bytes.
bool SetCityNameText(Game *game, CityName *name, int length, const u8 *text) {
  if (length < 0) {
    SetGameError(game, SC2K_ERROR_GUEST_BUFFER_TOO_SMALL);
    return false;
  }
  if (length < CITY_NAME_CAPACITY) {
    if (length != 0)
      memmove(name->text, text, (size_t)length);
    name->text[length] = 0;
    if (name->overflow != NULL)
      free(name->overflow);
    name->overflow = NULL;
    name->overflow_capacity = 0;
  } else if (name->overflow != NULL && length <= name->overflow_capacity) {
    memmove(name->overflow, text, (size_t)length);
    name->overflow[length] = 0;
  } else {
    char *long_name = malloc((size_t)length + 1);
    if (long_name == NULL) {
      SetGameError(game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
      return false;
    }
    memcpy(long_name, text, (size_t)length);
    long_name[length] = 0;
    if (name->overflow != NULL)
      free(name->overflow);
    name->overflow = long_name;
    name->overflow_capacity = length;
    name->text[0] = 0;
  }
  name->length = length;
  return true;
}

// Release a name's overflow allocation and reset its length and capacity metadata. Requires an initialized CityName;
// its inline storage is part of the object and is never freed separately.
void FreeCityNameStorage(CityName *name) {
  free(name->overflow);
  *name = (CityName){0};
}
