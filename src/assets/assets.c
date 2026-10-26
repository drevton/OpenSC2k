#include "assets/assets.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "assets/asset_file.h"
#include "assets/catalog.h"
#include "assets/cursors.h"
#include "assets/fonts.h"
#include "assets/graphics.h"
#include "assets/resources.h"
#include "assets/run_sprite.h"
#include "base/byte_order.h"
#include "base/file.h"
#include "base/text.h"

enum {
  kArchiveHeaderBytes = 32, kArchiveRecordBytes = 24,
  kAssetDecodedLimit = 512 * 1024 * 1024,
};
typedef enum AssetCategory { ASSET_INTRO, ASSET_GRAPHICS, ASSET_AUDIO } AssetCategory;
typedef struct { AssetCategory category; const char *path; } AssetDescription;
static const AssetDescription kAssetCatalog[CITY_ASSET_FILE_COUNT] = {
#define CITY_ASSET(symbol, category, path) [CITY_ASSET_##symbol] = {ASSET_##category, path},
#include "assets/asset_catalog.inc"
#undef CITY_ASSET
};
static const char *const kCategoryNames[] = {"intro", "graphics", "audio"};

AssetOpenResult OpenAssetSource(AssetFile *file, const CityAssets *assets, u32 id, const char *root) {
  if (!root) return OpenAssetSpan(file, GetAsset(assets, id));
  char *path = NULL;
  FormatText(&path, "%s/%s", root, kAssetCatalog[id].path);
  AssetOpenResult result = OpenAssetFile(file, path);
  free(path);
  return result;
}

static void ReleaseAssetBlocks(CityAssets *assets) {
  for (int i = 0; i < assets->asset_block_count; ++i) free(assets->asset_blocks[i]);
  memset(assets->asset_blocks, 0, sizeof(assets->asset_blocks));
  memset(assets->asset_spans, 0, sizeof(assets->asset_spans));
  assets->asset_block_count = 0;
  assets->asset_packs_loaded = false;
}

// Decoded image/font references must be released before the pack blocks they borrow.
void ReleaseCityAssets(CityAssets *assets) {
  for (int i = 0; i < CITY_INTRO_IMAGE_COUNT; ++i) FreeRunSprite(assets->intro.images[i]);
  assets->intro = (CityIntroArt){0};
  ReleaseCityInterfaceFont(&assets->interface_font);
  ReleaseCityNewspaperResources(&assets->newspaper);
  ReleaseCitySpriteAssets(assets);
  ReleaseCityCursorArtwork(assets);
  for (int stage = 0; stage < CITY_NEIGHBOR_ART_COUNT; ++stage) {
    FreeRunSprite(assets->neighbors[stage]);
    assets->neighbors[stage] = NULL;
  }
  memset(assets->facility_information, 0, sizeof(assets->facility_information));
  ReleaseCityInterfaceIcons(&assets->main_toolbar_icons);
  ReleaseCityInterfaceIcons(&assets->map_category_icons);
  ReleaseCityInterfaceIcons(&assets->bridge_choice_icons);
  for (int i = 0; i < CITY_ADVISOR_PORTRAIT_COUNT; ++i) {
    FreeRunSprite(assets->advisors[i]);
    assets->advisors[i] = NULL;
  }
  for (int i = 0; i <= CITY_NEWSPAPER_PICTURE_COUNT; ++i) {
    FreeRunSprite(assets->illustrations[i]);
    assets->illustrations[i] = NULL;
  }
  for (int i = 0; i < CITY_MUSIC_SPRITE_COUNT; ++i) FreeRunSprite(assets->music_buttons.images[i]);
  assets->music_buttons = (CityMusicAssets){0};
  ReleaseAssetBlocks(assets);
}

