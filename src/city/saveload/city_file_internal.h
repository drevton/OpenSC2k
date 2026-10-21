#ifndef SC2K_CITY_FILE_INTERNAL_H
#define SC2K_CITY_FILE_INTERNAL_H

#include "city/maps/tile_storage.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "audio/audio.h"
#include "city/state.h"
#include "city/saveload/file.h"
#include "base/byte_order.h"
#include "city/saveload/map_format.h"
#include "game/errors.h"
#include "game/state.h"
#include "audio/state.h"
#include "assets/state.h"
#include "base/text.h"

// Shared file schemas and private codec interfaces. Each implementation is compiled separately.
// SCDH city file I/O. Chunk geometry and conversions are shared by load and save.

typedef enum CityFileStorage {
  CITY_FILE_ONLY,
  CITY_FILE_MAP,
  CITY_FILE_HALF_MAP,
  CITY_FILE_QUARTER_MAP,
  CITY_FILE_TILE,
  CITY_FILE_SIMULATION
} CityFileStorage;

typedef enum CityFileConversion {
  CITY_FILE_BYTES,
  CITY_FILE_WORDS16,
  CITY_FILE_WORDS32,
  CITY_FILE_LABELS,
  CITY_FILE_FACILITIES,
  CITY_FILE_NAME,
  CITY_FILE_EVENTS
} CityFileConversion;

typedef struct CityFileChunkSpec {
  char tag[5];
  u32 size;
  CityFileStorage storage;
  // Offset into City, TileInfo or the selected coarse cell record.
  u32 native_offset;
  u16 rows;
  u16 row_bytes;
  CityFileConversion conversion;
} CityFileChunkSpec;

typedef enum CityFileInputKind {
  CITY_FILE_INPUT_PLATFORM,
  CITY_FILE_INPUT_STDIO,
  CITY_FILE_INPUT_MEMORY
} CityFileInputKind;

// File transport data; no registered callbacks or changes to guest object layouts.
typedef struct {
  CityFileInputKind kind;
  void *stream;
  const u8 *bytes;
  u32 size;
  u32 position;
  u32 first_tag;
  bool has_first_tag;
} CityFileInput;

typedef struct {
  u16 file_index;
  u8 width;
  u8 signed_value;
  u16 native_offset;
  u8 native_width;
} CityMiscScalar;

typedef struct {
  u16 file_index;
  u16 member_offset;
  u8 width;
  u8 signed_value;
} CityMiscAudioScalar;

typedef struct {
  u16 file_index;
  u16 file_stride;
  u16 count;
  u8 width;
  u8 signed_value;
  u8 record_stride;
  u8 record_offset;
  u32 native_offset;
  u8 native_width;
} CityMiscArray;

// MISC stores three current totals followed by alternating monthly amount/rate.
static inline void *CityFileEconomyField(EconomySector *sector, int field) {
  switch (field) {
  case 0: return &sector->current_amount;
  case 1: return &sector->current_rate;
  case 2: return &sector->accumulated;
  default:
    if (field & 1) return &sector->monthly_amount[(field - 3) / 2];
    return &sector->monthly_rate[(field - 3) / 2];
  }
}

typedef struct {
  char tag[5];
  const CityFileChunkSpec *spec;
  u8 *decoded;
  u8 *original;
  u32 size;
  u8 *encoded;
  u32 encoded_size;
} CityFileChunk;

struct CityFile {
  CityFileChunk *chunk;
  u32 count;
  u32 capacity;
  char *source_path;
  u8 *source;
  u32 source_size;
  i32 metadata_complete;
};

typedef struct {
  u8 offset;
} CityScenarioWordField;

extern const CityFileChunkSpec kCityFileChunks[21];
extern const CityMiscScalar kCityMiscScalars[];
extern const size_t kCityMiscScalarCount;
extern const CityMiscAudioScalar kCityMiscAudioScalars[2];
extern const CityMiscArray kCityMiscArrays[27];

