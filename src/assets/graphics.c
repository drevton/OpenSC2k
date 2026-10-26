#include "assets/catalog.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/asset_file.h"
#include "assets/assets.h"
#include "assets/cursors.h"
#include "assets/fonts.h"
#include "assets/graphics.h"
#include "assets/resource_types.h"
#include "assets/state.h"
#include "base/byte_order.h"
#include "city/infrastructure/types.h"
#include "assets/run_sprite.h"

// Graphics Assets

bool ValidateSpriteVariants(SpriteVariants variants, int sprite_count, int header_count) {
  int previous = -1;
  for (u32 at = 0; at < variants.size;) {
    if (variants.size - at < 4) return false;
    const u8 *record = variants.data + at;
    int sprite = ReadLE16(record), mask = record[2];
    if (sprite <= previous || sprite >= sprite_count || !mask || (mask & ~15) || record[3]) return false;
    previous = sprite;
    at += 4;
    for (int level = 0; level < 4; ++level) {
      if (!(mask & (1 << level))) continue;
      if (variants.size - at < 2) return false;
      int header = ReadLE16(variants.data + at) & CITY_SPRITE_HEADER_MASK;
      if (!header || header > header_count) return false;
      at += 2;
    }
  }
  return true;
}

u16 ResolveSpriteVariant(SpriteVariants variants, int sprite, int level, u16 fallback) {
  // Preparation only. Painting uses the selected, prescaled image directly.
  for (u32 at = 0; at < variants.size;) {
    const u8 *record = variants.data + at;
    int id = ReadLE16(record), mask = record[2];
    if (id > sprite) break;
    at += 4;
    for (int bit = 0; bit < 4; ++bit) {
      if (!(mask & (1 << bit))) continue;
      if (id == sprite && bit == level) return ReadLE16(variants.data + at);
      at += 2;
    }
    if (id == sprite) break;
  }
  return fallback;
}

// Read one native run image with validated dimensions. The caller owns the returned
// reference; malformed offsets/runs and truncated input fail without retaining an image.
RunSprite *ReadAssetRunSprite(AssetFile *file, int width, int height, u32 size) {
  assert(width > 0 && width <= SPRITE_RUN_LIMIT && height > 0 && height <= SPRITE_RUN_LIMIT);
  u32 row_bytes = 4 * (height + 1);
  if (size < row_bytes || size > row_bytes + 6 * (u64)width * height) return NULL;
  RunSprite *sprite = malloc(sizeof(*sprite) + size);
  if (!sprite) return NULL;
  *sprite = (RunSprite){.width = width, .height = height, .stream_size = size - row_bytes, .references = 1};
  bool valid = ReadAssetFile(file, sprite + 1, size) == size;
  u32 *rows = (u32 *)(sprite + 1);
  for (int row = 0; valid && row <= height; ++row) rows[row] = ReadLE32((u8 *)rows + 4 * row);
  if (!valid || !ValidateRunSprite(sprite)) { FreeRunSprite(sprite); return NULL; }
  return sprite;
}

// Picture records hold an ID, dimensions, optional image-local RGB colors and native runs.
// Zero expected dimensions accept the bounded record dimensions.
// Reject animation IDs and palette runs without an image palette at this boundary.
static RunSprite *ReadAssetPicture(AssetFile *file, int id, int width, int height) {
  u8 record[12], colors[256 * 3];
  if (ReadAssetFile(file, record, sizeof(record)) != sizeof(record) || ReadLE16(record) != id) return NULL;
  int w = ReadLE16(record + 2), h = ReadLE16(record + 4), count = ReadLE16(record + 6);
  if (!w || !h || w > 2048 || h > 2048 || (width && w != width) || (height && h != height) ||
      (count && count != 256)) return NULL;
  SpritePalette *palette = NULL;
  if (count) {
    if (ReadAssetFile(file, colors, sizeof(colors)) != sizeof(colors)) return NULL;
    palette = calloc(1, sizeof(*palette));
    if (!palette) return NULL;
    palette->references = 1;
    for (int i = 0; i < 256; ++i)
      palette->colors[i] = colors[3 * i] << 16 | colors[3 * i + 1] << 8 | colors[3 * i + 2];
  }
  RunSprite *sprite = ReadAssetRunSprite(file, w, h, ReadLE32(record + 8));
  if (!sprite) { FreeSpritePalette(palette); return NULL; }
  sprite->palette = palette;
  if (!RunSpriteHasOnlyKinds(sprite, palette ? SPRITE_RUN_STATIC_KINDS : SPRITE_RUN_DIRECT_KINDS)) {
    FreeRunSprite(sprite);
    return NULL;
  }
  return sprite;
}

