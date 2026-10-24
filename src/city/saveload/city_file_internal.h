#ifndef SC2K_CITY_FILE_INTERNAL_H
#define SC2K_CITY_FILE_INTERNAL_H

#include "city/maps/map_types.h"
#include "city/saveload/file.h"

typedef struct CityFileInput CityFileInput;

// Shared file schemas and private codec interfaces. Each implementation is compiled separately.
// SCDH city file I/O. Chunk geometry and conversions are shared by load and save.

// Direction through the stock staging representation. Zero exports; nonzero imports retain the original flag contract.
typedef enum CityFileDirection {
  CITY_FILE_EXPORT = 0,
  CITY_FILE_IMPORT = 1,
} CityFileDirection;
_Static_assert(sizeof(CityFileDirection) == sizeof(i32), "file direction ABI width");

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
  bool metadata_complete; // Only the optional editing index; original bytes remain available when false.
};

extern const CityFileChunkSpec kCityFileChunks[21];
extern const CityMiscScalar kCityMiscScalars[];
extern const size_t kCityMiscScalarCount;
extern const CityMiscAudioScalar kCityMiscAudioScalars[2];
extern const CityMiscArray kCityMiscArrays[27];

enum { CITY_FILE_STANDARD_CHUNKS = sizeof(kCityFileChunks) / sizeof(kCityFileChunks[0]) };

bool CityFileSkipCityChunk(CityFileRuntime *file, CityFileInput *input, u32 size);
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
void CopyCityFileChunkBytes(City *city, const CityFileChunkSpec *spec, u8 *bytes, u32 size,
    CityFileDirection direction);
bool DecodeCityRlePacket(const u8 *encoded, u32 encoded_size, u32 *source,
    u8 *decoded, u32 decoded_size, u32 *target, int stride);
bool CityFileDecodeRLE(const u8 *encoded, u32 encoded_size, u8 *decoded, u32 decoded_size);
int EncodeCityFileRle(const u8 *input, int size, u8 *output);
void CityFileConvertWords(u8 *data, u32 size, u32 width);
void ByteSwapCityFacilityRecords(u8 *data, int size);
void CityFileConvertChunk(u8 *data, const CityFileChunkSpec *spec, CityFileDirection direction);
bool CityFileStateChunkBytes(City *city, const CityFileChunkSpec *spec, u8 *data, CityFileDirection direction);
bool CityFileMiscFields(City *city, u8 *misc, CityFileDirection direction);
i32 LegacyCityCash(i64 cash);

bool ApplyLoadedCityMisc(City *city);
// Prepare the active operation's MISC workspace; its existing release path retains ownership.
bool AllocateCityMiscBuffer(CityFileRuntime *file);
// Serialize into that workspace. False means the caller must stop saving; the City is not rolled back.
bool SaveCityMiscToBuffer(City *city);
bool LoadCityMiscFromInput(City *city, CityFileInput *input, u32 encoded_size);
bool LoadCityChunksFromInput(City *city, CityFileInput *input, const char *path, bool allocate_misc);
// Borrow the output stream and consume the temporary path on every normal return, retaining completed writes.
bool SaveCityFileChunks(City *city, void *stream, CityPath *path);
u8 *BuildCityFileBytes(const CityFile *archive, u32 *size);

void ExportStockCityOverlayNames(const City *city, u8 *bytes);
void DecodeLoadedCityOverlayNames(City *city);

#endif
