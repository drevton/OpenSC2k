#include "drawing/sprites/surface.h"

#include <stdlib.h>
#include <string.h>

#if defined(__aarch64__) && defined(__ARM_NEON) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#include <arm_neon.h>
#endif

#include "drawing/raster/screen_damage.h"

// Run validation, shared reference lifetime, raster drawing and palette animation.

// Run Sprite

const u32 *RunSpriteRows(const RunSprite *sprite) {
  return (const u32 *)((sprite->image ? sprite->image : sprite) + 1);
}
const u8 *RunSpriteData(const RunSprite *sprite) {
  return (const u8 *)(RunSpriteRows(sprite) + sprite->height + 1);
}

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

static u32 SurfaceIndexedColor(const SpriteSurface *surface, int kind, int index) {
  if (SpriteRunAnimated(kind))
    return ((u32)SpriteAnimationSource(index) << 24) | (surface->animation_palette[index] & 0xFFFFFF);
  // The display ignores the upper byte; global palette colors retain their source index there.
  // Only compact animation IDs are stored in the separate animation plane.
  return (surface->image_palette ? 0 : (u32)index << 24) | (surface->palette[index] & 0xFFFFFF);
}

#if defined(__aarch64__) && defined(__ARM_NEON) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
static uint32x4_t PaletteIndexBuckets(uint32x4_t indices) {
  uint32x4_t shifts = vandq_u32(indices, vdupq_n_u32(31));
  return vandq_u32(vshlq_u32(vdupq_n_u32(1), vreinterpretq_s32_u32(shifts)),
      vcgtq_u32(indices, vdupq_n_u32(0)));
}

static void AccumulatePaletteBuckets(uint8x16x4_t *buckets, uint8x16_t indices) {
  uint8x16_t bits = vandq_u8(vshlq_u8(vdupq_n_u8(1),
      vreinterpretq_s8_u8(vandq_u8(indices, vdupq_n_u8(7)))), vcgtq_u8(indices, vdupq_n_u8(0)));
  uint8x16_t groups = vandq_u8(indices, vdupq_n_u8(24));
  for (int group = 0; group < 4; ++group)
    buckets->val[group] = vorrq_u8(buckets->val[group],
        vandq_u8(bits, vceqq_u8(groups, vdupq_n_u8(group * 8))));
}

static u32 CombinePaletteBuckets(uint8x16x4_t buckets) {
  u32 result = 0;
  for (int group = 0; group < 4; ++group) {
    uint8x8_t bytes = vorr_u8(vget_low_u8(buckets.val[group]), vget_high_u8(buckets.val[group]));
    u64 combined = vget_lane_u64(vreinterpret_u64_u8(bytes), 0);
    combined |= combined >> 32;
    combined |= combined >> 16;
    combined |= combined >> 8;
    result |= (combined & 255) << (group * 8);
  }
  return result;
}

static u32 CombineWidePaletteBuckets(uint32x4_t buckets) {
  uint32x2_t halves = vorr_u32(vget_low_u32(buckets), vget_high_u32(buckets));
  return vget_lane_u32(halves, 0) | vget_lane_u32(halves, 1);
}

static uint8x16_t ReverseSpriteBytes(uint8x16_t bytes) {
  uint8x16_t reversed = vrev64q_u8(bytes);
  return vextq_u8(reversed, reversed, 8);
}

static uint32x4_t ReverseSpriteWords(uint32x4_t words) {
  uint32x4_t reversed = vrev64q_u32(words);
  return vextq_u32(reversed, reversed, 2);
}

static uint8x8_t BlendSpriteChannel8(uint8x8_t source, uint8x8_t destination,
    uint8x8_t alpha, uint8x8_t inverse) {
  uint16x8_t sum = vmlal_u8(vmull_u8(source, alpha), destination, inverse);
  // For sums through 65025, this exactly equals (sum + 127) / 255.
  sum = vaddq_u16(sum, vdupq_n_u16(128));
  return vshrn_n_u16(vaddq_u16(sum, vshrq_n_u16(sum, 8)), 8);
}

static uint8x16_t BlendSpriteChannel(uint8x16_t source, uint8x16_t destination,
    uint8x16_t alpha, uint8x16_t inverse) {
  return vcombine_u8(BlendSpriteChannel8(vget_low_u8(source), vget_low_u8(destination),
      vget_low_u8(alpha), vget_low_u8(inverse)), BlendSpriteChannel8(vget_high_u8(source),
      vget_high_u8(destination), vget_high_u8(alpha), vget_high_u8(inverse)));
}

#endif