static bool LoadIntroArtwork(CityResourceState *assets) {
  AssetFile file;
  if (OpenAssetSpan(&file, GetAsset(assets, CITY_ASSET_INTRO)) != ASSET_OPEN_OK) return false;
  u8 header[12];
  bool valid = ReadAssetFile(&file, header, sizeof(header)) == sizeof(header) &&
      !memcmp(header, "SC2KPIC1", 8) && ReadLE32(header + 8) == CITY_INTRO_IMAGE_COUNT;
  CityIntroArt art = {0};
  for (int i = 0; valid && i < CITY_INTRO_IMAGE_COUNT; ++i) {
    art.images[i] = ReadAssetPicture(&file, i, 0, 0);
    valid = art.images[i] != NULL;
  }
  if (valid) valid = ReadAssetByte(&file) == EOF && !file.failed;
  if (!CloseAssetFile(&file)) valid = false;
  for (int i = 0; i < CITY_INTRO_IMAGE_COUNT; ++i) {
    if (valid) FreeRunSprite(assets->intro.images[i]);
    else FreeRunSprite(art.images[i]);
  }
  if (valid) assets->intro = art;
  return valid;
}

bool LoadApplicationIntroAssets(CityResourceState *assets) {
  return assets->intro.images[0] || (PreloadCityAssetPacks(assets, true) && LoadIntroArtwork(assets));
}

void ReleaseCityInterfaceIcons(CityInterfaceIcons *icons) {
  for (int i = 0; i < icons->header_count; ++i) FreeRunSprite(icons->images[i]);
  free(icons->images);
  *icons = (CityInterfaceIcons){0};
}

bool LoadCityInterfaceIcons(CityAssets *assets, u32 id) {
  CityInterfaceIcons *destination;
  u32 count;
  switch (id) {
  case CITY_ASSET_MAP_ICONS:
    destination = &assets->map_category_icons;
    count = CITY_MAP_ICON_COUNT;
    break;
  case CITY_ASSET_BRIDGES:
    destination = &assets->bridge_choice_icons;
    count = CITY_BRIDGE_CHOICE_ICON_COUNT;
    break;
  case CITY_ASSET_TOOLBAR:
    destination = &assets->main_toolbar_icons;
    count = CITY_TOOLBAR_ICON_COUNT;
    break;
  default:
    return false;
  }
  ByteSpan span = GetAsset(assets, id);
  const u8 *data = span.data;
  if (span.size < 24 || memcmp(data, "SC2KICO3", 8) || ReadLE32(data + 8) != count) return false;
  u32 headers = ReadLE32(data + 12), size = ReadLE32(data + 16), offset = ReadLE32(data + 20);
  u32 first = 24 + count * 2;
  if (!headers || headers > CITY_SPRITE_HEADER_MASK || size > count * 12 ||
      offset != ((first + size + 3) & ~3) || (u64)offset + headers * 12 > span.size) return false;
  CityInterfaceIcons icons = {.data = span, .variants = {data + first, size},
      .lookup = data + 24, .count = count};
  if (!ValidateSpriteVariants(icons.variants, count, headers)) return false;
  for (u32 icon = 0; icon < count; ++icon) {
    u32 header = ReadLE16(data + 24 + icon * 2) & CITY_SPRITE_HEADER_MASK;
    if (!header || header > headers) return false;
  }
  icons.images = calloc(headers, sizeof(*icons.images));
  if (!icons.images) return false;
  u32 end = offset + headers * 12;
  bool valid = true;
  for (u32 header = 0; valid && header < headers; ++header) {
    const u8 *record = data + offset + header * 12;
    u32 detail = ReadLE32(record), pixels = ReadLE32(record + 4), length = ReadLE32(record + 8);
    valid = detail >= 1 && detail <= 512 && pixels == end && (u64)pixels + length <= span.size;
    if (!valid) break;
    AssetFile file;
    OpenAssetSpan(&file, (ByteSpan){data + pixels, length});
    icons.images[header] = ReadAssetRunSprite(&file, detail, detail, length);
    valid = icons.images[header] != NULL;
    CloseAssetFile(&file);
    if (valid) {
      icons.header_count++;
      // Interface packs carry only fixed RGB/alpha, never palette or animation IDs.
      valid = RunSpriteHasOnlyKinds(icons.images[header], SPRITE_RUN_DIRECT_KINDS);
    }
    end += length;
  }
  valid = valid && end == span.size;
  if (!valid) { ReleaseCityInterfaceIcons(&icons); return false; }
  ReleaseCityInterfaceIcons(destination);
  *destination = icons;
  return true;
}

