#include "assets/graphics.h"

#include "city/maps/trees.h"
#include "city/maps/tile_storage.h"
#include "assets/catalog.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/asset_file.h"
#include "assets/assets.h"
#include "base/byte_order.h"
#include "assets/run_sprite.h"

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

// Compact archive commands resolve shared images and references once at load time.
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

// The binding loader has bounded limit to the stored header range. Keep command IDs unsigned until admitted;
// zero is a valid image ID and the global palette sentinel, while sprite references are one-based.
static bool ReadGraphicsCommands(AssetFile *file, CityLandscapeSprite *sprites, u32 limit,
    u8 water[20], u32 bridge[CITY_LANDSCAPE_BRIDGE_STYLES][256]) {
  RunSprite **images = calloc(limit, sizeof(*images));
  SpritePalette **palettes = calloc(limit, sizeof(*palettes));
  bool valid = images && palettes, finished = false, have_water = false, have_bridge = false;
  while (valid && !finished) {
    int command = ReadAssetByte(file);
    switch (command) {
    case GRAPHICS_END: finished = true; break;
    case GRAPHICS_PALETTE: {
      u32 id;
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
    }
    case GRAPHICS_IMAGE: {
      u32 id, width, height, palette, size;
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
    }
    case GRAPHICS_SPRITE: {
      u32 id, image, scale, baseline, offset, palette;
      valid = ReadGraphicsInteger(file, &id) && id > 0 && id <= limit && !sprites[id - 1].sprite &&
          ReadGraphicsInteger(file, &image) && image < limit && images[image] &&
          ReadGraphicsInteger(file, &scale) && scale > 0 && scale <= 32 &&
          ReadGraphicsInteger(file, &baseline) && baseline <= (u32)images[image]->height &&
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
    }
    case GRAPHICS_WATER_COLORS:
      valid = !have_water && ReadAssetFile(file, water, 20) == 20;
      have_water = valid;
      break;
    case GRAPHICS_BRIDGE_PALETTES: {
      u32 palette;
      valid = !have_bridge;
      for (int style = 0; valid && style < CITY_LANDSCAPE_BRIDGE_STYLES; ++style) {
        valid = ReadGraphicsInteger(file, &palette) && palette > 0 && palette < limit && palettes[palette];
        if (valid) memcpy(bridge[style], palettes[palette]->colors, 256 * sizeof(u32));
      }
      have_bridge = valid;
      break;
    }
    default: valid = false; break;
    }
  }
  for (u32 i = 0; i < limit; ++i) {
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
    const u8 *lookup, int count) {
  for (int id = 0; id < limit; ++id)
    if (sprites[id].sprite && (ReadLE16(lookup + 2 * id) & CITY_SPRITE_HEADER_MASK) != id + 1) return false;
  for (int id = 0; id < count; ++id) {
    u16 reference = ReadLE16(lookup + 2 * id);
    int header = reference & CITY_SPRITE_HEADER_MASK;
    if (header ? header > limit || !sprites[header - 1].sprite : reference != 0) return false;
  }
  for (int id = 0; id < limit; ++id) {
    u16 reference = ReadLE16(lookup + 2 * id);
    int header = reference & CITY_SPRITE_HEADER_MASK;
    if (!header || header == id + 1) continue;
    sprites[id] = sprites[header - 1];
    const RunSprite *source = sprites[id].sprite;
    sprites[id].sprite = reference & CITY_SPRITE_HEADER_FLIPPED ?
        ReferenceRunSprite(source, source->palette, true) : RetainRunSprite(source);
    if (!sprites[id].sprite) return false;
  }
  for (int id = 0; id < limit; ++id) {
    u16 reference = ReadLE16(lookup + 2 * id);
    if ((reference & CITY_SPRITE_HEADER_MASK) != id + 1 || !(reference & CITY_SPRITE_HEADER_FLIPPED)) continue;
    RunSprite *source = sprites[id].sprite;
    sprites[id].sprite = ReferenceRunSprite(source, source->palette, true);
    FreeRunSprite(source);
    if (!sprites[id].sprite) return false;
  }
  return true;
}

// count is read directly from the file. Validate it before sizing the borrowed lookup; never narrow wire counts.
static bool ReadGraphicsSpriteIndex(AssetFile *file, CityResourceState *assets, u32 count) {
  if (count < CITY_LANDSCAPE_SPRITE_COUNT || count > 65536 ||
      count != assets->map_group_sprite_first + assets->map_group_sprite_count) return false;
  ByteSpan lookup = ReadAssetSpan(file, count * sizeof(u16));
  if (!lookup.data) return false;
  assets->sprite_index_lookup = lookup.data;
  assets->map_sprite_index_count = count;
  return true;
}

// Bindings and every referenced image group have already passed catalog validation. This chunk supplies native
// per-kind heights; it does not repeat image lookup, choose a camera direction or access renderer state.
static bool ReadKindHeightMetadata(AssetFile *file, CityResourceState *assets, const u16 kind_sprites[]) {
  u8 header[8];
  if (ReadAssetFile(file, header, sizeof(header)) != sizeof(header) || memcmp(header, "SC2KHGT1", 8) ||
      ReadAssetFile(file, assets->kind_sprite_heights, sizeof(assets->kind_sprite_heights)) !=
          sizeof(assets->kind_sprite_heights)) return false;
  for (int kind = 0; kind < TK_COUNT; ++kind) {
    bool building = CityBuildingArtworkSourceFirst(kind_sprites[kind]) >= 0;
    int height = assets->kind_sprite_heights[kind];
    if (building ? height < 4 : height != 0) return false;
  }
  return true;
}

static bool ReadGraphicsVariants(AssetFile *file, CityResourceState *assets) {
  u8 header[4];
  if (ReadAssetFile(file, header, sizeof(header)) != sizeof(header)) return false;
  u32 size = ReadLE32(header);
  if (size > assets->map_sprite_index_count * 12) return false;
  ByteSpan bytes = ReadAssetSpan(file, size);
  if (!bytes.data) return false;
  SpriteVariants variants = {bytes.data, size};
  if (!ValidateSpriteVariants(variants, assets->map_sprite_index_count, assets->map_group_sprite_first)) return false;
  assets->sprite_variants = variants;
  return true;
}

static bool ValidateLandscapeSprites(CityLandscapeSprite *sprites,
    CityRect tree_bounds[CITY_LANDSCAPE_TREE_ART_COUNT], int count) {
  bool valid = true;
  for (int id = 0; valid && id < count; ++id) {
    const RunSprite *sprite = sprites[id].sprite;
    if (!sprite) continue;
    int width = sprite->width, height = sprite->height;
    const u32 *rows = RunSpriteRows(sprite);
    valid = ValidateRunSprite(sprite);
    const u8 *data = RunSpriteData(sprite);
    bool tree_preview = id >= CITY_LANDSCAPE_TREE_FIRST && id < CITY_LANDSCAPE_POINT &&
        (id - CITY_LANDSCAPE_TREE_FIRST) % CITY_LANDSCAPE_TREE_STAGES == CITY_TREE_GROWN;
    // Catalog families allow different indexed colors and animation tags; the policy is fixed for this image.
    bool point = id >= CITY_LANDSCAPE_POINT && id < CITY_LANDSCAPE_RAPIDS_FIRST;
    bool selector = (id >= CITY_LANDSCAPE_SELECTION_FIRST && id < CITY_LANDSCAPE_SELECTION_END) ||
        (id >= CITY_LANDSCAPE_UNDERWORLD_SELECTION_FIRST && id < CITY_LANDSCAPE_HIGHWAY_MERGE_FIRST) ||
        point || (id >= CITY_LANDSCAPE_BAKED_SELECTION_FIRST && id < CITY_LANDSCAPE_BAKED_FACE_FIRST);
    bool infrastructure = (id >= CITY_MAP_TILE_SPRITE_POWER_LINE_FIRST &&
        id <= CITY_MAP_TILE_SPRITE_HIGHWAY_LAST) ||
        (id >= CITY_LANDSCAPE_ROAD_BRIDGE_FIRST && id < CITY_LANDSCAPE_CIVIC_FIRST);
    bool civic = id >= CITY_LANDSCAPE_CIVIC_FIRST;
    bool underworld_ground = (id >= CITY_LANDSCAPE_BAKED_UNDERWORLD_FIRST &&
        id < CITY_LANDSCAPE_BAKED_PIPE_FIRST) || id == CITY_LANDSCAPE_BAKED_FACE_FIRST;
    CityRect visible = {width, height, 0, 0};
    for (int y = 0; valid && y < height; ++y) {
      const u8 *at = data + rows[y], *end = data + rows[y + 1];
      int x = at < end ? ReadSpriteInitialSkip(&at) : 0;
      while (valid && at < end) {
        int kind, pixels = ReadSpriteRun(&at, &kind), bytes = SpriteRunPayloadBytes(kind);
        if (SpriteRunIndexed(kind)) {
          for (int pixel = 0; pixel < pixels; ++pixel) {
            int index = at[pixel * bytes];
            if (sprite->palette && !SpriteRunAnimated(kind) && sprite->palette->colors[index] >> 24 != 255)
              valid = false;
            bool allowed = civic ? !SpriteRunAnimated(kind) || (index > 0 && index < SPRITE_ANIMATION_COLORS) :
                infrastructure || underworld_ground || (selector ?
                (index >= 1 && index <= 15) || index == 23 || (point && index == 0xA9) :
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
  AssetOpenResult opened = OpenAssetSpan(&file, GetAsset(assets, CITY_ASSET_DEMOLITION));
  if (opened != ASSET_OPEN_OK) return false;
  u8 header[28], water[20];
  bool valid = ReadAssetFile(&file, header, 12) == 12 && !memcmp(header, "SC2KLAN2", 8);
  u32 count = valid ? ReadLE32(header + 8) : 0;
  valid = valid && count > 0 && count <= CITY_DEMOLITION_FRAME_COUNT &&
      ReadAssetFile(&file, water, sizeof(water)) == sizeof(water);
  CityLandscapeSprite sprites[CITY_DEMOLITION_FRAME_COUNT] = {0};
  for (u32 item = 0; valid && item < count; ++item) {
    valid = ReadAssetFile(&file, header, sizeof(header)) == sizeof(header);
    if (!valid) break;
    u32 id = ReadLE32(header), width = ReadLE32(header + 4), height = ReadLE32(header + 8);
    u32 baseline = ReadLE32(header + 12), scale = ReadLE32(header + 16), size = ReadLE32(header + 24);
    int offset_x = (i32)ReadLE32(header + 20);
    valid = id < CITY_DEMOLITION_FRAME_COUNT && !sprites[id].sprite && width > 0 && width <= 512 &&
        height > 0 && height <= 1024 && baseline <= height && scale > 0 && scale <= 32 &&
        offset_x >= -(int)width && offset_x <= (int)width;
    if (!valid) break;
    RunSprite *sprite = ReadAssetRunSprite(&file, width, height, size);
    if (!sprite) { valid = false; break; }
    sprites[id] = (CityLandscapeSprite){sprite, scale, baseline, offset_x};
    valid = RunSpriteHasOnlyKinds(sprite, SPRITE_RUN_DIRECT_KINDS);
  }
  for (int id = 0; valid && id < CITY_DEMOLITION_FRAME_COUNT; ++id) valid = sprites[id].sprite != NULL;
  valid = valid && ReadAssetByte(&file) == EOF && !file.failed;
  if (!CloseAssetFile(&file)) valid = false;
  for (int id = 0; id < CITY_DEMOLITION_FRAME_COUNT; ++id) {
    if (valid) {
      FreeRunSprite(assets->demolition[id].sprite);
      assets->demolition[id] = sprites[id];
    } else FreeRunSprite(sprites[id].sprite);
  }
  return valid;
}

// Decode unsigned emitter positions against plant images with validated positive dimensions. The caller supplies
// temporary metadata; failure leaves its partial writes in place and the cursor open at the consumed position.
static bool ReadPowerSmokeArtwork(AssetFile *file, const CityLandscapeSprite *sprites,
    CityPowerSmoke power_smoke[CITY_POWER_SMOKE_KINDS][CITY_CIVIC_VIEWS]) {
  u8 header[16];
  bool valid = ReadAssetFile(file, header, 8) == 8 && !memcmp(header, "SC2KSMK1", 8);
  static const u8 kPlantSprites[CITY_POWER_SMOKE_KINDS] = {
    [CITY_POWER_SMOKE_OIL] = CITY_MAP_TILE_SPRITE_OIL_POWER_PLANT,
    [CITY_POWER_SMOKE_NUCLEAR] = CITY_MAP_TILE_SPRITE_NUCLEAR_POWER_PLANT,
    [CITY_POWER_SMOKE_COAL] = CITY_MAP_TILE_SPRITE_COAL_POWER_PLANT,
  };
  for (int profile = 0; valid && profile < CITY_POWER_SMOKE_KINDS; ++profile) {
    for (int view = 0; valid && view < CITY_CIVIC_VIEWS; ++view) {
      valid = ReadAssetFile(file, header, 4) == 4;
      u32 count = valid ? ReadLE32(header) : 0;
      valid = valid && count > 0 && count <= CITY_POWER_SMOKE_EMITTERS;
      CityPowerSmoke *smoke = &power_smoke[profile][view];
      smoke->count = count;
      const CityLandscapeSprite *plant = &sprites[CITY_LANDSCAPE_CIVIC_FIRST +
          (kPlantSprites[profile] - CITY_CIVIC_KIND_FIRST) * CITY_CIVIC_VIEWS + view];
      valid = valid && plant->sprite;
      for (u32 emitter = 0; valid && emitter < count; ++emitter) {
        valid = ReadAssetFile(file, header, 16) == 16;
        if (!valid) break;
        u32 x = ReadLE32(header), y = ReadLE32(header + 4);
        u32 type = ReadLE32(header + 8), flipped = ReadLE32(header + 12);
        valid = x < (u32)plant->sprite->width && y < (u32)plant->sprite->height &&
            type < CITY_POWER_SMOKE_TYPES && flipped <= 1;
        smoke->emitters[emitter] = (CityPowerSmokeEmitter){x, y, type, flipped};
      }
    }
  }
  return valid;
}

// Decode vehicle palettes directly into Assets. Preserve earlier colors, including the rejected color, on failure;
// the cursor stays open at the consumed position.
static bool ReadRoadVehiclePalettes(AssetFile *file, u32 palettes[CITY_ROAD_VEHICLE_PALETTES][256]) {
  u8 header[8], colors[CITY_ROAD_VEHICLE_PALETTES * 256 * 4];
  bool valid = ReadAssetFile(file, header, sizeof(header)) == sizeof(header) && !memcmp(header, "SC2KCAR1", 8) &&
      ReadAssetFile(file, colors, sizeof(colors)) == sizeof(colors);
  for (int style = 0; valid && style < CITY_ROAD_VEHICLE_PALETTES; ++style)
    for (int color = 0; valid && color < 256; ++color) {
      u32 value = ReadLE32(colors + (style * 256 + color) * 4);
      valid = (value >> 24) == 255;
      palettes[style][color] = value;
    }
  return valid;
}

static bool LoadCityLandscapeSpriteSet(CityResourceState *assets, const u16 kind_sprites[]) {
  int limit = assets->map_group_sprite_first;
  AssetFile file;
  AssetOpenResult opened = OpenAssetSpan(&file, GetAsset(assets, CITY_ASSET_LANDSCAPE));
  if (opened != ASSET_OPEN_OK) return false;
  u8 header[12], palette_bytes[20];
  bool valid = ReadAssetFile(&file, header, 12) == 12 &&
      !memcmp(header, "SC2KGFX3", 8);
  if (valid) valid = ReadGraphicsSpriteIndex(&file, assets, ReadLE32(header + 8));
  if (valid) valid = ReadGraphicsVariants(&file, assets);
  valid = valid && limit > 0;
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
  // Header resolution proved every nonempty reference. Require the entire bound group bank before metadata reads.
  for (u32 id = assets->map_group_sprite_first; valid && id < assets->map_sprite_index_count; ++id)
    valid = (ReadLE16(assets->sprite_index_lookup + 2 * id) & CITY_SPRITE_HEADER_MASK) != 0;
  if (valid) valid = ValidateLandscapeSprites(sprites, tree_bounds, limit);
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
  if (valid) valid = ReadPowerSmokeArtwork(&file, sprites, power_smoke);
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
    if (valid) valid = ReadRoadVehiclePalettes(&file, assets->road_vehicle_palettes);
    valid = valid && ReadKindHeightMetadata(&file, assets, kind_sprites);
  }
  valid = valid && ReadAssetByte(&file) == EOF && !file.failed;
  if (!valid) fprintf(stderr, "Invalid landscape sprite data at byte %llu\n", (unsigned long long)file.position);
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

static bool LoadCityMapGraphics(CityResourceState *assets, const u16 kind_sprites[]) {
  ByteSpan span = GetAsset(assets, CITY_ASSET_TILE_GRAPHICS);
  const u8 *data = span.data;
  bool valid = span.size >= 20 && !memcmp(data, "SC2KTIL1", 8);
  u32 layouts = valid ? ReadLE32(data + 8) : 0;
  u32 first = valid ? ReadLE32(data + 12) : 0;
  u32 count = valid ? ReadLE32(data + 16) : 0;
  valid = valid && layouts > 0 && layouts <= CITY_MAP_SPRITES_PER_ZOOM && first >= CITY_LANDSCAPE_SPRITE_COUNT &&
      first <= CITY_SPRITE_HEADER_MASK && count > 0 && count <= 65536 && first + count <= 65536 && !(first & 3) && !(count & 3) &&
      span.size == 20 + 10 * layouts;
  const u8 *bindings = valid ? data + 20 : NULL;
  const u8 *rotations = valid ? bindings + 2 * layouts : NULL;
  for (u32 i = 0; valid && i < layouts; ++i) {
    u32 binding = ReadLE16(bindings + 2 * i);
    valid = binding >= first && binding < first + count && ReadLE16(rotations + 8 * i) == i;
    for (int turn = 0; valid && turn < 4; ++turn) valid = ReadLE16(rotations + 2 * (4 * i + turn)) < layouts;
  }
  for (int kind = 0; valid && kind < TK_COUNT; ++kind)
    valid = kind_sprites[kind] < layouts;
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
    int index = sprite < assets->map_tile_sprite_count ?
        ReadLE16(assets->map_sprite_sources + 2 * sprite) : sprite;
    u16 fallback = ReadLE16(assets->sprite_index_lookup + 2 * index);
    u16 reference = ResolveSpriteVariant(assets->sprite_variants, index, zoom, fallback);
    int header = reference & CITY_SPRITE_HEADER_MASK;
    const CityLandscapeSprite *art = header ? &assets->landscape[header - 1] : NULL;
    int divisor = art ? art->scale << (2 - zoom) : 1;
    assets->map_sprite_metrics[id] = (MapSpriteMetrics){
        .width = art ? (art->sprite->width + divisor - 1) / divisor : 8 << zoom,
        .baseline = art ? (art->baseline + divisor - 1) / divisor : 4 << zoom};
  }
}

bool LoadCityLandscapeAssets(CityResourceState *assets, const u16 kind_sprites[]) {
  if (!LoadCityMapGraphics(assets, kind_sprites)) {
    fprintf(stderr, "Invalid tile graphics bindings\n");
    return false;
  }
  if (!LoadCityLandscapeSpriteSet(assets, kind_sprites)) return false;
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
