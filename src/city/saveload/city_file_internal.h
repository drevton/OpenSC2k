#ifndef SC2K_CITY_FILE_INTERNAL_H
#define SC2K_CITY_FILE_INTERNAL_H

#include "city/maps/map_types.h"
#include "city/saveload/file.h"

typedef struct CityFileInput CityFileInput;
typedef struct CitySaveStream CitySaveStream;

// Shared file schemas and private codec interfaces. Each implementation is compiled separately.
// Stock chunk geometry and MISC mappings are shared by load and save.

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

// MISC indices and file strides count 32-bit words. width is the original value's byte width within a word.
// native_offset counts bytes in City; zero native_width reuses width. Omitted signed_value means unsigned.
typedef struct {
  u16 file_index;
  u8 width;
  u8 signed_value;
  u16 native_offset;
  u8 native_width;
} CityMiscScalar;

// Native record_stride and record_offset count bytes; zero stride uses the resolved native_width.
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
extern const CityMiscArray kCityMiscArrays[27];

enum { CITY_FILE_STANDARD_CHUNKS = sizeof(kCityFileChunks) / sizeof(kCityFileChunks[0]) };

bool CityFileSkipCityChunk(CityFileRuntime *file, CityFileInput *input, u32 size);
bool ReadCityCompressedChunkStrided(CityFileRuntime *file, CityFileInput *input, u32 encoded_size,
    void *destination, u32 decoded_size, int stride);
bool CityFileReadCompressedCityChunk(CityFileRuntime *file, CityFileInput *input, u32 encoded_size,
    void *destination, u32 decoded_size);
bool CityFileValidateCityFileContainer(City *city, CityFileInput *input, const char *path, u32 *length);
const CityFileChunkSpec *FindCityFileChunkSpec(const char tag[4]);
// Borrow schema-selected backing from City; unavailable/file-only storage returns NULL. Writable access requires
// a mutable City. Both views refer to the same storage and remain valid only while that backing stays bound.
const u8 *CityFileChunkData(const City *city, const CityFileChunkSpec *spec);
u8 *CityFileMutableChunkData(City *city, const CityFileChunkSpec *spec);
// Dense file bytes retain their original order; native tile members are strided.
static inline size_t CityFileChunkByteOffset(const CityFileChunkSpec *spec, size_t byte) {
  if (spec->storage == CITY_FILE_HALF_MAP) return byte * sizeof(CityHalfMapStatistics);
  if (spec->storage == CITY_FILE_QUARTER_MAP) return byte * sizeof(CityQuarterMapStatistics);
  if (spec->storage != CITY_FILE_TILE) return byte;
  int width = spec->conversion == CITY_FILE_WORDS16 ? 2 : 1;
  return byte / width * sizeof(TileInfo) + byte % width;
}
// Copy bounded City backing bytes without numeric or kind conversion. Size must fit both representations.
// Read observes effective event overlays; write assigns the admitted raw fields, including partial load results.
void ReadCityFileChunkBytes(const City *city, const CityFileChunkSpec *spec, u8 *bytes, u32 size);
void WriteCityFileChunkBytes(City *city, const CityFileChunkSpec *spec, const u8 *bytes, u32 size);
bool DecodeCityRlePacket(const u8 *encoded, u32 encoded_size, u32 *source,
    u8 *decoded, u32 decoded_size, u32 *target, int stride);
bool CityFileDecodeRLE(const u8 *encoded, u32 encoded_size, u8 *decoded, u32 decoded_size);
int EncodeCityFileRle(const u8 *input, int size, u8 *output);
void CityFileConvertWords(u8 *data, u32 size, u32 width);
void ByteSwapCityFacilityRecords(u8 *data, int size);
// Convert complete schema-sized staging bytes in place. Label conversion retains the stock slot stride; it does not
// project the table into City's wider name records. Other conversions retain their word/record byte widths.
void CityFileConvertChunk(u8 *data, const CityFileChunkSpec *spec, CityFileDirection direction);
// Full schema-sized staging records: export projects stock map/label/event representations without changing City;
// import writes City backing and decodes events, retaining earlier effects on failure. Numeric conversion is separate.
bool ExportCityFileStateChunk(const City *city, const CityFileChunkSpec *spec, u8 *data);
bool ImportCityFileStateChunk(City *city, const CityFileChunkSpec *spec, const u8 *data);
// Records contain CITY_MISC_WORD_COUNT big-endian words. Encoding requires the version word initialized;
// reads City and presentation preferences without changing them, and may leave partial bytes on failure.
bool EncodeCityMiscRecord(const City *city, u8 *misc);
// Applying rejects an unsupported version before changing City. Later words preserve callback order and partial writes;
// caller keeps the record alive through those callbacks. The operation never writes into the supplied bytes.
bool ApplyCityMiscRecord(City *city, const u8 *misc);
i32 LegacyCityCash(i64 cash);

bool ApplyLoadedCityMisc(City *city);
// Prepare the active operation's MISC workspace; its existing release path retains ownership.
bool AllocateCityMiscBuffer(CityFileRuntime *file);
// Serialize into that workspace. False means the caller must stop saving; the City is not rolled back.
// Success leaves the operation's MISC workspace active and filled with the complete host-endian record.
bool SaveCityMiscToBuffer(City *city);
bool LoadCityMiscFromInput(City *city, CityFileInput *input, u32 encoded_size);
bool LoadCityChunksFromInput(City *city, CityFileInput *input, const char *path, bool allocate_misc);
// Borrow the output stream and consume the temporary path on every normal return, retaining completed writes.
bool SaveCityFileChunks(City *city, CitySaveStream *stream, CityPath *path);
u8 *BuildCityFileBytes(const CityFile *archive, u32 *size);

void ExportStockCityOverlayNames(const City *city, u8 *bytes);
void DecodeLoadedCityOverlayNames(City *city);

#endif