// Read and validate one family image before its owning slot can be replaced.
// The cursor stays open at the consumed position on failure; only the new sprite is released.
static RunSprite *ReadPicturePackImage(AssetFile *file, u32 asset_id, int index) {
  bool advisors = asset_id == CITY_ASSET_ADVISORS, neighbors = asset_id == CITY_ASSET_NEIGHBORS;
  int id, width, height;
  if (neighbors) {
    id = index;
    width = CITY_NEIGHBOR_ART_WIDTH;
    height = CITY_NEIGHBOR_ART_HEIGHT;
  } else if (advisors) {
    id = CITY_ADVISOR_PORTRAIT_FIRST + index;
    width = CITY_ADVISOR_PORTRAIT_WIDTH;
    height = CITY_ADVISOR_PORTRAIT_HEIGHT;
  } else if (index == 0) {
    id = CITY_BITMAP_NOTICE_128;
    width = 106;
    height = 53;
  } else {
    id = CITY_NEWSPAPER_PICTURE_FIRST + index - 1;
    width = id == 411 ? 154 : 155;
    height = 100;
  }
  RunSprite *sprite = ReadAssetPicture(file, id, width, height);
  if (!sprite || !sprite->palette) { FreeRunSprite(sprite); return NULL; }
  bool valid = true;
  const u32 *rows = RunSpriteRows(sprite);
  const u8 *data = RunSpriteData(sprite);
  for (int y = 0; valid && y < height; ++y) {
    const u8 *at = data + rows[y], *end = data + rows[y + 1];
    int covered = at < end ? ReadSpriteInitialSkip(&at) : 0;
    if (!neighbors) valid = at < end && covered == 0;
    while (valid && at < end) {
      int kind, length = ReadSpriteRun(&at, &kind);
      if (kind == SPRITE_RUN_PALETTE) {
        if (neighbors) valid = !memchr(at, 0, length);
        at += length;
      } else valid = neighbors && kind == SPRITE_RUN_TRANSPARENT;
      covered += length;
    }
    if (!neighbors) valid = valid && covered == width;
  }
  if (!valid) { FreeRunSprite(sprite); return NULL; }
  return sprite;
}

// Picture packs store native run images and individual palettes. Assets owns their
// references; windows borrow them and the normal sprite drawer handles painting.
bool LoadCityPictureAssets(CityAssets *assets, u32 asset_id) {
  RunSprite **images;
  u32 count;
  switch (asset_id) {
  case CITY_ASSET_ILLUSTRATIONS:
    images = assets->illustrations;
    count = CITY_NEWSPAPER_PICTURE_COUNT + 1;
    break;
  case CITY_ASSET_ADVISORS:
    images = assets->advisors;
    count = CITY_ADVISOR_PORTRAIT_COUNT;
    break;
  case CITY_ASSET_NEIGHBORS:
    images = assets->neighbors;
    count = CITY_NEIGHBOR_ART_COUNT;
    break;
  default:
    return false;
  }
  AssetFile file;
  if (OpenAssetSpan(&file, GetAsset(assets, asset_id)) != ASSET_OPEN_OK) return false;
  u8 header[12];
  bool valid = ReadAssetFile(&file, header, sizeof(header)) == sizeof(header) &&
      !memcmp(header, "SC2KPIC1", 8) && ReadLE32(header + 8) == count;
  for (u32 index = 0; valid && index < count; ++index) {
    RunSprite *sprite = ReadPicturePackImage(&file, asset_id, index);
    if (!sprite) { valid = false; break; }
    FreeRunSprite(images[index]);
    images[index] = sprite;
  }
  if (valid) valid = ReadAssetByte(&file) == EOF && !file.failed;
  if (!CloseAssetFile(&file)) valid = false;
  return valid;
}

