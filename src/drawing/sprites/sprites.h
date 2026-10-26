#ifndef SC2K_DRAWING_SPRITES_SPRITES_H
#define SC2K_DRAWING_SPRITES_SPRITES_H

#include "assets/run_sprite.h"

typedef struct SpriteSurface SpriteSurface;

typedef enum SpriteDrawMode {
  SPRITE_COPY, SPRITE_FIXED_PALETTE, SPRITE_OPAQUE_PALETTE
} SpriteDrawMode;
// Mode occupies the low three bits; zero transparency preserves opaque fast paths.
enum { SPRITE_MODE_MASK = 7, SPRITE_FLIPPED = 8, SPRITE_DISABLED = 16, SPRITE_TRANSPARENCY_SHIFT = 24 };

// A null palette uses the image palette for normal copies, otherwise the surface palette.
void DrawRunSprite(SpriteSurface *surface, const RunSprite *sprite, int x, int y, u32 flags, const u32 *palette);

#endif