// SC2KPAK1 stores a compressed index, then consecutive compressed content blocks.
// Index entries and block descriptors are separate little-endian record arrays.
static AssetOpenResult LoadAssetArchive(CityAssets *assets, const char *root, AssetCategory category) {
  char *path = NULL;
  FormatText(&path, "%s/%s.sc2assets", root, kCategoryNames[category]);
  FILE *file = fopen(path, "rb");
  free(path);
  if (!file) return errno == ENOENT ? ASSET_OPEN_MISSING : ASSET_OPEN_ERROR;
  struct stat status;
  u8 header[kArchiveHeaderBytes];
  u8 index[CITY_ASSET_FILE_COUNT * 2 * kArchiveRecordBytes];
  bool valid = !fstat(fileno(file), &status) && status.st_size >= kArchiveHeaderBytes &&
      fread(header, 1, sizeof(header), file) == sizeof(header) && !memcmp(header, "SC2KPAK1", 8);
  u64 index_compressed_size = valid ? ReadLE64(header + 8) : 0;
  u64 index_size = valid ? ReadLE64(header + 16) : 0;
  u32 entry_count = valid ? ReadLE32(header + 24) : 0, block_count = valid ? ReadLE32(header + 28) : 0;
  valid = valid && entry_count > 0 && entry_count <= CITY_ASSET_FILE_COUNT &&
      block_count > 0 && block_count <= entry_count &&
      index_compressed_size > 0 && index_compressed_size <= (u64)status.st_size - kArchiveHeaderBytes &&
      index_size == kArchiveRecordBytes * (entry_count + block_count) &&
      DecodeAssetSection(file, kArchiveHeaderBytes, index_compressed_size, index, index_size);
  u32 previous_id = 0;
  for (u32 entry_index = 0; valid && entry_index < entry_count; ++entry_index) {
    const u8 *entry = index + kArchiveRecordBytes * entry_index;
    u32 id = ReadLE32(entry), block = ReadLE32(entry + 4);
    valid = id < CITY_ASSET_FILE_COUNT && (!entry_index || id > previous_id) && block < block_count &&
        kAssetCatalog[id].category == category && !assets->asset_spans[id].data;
    previous_id = id;
  }
  u64 content_offset = kArchiveHeaderBytes + index_compressed_size, content_end = 0;
  // Block offsets are relative to the content area. Entries address bytes inside a decoded block.
  for (u32 block = 0; valid && block < block_count; ++block) {
    const u8 *record = index + kArchiveRecordBytes * (entry_count + block);
    u64 block_offset = ReadLE64(record), compressed_size = ReadLE64(record + 8);
    u64 decoded_size = ReadLE64(record + 16);
    valid = block_offset == content_end && compressed_size > 0 &&
        compressed_size <= (u64)status.st_size - content_offset - content_end &&
        decoded_size > 0 && decoded_size <= kAssetDecodedLimit;
    for (u32 entry_index = 0; valid && entry_index < entry_count; ++entry_index) {
      const u8 *entry = index + kArchiveRecordBytes * entry_index;
      if (ReadLE32(entry + 4) != block) continue;
      u64 asset_offset = ReadLE64(entry + 8), asset_size = ReadLE64(entry + 16);
      valid = asset_size > 0 && asset_offset <= decoded_size && asset_size <= decoded_size - asset_offset;
    }
    u8 *decoded = valid ? malloc(decoded_size) : NULL;
    valid = valid && decoded &&
        DecodeAssetSection(file, content_offset + block_offset, compressed_size, decoded, decoded_size);
    if (!valid) {
      free(decoded);
      break;
    }
    // Publish only a fully validated block. The preload caller releases earlier blocks if a later step fails.
    assets->asset_blocks[assets->asset_block_count++] = decoded;
    for (u32 entry_index = 0; entry_index < entry_count; ++entry_index) {
      const u8 *entry = index + kArchiveRecordBytes * entry_index;
      if (ReadLE32(entry + 4) == block)
        assets->asset_spans[ReadLE32(entry)] = (ByteSpan){decoded + ReadLE64(entry + 8), ReadLE64(entry + 16)};
    }
    content_end += compressed_size;
  }
  valid = valid && content_end == (u64)status.st_size - content_offset;
  if (fclose(file)) valid = false;
  if (valid)
    for (u32 id = 0; id < CITY_ASSET_FILE_COUNT; ++id)
      if (kAssetCatalog[id].category == category && id != CITY_ASSET_SOUND_OVERRIDES &&
          !assets->asset_spans[id].data) valid = false;
  return valid ? ASSET_OPEN_OK : ASSET_OPEN_ERROR;
}