// Resource IDs resolve to fixed immutable sprite slots; there is no decoded bitmap cache.
const RunSprite *ReadCityPicture(const CityAssets *assets, int id) {
  if (id >= CITY_ADVISOR_PORTRAIT_FIRST && id < CITY_ADVISOR_PORTRAIT_FIRST + CITY_ADVISOR_PORTRAIT_COUNT)
    return assets->advisors[id - CITY_ADVISOR_PORTRAIT_FIRST];
  if (id == CITY_BITMAP_NOTICE_128) return assets->illustrations[0];
  if (id >= CITY_NEWSPAPER_PICTURE_FIRST && id < CITY_NEWSPAPER_PICTURE_FIRST + CITY_NEWSPAPER_PICTURE_COUNT)
    return assets->illustrations[1 + id - CITY_NEWSPAPER_PICTURE_FIRST];
  return NULL;
}

bool LoadCityCursorArtwork(CityAssets *assets, const char *graphics_root) {
  AssetFile file;
  AssetOpenResult opened = OpenAssetSource(&file, assets, CITY_ASSET_CURSORS, graphics_root);
  if (opened != ASSET_OPEN_OK) return false;
  u8 header[SC2K_CURSOR_ASSET_HEADER_SIZE];
  i32 valid = ReadAssetFile(&file, header, sizeof(header)) == sizeof(header) &&
      memcmp(header, SC2K_CURSOR_ASSET_MAGIC, 8) == 0 &&
      ReadLE32(header + 8) == SC2K_CURSOR_ASSET_SOURCE_SIZE &&
      ReadLE32(header + 12) == SC2K_CURSOR_ASSET_SOURCE_SIZE &&
      ReadLE32(header + 16) == SC2K_CURSOR_ASSET_ICON_SIZE &&
      ReadLE32(header + 20) == SC2K_CURSOR_ASSET_ICON_SIZE &&
      ReadLE32(header + 24) == SC2K_CURSOR_ASSET_ICON_COUNT;
  const size_t byte_count = SC2K_CURSOR_ASSET_PIXEL_COUNT * sizeof(u32);
  u32 *pixels = valid ? malloc(byte_count) : NULL;
  valid = pixels != NULL && ReadAssetFile(&file, pixels, byte_count) == byte_count &&
      ReadAssetByte(&file) == EOF && !file.failed;
  if (!CloseAssetFile(&file)) valid = 0;
  if (!valid) { free(pixels); return false; }
  for (i32 index = 0; index < SC2K_CURSOR_ASSET_PIXEL_COUNT; ++index)
    pixels[index] = ReadLE32((const u8 *)&pixels[index]);
  free(assets->cursors.pixels);
  assets->cursors.pixels = pixels;
  return true;
}


void ReleaseCityCursorArtwork(CityAssets *assets) {
  free(assets->cursors.pixels);
  assets->cursors = (CityCursorAssets){0};
}

// Music Assets

bool LoadCityMusicButtonAssets(CityAssets *resources, const char *root) {
  AssetFile file;
  if (OpenAssetSource(&file, resources, CITY_ASSET_MUSIC_BUTTONS, root) != ASSET_OPEN_OK) return false;
  u8 header[12];
  bool valid = ReadAssetFile(&file, header, sizeof(header)) == sizeof(header) &&
      !memcmp(header, "SC2KPIC1", 8) && ReadLE32(header + 8) == CITY_MUSIC_SPRITE_COUNT;
  CityMusicAssets images = {0};
  for (int i = 0; valid && i < CITY_MUSIC_SPRITE_COUNT; ++i) {
    images.images[i] = ReadAssetPicture(&file, i, CITY_MUSIC_SPRITE_SIZE, CITY_MUSIC_SPRITE_SIZE);
    valid = images.images[i] != NULL;
  }
  if (valid) valid = ReadAssetByte(&file) == EOF && !file.failed;
  if (!CloseAssetFile(&file)) valid = false;
  for (int i = 0; i < CITY_MUSIC_SPRITE_COUNT; ++i) {
    if (valid) FreeRunSprite(resources->music_buttons.images[i]);
    else FreeRunSprite(images.images[i]);
  }
  if (valid) resources->music_buttons = images;
  return valid;
}
