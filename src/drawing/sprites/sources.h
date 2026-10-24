#ifndef SC2K_DRAWING_SPRITES_SOURCES_H
#define SC2K_DRAWING_SPRITES_SOURCES_H

#include "assets/graphics.h"

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
// Cache preparation translates its public slots to opaque resource indexes.
int TileSpriteResourceIndex(const CityResourceState *assets, int sprite, int direction);
int LandscapeSpriteResourceIndex(const CityResourceState *assets, int source);
const CityLandscapeSprite *GetTileSpriteSource(const CityResourceState *assets, int sprite, int direction);

#endif
