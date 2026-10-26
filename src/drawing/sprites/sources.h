#ifndef SC2K_DRAWING_SPRITES_SOURCES_H
#define SC2K_DRAWING_SPRITES_SOURCES_H

#include "base/types.h"

typedef struct RunSprite RunSprite;
typedef struct CityInterfaceIcons CityInterfaceIcons;
typedef struct CityLandscapeSprite CityLandscapeSprite;
typedef struct CityResourceState CityResourceState;

// These queries borrow immutable loaded artwork. IDs must be valid for the installed catalog;
// they do not allocate, prepare images, change caches or perform I/O. Map cache slots and resource IDs differ.
// UI detail is selected from a positive final scale percentage, independently of map zoom.
int InterfaceSpriteLevel(int scale);
// One borrowed selection. The image remains valid until its atlas is replaced or released;
// flipped requests an additional mirror relative to that image. No reference is retained by lookup.
typedef struct {
  const RunSprite *image;
  bool flipped;
} InterfaceSpriteSource;
// Valid icon/detail lookup returns its image and mirror selection together without changing caller state.
InterfaceSpriteSource GetInterfaceSprite(const CityInterfaceIcons *icons, int sprite, int level);
// Borrow base metadata, or NULL for an empty reference. Alias mirroring is applied during cache preparation.
const CityLandscapeSprite *GetResourceSprite(const CityResourceState *assets, int index);
// One borrowed landscape variant and its mirror relative to artwork->sprite. Metadata/images remain valid until
// Assets replaces or releases the catalog. No image reference is retained by lookup.
typedef struct {
  const CityLandscapeSprite *artwork;
  bool flipped;
} LandscapeSpriteSource;
// Resolve a valid resource/detail selection. With base_fallback, a missing variant uses the base reference;
// otherwise it returns an empty selection. Packed references and relative mirroring stay inside source lookup.
LandscapeSpriteSource GetResourceSpriteVariant(const CityResourceState *assets, int index, int level,
    bool base_fallback);
// Cache preparation translates its public slots to opaque resource indexes.
int TileSpriteResourceIndex(const CityResourceState *assets, int sprite, int direction);
int LandscapeSpriteResourceIndex(const CityResourceState *assets, int source);
const CityLandscapeSprite *GetTileSpriteSource(const CityResourceState *assets, int sprite, int direction);

#endif
