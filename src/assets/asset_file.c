#include "assets/asset_file.h"

#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <zstd.h>

#include "assets/assets.h"
#include "base/byte_order.h"
#include "base/text.h"

enum { kAssetInputBytes = 128 * 1024, kAssetOutputBytes = 32 * 1024, kAssetWindowLogMax = 23 };

static void ReleaseAssetDecoder(AssetFile *asset) {
  ZSTD_freeDStream(asset->decoder);
  free(asset->input);
  free(asset->output);
  asset->decoder = NULL;
  asset->input = asset->output = NULL;
}

bool CloseAssetFile(AssetFile *asset) {
  bool valid = !asset->failed && (!asset->decoder || asset->finished);
  if (asset->file) {
    if (ferror(asset->file)) valid = false;
    if (fclose(asset->file)) valid = false;
  }
  ReleaseAssetDecoder(asset);
  *asset = (AssetFile){0};
  return valid;
}

static bool InitializeAssetDecoder(AssetFile *asset) {
  asset->decoder = ZSTD_createDStream();
  asset->input = malloc(kAssetInputBytes);
  asset->output = malloc(kAssetOutputBytes);
  if (!asset->decoder || !asset->input || !asset->output || ZSTD_isError(ZSTD_initDStream(asset->decoder)) ||
      ZSTD_isError(ZSTD_DCtx_setParameter(asset->decoder, ZSTD_d_windowLogMax, kAssetWindowLogMax))) {
    ReleaseAssetDecoder(asset);
    return false;
  }
  return true;
}

static int OpenAssetFile(AssetFile *asset, const char *path) {
  *asset = (AssetFile){0};
  asset->file = fopen(path, "rb");
  if (!asset->file) return errno == ENOENT ? 0 : -1;
  struct stat status;
  if (fstat(fileno(asset->file), &status) || status.st_size < 0) {
    CloseAssetFile(asset);
    return -1;
  }
  asset->length = status.st_size;
  u8 magic[4];
  size_t count = fread(magic, 1, sizeof(magic), asset->file);
  if (ferror(asset->file) || fseek(asset->file, 0, SEEK_SET)) {
    CloseAssetFile(asset);
    return -1;
  }
  if (count != sizeof(magic) || memcmp(magic, "\x28\xb5\x2f\xfd", sizeof(magic))) return 1;
  if (InitializeAssetDecoder(asset)) return 1;
  CloseAssetFile(asset);
  return -1;
}

static size_t DecodeAssetBytes(AssetFile *asset, void *destination, size_t size) {
  ZSTD_outBuffer output = {destination, size, 0};
  while (output.pos < output.size && !asset->finished && !asset->failed) {
    if (asset->input_offset == asset->input_size) {
      size_t wanted = kAssetInputBytes;
      if (asset->bounded_stream && asset->compressed_remaining < wanted) wanted = asset->compressed_remaining;
      asset->input_size = fread(asset->input, 1, wanted, asset->file);
      if (asset->bounded_stream) {
        asset->compressed_remaining -= asset->input_size;
        if (asset->input_size != wanted) asset->failed = true;
      }
      asset->input_offset = 0;
      if (ferror(asset->file)) { asset->failed = true; break; }
    }
    ZSTD_inBuffer input = {asset->input, asset->input_size, asset->input_offset};
    size_t previous = output.pos;
    size_t result = ZSTD_decompressStream(asset->decoder, &output, &input);
    if (ZSTD_isError(result)) { asset->failed = true; break; }
    bool progress = input.pos != asset->input_offset || output.pos != previous;
    asset->input_offset = input.pos;
    if (!result) {
      asset->finished = true;
      // Each bounded archive section contains one complete checksummed frame.
      asset->failed = input.pos != input.size || ferror(asset->file) ||
          (asset->bounded_stream ? asset->compressed_remaining != 0 : fgetc(asset->file) != EOF);
    } else if (!progress) {
      // Empty input can still flush buffered output. No progress means truncation.
      asset->failed = true;
    }
  }
  asset->decoded_total += output.pos;
  if (asset->bounded_stream && (asset->decoded_total > asset->length ||
          (asset->finished && asset->decoded_total != asset->length))) asset->failed = true;
  return output.pos;
}

size_t ReadAssetFile(AssetFile *asset, void *destination, size_t size) {
  if (asset->failed) return 0;
  if (!asset->decoder) {
    if (asset->length - asset->position < size) size = asset->length - asset->position;
    if (asset->memory) {
      memcpy(destination, asset->memory + asset->position, size);
      asset->position += size;
      return size;
    }
    size_t count = fread(destination, 1, size, asset->file);
    asset->position += count;
    asset->failed = count != size || ferror(asset->file) != 0;
    return count;
  }
  u8 *target = destination;
  size_t count = 0;
  while (count < size && !asset->failed) {
    size_t buffered = asset->output_size - asset->output_offset;
    if (buffered) {
      size_t bytes = buffered < size - count ? buffered : size - count;
      memcpy(target + count, asset->output + asset->output_offset, bytes);
      asset->output_offset += bytes;
      count += bytes;
    } else if (asset->finished) break;
    else if (size - count >= kAssetOutputBytes) {
      count += DecodeAssetBytes(asset, target + count, size - count);
    } else {
      asset->output_size = DecodeAssetBytes(asset, asset->output, kAssetOutputBytes);
      asset->output_offset = 0;
    }
  }
  asset->position += count;
  return count;
}

