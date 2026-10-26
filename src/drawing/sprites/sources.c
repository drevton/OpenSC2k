#include "drawing/sprites/sources.h"

#include "assets/state.h"
#include "base/byte_order.h"

// Immutable interface variants and map catalog bindings. Source lookup does not touch Drawing state.

int InterfaceSpriteLevel(int scale) {
  return scale <= 100 ? 0 : scale <= 200 ? 1 : 2;
}

InterfaceSpriteSource GetInterfaceSprite(const CityInterfaceIcons *icons, int sprite, int level) {
  u16 reference = ResolveSpriteVariant(icons->variants, sprite, level, ReadLE16(icons->lookup + (size_t)sprite * 2));
  return (InterfaceSpriteSource){.image = icons->images[(reference & CITY_SPRITE_HEADER_MASK) - 1],
      .flipped = (reference & CITY_SPRITE_HEADER_FLIPPED) != 0};
}

int TileSpriteResourceIndex(const CityResourceState *assets, int sprite, int direction) {
  int index = ReadLE16(assets->map_sprite_sources + (size_t)sprite * 2);
  return (index & ~3) | ((index + direction) & 3);
}

const CityLandscapeSprite *GetTileSpriteSource(const CityResourceState *assets, int sprite, int direction) {
  int index = sprite < assets->map_tile_sprite_count ? TileSpriteResourceIndex(assets, sprite, direction) : sprite;
  return GetResourceSprite(assets, index);
}

// Internal zoom-cache slots stay stable. Their images come from ordinary sprite references.
int LandscapeSpriteResourceIndex(const CityResourceState *assets, int source) {
  int sprite = -1, direction = 0;
  if (source >= CITY_LANDSCAPE_CIVIC_FIRST && source < CITY_LANDSCAPE_POWER_SMOKE_FIRST) {
    int slot = (source - CITY_LANDSCAPE_CIVIC_FIRST) / 4;
    sprite = slot == CITY_CIVIC_KIND_COUNT ? CITY_MAP_TILE_SPRITE_PARK : CITY_CIVIC_KIND_FIRST + slot;
    direction = (source - CITY_LANDSCAPE_CIVIC_FIRST) & 3;
  } else if (source >= CITY_LANDSCAPE_BUILDING_FIRST && source < CITY_LANDSCAPE_CONSTRUCTION_FIRST) {
    sprite = CITY_BUILDING_ART_FIRST + (source - CITY_LANDSCAPE_BUILDING_FIRST) / 4;
    direction = (source - CITY_LANDSCAPE_BUILDING_FIRST) & 3;
  }
  return sprite < 0 ? source : TileSpriteResourceIndex(assets, sprite, direction);
}

// Map code passes an opaque resource index. Header aliases and artwork flips are
// interpreted only here and by the sprite-pack loader, never by tile selection.
const CityLandscapeSprite *GetResourceSprite(const CityResourceState *assets, int index) {
  int header = ReadLE16(assets->sprite_index_lookup + (size_t)index * 2) & CITY_SPRITE_HEADER_MASK;
  return header ? &assets->landscape[header - 1] : NULL;
}

LandscapeSpriteSource GetResourceSpriteVariant(const CityResourceState *assets, int index, int level,
    bool base_fallback) {
  u16 fallback = base_fallback ? ReadLE16(assets->sprite_index_lookup + (size_t)index * 2) : 0;
  u16 reference = ResolveSpriteVariant(assets->sprite_variants, index, level, fallback);
  if (!reference) return (LandscapeSpriteSource){0};
  int header = (reference & CITY_SPRITE_HEADER_MASK) - 1;
  return (LandscapeSpriteSource){.artwork = &assets->landscape[header],
      .flipped = ((reference ^ ReadLE16(assets->sprite_index_lookup + (size_t)header * 2)) & CITY_SPRITE_HEADER_FLIPPED) != 0};
}
