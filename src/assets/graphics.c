#include "assets/resource_types.h"
#include "city/infrastructure/types.h"
#include "base/byte_order.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/asset_file.h"
#include "assets/assets.h"

#include "assets/catalog.h"
#include "assets/resources.h"
#include "assets/state.h"
#include "game/state.h"
#include "base/file.h"
#include "base/text.h"

#include "drawing/bitmaps.h"
#include "drawing/sprites/sprites.h"
#include "drawing/state.h"
#include "ui/menus/menu.h"

// Graphics Assets

static bool LoadIntroArtwork(CityResourceState *assets) {
  AssetFile file;
  if (OpenAssetSpan(&file, GetAsset(assets, CITY_ASSET_INTRO)) != 1) return false;
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

static bool PreloadApplicationAssets(CityAssets *assets, bool intro_only) {
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

bool LoadApplicationIntroAssets(CityResourceState *assets) {
  return assets->intro.images[0] || (PreloadApplicationAssets(assets, true) && LoadIntroArtwork(assets));
}

static bool LoadCityPictureAssets(CityAssets *assets, u32 asset_id);

static void ReleaseInterfaceIcons(CityInterfaceIcons *icons) {
  for (int i = 0; i < icons->header_count; ++i) FreeRunSprite(icons->images[i]);
  free(icons->images);
  *icons = (CityInterfaceIcons){0};
}

static bool LoadInterfaceIcons(CityAssets *assets, CityInterfaceIcons *destination, u32 id, u32 count) {
  ByteSpan span = GetAsset(assets, id);
  const u8 *data = span.data;
  if (span.size < 24 || memcmp(data, "SC2KICO3", 8) || ReadLE32(data + 8) != count) return false;
  u32 headers = ReadLE32(data + 12), size = ReadLE32(data + 16), offset = ReadLE32(data + 20);
  u32 first = 24 + count * 2;
  if (!headers || headers > CITY_SPRITE_HEADER_MASK || size > count * 12 ||
      offset != ((first + size + 3) & ~3) || (u64)offset + headers * 12 > span.size) return false;
  CityInterfaceIcons icons = {.data = span, .variants = {data + first, size},
      .lookup = (const u16 *)(data + 24), .count = count};
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
      const RunSprite *sprite = icons.images[header];
      const u32 *rows = RunSpriteRows(sprite);
      const u8 *runs = RunSpriteData(sprite);
      for (int y = 0; valid && y < sprite->height; ++y) {
        const u8 *at = runs + rows[y], *last = runs + rows[y + 1];
        if (at < last) ReadSpriteInitialSkip(&at);
        while (valid && at < last) {
          int kind, length = ReadSpriteRun(&at, &kind);
          valid = kind <= SPRITE_RUN_RGBA;
          at += length * SpriteRunPayloadBytes(kind);
        }
      }
    }
    end += length;
  }
  valid = valid && end == span.size;
  if (!valid) { ReleaseInterfaceIcons(&icons); return false; }
  ReleaseInterfaceIcons(destination);
  *destination = icons;
  return true;
}

static bool GraphicsAssetLoadFailure(const char *name) {
  fprintf(stderr, "Unable to load graphics asset: %s\n", name);
  return false;
}

bool LoadApplicationGraphicsAssets(Game *game) {
  CityAssets *assets = game->resources;
  if (!PreloadApplicationAssets(assets, false)) return false;
  if (!LoadCityLandscapeAssets(assets)) return GraphicsAssetLoadFailure("landscape");
  if (!LoadCityDemolitionAssets(assets)) return GraphicsAssetLoadFailure("demolition");
  if (!LoadCityCursorAssets(game, NULL)) return GraphicsAssetLoadFailure("cursors");
  if (!LoadCityMusicAssets(assets, NULL)) return GraphicsAssetLoadFailure("music controls");
  if (!LoadCityOverlayFont(&game->drawing->overlay_font, assets, NULL)) return GraphicsAssetLoadFailure("overlay font");
  if (!LoadCityInterfaceFont(assets, NULL)) return GraphicsAssetLoadFailure("interface fonts");
  if (!LoadCityPictureAssets(assets, CITY_ASSET_ILLUSTRATIONS)) return GraphicsAssetLoadFailure("illustrations");
  if (!LoadCityPictureAssets(assets, CITY_ASSET_ADVISORS)) return GraphicsAssetLoadFailure("advisors");
  if (!LoadCityPictureAssets(assets, CITY_ASSET_NEIGHBORS)) return GraphicsAssetLoadFailure("neighbors");
  if (!LoadInterfaceIcons(assets, &assets->map_category_icons, CITY_ASSET_MAP_ICONS, CITY_MAP_ICON_COUNT))
    return GraphicsAssetLoadFailure("map icons");
  if (!LoadInterfaceIcons(assets, &assets->bridge_choice_icons, CITY_ASSET_BRIDGES, CITY_BRIDGE_CHOICE_ICON_COUNT))
    return GraphicsAssetLoadFailure("bridge icons");
  if (!LoadInterfaceIcons(assets, &assets->main_toolbar_icons, CITY_ASSET_TOOLBAR, CITY_TOOLBAR_ICON_COUNT))
    return GraphicsAssetLoadFailure("toolbar icons");
  return true;
}