int ReadAssetByte(AssetFile *asset) {
  u8 value;
  return ReadAssetFile(asset, &value, 1) == 1 ? value : EOF;
}

static u8 *ReadAllAssetFile(AssetFile *asset, size_t limit, size_t *size) {
  *size = 0;
  size_t capacity = limit < kAssetOutputBytes ? limit : kAssetOutputBytes;
  u8 *data = malloc(capacity);
  if (!data) return NULL;
  for (;;) {
    *size += ReadAssetFile(asset, data + *size, capacity - *size);
    if (asset->failed) break;
    if (*size < capacity || capacity == limit) {
      if (ReadAssetByte(asset) == EOF && !asset->failed) return data;
      break;
    }
    capacity = capacity > limit / 2 ? limit : capacity * 2;
    u8 *grown = realloc(data, capacity);
    if (!grown) break;
    data = grown;
  }
  free(data);
  *size = 0;
  return NULL;
}

int OpenAssetSpan(AssetFile *asset, ByteSpan span) {
  *asset = (AssetFile){.memory = span.data, .length = span.size};
  return span.data ? 1 : 0;
}

bool SeekAssetFile(AssetFile *asset, u64 position) {
  if (asset->failed || asset->decoder || position > asset->length) return false;
  if (!asset->memory && (position > INT64_MAX || fseeko(asset->file, position, SEEK_SET))) return false;
  asset->position = position;
  return true;
}

enum { kArchiveHeaderBytes = 32, kAssetDecodedLimit = 512 * 1024 * 1024 };
typedef enum AssetCategory { ASSET_INTRO, ASSET_GRAPHICS, ASSET_AUDIO } AssetCategory;
typedef struct { AssetCategory category; const char *path; } AssetDescription;
static const AssetDescription kAssetCatalog[CITY_ASSET_FILE_COUNT] = {
#define CITY_ASSET(symbol, category, path) [CITY_ASSET_##symbol] = {ASSET_##category, path},
#include "assets/asset_catalog.inc"
#undef CITY_ASSET
};
static const char *const kCategoryNames[] = {"intro", "graphics", "audio"};

int OpenAssetSource(AssetFile *file, CityAssets *assets, u32 id, const char *root) {
  if (!root) return OpenAssetSpan(file, GetAsset(assets, id));
  char *path = NULL;
  FormatText(&path, "%s/%s", root, kAssetCatalog[id].path);
  int result = OpenAssetFile(file, path);
  free(path);
  return result;
}

void ReleaseDecodedCityAssets(CityAssets *assets) {
  for (int i = 0; i < assets->asset_block_count; ++i) free(assets->asset_blocks[i]);
  memset(assets->asset_blocks, 0, sizeof(assets->asset_blocks));
  memset(assets->asset_spans, 0, sizeof(assets->asset_spans));
  assets->asset_block_count = 0;
  assets->assets_loaded = false;
}

static bool DecodeAssetSection(FILE *file, u64 offset, u64 compressed, u8 *data, size_t size) {
  AssetFile reader = {.file = file, .bounded_stream = true, .compressed_remaining = compressed, .length = size};
  bool valid = offset <= INT64_MAX && !fseeko(file, offset, SEEK_SET);
  // The archive owns the stream; this operation owns only the decoder.
  valid = valid && InitializeAssetDecoder(&reader);
  valid = valid && ReadAssetFile(&reader, data, size) == size && ReadAssetByte(&reader) == EOF && !reader.failed;
  ReleaseAssetDecoder(&reader);
  return valid;
}

