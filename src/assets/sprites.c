#include "city/maps/trees.h"
#include "city/maps/tile_storage.h"
#include "assets/graphics.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/asset_file.h"
#include "assets/assets.h"
#include "base/byte_order.h"
#include "city/maps/layers.h"
#include "drawing/maps/tables.h"
#include "drawing/sprites/sprites.h"

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

const u16 kCityConstructionHeights[CITY_CONSTRUCTION_HEIGHTS] = {32, 64, 128, 192, 320, 512};

int CityBuildingArtworkSourceFirst(int sprite) {
  if (sprite == CITY_MAP_TILE_SPRITE_PARK) return CITY_LANDSCAPE_SMALL_PARK_FIRST;
  if (sprite >= CITY_BUILDING_ART_FIRST && sprite < CITY_BUILDING_ART_FIRST + CITY_BUILDING_ART_COUNT)
    return CITY_LANDSCAPE_BUILDING_FIRST + (sprite - CITY_BUILDING_ART_FIRST) * CITY_CIVIC_VIEWS;
  if (sprite < CITY_CIVIC_KIND_FIRST || sprite >= CITY_CIVIC_KIND_FIRST + CITY_CIVIC_KIND_COUNT) return -1;
  return CITY_LANDSCAPE_CIVIC_FIRST + (sprite - CITY_CIVIC_KIND_FIRST) * CITY_CIVIC_VIEWS;
}

