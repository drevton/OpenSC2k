#ifndef SC2K_FILE_CITY_SC2K_H
#define SC2K_FILE_CITY_SC2K_H

#include "city/types.h"
#include "city/saveload/types.h"

typedef struct CityFileInput CityFileInput;
typedef struct CitySaveStream CitySaveStream;

// Fixed-width native fields. Packet size determines the available prefix.
// Encode members explicitly in little endian; never serialize native padding.
typedef struct {
  i64 cash;
  u8 reserved[4];
  u32 landscape_cursor, landscape_tick, landscape_random;
  u8 tree_lifecycle, tree_tick_counter;
  u8 snow_line, snow_variation;
  u32 snow_seed;
  u8 terrain_climate, climate_reserved;
  u16 desert_limit;
  u8 protect_terrain_structures;
  u8 reserved_tool_options[2];
  u8 native_map_storage;
  u64 asset_value;
  u64 fixes;
} CitySaveExtension;
_Static_assert(offsetof(CitySaveExtension, cash) == 0 && offsetof(CitySaveExtension, reserved) == 8 &&
    offsetof(CitySaveExtension, landscape_cursor) == 12 &&
    offsetof(CitySaveExtension, landscape_tick) == 16 && offsetof(CitySaveExtension, landscape_random) == 20 &&
    offsetof(CitySaveExtension, tree_lifecycle) == 24 && offsetof(CitySaveExtension, tree_tick_counter) == 25 &&
    offsetof(CitySaveExtension, snow_line) == 26 && offsetof(CitySaveExtension, snow_variation) == 27 &&
    offsetof(CitySaveExtension, snow_seed) == 28 && offsetof(CitySaveExtension, terrain_climate) == 32 &&
    offsetof(CitySaveExtension, desert_limit) == 34 && offsetof(CitySaveExtension, protect_terrain_structures) == 36 &&
    offsetof(CitySaveExtension, reserved_tool_options) == 37 &&
    offsetof(CitySaveExtension, native_map_storage) == 39 &&
    offsetof(CitySaveExtension, asset_value) == 40 && offsetof(CitySaveExtension, fixes) == 48 &&
    sizeof(CitySaveExtension) == 56,
    "native save extension offsets");
// Serialized prefix ends at the last field, excluding native tail padding.
enum { CITY_SAVE_LANDSCAPE_PREFIX_SIZE = 24, CITY_SAVE_TREE_PREFIX_SIZE = 26,
    CITY_SAVE_CLIMATE_PREFIX_SIZE = 36, CITY_SAVE_PROTECTION_PREFIX_SIZE = 37,
    CITY_SAVE_POINT_EDIT_PREFIX_SIZE = 38, CITY_SAVE_NO_SEAWATER_PREFIX_SIZE = 39,
    CITY_SAVE_NATIVE_MAP_PREFIX_SIZE = 40, CITY_SAVE_ASSET_VALUE_PREFIX_SIZE = 48, CITY_SAVE_EXTENSION_SIZE = 56,
};
// Known scalar prefix needed by this City, shared by stock XNAT and native META settings. No allocation or mutation.
u32 CitySaveExtensionSize(const City *city);
// Write the known prefix into size writable bytes, preserving any unknown tail. False may retain earlier prefix writes.
bool EncodeCitySaveExtension(const City *city, u8 *bytes, u32 size);
// Apply after base fields. Validate the scalar metadata before writes; preserve externally edited legacy cash/assets.
bool ApplyCitySaveExtension(City *city, const u8 *bytes, u32 size);

// META: explicit little-endian header followed by the city's UTF-8 name.
// Bump this number whenever a release breaks the native save layout or meaning.
enum { CITY_SAVE_FORMAT_VERSION = 4, CITY_SC2K_METADATA_SETTINGS_OFFSET = 24,
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
// Validate META without loading City. Successful name/settings spans borrow bytes; name is length-delimited.
// Failure clears the output record.
bool ReadCitySc2kMetadata(CitySc2kMetadata *metadata, const u8 *bytes, u32 size);
// Decode VERS into owned fixed fields and revision text. Failure clears the output record.
bool ReadCitySc2kVersion(CityFileVersion *version, const u8 *bytes, u32 size);

// Requires an active file operation. Own temporary archive/metadata storage through its cleanup stack;
// failures keep partial City writes, while success retains only unknown archive chunks and the native-format marker.
bool LoadCitySc2k(City *city, CityFileInput *input);
// Finish pending simulation work before staging; reject remaining work, active questions or zone-drag input.
// Record encoders only read City. Borrow an open save stream through completion; the caller retains close ownership.
// Failure retains any completed work and any output already written.
bool SaveCitySc2k(City *city, CitySaveStream *stream);

#endif