static AssetOpenResult LoadLooseAsset(CityAssets *assets, u32 id, const char *root) {
  AssetFile file;
  AssetOpenResult opened = OpenAssetSource(&file, assets, id, root);
  if (opened != ASSET_OPEN_OK) return opened;
  size_t size = 0;
  u8 *data = ReadAllAssetFile(&file, kAssetDecodedLimit, &size);
  bool valid = data && size > 0;
  if (!CloseAssetFile(&file)) valid = false;
  if (!valid) { free(data); return ASSET_OPEN_ERROR; }
  assets->asset_blocks[assets->asset_block_count++] = data;
  assets->asset_spans[id] = (ByteSpan){data, size};
  return ASSET_OPEN_OK;
}

// The loading screen needs only the intro pack. Missing or invalid packs never select checkout files.
static bool LoadCityIntroAsset(CityAssets *assets, const char *bundle_root) {
  if (GetAsset(assets, CITY_ASSET_INTRO).data) return true;
  if (LoadAssetArchive(assets, bundle_root ? bundle_root : ".", ASSET_INTRO) == ASSET_OPEN_OK) return true;
  ReleaseAssetBlocks(assets);
  return false;
}

// Retain decoded blocks for every category, then apply explicit startup overrides before typed resource decoding.
static bool LoadCityAssets(CityAssets *assets, const char *bundle_root) {
  if (assets->asset_packs_loaded) return true;
  bool valid = true;
  for (AssetCategory category = ASSET_INTRO; valid && category <= ASSET_AUDIO; ++category) {
    bool complete = true;
    for (u32 id = 0; id < CITY_ASSET_FILE_COUNT; ++id)
      if (kAssetCatalog[id].category == category && id != CITY_ASSET_SOUND_OVERRIDES &&
          !GetAsset(assets, id).data) complete = false;
    if (!complete)
      valid = LoadAssetArchive(assets, bundle_root ? bundle_root : ".", category) == ASSET_OPEN_OK;
  }
  // Explicit development overrides are read once at startup, after the three required packs.
  const char *graphics = getenv("SC2K_GRAPHICS_OVERRIDE");
  if (valid && graphics && graphics[0])
    valid = LoadLooseAsset(assets, CITY_ASSET_LANDSCAPE, graphics) == ASSET_OPEN_OK;
  const char *audio = getenv("SC2K_AUDIO_OVERRIDES");
  if (valid && audio && audio[0])
    valid = LoadLooseAsset(assets, CITY_ASSET_SOUND_OVERRIDES, audio) != ASSET_OPEN_ERROR;
  if (!valid) {
    fprintf(stderr, "Unable to preload intro, graphics and audio packs\n");
    ReleaseAssetBlocks(assets);
    return false;
  }
  assets->asset_packs_loaded = true;
  return true;
}

bool PreloadCityAssetPacks(CityAssets *assets, bool intro_only) {
  char *directory = ReadExecutableDirectory();
  if (directory) {
    char *root = NULL;
#ifdef __APPLE__
    FormatText(&root, "%s/../Resources", directory);
#else
    SetText(&root, directory);
#endif
    free(directory);
    bool loaded = intro_only ? LoadCityIntroAsset(assets, root) : LoadCityAssets(assets, root);
    free(root);
    return loaded;
  }
  return intro_only ? LoadCityIntroAsset(assets, NULL) : LoadCityAssets(assets, NULL);
}
