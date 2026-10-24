#ifndef SC2K_DRAWING_SPRITES_SURFACE_H
#define SC2K_DRAWING_SPRITES_SURFACE_H

#include "drawing/canvas.h"
#include "drawing/sprites/sprites.h"

// Writable raster descriptor with borrowed palette and animation planes.
// This descriptor owns none of its backing storage or referenced image data.
typedef struct SpriteSurface {
  Canvas canvas;
  u8 *indices; // Optional animation identity layer, same pixel dimensions as canvas; zero is fixed RGB.
  int index_stride;
  const u32 *palette; // 256 colors; fixed-palette drawing uses full 0xAARRGGBB including alpha.
  u32 *row_flags; // Bit n hints that this row contains a nonzero palette index with (index & 31) == n.
  const u32 *animation_palette; // Compact animation IDs; entry zero is unused.
  const u8 *remap; // Optional source-palette translation for fixed-palette overlays; water has its own namespace.
  struct ScreenDamage *upload_damage; // Optional final-framebuffer changes, retained until presentation.
  bool image_palette; // Static image colours must not retain global palette/selection identity.
} SpriteSurface;

// A null palette uses the image palette for normal copies, otherwise the surface palette.
void DrawRunSprite(SpriteSurface *surface, const RunSprite *sprite, int x, int y, u32 flags, const u32 *palette);
void FillSpriteSurface(SpriteSurface *surface, CityRect rect, u8 index);
bool AnimateSpriteSurface(SpriteSurface *surface, int first, int count);
bool AnimateSpriteSurfaceChanges(SpriteSurface *surface, u32 changed_buckets);
bool AnimateSpriteSurfaceWater(SpriteSurface *surface, u32 changed_buckets);

#endif