static u32 BlendRGB(u32 source, u32 destination, int alpha) {
  int inverse = 255 - alpha;
  // Red and blue occupy separate 16-bit lanes. Each weighted sum fits u16,
  // so their multiplication and exact rounded division can share u32 work.
  u32 rb = (source & 0xFF00FF) * alpha + (destination & 0xFF00FF) * inverse + 0x800080;
  rb = (rb + ((rb >> 8) & 0xFF00FF)) >> 8 & 0xFF00FF;
  u32 green = (source >> 8 & 255) * alpha + (destination >> 8 & 255) * inverse + 128;
  green = (green + (green >> 8)) >> 8;
  return rb | green << 8;
}

static inline void CopyOpaquePaletteRun(u32 *destination, const u8 *payload, const u32 *palette,
    int length, bool flipped, u32 mask) {
  int i = 0;
  if (flipped) {
    for (; i + 4 <= length; i += 4) {
      u32 first = palette[payload[i]] & mask, second = palette[payload[i + 1]] & mask;
      u32 third = palette[payload[i + 2]] & mask, fourth = palette[payload[i + 3]] & mask;
      destination[-i] = first;
      destination[-i - 1] = second;
      destination[-i - 2] = third;
      destination[-i - 3] = fourth;
    }
    for (; i < length; ++i) destination[-i] = palette[payload[i]] & mask;
  } else {
    for (; i + 4 <= length; i += 4) {
      u32 first = palette[payload[i]] & mask, second = palette[payload[i + 1]] & mask;
      u32 third = palette[payload[i + 2]] & mask, fourth = palette[payload[i + 3]] & mask;
      destination[i] = first;
      destination[i + 1] = second;
      destination[i + 2] = third;
      destination[i + 3] = fourth;
    }
    for (; i < length; ++i) destination[i] = palette[payload[i]] & mask;
  }
}

#if defined(__aarch64__) && defined(__ARM_NEON) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
typedef struct {
  u32 *pixels;
  u8 *indices;
  int length;
  SpriteRunKind kind;
  bool flipped, clear_mapping, opaque_palette;
  u32 buckets;
} SpriteCopyRun;

