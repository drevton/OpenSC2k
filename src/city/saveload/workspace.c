#include "city/saveload/runtime.h"

#include <stdlib.h>
#include <string.h>

#include "city/saveload/state.h"
#include "game/errors.h"

// Borrow a zeroed slot from the current command's scratch pool or allocate a zeroed fallback. Returns NULL on
// allocation failure with Game's error set; release only through FreeCityFileScratch with the same file owner.
void *AllocateCityFileScratch(CityFileRuntime *file, size_t size) {
  if (file->scratch != NULL && size <= CITY_FILE_SCRATCH_CAPACITY) {
    for (int slot = 0; slot < CITY_FILE_SCRATCH_COUNT; ++slot) {
      if (file->scratch->used[slot]) continue;
      file->scratch->used[slot] = true;
      memset(file->scratch->bytes[slot], 0, CITY_FILE_SCRATCH_CAPACITY);
      return file->scratch->bytes[slot];
    }
  }
  void *bytes = calloc(size != 0 ? size : 1, 1);
  if (bytes == NULL) SetGameError(file->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
  return bytes;
}

// Return a scratch-pool slot or free its fallback allocation. Pointer must originate from the matching file owner, or
// be NULL; pool storage must never be passed directly to free.
void FreeCityFileScratch(CityFileRuntime *file, void *pointer) {
  if (file->scratch != NULL)
    for (int slot = 0; slot < CITY_FILE_SCRATCH_COUNT; ++slot) {
      if (pointer != file->scratch->bytes[slot]) continue;
      file->scratch->used[slot] = false;
      return;
    }
  free(pointer);
}

// Release an owned MISC allocation or detach borrowed scratch workspace. Clears its pointer and ownership flags;
// callers cannot retain MISC pointers after release.
void ReleaseCityMiscWorkspace(CityFileRuntime *file) {
  if (file->misc_owned) free(file->misc);
  file->misc = NULL;
  file->misc_active = file->misc_owned = false;
}

// Replace the file operation's MISC workspace with zeroed command scratch or owned heap storage. Returns false and
// records an allocation error on failure; any previous workspace is released first.
bool PrepareCityMiscWorkspace(CityFileRuntime *file) {
  ReleaseCityMiscWorkspace(file);
  if (file->scratch != NULL) {
    file->misc = &file->scratch->misc;
    memset(file->misc, 0, sizeof(*file->misc));
  } else {
    file->misc = calloc(1, sizeof(*file->misc));
    file->misc_owned = file->misc != NULL;
  }
  file->misc_active = file->misc != NULL;
  if (!file->misc_active) SetGameError(file->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
  return file->misc_active;
}
