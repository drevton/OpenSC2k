#ifndef SC2K_DOMAIN_SRC_DRAWING_SPRITES_SPRITES_H
#define SC2K_DOMAIN_SRC_DRAWING_SPRITES_SPRITES_H

#include <stddef.h>
#include <stdatomic.h>

#include "base/types.h"

// Run Sprite

// A nonempty row starts with an initial skip (255 escapes to a full LE u16).
// Run headers have three mode bits and five length bits. Lengths 1..31 are
// inline; 31 escapes to 32+u8, with extension 255 escaping to a full LE u16.
// Empty rows have equal offsets; trailing transparent pixels are omitted.
enum { SPRITE_RUN_LIMIT = 65535 };
enum { SPRITE_WATER_COLORS = 5, SPRITE_WATER_GLITTER_COLORS = 5,
  SPRITE_WATER_PALETTE_COLORS = SPRITE_WATER_COLORS + SPRITE_WATER_GLITTER_COLORS };
enum { SPRITE_ANIMATION_FAST_FIRST = 1, SPRITE_ANIMATION_SLOW_FIRST = 50,
  SPRITE_ANIMATION_WATER_FIRST = 66, SPRITE_ANIMATION_COLORS = 76 };
typedef enum SpriteRunKind {
  SPRITE_RUN_TRANSPARENT, SPRITE_RUN_RGB, SPRITE_RUN_RGBA, SPRITE_RUN_PALETTE,
  SPRITE_RUN_PALETTE_ALPHA, SPRITE_RUN_ANIMATED, SPRITE_RUN_ANIMATED_ALPHA, SPRITE_RUN_RESERVED
} SpriteRunKind;
// Allowed encoded run kinds for fixed-color artwork and static images with an image-local palette.
enum {
  SPRITE_RUN_DIRECT_KINDS = (1 << SPRITE_RUN_TRANSPARENT) | (1 << SPRITE_RUN_RGB) | (1 << SPRITE_RUN_RGBA),
  SPRITE_RUN_STATIC_KINDS = SPRITE_RUN_DIRECT_KINDS | (1 << SPRITE_RUN_PALETTE) | (1 << SPRITE_RUN_PALETTE_ALPHA),
};

static inline int SpriteRunPayloadBytes(int kind) {
  const u8 bytes[] = {0, 3, 4, 1, 2, 1, 2, 0};
  return bytes[kind];
}
static inline bool SpriteRunIndexed(int kind) { return kind >= SPRITE_RUN_PALETTE && kind < SPRITE_RUN_RESERVED; }
static inline bool SpriteRunAnimated(int kind) {
  return kind == SPRITE_RUN_ANIMATED || kind == SPRITE_RUN_ANIMATED_ALPHA;
}
static inline bool SpriteRunHasAlpha(int kind) {
  return kind == SPRITE_RUN_RGBA || kind == SPRITE_RUN_PALETTE_ALPHA || kind == SPRITE_RUN_ANIMATED_ALPHA;
}
// Source palette indices and retained animation IDs are different namespaces.
static inline int SpriteAnimationId(int index) {
  if (index >= 171 && index <= 219) return index - 170;
  if (index >= 224 && index <= 239) return index - 174;
  return 0;
}
static inline int SpriteAnimationSource(int id) {
  if (id >= 1 && id < 50) return id + 170;
  if (id >= 50 && id < 66) return id + 174;
  return 0;
}
// Trusted readers: external streams are checked by ValidateRunSprite first.
static inline int ReadSpriteInitialSkip(const u8 **source) {
  int skip = *(*source)++;
  if (skip == 255) { skip = (*source)[0] | (*source)[1] << 8; *source += 2; }
  return skip;
}
static inline int ReadSpriteRun(const u8 **source, int *kind) {
  int header = *(*source)++, length = (header & 31) + 1;
  *kind = header >> 5;
  if (length == 32) {
    int extension = *(*source)++;
    length = extension + 32;
    if (extension == 255) { length = (*source)[0] | (*source)[1] << 8; *source += 2; }
  }
  return length;
}
typedef enum SpriteDrawMode {
  SPRITE_COPY, SPRITE_FIXED_PALETTE, SPRITE_OPAQUE_PALETTE
} SpriteDrawMode;
// Mode occupies the low three bits; zero transparency preserves opaque fast paths.
enum { SPRITE_MODE_MASK = 7, SPRITE_FLIPPED = 8, SPRITE_DISABLED = 16, SPRITE_TRANSPARENCY_SHIFT = 24 };

typedef struct {
  atomic_uint references;
  u32 colors[256]; // Loaded image colors are framebuffer-ready 0x00RRGGBB; alpha belongs to runs.
} SpritePalette;

typedef struct RunSprite {
  int width, height;
  u32 stream_size;
  atomic_uint references;
  const struct RunSprite *image; // References share immutable rows; never chains.
  SpritePalette *palette; // Shared image palette; alpha remains in the pixel stream.
  bool mirrored;
  // Owned image: this record, height+1 row offsets, then packed runs.
} RunSprite;

void FreeRunSprite(RunSprite *sprite);
SpritePalette *RetainSpritePalette(SpritePalette *palette);
void FreeSpritePalette(SpritePalette *palette);
RunSprite *RetainRunSprite(const RunSprite *sprite);
RunSprite *ReferenceRunSprite(const RunSprite *sprite, SpritePalette *palette, bool mirrored);
const u32 *RunSpriteRows(const RunSprite *sprite);
const u8 *RunSpriteData(const RunSprite *sprite);
bool ValidateRunSprite(const RunSprite *sprite);
// Query a structurally validated image's encoded run kinds, one bit per SpriteRunKind; no allocation or mutation.
// Empty rows and implicit transparent skips have no encoded run kind.
// Palette ownership and index policy stay with callers.
bool RunSpriteHasOnlyKinds(const RunSprite *sprite, u32 allowed_kinds);

#endif
