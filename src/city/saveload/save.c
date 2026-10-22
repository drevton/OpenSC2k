#include "city/saveload/services.h"
#include "game/state.h"
#include "city/presentation.h"
#include <strings.h>

#include "city/saveload/sc2k.h"
#include "city/saveload/city_file_internal.h"
#include <string.h>
#include "city/state.h"
#include "game/errors.h"

// Append the requested extension without rewriting the host filename. Path must be initialized and extension
// terminated; may grow path-owned storage and returns whether the normalized path is ready.
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

// File-dialog cancellation occurs outside this function. Requires an active file operation and initialized path; false
// preserves the failure/partial output and does not commit a successful host-path transition.
bool SaveCityFile(City *city, CityPath *path) {
  CityFileRuntime *file = city->game->file;
  bool sc2k = CityUsesSc2k(city);
  const char *extension = sc2k ? "sc2k" : "sc2";
  if (!EnsureCitySaveExtension(city->game, path, extension)) return false;
  const char *filename = CityPathText(path);
  void *stream = OpenCitySaveStream(file, filename);
  if (stream == NULL) {
    ReportCitySaveError(file, CITY_NOTICE_ERROR_OPENING_FILE, filename);
    return 0;
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
    return 0;
  }
  i32 saved;
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