// Keep SIMD register setup out of tiny runs and conditional drawing modes.
static int DrawSpriteCopyRun(const SpriteSurface *surface, const u8 *payload, SpriteCopyRun *run) {
  u32 *destination = run->pixels;
  u8 *mapping = run->indices;
  int length = run->length;
  SpriteRunKind kind = run->kind;
  bool flipped = run->flipped, clear_mapping = run->clear_mapping;
  int copied = 0;
  if (kind == SPRITE_RUN_RGB || kind == SPRITE_RUN_RGBA) {
    for (; copied + 16 <= length; copied += 16) {
      int offset = flipped ? -copied - 15 : copied;
      u32 *target = destination + offset;
      uint8x16x4_t color;
      if (kind == SPRITE_RUN_RGB) {
        uint8x16x3_t rgb = vld3q_u8(payload + copied * 3);
        if (flipped)
          for (int channel = 0; channel < 3; ++channel) rgb.val[channel] = ReverseSpriteBytes(rgb.val[channel]);
        color = (uint8x16x4_t){{rgb.val[2], rgb.val[1], rgb.val[0], vdupq_n_u8(0)}};
        if (clear_mapping) vst1q_u8(mapping + offset, vdupq_n_u8(0));
      } else {
        uint8x16x4_t rgba = vld4q_u8(payload + copied * 4);
        if (flipped)
          for (int channel = 0; channel < 4; ++channel) rgba.val[channel] = ReverseSpriteBytes(rgba.val[channel]);
        uint8x16x4_t before = vld4q_u8((const u8 *)target);
        uint8x16_t alpha = rgba.val[3], inverse = vsubq_u8(vdupq_n_u8(255), alpha);
        color = (uint8x16x4_t){{BlendSpriteChannel(rgba.val[2], before.val[0], alpha, inverse),
            BlendSpriteChannel(rgba.val[1], before.val[1], alpha, inverse),
            BlendSpriteChannel(rgba.val[0], before.val[2], alpha, inverse), vdupq_n_u8(0)}};
        uint8x16_t keep = vceqq_u8(alpha, vdupq_n_u8(0));
        color.val[3] = vandq_u8(before.val[3], keep);
        if (clear_mapping) vst1q_u8(mapping + offset, vandq_u8(vld1q_u8(mapping + offset), keep));
      }
      vst4q_u8((u8 *)target, color);
    }
    if (copied + 8 <= length) {
      int offset = flipped ? -copied - 7 : copied;
      u32 *target = destination + offset;
      uint8x8x4_t color;
      if (kind == SPRITE_RUN_RGB) {
        uint8x8x3_t rgb = vld3_u8(payload + copied * 3);
        if (flipped)
          for (int channel = 0; channel < 3; ++channel) rgb.val[channel] = vrev64_u8(rgb.val[channel]);
        color = (uint8x8x4_t){{rgb.val[2], rgb.val[1], rgb.val[0], vdup_n_u8(0)}};
        if (clear_mapping) vst1_u8(mapping + offset, vdup_n_u8(0));
      } else {
        uint8x8x4_t rgba = vld4_u8(payload + copied * 4);
        if (flipped)
          for (int channel = 0; channel < 4; ++channel) rgba.val[channel] = vrev64_u8(rgba.val[channel]);
        uint8x8x4_t before = vld4_u8((const u8 *)target);
        uint8x8_t alpha = rgba.val[3], inverse = vsub_u8(vdup_n_u8(255), alpha);
        color = (uint8x8x4_t){{BlendSpriteChannel8(rgba.val[2], before.val[0], alpha, inverse),
            BlendSpriteChannel8(rgba.val[1], before.val[1], alpha, inverse),
            BlendSpriteChannel8(rgba.val[0], before.val[2], alpha, inverse), vdup_n_u8(0)}};
        uint8x8_t keep = vceq_u8(alpha, vdup_n_u8(0));
        color.val[3] = vand_u8(before.val[3], keep);
        if (clear_mapping) vst1_u8(mapping + offset, vand_u8(vld1_u8(mapping + offset), keep));
      }
      vst4_u8((u8 *)target, color);
      copied += 8;
    }
    if (kind == SPRITE_RUN_RGB && copied + 4 <= length) {
      // Exactly twelve source bytes: never read past a clipped RGB run.
      const u8 order[16] = {2, 1, 0, 255, 5, 4, 3, 255, 8, 7, 6, 255, 11, 10, 9, 255};
      uint8x16_t rgb = vcombine_u8(vld1_u8(payload + copied * 3), vdup_n_u8(0));
      u32 tail;
      memcpy(&tail, payload + copied * 3 + 8, sizeof(tail));
      rgb = vreinterpretq_u8_u32(vsetq_lane_u32(tail, vreinterpretq_u32_u8(rgb), 2));
      uint32x4_t color = vreinterpretq_u32_u8(vqtbl1q_u8(rgb, vld1q_u8(order)));
      int offset = flipped ? -copied - 3 : copied;
      vst1q_u32(destination + offset, flipped ? ReverseSpriteWords(color) : color);
      if (clear_mapping) memset(mapping + offset, 0, 4);
      copied += 4;
    }
  } else if (kind == SPRITE_RUN_PALETTE_ALPHA) {
    for (; copied + 8 <= length; copied += 8) {
      const u8 *source = payload + copied * 2;
      const u32 *palette = surface->palette;
      u32 colors[8] = {palette[source[0]], palette[source[2]], palette[source[4]], palette[source[6]],
          palette[source[8]], palette[source[10]], palette[source[12]], palette[source[14]]};
      uint8x8x4_t color = vld4_u8((const u8 *)colors);
      uint8x8x2_t indexed = vld2_u8(source);
      uint8x8_t alpha = indexed.val[1];
      uint8x8_t identity = surface->image_palette || run->opaque_palette ? vdup_n_u8(0) : indexed.val[0];
      if (flipped) {
        for (int channel = 0; channel < 3; ++channel) color.val[channel] = vrev64_u8(color.val[channel]);
        alpha = vrev64_u8(alpha);
        identity = vrev64_u8(identity);
      }
      int offset = flipped ? -copied - 7 : copied;
      u32 *target = destination + offset;
      uint8x8x4_t before = vld4_u8((const u8 *)target);
      uint8x8_t inverse = vsub_u8(vdup_n_u8(255), alpha);
      for (int channel = 0; channel < 3; ++channel)
        color.val[channel] = BlendSpriteChannel8(color.val[channel], before.val[channel], alpha, inverse);
      uint8x8_t keep = vceq_u8(alpha, vdup_n_u8(0));
      color.val[3] = vbsl_u8(vceq_u8(alpha, vdup_n_u8(255)), identity, vand_u8(before.val[3], keep));
      if (clear_mapping) vst1_u8(mapping + offset, vand_u8(vld1_u8(mapping + offset), keep));
      vst4_u8((u8 *)target, color);
    }
  } else if ((kind == SPRITE_RUN_PALETTE || kind == SPRITE_RUN_ANIMATED) && length >= 8) {
    uint32x4_t buckets = vdupq_n_u32(0);
    bool animated = kind == SPRITE_RUN_ANIMATED;
    const u32 *palette = animated ? surface->animation_palette : surface->palette;
    uint8x16x3_t water = {{vdupq_n_u8(0), vdupq_n_u8(0), vdupq_n_u8(0)}};
    if (animated) {
      const u32 *colors = palette + SPRITE_ANIMATION_WATER_FIRST;
      water.val[0] = vreinterpretq_u8_u32(vld1q_u32(colors));
      water.val[1] = vreinterpretq_u8_u32(vld1q_u32(colors + 4));
      water.val[2] = vcombine_u8(vreinterpret_u8_u32(vld1_u32(colors + 8)), vdup_n_u8(0));
    }
    for (; copied + 8 <= length; copied += 8) {
      uint8x8_t indices = vld1_u8(payload + copied);
      uint8x8_t animation = animated ? indices : vdup_n_u8(0);
      if (animated || clear_mapping)
        vst1_u8(flipped ? mapping - copied - 7 : mapping + copied,
            flipped ? vrev64_u8(animation) : animation);
      uint16x8_t wide = vmovl_u8(indices);
      bool all_water = animated && vminv_u8(indices) >= SPRITE_ANIMATION_WATER_FIRST &&
          vmaxv_u8(indices) < SPRITE_ANIMATION_COLORS;
      for (int half = 0; half < 2; ++half) {
        int at = copied + half * 4;
        uint32x4_t index = vmovl_u16(half ? vget_high_u16(wide) : vget_low_u16(wide));
        if (animated) buckets = vorrq_u32(buckets, PaletteIndexBuckets(index));
        uint32x4_t color;
        if (all_water) {
          uint32x4_t offsets = vaddq_u32(vmulq_n_u32(
              vsubq_u32(index, vdupq_n_u32(SPRITE_ANIMATION_WATER_FIRST)), 0x04040404), vdupq_n_u32(0x03020100));
          color = vandq_u32(vreinterpretq_u32_u8(vqtbl3q_u8(water, vreinterpretq_u8_u32(offsets))),
              vdupq_n_u32(0xFFFFFF));
        } else {
          u32 colors[4] = {palette[payload[at]], palette[payload[at + 1]], palette[payload[at + 2]],
              palette[payload[at + 3]]};
          uint32x4_t source_index = surface->image_palette || run->opaque_palette ? vdupq_n_u32(0) : index;
          if (animated) {
            source_index = vaddq_u32(index, vbslq_u32(vcltq_u32(index, vdupq_n_u32(50)),
                vdupq_n_u32(170), vdupq_n_u32(174)));
            source_index = vandq_u32(source_index, vcltq_u32(index, vdupq_n_u32(SPRITE_ANIMATION_WATER_FIRST)));
          }
          color = vorrq_u32(vandq_u32(vld1q_u32(colors), vdupq_n_u32(0xFFFFFF)),
              vshlq_n_u32(source_index, 24));
        }
        vst1q_u32(flipped ? destination - at - 3 : destination + at,
            flipped ? ReverseSpriteWords(color) : color);
      }
    }
    run->buckets = CombineWidePaletteBuckets(buckets);
  }
  return copied;
}

