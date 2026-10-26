#include "city/saveload/file.h"

#include "base/file.h"
#include "city/engine/city_name.h"
#include "city/events/events.h"
#include "city/maps/kinds.h"
#include "city/maps/labels.h"
#include "city/presentation.h"
#include "city/saveload/city_file_internal.h"
#include "city/saveload/legacy_text.h"
#include "city/saveload/runtime.h"
#include "city/saveload/sc2k.h"
#include "city/saveload/services.h"
#include "city/saveload/state.h"
#include "city/state.h"
#include "game/state.h"

#include <errno.h>
#include <string.h>
#include <strings.h>

// Decide whether City needs or requests native saves from text, event capacity, map size and retained extensions.
bool CityUsesSc2k(const City *city) {
  if (city->individual_facilities || CityHasExtendedAssociations(city)) return true;
  if (!CanEncodeLegacyCityText(CityNameText(&city->name), CITY_NAME_LIMIT)) return true;
  for (int slot = 0; slot < CITY_OVERLAY_NAME_COUNT; ++slot)
    if (!CanEncodeLegacyCityText((const char *)city->overlay_names.entry[slot].character,
        CITY_OVERLAY_LEGACY_NAME_MAX_LENGTH)) return true;
  int ships = 0;
  for (int slot = CITY_EVENT_SLOT_FIRST; slot < city->events.capacity; ++slot) {
    int type = city->events.event[slot].type;
    if (slot >= CITY_STOCK_EVENT_SLOT_COUNT && type != CITY_MAP_EVENT_UNUSED) return true;
    if (type == CITY_MAP_EVENT_SHIP && ++ships > 1) return true;
  }
  if (city->game->save_extended || city->file_extensions ||
      city->map_width != CITY_MAP_SIZE || city->map_height != CITY_MAP_SIZE) return true;
  for (TileIndex tile = 0; tile < city->map_cell_count; ++tile)
    if (IsTileKindShoreTree(city->tile[tile].kind)) return true;
  return false;
}

// File-dialog cancellation occurs outside this function. Requires an active file operation and initialized path; false
// preserves the failure/partial output and does not commit a successful host-path transition.
bool SaveCityFile(City *city, CityPath *path) {
  CityFileRuntime *file = city->game->file;
  bool sc2k = CityUsesSc2k(city);
  const char *extension = sc2k ? "sc2k" : "sc2";
  if (!EnsureCitySaveExtension(city->game, path, extension)) return false;
  const char *filename = CityPathText(path);
  CitySaveStream *stream = OpenCitySaveStream(file, filename);
  if (stream == NULL) {
    ReportCitySaveError(file, CITY_NOTICE_ERROR_OPENING_FILE, filename);
    return false;
  }
  if (!AllocateCityMiscBuffer(file)) {
    CloseCitySaveStream(stream);
    RemoveCitySaveFile(file, filename);
    return false;
  }
  CityMapViewConfig *magnified_view = BeginCitySaveView(city);
  CityPath temporary = {0};
  if (!SetCityPathBytes(city->game, &temporary, path->length, CityPathText(path))) {
    ReleaseCityMiscWorkspace(file);
    CloseCitySaveStream(stream);
    return false;
  }
  bool saved;
  if (sc2k) { saved = SaveCitySc2k(city, stream); FreeCityPathStorage(&temporary); }
  else saved = SaveCityFileChunks(city, stream, &temporary);
  EndCitySaveView(city, magnified_view);
  ReleaseCityMiscWorkspace(file);
  CloseCitySaveStream(stream);
  if (!saved) {
    RemoveCitySaveFile(file, filename);
  }
  return saved;
}

// Accepted save paths

// Save using an explicit selected host destination inside a private attempt. Requires an active file operation and
// valid path; returns true only when serialization and all attempt I/O completed successfully.
bool SaveCityToDestination(City *city, CityPath *path, const char *destination) {
  CityFileRuntime *file = city->game->file;
  if (path == NULL || destination == NULL || destination[0] == 0) {
    file->save_io.last_error = errno = EINVAL;
    return false;
  }
  NativeCitySaveAttempt *attempt = BeginCitySaveAttempt(file);
  if (attempt == NULL) return false;
  if (!SelectCitySaveDestination(file, attempt, destination)) {
    EndCitySaveAttempt(file, attempt);
    return false;
  }
  bool saved = SaveCityFile(city, path);
  int error = EndCitySaveAttempt(file, attempt);
  return saved && !error;
}

// Derive the City's UTF-8 name from a host filename, stripping a recognized city extension. Path must be terminated;
// name storage is copied through AssignCityName and does not borrow the path.
void AssignCityNameFromSavePath(City *city, const char *path) {
  const char *text = PathFilename(path);
  int length = strlen(text);
  const char *extension = strrchr(text, '.');
  if (extension && (!strcasecmp(extension, ".sc2") || !strcasecmp(extension, ".sc2k")))
    length = extension - text;
  AssignCityName(city, length, (const u8 *)text);
}

// Save a byte-counted host path, optionally deriving the City name, with a file operation when needed. Text must
// provide length readable bytes; returns success and releases its temporary path/operation before returning.
bool SaveCityToPath(City *city, const u8 *text, int length, bool rename) {
  CityFileRuntime operation;
  bool owns_operation = city->game->file == NULL;
  if (owns_operation) BeginCityFileOperation(city->game, &operation);
  CityPath path = {0};
  bool saved = false;
  if (!SetCityPathBytes(city->game, &path, length, (const char *)text)) goto cleanup;
  const char *extension = CityUsesSc2k(city) ? "sc2k" : "sc2";
  if (!EnsureCitySaveExtension(city->game, &path, extension)) goto cleanup;
  const char *destination = CityPathText(&path);
  if (rename) AssignCityNameFromSavePath(city, destination);
  saved = SaveCityToDestination(city, &path, destination);
  // Saving may normalize the extension and reallocate path storage.
  if (saved) SetCityPathBytes(city->game, &city->game->city_path, path.length, CityPathText(&path));
cleanup:
  FreeCityPathStorage(&path);
  if (owns_operation) EndCityFileOperation(&operation);
  return saved;
}