// Graphics Resources
// Retain static picture sprites for ordinary canvas drawing.

// Picture packs store native run images and individual palettes. Assets owns their
// references; windows borrow them and the normal sprite drawer handles painting.
static bool LoadCityPictureAssets(CityAssets *assets, u32 asset_id) {
  assert(asset_id == CITY_ASSET_ILLUSTRATIONS || asset_id == CITY_ASSET_ADVISORS || asset_id == CITY_ASSET_NEIGHBORS);
  bool advisors = asset_id == CITY_ASSET_ADVISORS, neighbors = asset_id == CITY_ASSET_NEIGHBORS;
  int count = neighbors ? CITY_NEIGHBOR_ART_COUNT :
      advisors ? CITY_ADVISOR_PORTRAIT_COUNT : CITY_NEWSPAPER_PICTURE_COUNT + 1;
  AssetFile file;
  if (!OpenAssetSpan(&file, GetAsset(assets, asset_id))) return false;
  u8 header[12];
  bool valid = ReadAssetFile(&file, header, sizeof(header)) == sizeof(header) &&
      !memcmp(header, "SC2KPIC1", 8) && ReadLE32(header + 8) == count;
  for (int index = 0; valid && index < count; ++index) {
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
    RunSprite *sprite = ReadAssetPicture(&file, id, width, height);
    if (!sprite || !sprite->palette) { FreeRunSprite(sprite); valid = false; break; }
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
    if (!valid) { FreeRunSprite(sprite); break; }
    RunSprite **destination = neighbors ? &assets->neighbors[index] :
        advisors ? &assets->advisors[index] : &assets->illustrations[index];
    FreeRunSprite(*destination);
    *destination = sprite;
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

void ReleaseCityAssets(CityAssets *assets) {
  for (int i = 0; i < CITY_INTRO_IMAGE_COUNT; ++i) FreeRunSprite(assets->intro.images[i]);
  assets->intro = (CityIntroArt){0};
  ReleaseCityInterfaceFont(&assets->interface_font);
  ReleaseCityNewspaperResources(&assets->newspaper);
  ReleaseCitySpriteAssets(assets);
  free(assets->cursors.pixels);
  assets->cursors = (CityCursorAssets){0};
  for (int stage = 0; stage < CITY_NEIGHBOR_ART_COUNT; ++stage) {
    FreeRunSprite(assets->neighbors[stage]);
    assets->neighbors[stage] = NULL;
  }
  memset(assets->facility_information, 0, sizeof(assets->facility_information));
  ReleaseInterfaceIcons(&assets->main_toolbar_icons);
  ReleaseInterfaceIcons(&assets->map_category_icons);
  ReleaseInterfaceIcons(&assets->bridge_choice_icons);
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
  ReleaseDecodedCityAssets(assets);
}

bool LoadCityCursorAssets(Game *game, const char *graphics_root) {
  AssetFile file;
  int opened = OpenAssetSource(&file, game->resources, CITY_ASSET_CURSORS, graphics_root);
  if (opened != 1) return false;
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
  free(game->resources->cursors.pixels);
  game->resources->cursors.pixels = pixels;
  game->drawing->cursors.initialized = 0;
  return true;
}


void ReleaseCityCursorAssets(Game *game) {
  free(game->resources->cursors.pixels);
  game->resources->cursors = (CityCursorAssets){0};
  game->drawing->cursors.initialized = false;
}
