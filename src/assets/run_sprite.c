#include "assets/run_sprite.h"

#include <stdlib.h>

bool ValidateRunSprite(const RunSprite *sprite) {
  if (sprite->width <= 0 || sprite->height <= 0) return false;
  const u32 *rows = RunSpriteRows(sprite);
  const u8 *data = RunSpriteData(sprite);
  if (rows[0] || rows[sprite->height] != sprite->stream_size) return false;
  for (int y = 0; y < sprite->height; ++y) {
    if (rows[y] > rows[y + 1] || rows[y + 1] > sprite->stream_size) return false;
    const u8 *at = data + rows[y], *end = data + rows[y + 1];
    if (at == end) continue;
    if (*at == 255 && end - at < 3) return false;
    int x = ReadSpriteInitialSkip(&at);
    if (x >= sprite->width || at == end) return false;
    bool first = true;
    while (at < end) {
      int header = *at++, kind = header >> 5, length = (header & 31) + 1;
      if (kind == SPRITE_RUN_RESERVED || (first && kind == SPRITE_RUN_TRANSPARENT)) return false;
      first = false;
      if (length == 32) {
        if (at == end) return false;
        int extension = *at++;
        length = 32 + extension;
        if (extension == 255) {
          if (end - at < 2) return false;
          length = at[0] | at[1] << 8;
          at += 2;
          if (length < 287) return false;
        }
      }
      int bytes = SpriteRunPayloadBytes(kind);
      if (length > sprite->width - x || (size_t)(end - at) < (size_t)length * bytes) return false;
      if (SpriteRunAnimated(kind))
        for (int i = 0; i < length; ++i)
          if (!at[i * bytes] || at[i * bytes] >= SPRITE_ANIMATION_COLORS) return false;
      at += length * bytes;
      x += length;
      if (at == end && kind == SPRITE_RUN_TRANSPARENT) return false;
    }
  }
  return true;
}

bool RunSpriteHasOnlyKinds(const RunSprite *sprite, u32 allowed_kinds) {
  const u32 *rows = RunSpriteRows(sprite);
  const u8 *data = RunSpriteData(sprite);
  for (int y = 0; y < sprite->height; ++y) {
    const u8 *at = data + rows[y], *end = data + rows[y + 1];
    if (at < end) ReadSpriteInitialSkip(&at);
    while (at < end) {
      int kind, length = ReadSpriteRun(&at, &kind);
      if (!(allowed_kinds & (1 << kind))) return false;
      at += length * SpriteRunPayloadBytes(kind);
    }
  }
  return true;
}

SpritePalette *RetainSpritePalette(SpritePalette *palette) {
  if (palette) atomic_fetch_add_explicit(&palette->references, 1, memory_order_relaxed);
  return palette;
}

void FreeSpritePalette(SpritePalette *palette) {
  if (palette && atomic_fetch_sub_explicit(&palette->references, 1, memory_order_acq_rel) == 1) free(palette);
}

RunSprite *RetainRunSprite(const RunSprite *sprite) {
  RunSprite *owned = (RunSprite *)sprite;
  if (owned) atomic_fetch_add_explicit(&owned->references, 1, memory_order_relaxed);
  return owned;
}

RunSprite *ReferenceRunSprite(const RunSprite *sprite, SpritePalette *palette, bool mirrored) {
  if (!mirrored && palette == sprite->palette) return RetainRunSprite(sprite);
  RunSprite *reference = malloc(sizeof(*reference));
  if (!reference) return NULL;
  *reference = (RunSprite){.width = sprite->width, .height = sprite->height,
      .stream_size = sprite->stream_size, .references = 1,
      .image = RetainRunSprite(sprite->image ? sprite->image : sprite),
      .palette = palette, .mirrored = sprite->mirrored ^ mirrored};
  RetainSpritePalette(reference->palette);
  return reference;
}

void FreeRunSprite(RunSprite *sprite) {
  if (!sprite || atomic_fetch_sub_explicit(&sprite->references, 1, memory_order_acq_rel) != 1) return;
  FreeRunSprite((RunSprite *)sprite->image);
  FreeSpritePalette(sprite->palette);
  free(sprite);
}

