#include "city/saveload/services.h"
#include "city/saveload/runtime.h"
#include "city/saveload/state.h"
#include "city/saveload/types.h"
#include "game/state.h"
#include "city/presentation.h"
#include "city/state.h"
#include "city/saveload/city_file_internal.h"

// Decode and apply stock MISC words in their original order from the configured input. Requires an active MISC
// workspace; errors preserve preceding writes and callback-visible side effects.
bool LoadCityMiscFromInput(City *city, CityFileInput *input, u32 encoded_size) {
  CityFileRuntime *file = city->game->file;
  CityMiscBuffer *buffer = (file->misc_active ? file->misc : NULL);
  if (!CityFileReadCompressedCityChunk(file, input, encoded_size, buffer, sizeof(CityMiscBuffer)))
    return false;
  // The read can invoke callbacks; use the workspace still owned by this operation.
  buffer = file->misc_active ? file->misc : NULL;
  if (buffer == NULL) return false;
  CityFileConvertWords((u8 *)buffer, sizeof(*buffer), 4);
  if (buffer->words[CITY_MISC_WORD_VERSION] != CITY_MISC_VERSION) {
    ReportCityLoadError(city, CITY_NOTICE_INVALID_CITY_CONTENTS,
        CityNoticeText(CITY_NOTICE_INVALID_CITY_CONTENTS));
    return false;
  }
  if (!ApplyLoadedCityMisc(city)) return false;
  ReleaseCityMiscWorkspace(file);
  return true;
}
