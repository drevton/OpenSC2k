#ifndef SC2K_FILE_CITY_SC2K_H
#define SC2K_FILE_CITY_SC2K_H

#include "city/saveload/city_file_internal.h"
#include "city/saveload/types.h"

// META: explicit little-endian header followed by the city's UTF-8 name.
// Bump this number whenever a release breaks the native save layout or meaning.
enum { CITY_SAVE_FORMAT_VERSION = 3, CITY_SC2K_METADATA_SETTINGS_OFFSET = 24,
    CITY_SC2K_METADATA_HEADER_SIZE = CITY_SC2K_METADATA_SETTINGS_OFFSET + CITY_SAVE_EXTENSION_SIZE,
    CITY_SC2K_METADATA_LIMIT = CITY_SC2K_METADATA_HEADER_SIZE + 256 };

typedef struct {
  int width, height;
  u32 next_sign;
  u32 format_version;
  int start_year;
  u32 tick;
  ByteSpan name, settings;
} CitySc2kMetadata;
bool ReadCitySc2kMetadata(CitySc2kMetadata *metadata, const u8 *bytes, u32 size);
bool ReadCitySc2kVersion(CityFileVersion *version, const u8 *bytes, u32 size);

bool LoadCitySc2k(City *city, CityFileInput *input);
bool SaveCitySc2k(City *city, void *stream);
void ReleaseCityFileExtensions(City *city);
bool CityUsesSc2k(const City *city);

#endif