#endif

// Payload kind and blend mode are selected outside the pixel loop. Runs never
// cross rows; clipping computes one interval per run, including flipped runs.
void DrawRunSprite(SpriteSurface *surface, const RunSprite *sprite, int x, int y, u32 flags, const u32 *palette) {
  int mode = flags & SPRITE_MODE_MASK, opacity = 255 - (flags >> SPRITE_TRANSPARENCY_SHIFT);
  bool flipped = (flags & SPRITE_FLIPPED) != 0, disabled = (flags & SPRITE_DISABLED) != 0;
  if (!opacity) return;
  SpriteSurface local;
  if (palette || (sprite->palette && mode == SPRITE_COPY)) {
    local = *surface;
    local.palette = palette ? palette : sprite->palette->colors;
    local.remap = NULL;
    local.image_palette = mode == SPRITE_COPY;
    surface = &local;
  }
  flipped ^= sprite->mirrored;
  Canvas *canvas = &surface->canvas;
  CityRect clip = canvas->clip;
  clip.left = IntMax(0, clip.left); clip.top = IntMax(0, clip.top);
  clip.right = IntMin(canvas->width, clip.right); clip.bottom = IntMin(canvas->height, clip.bottom);
  i64 right = (i64)x + sprite->width, bottom = (i64)y + sprite->height;
  if (right <= clip.left || x >= clip.right || bottom <= clip.top || y >= clip.bottom) return;
  if (canvas->draw_stats && canvas->draw_stats->active) ++canvas->draw_stats->sprite_calls;
  int first_row = (int)IntMax(0, (i64)clip.top - y);
  int end_row = (int)IntMin(sprite->height, (i64)clip.bottom - y);
  int first_pixel = (int)IntMax(0, flipped ? right - clip.right : (i64)clip.left - x);
  int end_pixel = (int)IntMin(sprite->width, flipped ? right - clip.left : (i64)clip.right - x);
  const u32 *rows = RunSpriteRows(sprite);
  const u8 *data = RunSpriteData(sprite);
#if defined(__aarch64__) && defined(__ARM_NEON) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  bool vector_copy = mode == SPRITE_COPY && surface->indices;
#endif
  for (int row = first_row; row < end_row; ++row) {
    u32 row_flag = 0;
    bool clear_mapping = surface->indices && (!surface->row_flags || surface->row_flags[y + row] != 0);
    const u8 *source = data + rows[row], *end = data + rows[row + 1];
    int position = source < end ? ReadSpriteInitialSkip(&source) : 0;
    while (source < end) {
      int kind, count = ReadSpriteRun(&source, &kind), bytes = SpriteRunPayloadBytes(kind);
      const u8 *payload = source;
      source = payload + count * bytes;
      int run_first = position;
      position += count;
      if (run_first >= end_pixel) break;
      if (kind == SPRITE_RUN_TRANSPARENT || position <= first_pixel) continue;
      int first = IntMax(run_first, first_pixel), finish = IntMin(position, end_pixel);
      int length = finish - first;
      int left = flipped ? (int)(right - finish) : x + first, last = left + length;
      payload += (first - run_first) * bytes;
      int step = flipped ? -1 : 1;
      int start = flipped ? last - 1 : left;
      u32 *destination = canvas->pixels + (size_t)(y + row) * canvas->stride + start;
      u8 *mapping = surface->indices ? surface->indices + (size_t)(y + row) * surface->index_stride + start : NULL;
      if (opacity != 255 || disabled) {
        // Apply whole-sprite opacity only to covered pixels, without rebuilding palettes.
        for (int i = 0; i < length; ++i, payload += bytes) {
          int alpha = SpriteRunHasAlpha(kind) ? payload[bytes - 1] : 255;
          u32 color;
          int animation = 0;
          if (SpriteRunIndexed(kind)) {
            int index = payload[0], color_kind = kind;
            bool fixed = mode != SPRITE_COPY;
            if (fixed && surface->remap) {
              int source_index = SpriteRunAnimated(kind) ? SpriteAnimationSource(index) : index;
              if (!SpriteRunAnimated(kind) || source_index) {
                index = surface->remap[source_index];
                color_kind = SPRITE_RUN_PALETTE;
              }
            }
            if (mode == SPRITE_FIXED_PALETTE) alpha = alpha * (surface->palette[index] >> 24) / 255;
            color = SurfaceIndexedColor(surface, color_kind, index);
            if (!fixed && SpriteRunAnimated(kind)) animation = index;
          } else color = payload[0] << 16 | payload[1] << 8 | payload[2];
          if (disabled) {
            int gray = (((color >> 16 & 255) + (color >> 8 & 255) + (color & 255)) / 3 + 131) / 2;
            color = gray * 0x010101;
            animation = 0;
          }
          alpha = alpha * opacity / 255;
          if (alpha) {
            if (canvas->palette_composite_rows && mapping) {
              int pixel = start + i * step;
              RecordCanvasPaletteComposite(canvas, y + row, pixel, pixel + 1,
                  CanvasPaletteIndexMask(animation) | CanvasPaletteIndexMask(*mapping));
            }
            *destination = BlendRGB(color, *destination, alpha);
            if (mapping) *mapping = 0;
          }
          if (i + 1 < length) {
            if (mapping) mapping += step;
            destination += step;
          }
        }
        continue;
      }
      bool opaque_palette = mode == SPRITE_OPAQUE_PALETTE && !surface->remap;
      if (kind == SPRITE_RUN_PALETTE && ((mode == SPRITE_COPY && surface->image_palette) || opaque_palette)) {
        // Four independent lookups and stores, with no blend or vector packing.
        if (clear_mapping) memset(flipped ? mapping - length + 1 : mapping, 0, length);
        if (opaque_palette) CopyOpaquePaletteRun(destination, payload, surface->palette, length, flipped, 0xFFFFFF);
        else CopyOpaquePaletteRun(destination, payload, surface->palette, length, flipped, 0xFFFFFFFF);
        continue;
      }
#if defined(__aarch64__) && defined(__ARM_NEON) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      // Never load across a clipped run.
      if (surface->indices && (vector_copy || (opaque_palette && kind == SPRITE_RUN_PALETTE_ALPHA)) &&
          (length >= 8 || (kind == SPRITE_RUN_RGB && length >= 4))) {
        if ((kind == SPRITE_RUN_RGBA || kind == SPRITE_RUN_PALETTE_ALPHA) && canvas->palette_composite_rows &&
            (!surface->row_flags || surface->row_flags[y + row] || row_flag)) {
          // Only background pixels actually covered by a translucent run
          // contribute palette dependencies. A water pixel elsewhere on the
          // same scanline must not make this entire sprite need repainting.
          u32 buckets = 0;
          int dependent_left = last, dependent_right = left;
          for (int i = 0; i < length; ++i) {
            int index = mapping[i * step];
            if (!index) continue;
            int alpha = payload[i * bytes + bytes - 1];
            if (!alpha || alpha == 255) continue;
            buckets |= CanvasPaletteIndexMask(index);
            int pixel = start + i * step;
            dependent_left = IntMin(dependent_left, pixel);
            dependent_right = IntMax(dependent_right, pixel + 1);
          }
          RecordCanvasPaletteComposite(canvas, y + row, dependent_left, dependent_right, buckets);
        }
        SpriteCopyRun run = {destination, mapping, length, kind, flipped, clear_mapping, opaque_palette, 0};
        int copied = DrawSpriteCopyRun(surface, payload, &run);
        row_flag |= run.buckets;
        if (copied == length) continue;
        payload += copied * bytes;
        destination += copied * step;
        mapping += copied * step;
        start += copied * step;
        length -= copied;
      }
#endif
      if (((mode == SPRITE_COPY && surface->image_palette) || opaque_palette) &&
          !canvas->palette_composite_rows && kind == SPRITE_RUN_PALETTE_ALPHA) {
        // Image and opaque replacement palettes retain no animation identity.
        // Keep their small runs and SIMD tails out of the general mode and
        // remapping branches for every individual pixel.
        const u32 *palette = surface->palette;
        for (int i = 0; i < length; ++i) {
          int alpha = payload[i * 2 + 1];
          if (!alpha) continue;
          u32 color = palette[payload[i * 2]];
          u32 *pixel = destination + i * step;
          *pixel = alpha == 255 ? color & 0xFFFFFF : BlendRGB(color, *pixel, alpha);
          if (clear_mapping) mapping[i * step] = 0;
        }
        continue;
      }
      if (kind == SPRITE_RUN_PALETTE && mode == SPRITE_OPAQUE_PALETTE) {
        // Local palettes contain opaque RGB. No background read, palette-alpha
        // composition, or registration with the global animation palette.
        for (int i = 0; i < length; ++i, ++payload) {
          *destination = surface->palette[*payload] & 0xFFFFFF;
          if (mapping) *mapping = 0;
          if (i + 1 < length) {
            if (mapping) mapping += step;
            destination += step;
          }
        }
      } else if (SpriteRunIndexed(kind)) {
        for (int i = 0; i < length; ++i, payload += bytes) {
          int index = payload[0], alpha = SpriteRunHasAlpha(kind) ? payload[1] : 255;
          int color_kind = kind;
          bool fixed = mode == SPRITE_FIXED_PALETTE || mode == SPRITE_OPAQUE_PALETTE;
          if (fixed && surface->remap) {
            int source_index = SpriteRunAnimated(kind) ? SpriteAnimationSource(index) : index;
            if (!SpriteRunAnimated(kind) || source_index) {
              index = surface->remap[source_index];
              color_kind = SPRITE_RUN_PALETTE;
            }
          }
          if (fixed && mode != SPRITE_OPAQUE_PALETTE) alpha = alpha * (surface->palette[index] >> 24) / 255;
          if (alpha) {
            u32 color = SurfaceIndexedColor(surface, color_kind, index);
            int animation = !fixed && SpriteRunAnimated(kind) ? index : 0;
            if (alpha == 255) {
              *destination = fixed ? color & 0xFFFFFF : color;
              if (mapping) *mapping = animation;
              row_flag |= CanvasPaletteIndexMask(animation);
            } else {
              if (canvas->palette_composite_rows && mapping) {
                u32 buckets = CanvasPaletteIndexMask(animation) | CanvasPaletteIndexMask(*mapping);
                int pixel = start + i * step;
                RecordCanvasPaletteComposite(canvas, y + row, pixel, pixel + 1, buckets);
              }
              *destination = BlendRGB(color, *destination, alpha);
              if (mapping) *mapping = 0;
            }
          }
          if (i + 1 < length) {
            if (mapping) mapping += step;
            destination += step;
          }
        }
      } else if (kind == SPRITE_RUN_RGB) {
        for (int i = 0; i < length; ++i, payload += 3) {
          *destination = (payload[0] << 16) | (payload[1] << 8) | payload[2];
          if (clear_mapping) *mapping = 0;
          if (i + 1 < length) {
            if (mapping) mapping += step;
            destination += step;
          }
        }
      } else {
        for (int i = 0; i < length; ++i, payload += 4) {
          if (payload[3]) {
            if (canvas->palette_composite_rows && mapping && payload[3] != 255 && *mapping) {
              int pixel = start + i * step;
              RecordCanvasPaletteComposite(canvas, y + row, pixel, pixel + 1, CanvasPaletteIndexMask(*mapping));
            }
            u32 rgb = (payload[0] << 16) | (payload[1] << 8) | payload[2];
            *destination = BlendRGB(rgb, *destination, payload[3]);
            if (clear_mapping) *mapping = 0;
          }
          if (i + 1 < length) {
            if (mapping) mapping += step;
            destination += step;
          }
        }
      }
    }
    if (surface->row_flags) surface->row_flags[y + row] |= row_flag;
  }
}

