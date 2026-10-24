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
#include "drawing/sprites/sprites.h"
#include "base/byte_order.h"
#include "base/file.h"
#include "base/text.h"

enum { kArchiveHeaderBytes = 32, kAssetDecodedLimit = 512 * 1024 * 1024 };
typedef enum AssetCategory { ASSET_INTRO, ASSET_GRAPHICS, ASSET_AUDIO } AssetCategory;
typedef struct { AssetCategory category; const char *path; } AssetDescription;
static const AssetDescription kAssetCatalog[CITY_ASSET_FILE_COUNT] = {
#define CITY_ASSET(symbol, category, path) [CITY_ASSET_##symbol] = {ASSET_##category, path},
#include "assets/asset_catalog.inc"
#undef CITY_ASSET
};
static const char *const kCategoryNames[] = {"intro", "graphics", "audio"};

AssetOpenResult OpenAssetSource(AssetFile *file, CityAssets *assets, u32 id, const char *root) {
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

static AssetOpenResult LoadAssetArchive(CityAssets *assets, const char *root, AssetCategory category) {
  char *path = NULL;
  FormatText(&path, "%s/%s.sc2assets", root, kCategoryNames[category]);
  FILE *file = fopen(path, "rb");
  free(path);
  if (!file) return errno == ENOENT ? ASSET_OPEN_MISSING : ASSET_OPEN_ERROR;
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