static int LoadAssetArchive(CityAssets *assets, const char *root, AssetCategory category) {
  char *path = NULL;
  FormatText(&path, "%s/%s.sc2assets", root, kCategoryNames[category]);
  FILE *file = fopen(path, "rb");
  free(path);
  if (!file) return errno == ENOENT ? 0 : -1;
  struct stat status;
  u8 header[kArchiveHeaderBytes], index[CITY_ASSET_FILE_COUNT * 48];
  bool valid = !fstat(fileno(file), &status) && status.st_size >= kArchiveHeaderBytes &&
      fread(header, 1, sizeof(header), file) == sizeof(header) && !memcmp(header, "SC2KPAK1", 8);
  u64 packed = valid ? ReadLE64(header + 8) : 0, size = valid ? ReadLE64(header + 16) : 0;
  u32 count = valid ? ReadLE32(header + 24) : 0, blocks = valid ? ReadLE32(header + 28) : 0;
  valid = valid && count > 0 && count <= CITY_ASSET_FILE_COUNT && blocks > 0 && blocks <= count &&
      packed > 0 && packed <= (u64)status.st_size - kArchiveHeaderBytes && size == 24 * (count + blocks) &&
      DecodeAssetSection(file, kArchiveHeaderBytes, packed, index, size);
  u32 previous = 0;
  for (u32 i = 0; valid && i < count; ++i) {
    const u8 *entry = index + 24 * i;
    u32 id = ReadLE32(entry), block = ReadLE32(entry + 4);
    valid = id < CITY_ASSET_FILE_COUNT && (!i || id > previous) && block < blocks &&
        kAssetCatalog[id].category == category && !assets->asset_spans[id].data;
    previous = id;
  }
  u64 content = kArchiveHeaderBytes + packed, end = 0;
  for (u32 block = 0; valid && block < blocks; ++block) {
    const u8 *record = index + 24 * (count + block);
    u64 offset = ReadLE64(record), compressed = ReadLE64(record + 8), bytes = ReadLE64(record + 16);
    valid = offset == end && compressed > 0 && compressed <= (u64)status.st_size - content - end &&
        bytes > 0 && bytes <= kAssetDecodedLimit;
    for (u32 i = 0; valid && i < count; ++i) {
      const u8 *entry = index + 24 * i;
      if (ReadLE32(entry + 4) != block) continue;
      u64 start = ReadLE64(entry + 8), length = ReadLE64(entry + 16);
      valid = length > 0 && start <= bytes && length <= bytes - start;
    }
    u8 *decoded = valid ? malloc(bytes) : NULL;
    valid = valid && decoded && DecodeAssetSection(file, content + offset, compressed, decoded, bytes);
    if (!valid) { free(decoded); break; }
    assets->asset_blocks[assets->asset_block_count++] = decoded;
    for (u32 i = 0; i < count; ++i) {
      const u8 *entry = index + 24 * i;
      if (ReadLE32(entry + 4) == block)
        assets->asset_spans[ReadLE32(entry)] = (ByteSpan){decoded + ReadLE64(entry + 8), ReadLE64(entry + 16)};
    }
    end += compressed;
  }
  valid = valid && end == (u64)status.st_size - content;
  if (fclose(file)) valid = false;
  if (valid)
    for (u32 id = 0; id < CITY_ASSET_FILE_COUNT; ++id)
      if (kAssetCatalog[id].category == category && id != CITY_ASSET_SOUND_OVERRIDES &&
          !assets->asset_spans[id].data) valid = false;
  return valid ? 1 : -1;
}

static int LoadLooseAsset(CityAssets *assets, u32 id, const char *root) {
  AssetFile file;
  int opened = OpenAssetSource(&file, assets, id, root);
  if (opened != 1) return opened;
  size_t size = 0;
  u8 *data = NULL;
  if (!file.decoder) {
    if (file.length > 0 && file.length <= kAssetDecodedLimit) {
      size = file.length;
      data = malloc(size);
      if (data && (ReadAssetFile(&file, data, size) != size || ReadAssetByte(&file) != EOF)) file.failed = true;
    }
  } else {
    data = ReadAllAssetFile(&file, kAssetDecodedLimit, &size);
    if (data && size) {
      u8 *compact = realloc(data, size);
      if (compact) data = compact;
    }
  }
  bool valid = data && size > 0;
  if (!CloseAssetFile(&file)) valid = false;
  if (!valid) { free(data); return -1; }
  assets->asset_blocks[assets->asset_block_count++] = data;
  assets->asset_spans[id] = (ByteSpan){data, size};
  return 1;
}

// The loading screen needs only the intro pack. Missing or invalid packs never select checkout files.
bool LoadCityIntroAsset(CityAssets *assets, const char *bundle_root) {
  if (GetAsset(assets, CITY_ASSET_INTRO).data) return true;
  if (LoadAssetArchive(assets, bundle_root ? bundle_root : ".", ASSET_INTRO) == 1) return true;
  ReleaseDecodedCityAssets(assets);
  return false;
}

bool LoadCityAssets(CityAssets *assets, const char *bundle_root) {
  if (assets->assets_loaded) return true;
  bool valid = true;
  for (AssetCategory category = ASSET_INTRO; valid && category <= ASSET_AUDIO; ++category) {
    bool complete = true;
    for (u32 id = 0; id < CITY_ASSET_FILE_COUNT; ++id)
      if (kAssetCatalog[id].category == category && id != CITY_ASSET_SOUND_OVERRIDES &&
          !GetAsset(assets, id).data) complete = false;
    if (!complete)
      valid = LoadAssetArchive(assets, bundle_root ? bundle_root : ".", category) == 1;
  }
  // Explicit development overrides are read once at startup, after the three required packs.
  const char *graphics = getenv("SC2K_GRAPHICS_OVERRIDE");
  if (valid && graphics && graphics[0]) valid = LoadLooseAsset(assets, CITY_ASSET_LANDSCAPE, graphics) == 1;
  const char *audio = getenv("SC2K_AUDIO_OVERRIDES");
  if (valid && audio && audio[0]) valid = LoadLooseAsset(assets, CITY_ASSET_SOUND_OVERRIDES, audio) >= 0;
  if (!valid) {
    fprintf(stderr, "Unable to preload intro, graphics and audio packs\n");
    ReleaseDecodedCityAssets(assets);
    return false;
  }
  assets->assets_loaded = true;
  return true;
}