void FillSpriteSurface(SpriteSurface *surface, CityRect rect, u8 index) {
  Canvas *canvas = &surface->canvas;
  rect.left = IntMax(0, IntMax(rect.left, canvas->clip.left));
  rect.top = IntMax(0, IntMax(rect.top, canvas->clip.top));
  rect.right = IntMin(canvas->width, IntMin(rect.right, canvas->clip.right));
  rect.bottom = IntMin(canvas->height, IntMin(rect.bottom, canvas->clip.bottom));
  if (rect.left >= rect.right || rect.top >= rect.bottom) return;
  u32 color = SurfaceIndexedColor(surface, SPRITE_RUN_PALETTE, index);
  int animation = SpriteAnimationId(index);
  for (int y = rect.top; y < rect.bottom; ++y) {
    u32 *row = canvas->pixels + (size_t)y * canvas->stride;
    for (int x = rect.left; x < rect.right; ++x) row[x] = color;
    if (animation || !surface->row_flags || surface->row_flags[y])
      memset(surface->indices + (size_t)y * surface->index_stride + rect.left, animation, rect.right - rect.left);
    if (surface->row_flags) surface->row_flags[y] |= CanvasPaletteIndexMask(animation);
  }
}


static bool RecolorSpriteSurface(SpriteSurface *surface, int first, int end, u32 changed_buckets) {
  bool changed = false;
  Canvas *canvas = &surface->canvas;
  if (!changed_buckets) return false;
  for (int y = 0; y < canvas->height; ++y) {
    if (surface->row_flags && !(surface->row_flags[y] & changed_buckets)) continue;
    u32 row_flag = 0;
    int dirty_left = canvas->width, dirty_right = 0;
    u32 *pixels = canvas->pixels + (size_t)y * canvas->stride;
    const u8 *indices = surface->indices + (size_t)y * surface->index_stride;
    int x = 0;
#if defined(__aarch64__) && defined(__ARM_NEON) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    // Eight index bytes feed two four-pixel vectors without reading past a row.
    {
      uint32x4_t row_buckets = vdupq_n_u32(0);
      for (; x + 8 <= canvas->width; x += 8) {
        uint8x8_t packed = vld1_u8(indices + x);
        uint16x8_t wide = vmovl_u8(packed);
        for (int half = 0; half < 2; ++half) {
          uint32x4_t index = vmovl_u16(half ? vget_high_u16(wide) : vget_low_u16(wide));
          uint32x4_t buckets = PaletteIndexBuckets(index);
          row_buckets = vorrq_u32(row_buckets, buckets);
          uint32x4_t selected = vandq_u32(vcgeq_u32(index, vdupq_n_u32(first)),
              vcltq_u32(index, vdupq_n_u32(end)));
          selected = vandq_u32(selected, vtstq_u32(buckets, vdupq_n_u32(changed_buckets)));
          if (!vmaxvq_u32(selected)) continue;
          int at = x + half * 4;
          uint32x4_t before = vld1q_u32(pixels + at);
          u32 palette[4] = {surface->animation_palette[indices[at]], surface->animation_palette[indices[at + 1]],
              surface->animation_palette[indices[at + 2]], surface->animation_palette[indices[at + 3]]};
          uint32x4_t color = vorrq_u32(vandq_u32(vld1q_u32(palette), vdupq_n_u32(0xFFFFFF)),
              vandq_u32(before, vdupq_n_u32(0xFF000000)));
          color = vbslq_u32(selected, color, before);
          if (vmaxvq_u32(veorq_u32(before, color))) {
            vst1q_u32(pixels + at, color);
            changed = true;
            dirty_left = IntMin(dirty_left, at); dirty_right = at + 4;
          }
        }
      }
      row_flag = CombineWidePaletteBuckets(row_buckets);
    }
#endif
    for (; x < canvas->width; ++x) {
      int index = indices[x];
      row_flag |= CanvasPaletteIndexMask(index);
      if (index < first || index >= end || !(CanvasPaletteIndexMask(index) & changed_buckets)) continue;
      u32 color = (pixels[x] & 0xFF000000) | (surface->animation_palette[index] & 0xFFFFFF);
      if (pixels[x] != color) {
        pixels[x] = color; changed = true;
        dirty_left = IntMin(dirty_left, x); dirty_right = x + 1;
      }
    }
    if (surface->row_flags) surface->row_flags[y] = row_flag;
    if (surface->upload_damage && dirty_left < dirty_right)
      MarkScreenDirty(surface->upload_damage, (CityRect){dirty_left, y, dirty_right, y + 1});
  }
  return changed;
}

