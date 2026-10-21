#include "city/saveload/types.h"
#include "game/state.h"
#include "city/presentation.h"
#include "base/text.h"
#include "city/state.h"
#include "city/saveload/city_file_internal.h"
#include "assets/catalog.h"

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

// Read a stock MISC chunk from an opened load stream through the shared input decoder. Stream remains caller-owned;
// false or throwing errors preserve the original partial-application behavior.
bool LoadCityMisc(City *city, void *stream, u32 encoded_size) {
  CityFileInput input = {.kind = CITY_FILE_INPUT_PLATFORM, .stream = stream};
  return LoadCityMiscFromInput(city, &input, encoded_size);
}