// Retired textures and composed road/rail approaches have no stored sprite.
static bool IsUnstoredLandscapeSprite(int id) {
  return (id >= CITY_LANDSCAPE_UNDERWORLD_GROUND_FIRST && id < CITY_LANDSCAPE_HIGHWAY_MERGE_FIRST) ||
      id == CITY_LANDSCAPE_COPPER_MATERIAL ||
      (id >= CITY_LANDSCAPE_UNDERWORLD_ROAD_TUNNEL_FIRST && id < CITY_LANDSCAPE_DIAGONAL_ROAD_FIRST) ||
      (id >= CITY_LANDSCAPE_DIAGONAL_ROAD_SHARED_FIRST && id < CITY_LANDSCAPE_DIAGONAL_RAIL_FIRST) ||
      (id >= CITY_LANDSCAPE_DIAGONAL_RAIL_SHARED_FIRST && id < CITY_LANDSCAPE_BAKED_UNDERWORLD_FIRST);
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

// Pictures share the landscape run decoder, with optional image-local colors.
// Reject animation IDs and palette runs without an image palette at this boundary.
RunSprite *ReadAssetPicture(AssetFile *file, int id, int width, int height) {
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
  const u32 *rows = RunSpriteRows(sprite);
  const u8 *data = RunSpriteData(sprite);
  for (int y = 0; y < h; ++y) {
    const u8 *at = data + rows[y], *end = data + rows[y + 1];
    if (at < end) ReadSpriteInitialSkip(&at);
    while (at < end) {
      int kind, length = ReadSpriteRun(&at, &kind);
      if (SpriteRunAnimated(kind) || (SpriteRunIndexed(kind) && !palette)) {
        FreeRunSprite(sprite);
        return NULL;
      }
      at += length * SpriteRunPayloadBytes(kind);
    }
  }
  return sprite;
}

static bool ReadBuildingArtwork(AssetFile *file, const CityLandscapeSprite *sprites,
    CityBuildingArtwork buildings[CITY_BUILDING_ART_COUNT]) {
  u8 header[20];
  if (ReadAssetFile(file, header, 8) != 8 || memcmp(header, "SC2KBLD2", 8)) return false;
  for (int kind = 0; kind < CITY_BUILDING_ART_COUNT; ++kind) {
    if (ReadAssetFile(file, header, sizeof(header)) != sizeof(header)) return false;
    u32 structure = ReadLE32(header), family = ReadLE32(header + 4);
    u32 width = ReadLE32(header + 8), height = ReadLE32(header + 12);
    u32 ground = ReadLE32(header + 16);
    u32 material = ground & ~CITY_BUILDING_GROUND_FLAGS;
    const RunSprite *front = sprites[CITY_LANDSCAPE_BUILDING_FIRST + kind * CITY_CIVIC_VIEWS].sprite;
    if (!front || !structure || structure > 1024 || family > 1 || width != (u32)front->width ||
        height != (u32)front->height || material >= CITY_BUILDING_GROUND_COUNT ||
        (material && !sprites[CITY_LANDSCAPE_BUILDING_GROUND_FIRST + material - 1].sprite)) return false;
    buildings[kind].structure_height = structure;
    buildings[kind].family = family;
    buildings[kind].ground = ground;
    for (int view = 0; view < CITY_CIVIC_VIEWS; ++view) {
      const RunSprite *sprite = sprites[CITY_LANDSCAPE_BUILDING_FIRST + kind * CITY_CIVIC_VIEWS + view].sprite;
      if (!sprite || sprite->width != front->width || sprite->height != front->height) return false;
    }
  }
  return true;
}

/* Compact archive commands resolve shared images and references once at load time. */
typedef enum GraphicsCommand {
  GRAPHICS_END, GRAPHICS_PALETTE, GRAPHICS_IMAGE, GRAPHICS_SPRITE,
  GRAPHICS_WATER_COLORS, GRAPHICS_BRIDGE_PALETTES
} GraphicsCommand;

enum { GRAPHICS_MAX_WIDTH = 512, GRAPHICS_MAX_HEIGHT = 2048 };

static bool ReadGraphicsInteger(AssetFile *file, u32 *value) {
  u32 result = 0;
  for (int shift = 0; shift <= 28; shift += 7) {
    int byte = ReadAssetByte(file);
    if (byte == EOF || (shift == 28 && byte > 15)) return false;
    result |= (u32)(byte & 127) << shift;
    if (!(byte & 128)) {
      if (shift && !byte) return false;
      *value = result;
      return true;
    }
  }
  return false;
}

static bool ReadGraphicsCommands(AssetFile *file, CityLandscapeSprite *sprites, int limit,
    u8 water[20], u32 bridge[CITY_LANDSCAPE_BRIDGE_STYLES][256]) {
  RunSprite **images = calloc(limit, sizeof(*images));
  SpritePalette **palettes = calloc(limit, sizeof(*palettes));
  bool valid = images && palettes, finished = false, have_water = false, have_bridge = false;
  while (valid && !finished) {
    int command = ReadAssetByte(file);
    u32 id, image, width, height, palette, size, baseline, scale, offset;
    switch (command) {
    case GRAPHICS_END: finished = true; break;
    case GRAPHICS_PALETTE:
      valid = ReadGraphicsInteger(file, &id) && id > 0 && id < limit && !palettes[id];
      int colors = valid ? ReadAssetByte(file) + 1 : 0;
      if (colors < 1 || colors > 256) { valid = false; break; }
      SpritePalette *table = calloc(1, sizeof(*table));
      if (!table) { valid = false; break; }
      table->references = 1;
      palettes[id] = table;
      for (int i = 0; valid && i < colors; ++i) {
        u8 rgb[3];
        valid = ReadAssetFile(file, rgb, 3) == 3;
        if (valid) table->colors[i] = 0xFF000000 | rgb[0] << 16 | rgb[1] << 8 | rgb[2];
      }
      break;
    case GRAPHICS_IMAGE:
      valid = ReadGraphicsInteger(file, &id) && id < limit && !images[id] &&
          ReadGraphicsInteger(file, &width) && width > 0 && width <= GRAPHICS_MAX_WIDTH &&
          ReadGraphicsInteger(file, &height) && height > 0 && height <= GRAPHICS_MAX_HEIGHT &&
          ReadGraphicsInteger(file, &palette) && palette < limit && (!palette || palettes[palette]) &&
          ReadGraphicsInteger(file, &size);
      if (!valid) break;
      RunSprite *sprite = ReadAssetRunSprite(file, width, height, size);
      images[id] = sprite;
      valid = sprite != NULL;
      if (sprite) sprite->palette = RetainSpritePalette(palettes[palette]);
      break;
    case GRAPHICS_SPRITE:
      valid = ReadGraphicsInteger(file, &id) && id > 0 && id <= limit && !sprites[id - 1].sprite &&
          ReadGraphicsInteger(file, &image) && image < limit && images[image] &&
          ReadGraphicsInteger(file, &scale) && scale > 0 && scale <= 32 &&
          ReadGraphicsInteger(file, &baseline) && baseline <= images[image]->height &&
          // Cropping can leave the original canvas anchor outside the remaining image pixels.
          ReadGraphicsInteger(file, &offset) && offset <= 2 * GRAPHICS_MAX_WIDTH;
      if (!valid) break;
      --id;
      int flags = ReadAssetByte(file);
      valid = flags == 0 || flags == 2;
      palette = 0;
      if (valid && (flags & 2)) valid = ReadGraphicsInteger(file, &palette) && palette < limit &&
          (!palette || palettes[palette]);
      if (!valid) break;
      sprites[id] = (CityLandscapeSprite){ReferenceRunSprite(images[image],
          flags ? palettes[palette] : images[image]->palette, false), scale, baseline,
          (int)(offset >> 1) ^ -(int)(offset & 1)};
      valid = sprites[id].sprite != NULL;
      break;
    case GRAPHICS_WATER_COLORS:
      valid = !have_water && ReadAssetFile(file, water, 20) == 20;
      have_water = valid;
      break;
    case GRAPHICS_BRIDGE_PALETTES:
      valid = !have_bridge;
      for (int style = 0; valid && style < CITY_LANDSCAPE_BRIDGE_STYLES; ++style) {
        valid = ReadGraphicsInteger(file, &palette) && palette > 0 && palette < limit && palettes[palette];
        if (valid) memcpy(bridge[style], palettes[palette]->colors, 256 * sizeof(u32));
      }
      have_bridge = valid;
      break;
    default: valid = false; break;
    }
  }
  for (int i = 0; i < limit; ++i) {
    if (images) FreeRunSprite(images[i]);
    if (palettes) FreeSpritePalette(palettes[i]);
  }
  free(images);
  free(palettes);
  return valid && finished && have_water && have_bridge;
}

// Resolve source aliases in the existing catalog. Headers remain unchanged until
// every alias has retained its image, so mirrored canonical sources are safe too.
static bool ResolveGraphicsSpriteIndexes(CityLandscapeSprite *sprites, int limit,
    const u16 *lookup, int count) {
  for (int id = 0; id < limit; ++id)
    if (sprites[id].sprite && (lookup[id] & CITY_SPRITE_HEADER_MASK) != id + 1) return false;
  for (int id = 0; id < count; ++id) {
    int header = lookup[id] & CITY_SPRITE_HEADER_MASK;
    if (header ? header > limit || !sprites[header - 1].sprite : lookup[id] != 0) return false;
  }
  for (int id = 0; id < limit; ++id) {
    int header = lookup[id] & CITY_SPRITE_HEADER_MASK;
    if (!header || header == id + 1) continue;
    sprites[id] = sprites[header - 1];
    const RunSprite *source = sprites[id].sprite;
    sprites[id].sprite = lookup[id] & CITY_SPRITE_HEADER_FLIPPED ?
        ReferenceRunSprite(source, source->palette, true) : RetainRunSprite(source);
    if (!sprites[id].sprite) return false;
  }
  for (int id = 0; id < limit; ++id) {
    if ((lookup[id] & CITY_SPRITE_HEADER_MASK) != id + 1 || !(lookup[id] & CITY_SPRITE_HEADER_FLIPPED)) continue;
    RunSprite *source = sprites[id].sprite;
    sprites[id].sprite = ReferenceRunSprite(source, source->palette, true);
    FreeRunSprite(source);
    if (!sprites[id].sprite) return false;
  }
  return true;
}

static bool ReadGraphicsSpriteIndex(AssetFile *file, CityResourceState *assets, int count) {
  if (count < CITY_LANDSCAPE_SPRITE_COUNT || count > 65536 ||
      count != assets->map_group_sprite_first + assets->map_group_sprite_count) return false;
  size_t bytes = count * sizeof(u16);
  if (bytes > file->length - file->position) return false;
  const u16 *lookup = (const u16 *)(file->memory + file->position);
  if (!SeekAssetFile(file, file->position + bytes)) return false;
  assets->sprite_index_lookup = lookup;
  assets->map_sprite_index_count = count;
  return true;
}

static bool ReadKindHeightMetadata(AssetFile *file, CityResourceState *assets, const CityLandscapeSprite *sprites) {
  u8 header[8];
  if (ReadAssetFile(file, header, sizeof(header)) != sizeof(header) || memcmp(header, "SC2KHGT1", 8) ||
      ReadAssetFile(file, assets->kind_sprite_heights, sizeof(assets->kind_sprite_heights)) !=
          sizeof(assets->kind_sprite_heights)) return false;
  for (int kind = 0; kind < TK_COUNT; ++kind) {
    int layout = kCityMapKindSprites[kind];
    bool building = CityBuildingArtworkSourceFirst(layout) >= 0;
    int height = assets->kind_sprite_heights[kind];
    if (building ? height < 4 : height != 0) return false;
    if (!building) continue;
    for (int view = 0; view < CITY_CIVIC_VIEWS; ++view) {
      int index = TileSpriteResourceIndex(assets, layout, view);
      int source = (assets->sprite_index_lookup[index] & CITY_SPRITE_HEADER_MASK) - 1;
      if (!sprites[source].sprite) return false;
    }
  }
  return true;
}

static bool ReadGraphicsVariants(AssetFile *file, CityResourceState *assets) {
  u8 header[4];
  if (ReadAssetFile(file, header, sizeof(header)) != sizeof(header)) return false;
  u32 size = ReadLE32(header);
  if (size > assets->map_sprite_index_count * 12) return false;
  if (size > file->length - file->position) return false;
  SpriteVariants variants = {file->memory + file->position, size};
  if (!SeekAssetFile(file, file->position + size) ||
      !ValidateSpriteVariants(variants, assets->map_sprite_index_count, assets->map_group_sprite_first)) return false;
  assets->sprite_variants = variants;
  return true;
}

static bool ValidateLandscapeSprites(CityLandscapeSprite *sprites,
    CityRect tree_bounds[CITY_LANDSCAPE_TREE_ART_COUNT], int count) {
  bool valid = true;
  for (int item = 0; valid && item < count; ++item) {
    u32 id = item, width, height;
    RunSprite *sprite;
    sprite = sprites[id].sprite;
    if (!sprite) continue;
    width = sprite->width; height = sprite->height;
    const u32 *rows = RunSpriteRows(sprite);
    valid = valid && ValidateRunSprite(sprite);
    const u8 *data = RunSpriteData(sprite);
    bool tree_preview = id >= CITY_LANDSCAPE_TREE_FIRST && id < CITY_LANDSCAPE_POINT &&
        (id - CITY_LANDSCAPE_TREE_FIRST) % CITY_LANDSCAPE_TREE_STAGES == CITY_TREE_GROWN;
    CityRect visible = {width, height, 0, 0};
    for (u32 y = 0; valid && y < height; ++y) {
      const u8 *at = data + rows[y], *end = data + rows[y + 1];
      int x = at < end ? ReadSpriteInitialSkip(&at) : 0;
      while (valid && at < end) {
        int kind, pixels = ReadSpriteRun(&at, &kind), bytes = SpriteRunPayloadBytes(kind);
        if (SpriteRunIndexed(kind)) {
          bool selector = (id >= CITY_LANDSCAPE_SELECTION_FIRST && id < CITY_LANDSCAPE_SELECTION_END) ||
              (id >= CITY_LANDSCAPE_UNDERWORLD_SELECTION_FIRST && id < CITY_LANDSCAPE_HIGHWAY_MERGE_FIRST) ||
              (id >= CITY_LANDSCAPE_POINT && id < CITY_LANDSCAPE_RAPIDS_FIRST) ||
              (id >= CITY_LANDSCAPE_BAKED_SELECTION_FIRST && id < CITY_LANDSCAPE_BAKED_FACE_FIRST);
          bool infrastructure = id >= CITY_MAP_TILE_SPRITE_POWER_LINE_FIRST && id <= CITY_MAP_TILE_SPRITE_HIGHWAY_LAST;
          infrastructure |= id >= CITY_LANDSCAPE_ROAD_BRIDGE_FIRST &&
              id < CITY_LANDSCAPE_CIVIC_FIRST;
          bool civic = id >= CITY_LANDSCAPE_CIVIC_FIRST;
          bool underworld_ground = (id >= CITY_LANDSCAPE_BAKED_UNDERWORLD_FIRST &&
              id < CITY_LANDSCAPE_BAKED_PIPE_FIRST) || id == CITY_LANDSCAPE_BAKED_FACE_FIRST;
          for (int pixel = 0; pixel < pixels; ++pixel) {
            int index = at[pixel * bytes];
            if (sprite->palette && !SpriteRunAnimated(kind) && sprite->palette->colors[index] >> 24 != 255)
              valid = false;
            bool allowed = civic ? !SpriteRunAnimated(kind) || (index > 0 && index < SPRITE_ANIMATION_COLORS) :
                infrastructure || underworld_ground || (selector ?
                (index >= 1 && index <= 15) || index == 23 ||
                (id >= CITY_LANDSCAPE_POINT && id < CITY_LANDSCAPE_RAPIDS_FIRST && index == 0xA9) :
                index >= SPRITE_ANIMATION_WATER_FIRST && index < SPRITE_ANIMATION_COLORS);
            if (!allowed || (!civic && SpriteRunAnimated(kind) !=
                (!selector && !infrastructure && !underworld_ground))) valid = false;
          }
        }
        if (tree_preview && kind != SPRITE_RUN_TRANSPARENT) {
          visible = UnionCityRects(visible, (CityRect){x, y, x + pixels, y + 1});
        }
        x += pixels;
        at += pixels * bytes;
      }
    }
    if (tree_preview && visible.right > visible.left)
      tree_bounds[(id - CITY_LANDSCAPE_TREE_FIRST) / CITY_LANDSCAPE_TREE_STAGES] = visible;
  }
  for (int id = CITY_MAP_TILE_SPRITE_TREE_FIRST; valid && id <= CITY_MAP_TILE_SPRITE_TREE_LAST; ++id)
    valid = sprites[id].sprite != NULL;
  for (int id = CITY_MAP_TILE_SPRITE_POWER_LINE_FIRST; valid && id <= CITY_MAP_TILE_SPRITE_HIGHWAY_LAST; ++id)
    valid = sprites[id].sprite != NULL;
  for (int id = CITY_SPRITE_CATALOG_FLAT_TERRAIN;
      valid && id < CITY_SPRITE_CATALOG_UNDERGROUND_FIRST; ++id)
    valid = sprites[id].sprite != NULL;
  int required_end = CITY_LANDSCAPE_CIVIC_FIRST;
  for (int id = CITY_LANDSCAPE_SELECTION_FIRST; valid && id < required_end; ++id) {
    if (!IsUnstoredLandscapeSprite(id)) {
      valid = sprites[id].sprite != NULL;
      if (!valid) fprintf(stderr, "Missing required landscape sprite %d\n", id);
    }
  }
  return valid;
}

bool LoadCityDemolitionAssets(CityResourceState *assets) {
  AssetFile file;
  int opened = OpenAssetSpan(&file, GetAsset(assets, CITY_ASSET_DEMOLITION));
  if (opened != 1) return false;
  u8 header[28], water[20];
  bool valid = ReadAssetFile(&file, header, 12) == 12 && !memcmp(header, "SC2KLAN2", 8);
  int count = valid ? ReadLE32(header + 8) : 0;
  valid = valid && count > 0 && count <= CITY_DEMOLITION_FRAME_COUNT &&
      ReadAssetFile(&file, water, sizeof(water)) == sizeof(water);
  CityLandscapeSprite *sprites = calloc(CITY_DEMOLITION_FRAME_COUNT, sizeof(*sprites));
  valid = valid && sprites;
  for (int item = 0; valid && item < count; ++item) {
    valid = ReadAssetFile(&file, header, sizeof(header)) == sizeof(header);
    if (!valid) break;
    u32 id = ReadLE32(header), width = ReadLE32(header + 4), height = ReadLE32(header + 8);
    u32 baseline = ReadLE32(header + 12), scale = ReadLE32(header + 16), size = ReadLE32(header + 24);
    int offset_x = (i32)ReadLE32(header + 20);
    valid = id < CITY_DEMOLITION_FRAME_COUNT && !sprites[id].sprite && width > 0 && width <= 512 &&
        height > 0 && height <= 1024 && baseline <= height && scale > 0 && scale <= 32 &&
        offset_x >= -(int)width && offset_x <= (int)width &&
        size >= 4 * (height + 1) && size <= 4 * (height + 1) + 6 * width * height;
    if (!valid) break;
    RunSprite *sprite = malloc(sizeof(*sprite) + size);
    if (!sprite) { valid = false; break; }
    sprites[id] = (CityLandscapeSprite){sprite, scale, baseline, offset_x};
    *sprite = (RunSprite){.width = width, .height = height, .stream_size = size - 4 * (height + 1), .references = 1};
    valid = ReadAssetFile(&file, sprite + 1, size) == size;
    if (!valid) break;
    u32 *rows = (u32 *)(sprite + 1);
    for (u32 row = 0; row <= height; ++row) rows[row] = ReadLE32((u8 *)rows + 4 * row);
    valid = ValidateRunSprite(sprite);
    for (u32 y = 0; valid && y < height; ++y) {
      const u8 *at = RunSpriteData(sprite) + rows[y], *end = RunSpriteData(sprite) + rows[y + 1];
      if (at < end) ReadSpriteInitialSkip(&at);
      while (valid && at < end) {
        int kind, pixels = ReadSpriteRun(&at, &kind);
        valid = !SpriteRunIndexed(kind);
        at += pixels * SpriteRunPayloadBytes(kind);
      }
    }
  }
  for (int id = 0; valid && id < CITY_DEMOLITION_FRAME_COUNT; ++id) valid = sprites[id].sprite != NULL;
  valid = valid && ReadAssetByte(&file) == EOF && !file.failed;
  if (!CloseAssetFile(&file)) valid = false;
  if (sprites) {
    for (int id = 0; id < CITY_DEMOLITION_FRAME_COUNT; ++id) {
      if (valid) {
        FreeRunSprite(assets->demolition[id].sprite);
        assets->demolition[id] = sprites[id];
      } else FreeRunSprite(sprites[id].sprite);
    }
  }
  free(sprites);
  return valid;
}

static bool LoadCityLandscapeSpriteSet(CityResourceState *assets) {
  int limit = assets->map_group_sprite_first;
  AssetFile file;
  int opened = OpenAssetSpan(&file, GetAsset(assets, CITY_ASSET_LANDSCAPE));
  if (opened != 1) return false;
  u8 header[28], palette_bytes[20];
  bool valid = ReadAssetFile(&file, header, 12) == 12 &&
      !memcmp(header, "SC2KGFX3", 8);
  if (valid) valid = ReadGraphicsSpriteIndex(&file, assets, ReadLE32(header + 8));
  if (valid) valid = ReadGraphicsVariants(&file, assets);
  int count = limit;
  valid = valid && count > 0 && count <= limit;
  u32 bridge_palettes[CITY_LANDSCAPE_BRIDGE_STYLES][256] = {0};
  CityLandscapeSprite *sprites = calloc(limit, sizeof(*sprites));
  valid = valid && sprites;
  CityPowerSmoke power_smoke[CITY_POWER_SMOKE_KINDS][CITY_CIVIC_VIEWS] = {0};
  CityBuildingArtwork buildings[CITY_BUILDING_ART_COUNT] = {0};
  u8 civic_ground[CITY_CIVIC_ART_COUNT] = {0};
  CityRect tree_bounds[CITY_LANDSCAPE_TREE_ART_COUNT] = {0};
  if (valid) valid = ReadGraphicsCommands(&file, sprites, limit, palette_bytes, bridge_palettes);
  if (valid) {
    for (u32 at = 0; valid && at < assets->sprite_variants.size;) {
      int mask = assets->sprite_variants.data[at + 2];
      at += 4;
      for (int level = 0; valid && level < 4; ++level) {
        if (!(mask & (1 << level))) continue;
        int header = ReadLE16(assets->sprite_variants.data + at) & CITY_SPRITE_HEADER_MASK;
        valid = sprites[header - 1].sprite != NULL;
        at += 2;
      }
    }
    if (valid) valid = ResolveGraphicsSpriteIndexes(sprites, limit,
        assets->sprite_index_lookup, assets->map_sprite_index_count);
  }
  for (u32 id = assets->map_group_sprite_first; valid && id < assets->map_sprite_index_count; ++id)
    valid = (assets->sprite_index_lookup[id] & CITY_SPRITE_HEADER_MASK) != 0;
  if (valid) valid = ValidateLandscapeSprites(sprites, tree_bounds, count);
  for (int first = CITY_LANDSCAPE_CIVIC_FIRST;
      valid && first < CITY_LANDSCAPE_POWER_SMOKE_FIRST; first += CITY_CIVIC_VIEWS) {
    const CityLandscapeSprite *front = &sprites[first];
    for (int view = 0; valid && view < CITY_CIVIC_VIEWS; ++view) {
      const CityLandscapeSprite *other = &sprites[first + view];
      valid = (front->sprite != NULL) == (other->sprite != NULL);
      if (valid && other->sprite)
        valid = other->scale == 4 && other->offset_x == 0 && other->baseline == other->sprite->height &&
            other->sprite->width == front->sprite->width && other->sprite->height == front->sprite->height;
    }
  }
  for (int type = 0; valid && type < CITY_POWER_SMOKE_TYPES; ++type) {
    for (int frame = 0; valid && frame < CITY_POWER_SMOKE_FRAMES; ++frame) {
      const CityLandscapeSprite *smoke = &sprites[CITY_LANDSCAPE_POWER_SMOKE_FIRST +
          type * CITY_POWER_SMOKE_FRAMES + frame];
      valid = smoke->sprite && smoke->sprite->width == CITY_POWER_SMOKE_WIDTH &&
          smoke->sprite->height == CITY_POWER_SMOKE_WIDTH && smoke->scale == 4 &&
          smoke->baseline == (type == CITY_POWER_SMOKE_STEAM ? CITY_POWER_STEAM_BASELINE : CITY_POWER_SMOKE_WIDTH) &&
          smoke->offset_x == 0;
    }
  }
  if (valid) {
    valid = ReadAssetFile(&file, header, 8) == 8 && !memcmp(header, "SC2KSMK1", 8);
    static const u8 kSmokeKinds[] = {TK_OIL_POWER_PLANT, TK_NUCLEAR_POWER_PLANT,
        TK_COAL_POWER_PLANT};
    for (int kind = 0; valid && kind < CITY_POWER_SMOKE_KINDS; ++kind) {
      for (int view = 0; valid && view < CITY_CIVIC_VIEWS; ++view) {
        valid = ReadAssetFile(&file, header, 4) == 4;
        int count = valid ? ReadLE32(header) : 0;
        valid = valid && count > 0 && count <= CITY_POWER_SMOKE_EMITTERS;
        CityPowerSmoke *smoke = &power_smoke[kind][view];
        smoke->count = count;
        const CityLandscapeSprite *plant = &sprites[CITY_LANDSCAPE_CIVIC_FIRST +
            (kCityMapKindSprites[kSmokeKinds[kind]] - CITY_CIVIC_KIND_FIRST) * CITY_CIVIC_VIEWS + view];
        valid = valid && plant->sprite;
        for (int emitter = 0; valid && emitter < count; ++emitter) {
          valid = ReadAssetFile(&file, header, 16) == 16;
          if (!valid) break;
          int x = ReadLE32(header), y = ReadLE32(header + 4);
          int type = ReadLE32(header + 8), flipped = ReadLE32(header + 12);
          valid = x >= 0 && x < plant->sprite->width && y >= 0 && y < plant->sprite->height &&
              type >= 0 && type < CITY_POWER_SMOKE_TYPES && (flipped == 0 || flipped == 1);
          smoke->emitters[emitter] = (CityPowerSmokeEmitter){x, y, type, flipped};
        }
      }
    }
  }
  for (int id = CITY_LANDSCAPE_WIND_MODERN_FIRST; valid && id < CITY_LANDSCAPE_NUCLEAR_STEAM_FIRST; ++id) {
    const CityLandscapeSprite *wind = &sprites[id];
    valid = wind->sprite && wind->sprite->width == 128 && wind->sprite->height <= 2048 &&
        wind->scale == 4 && wind->baseline == wind->sprite->height && wind->offset_x == 0;
  }
  for (int id = CITY_LANDSCAPE_NUCLEAR_STEAM_FIRST; valid && id < CITY_LANDSCAPE_BUILDING_FIRST; ++id) {
    const CityLandscapeSprite *steam = &sprites[id];
    valid = steam->sprite && steam->sprite->width == CITY_POWER_SMOKE_WIDTH &&
        steam->sprite->height == CITY_POWER_SMOKE_WIDTH && steam->baseline == CITY_POWER_STEAM_BASELINE &&
        steam->scale == 4 && steam->offset_x == 0;
  }
  if (valid) {
    valid = ReadBuildingArtwork(&file, sprites, buildings);
    for (int id = CITY_LANDSCAPE_CONSTRUCTION_FIRST; valid && id < CITY_LANDSCAPE_SPRITE_COUNT; ++id)
      valid = sprites[id].sprite != NULL;
    valid = valid && ReadAssetFile(&file, header, 8) == 8 && !memcmp(header, "SC2KGRND", 8) &&
        ReadAssetFile(&file, civic_ground, sizeof(civic_ground)) == sizeof(civic_ground);
    for (int kind = 0; valid && kind < CITY_CIVIC_ART_COUNT; ++kind) {
      int ground = civic_ground[kind] & ~CITY_BUILDING_GROUND_BAKED;
      valid = ground < CITY_BUILDING_GROUND_COUNT && (!ground ||
          sprites[CITY_LANDSCAPE_BUILDING_GROUND_FIRST + ground - 1].sprite != NULL);
    }
    u8 vehicle_colors[CITY_ROAD_VEHICLE_PALETTES * 256 * 4];
    valid = valid && ReadAssetFile(&file, header, 8) == 8 && !memcmp(header, "SC2KCAR1", 8) &&
        ReadAssetFile(&file, vehicle_colors, sizeof(vehicle_colors)) == sizeof(vehicle_colors);
    for (int style = 0; valid && style < CITY_ROAD_VEHICLE_PALETTES; ++style)
      for (int color = 0; valid && color < 256; ++color) {
        u32 value = ReadLE32(vehicle_colors + (style * 256 + color) * 4);
        valid = (value >> 24) == 255;
        assets->road_vehicle_palettes[style][color] = value;
      }
    valid = valid && ReadKindHeightMetadata(&file, assets, sprites);
  }
  valid = valid && ReadAssetByte(&file) == EOF && !file.failed;
  if (!valid) fprintf(stderr, "Invalid landscape sprite data at byte %zu\n", file.position);
  if (!CloseAssetFile(&file)) valid = false;
  if (valid) memcpy(assets->power_smoke, power_smoke, sizeof(power_smoke));
  if (valid) {
    memcpy(assets->buildings, buildings, sizeof(buildings));
    memcpy(assets->civic_ground, civic_ground, sizeof(civic_ground));
  }
  for (int id = 0; id < limit; ++id) {
    if (valid) {
      // Loading uses the high byte to validate populated palette entries above.
      // Clear it once per shared palette so opaque drawing can copy whole words.
      SpritePalette *palette = sprites[id].sprite ? sprites[id].sprite->palette : NULL;
      if (palette && palette->colors[0] >> 24)
        for (int color = 0; color < 256; ++color) palette->colors[color] &= 0xFFFFFF;
    } else if (sprites) FreeRunSprite(sprites[id].sprite);
  }
  if (valid) {
    for (int id = 0; id < assets->landscape_count; ++id) FreeRunSprite(assets->landscape[id].sprite);
    free(assets->landscape);
    assets->landscape = sprites;
    assets->landscape_count = limit;
  } else free(sprites);
  if (valid) {
    memcpy(assets->bridge_palettes, bridge_palettes, sizeof(bridge_palettes));
    for (int biome = 0; biome < 3; ++biome) {
      int first = biome == 0 ? 0 : biome == 1 ? 12 : 20;
      int count = biome == 1 ? 8 : 12;
      CityRect visible = {INT_MAX, INT_MAX, 0, 0};
      for (int species = first; species < first + count; ++species) {
        CityRect tree = tree_bounds[kCityTreeArtLayouts[species * 4][0]];
        if (tree.right <= tree.left) continue;
        visible = UnionCityRects(visible, tree);
      }
      assets->tree_preview_bounds[biome] = visible;
    }
    for (int color = 0; color < 5; ++color)
      assets->landscape_water_colors[color] = ReadLE32(palette_bytes + color * 4);
    ++assets->sprite_revision;
  }
  return valid;
}

static bool LoadCityMapGraphics(CityResourceState *assets) {
  ByteSpan span = GetAsset(assets, CITY_ASSET_TILE_GRAPHICS);
  const u8 *data = span.data;
  bool valid = span.size >= 20 && !memcmp(data, "SC2KTIL1", 8);
  u32 layouts = valid ? ReadLE32(data + 8) : 0;
  u32 first = valid ? ReadLE32(data + 12) : 0;
  u32 count = valid ? ReadLE32(data + 16) : 0;
  valid = valid && layouts > 0 && layouts <= CITY_MAP_SPRITES_PER_ZOOM && first >= CITY_LANDSCAPE_SPRITE_COUNT &&
      first <= CITY_SPRITE_HEADER_MASK && count > 0 && count <= 65536 && first + count <= 65536 && !(first & 3) && !(count & 3) &&
      span.size == 20 + 10 * layouts;
  const u16 *bindings = valid ? (const u16 *)(data + 20) : NULL;
  const u16 (*rotations)[4] = valid ? (const u16 (*)[4])(bindings + layouts) : NULL;
  for (u32 i = 0; valid && i < layouts; ++i) {
    valid = bindings[i] >= first && bindings[i] < first + count && rotations[i][0] == i;
    for (int turn = 0; valid && turn < 4; ++turn) valid = rotations[i][turn] < layouts;
  }
  for (int kind = 0; valid && kind < TK_COUNT; ++kind)
    valid = kCityMapKindSprites[kind] < layouts;
  if (!valid) return false;
  assets->map_sprite_sources = bindings;
  assets->map_sprite_rotations = rotations;
  assets->map_tile_sprite_count = layouts;
  assets->map_group_sprite_first = first;
  assets->map_group_sprite_count = count;
  return true;
}

static void InitializeMapSpriteMetrics(CityResourceState *assets) {
  for (int id = 0; id < CITY_MAP_SPRITE_COUNT; ++id) {
    int zoom = id / CITY_MAP_SPRITES_PER_ZOOM;
    int sprite = id % CITY_MAP_SPRITES_PER_ZOOM;
    int index = sprite < assets->map_tile_sprite_count ? TileSpriteResourceIndex(assets, sprite, 0) : sprite;
    u16 reference = ResolveSpriteVariant(assets->sprite_variants, index, zoom, assets->sprite_index_lookup[index]);
    int header = reference & CITY_SPRITE_HEADER_MASK;
    const CityLandscapeSprite *art = header ? &assets->landscape[header - 1] : NULL;
    int divisor = art ? art->scale << (2 - zoom) : 1;
    assets->map_sprite_metrics[id] = (MapSpriteMetrics){
        .width = art ? (art->sprite->width + divisor - 1) / divisor : 8 << zoom,
        .baseline = art ? (art->baseline + divisor - 1) / divisor : 4 << zoom};
  }
}

bool LoadCityLandscapeAssets(CityResourceState *assets) {
  if (!LoadCityMapGraphics(assets)) {
    fprintf(stderr, "Invalid tile graphics bindings\n");
    return false;
  }
  if (!LoadCityLandscapeSpriteSet(assets)) return false;
  InitializeMapSpriteMetrics(assets);
  static const u8 kColors[CITY_UNDERWORLD_GROUND_STYLES][3] = {
    {113, 115, 106}, {120, 122, 120}, {65, 87, 153}, {116, 103, 84}
  };
  // Indexed artwork stores baked light intensity; material colors are immutable Assets data.
  for (int style = 0; style < CITY_UNDERWORLD_GROUND_STYLES; ++style) {
    assets->underworld_palettes[style][0] = 0;
    for (int index = 1; index < 256; ++index) {
      int red = IntMin(255, (kColors[style][0] * index + 64) / 128);
      int green = IntMin(255, (kColors[style][1] * index + 64) / 128);
      int blue = IntMin(255, (kColors[style][2] * index + 64) / 128);
      assets->underworld_palettes[style][index] = 0xFF000000 | red << 16 | green << 8 | blue;
    }
  }
  return true;
}

void ReleaseCitySpriteAssets(CityResourceState *assets) {
  assets->map_sprite_sources = NULL;
  assets->map_sprite_rotations = NULL;
  assets->sprite_index_lookup = NULL;
  assets->sprite_variants = (SpriteVariants){0};
  assets->map_tile_sprite_count = assets->map_group_sprite_first = 0;
  assets->map_sprite_index_count = assets->map_group_sprite_count = 0;
  memset(assets->buildings, 0, sizeof(assets->buildings));
  memset(assets->power_smoke, 0, sizeof(assets->power_smoke));
  for (int id = 0; id < assets->landscape_count; ++id) FreeRunSprite(assets->landscape[id].sprite);
  free(assets->landscape);
  assets->landscape = NULL;
  assets->landscape_count = 0;
  for (int id = 0; id < CITY_DEMOLITION_FRAME_COUNT; ++id) {
    FreeRunSprite(assets->demolition[id].sprite);
    assets->demolition[id] = (CityLandscapeSprite){0};
  }
  memset(assets->map_sprite_metrics, 0, sizeof(assets->map_sprite_metrics));
  ++assets->sprite_revision;
}