bool AnimateSpriteSurface(SpriteSurface *surface, int first, int count) {
  if (count <= 0) return false;
  i64 limit = (i64)first + count;
  int end = limit < SPRITE_ANIMATION_COLORS ? (int)limit : SPRITE_ANIMATION_COLORS;
  first = IntMax(first, 1);
  u32 changed_buckets = 0;
  for (int index = first; index < end; ++index) changed_buckets |= CanvasPaletteIndexMask(index);
  return RecolorSpriteSurface(surface, first, end, changed_buckets);
}

bool AnimateSpriteSurfaceChanges(SpriteSurface *surface, u32 changed_buckets) {
  return RecolorSpriteSurface(surface, 1, SPRITE_ANIMATION_COLORS, changed_buckets);
}

// Ten consecutive water animation IDs fit a sixteen-byte table per channel.
// Static indexed pixels never enter this animation scan.
bool AnimateSpriteSurfaceWater(SpriteSurface *surface, u32 changed_buckets) {
  if (!surface->animation_palette || !changed_buckets) return false;
  Canvas *canvas = &surface->canvas;
  bool changed = false;
#if defined(__aarch64__) && defined(__ARM_NEON) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  // Deinterleaved channels let each NEON table lookup recolor sixteen pixels.
  // Index zero is fixed RGB; ten water colors fit in one table per channel.
  u32 water[16] = {0};
  u8 selected_indices[16] = {0};
  for (int index = 0; index < SPRITE_WATER_PALETTE_COLORS; ++index)
    water[index] = surface->animation_palette[SPRITE_ANIMATION_WATER_FIRST + index] & 0xFFFFFF;
  for (int index = 0; index < SPRITE_WATER_PALETTE_COLORS; ++index)
    if (CanvasPaletteIndexMask(SPRITE_ANIMATION_WATER_FIRST + index) & changed_buckets) selected_indices[index] = 255;
  uint8x16x4_t table = vld4q_u8((const u8 *)water);
  uint8x16_t selection = vld1q_u8(selected_indices);
#endif
  for (int y = 0; y < canvas->height; ++y) {
    if (surface->row_flags && !(surface->row_flags[y] & changed_buckets)) continue;
    u32 *pixels = canvas->pixels + (size_t)y * canvas->stride;
    const u8 *indices = surface->indices + (size_t)y * surface->index_stride;
    u32 row_flag = 0;
    int dirty_left = canvas->width, dirty_right = 0;
    int x = 0;
#if defined(__aarch64__) && defined(__ARM_NEON) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    {
      uint8x16x4_t row_buckets = {{vdupq_n_u8(0), vdupq_n_u8(0), vdupq_n_u8(0), vdupq_n_u8(0)}};
      for (; x + 16 <= canvas->width; x += 16) {
        uint8x16_t index = vld1q_u8(indices + x);
        if (!vmaxvq_u8(index)) continue;
        // Recompute in bytes: each vector accumulates eight adjacent buckets.
        // This shares the index load with recoloring and avoids widening to words.
        AccumulatePaletteBuckets(&row_buckets, index);
        uint8x16_t water_index = vsubq_u8(index, vdupq_n_u8(SPRITE_ANIMATION_WATER_FIRST));
        uint8x16_t selected = vqtbl1q_u8(selection, water_index);
        if (!vmaxvq_u8(selected)) continue;
        uint8x16x4_t before = vld4q_u8((const u8 *)(pixels + x));
        uint8x16x4_t color = before;
        uint8x16_t difference = vdupq_n_u8(0);
        for (int channel = 0; channel < 3; ++channel) {
          color.val[channel] = vbslq_u8(selected, vqtbl1q_u8(table.val[channel], water_index), before.val[channel]);
          difference = vorrq_u8(difference, veorq_u8(color.val[channel], before.val[channel]));
        }
        if (vmaxvq_u8(difference)) {
          vst4q_u8((u8 *)(pixels + x), color);
          changed = true;
          dirty_left = IntMin(dirty_left, x); dirty_right = x + 16;
        }
      }
      row_flag = CombinePaletteBuckets(row_buckets);
    }
#endif
    for (; x < canvas->width; ++x) {
      int index = indices[x];
      row_flag |= CanvasPaletteIndexMask(index);
      if (index < SPRITE_ANIMATION_WATER_FIRST || index >= SPRITE_ANIMATION_COLORS ||
          !(CanvasPaletteIndexMask(index) & changed_buckets)) continue;
      u32 color = (pixels[x] & 0xFF000000) | (surface->animation_palette[index] & 0xFFFFFF);
      if (pixels[x] != color) {
        pixels[x] = color;
        changed = true;
        dirty_left = IntMin(dirty_left, x); dirty_right = x + 1;
      }
    }
    if (surface->row_flags) surface->row_flags[y] = row_flag;
    if (surface->upload_damage && dirty_left < dirty_right)
      MarkScreenDirty(surface->upload_damage, (CityRect){dirty_left, y, dirty_right, y + 1});
  }
  return changed;
}