enum { CITY_FILE_STANDARD_CHUNKS = sizeof(kCityFileChunks) / sizeof(kCityFileChunks[0]) };

void *CityFileOpenStream(CityFileRuntime *file, const char *path);
void CityFileCloseStream(void *stream);
u8 *CityFileReadTaggedStream(void *stream, const char *tag, int version, u8 *output, size_t capacity);

u32 CityFileInputRead(CityFileInput *input, void *destination, u32 size);
i32 CityFileInputSeek(CityFileInput *input, i32 offset, i32 origin);
i32 CityFileInputPosition(CityFileInput *input);
bool CityFileSkipCityChunk(CityFileRuntime *file, CityFileInput *input, u32 size);
void ReportCityChunkError(CityFileRuntime *file, u32 resource, int load_string);
bool ReadCityCompressedChunkStrided(CityFileRuntime *file, CityFileInput *input, u32 encoded_size,
    void *destination, u32 decoded_size, int stride);
bool CityFileReadCompressedCityChunk(CityFileRuntime *file, CityFileInput *input, u32 encoded_size,
    void *destination, u32 decoded_size);
bool CityFileValidateCityFileContainer(City *city, CityFileInput *input, const char *path, u32 *length);
const CityFileChunkSpec *FindCityFileChunkSpec(const char tag[4]);
u8 *CityFileChunkData(City *city, const CityFileChunkSpec *spec);
// Dense file bytes retain their original order; native tile members are strided.
static inline size_t CityFileChunkByteOffset(const CityFileChunkSpec *spec, size_t byte) {
  if (spec->storage == CITY_FILE_HALF_MAP) return byte * sizeof(CityHalfMapStatistics);
  if (spec->storage == CITY_FILE_QUARTER_MAP) return byte * sizeof(CityQuarterMapStatistics);
  if (spec->storage != CITY_FILE_TILE) return byte;
  int width = spec->conversion == CITY_FILE_WORDS16 ? 2 : 1;
  return byte / width * sizeof(TileInfo) + byte % width;
}
void CopyCityFileChunkBytes(City *city, const CityFileChunkSpec *spec, u8 *bytes, u32 size, bool load);
bool DecodeCityRlePacket(const u8 *encoded, u32 encoded_size, u32 *source,
    u8 *decoded, u32 decoded_size, u32 *target, int stride);
bool CityFileDecodeRLE(const u8 *encoded, u32 encoded_size, u8 *decoded, u32 decoded_size);
int EncodeCityFileRle(const u8 *input, int size, u8 *output);
void CityFileConvertWords(u8 *data, u32 size, u32 width);
void ByteSwapCityFacilityRecords(u8 *data, int size);
void CityFileConvertChunk(u8 *data, const CityFileChunkSpec *spec, i32 from_file);
bool CityFileStateChunkBytes(City *city, const CityFileChunkSpec *spec, u8 *data, i32 load);
bool CityFileMiscFields(City *city, u8 *misc, i32 load);
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
    CITY_SAVE_CLIMATE_HEADER_SIZE = 8, CITY_SAVE_EXTENSION_LIMIT = 128 * 1024 * 1024 };
i32 LegacyCityCash(i64 cash);
u32 CitySaveExtensionSize(const City *city);
bool EncodeCitySaveExtension(const City *city, u8 *bytes, u32 size);
bool ApplyCitySaveExtension(City *city, const u8 *bytes, u32 size);

bool ApplyLoadedCityMisc(City *city);
bool LoadCityMiscFromInput(City *city, CityFileInput *input, u32 encoded_size);
bool LoadCityChunksFromInput(City *city, CityFileInput *input, const char *path, int allocate_misc);
u8 *BuildCityFileBytes(const CityFile *city, u32 *size);

void ExportStockCityOverlayNames(const City *city, u8 *bytes);
void DecodeLoadedCityOverlayNames(City *city);

#endif
