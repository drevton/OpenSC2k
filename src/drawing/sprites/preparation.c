#include "drawing/sprites/preparation.h"
#include "drawing/sprites/surface.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "drawing/raster/canvas_raster_internal.h"
#include "drawing/raster/screen_damage.h"

// Sprite encoding, region resampling and streamed image drawing share bounded row workspace.
// Map caches and startup workers live in drawing/maps/sprite_cache.c; immutable source queries use sources.c.

static RunSprite *ResizeRunSpriteRegion(const RunSprite *source, CityRect content, int width, int height);

static SpritePixel *SpritePreparationPixels(SpritePreparationWorkspace *workspace, size_t count) {
  size_t limit = SIZE_MAX / sizeof(SpritePixel);
  if (count > limit) return NULL;
  if (count > workspace->capacity) {
    size_t capacity = count;
    if (workspace->capacity <= limit / 2 && capacity < workspace->capacity * 2)
      capacity = workspace->capacity * 2;
    SpritePixel *pixels = realloc(workspace->pixels, capacity * sizeof(*pixels));
    if (!pixels) return NULL;
    workspace->pixels = pixels;
    workspace->capacity = capacity;
  }
  return workspace->pixels;
}

void FreeSpritePreparationWorkspace(SpritePreparationWorkspace *workspace) {
  free(workspace->pixels);
  workspace->pixels = NULL;
  workspace->capacity = 0;
}

static SpriteRunKind PixelKind(SpritePixel pixel) {
  int alpha = pixel.rgba >> 24;
  if (!alpha) return SPRITE_RUN_TRANSPARENT;
  if (SpriteRunIndexed(pixel.kind)) {
    if (SpriteRunAnimated(pixel.kind)) return alpha == 255 ? SPRITE_RUN_ANIMATED : SPRITE_RUN_ANIMATED_ALPHA;
    return alpha == 255 ? SPRITE_RUN_PALETTE : SPRITE_RUN_PALETTE_ALPHA;
  }
  return alpha == 255 ? SPRITE_RUN_RGB : SPRITE_RUN_RGBA;
}

static int SpriteRunHeaderSize(int count) { return count <= 31 ? 1 : count <= 286 ? 2 : 4; }

static u8 *WriteSpriteRun(u8 *destination, int kind, int count) {
  *destination++ = kind << 5 | IntMin(count - 1, 31);
  if (count >= 32) {
    *destination++ = count <= 286 ? count - 32 : 255;
    if (count > 286) { *destination++ = count; *destination++ = count >> 8; }
  }
  return destination;
}

// Nearest-neighbor expansion happens once per cached scale. The same row
// encoder sizes and writes the stream, avoiding divergent length decisions.
static size_t EncodeSpriteRowRange(const SpritePixel *row, int first, int last,
    int scale, u8 *destination) {
  while (first < last && PixelKind(row[first / scale]) == SPRITE_RUN_TRANSPARENT) ++first;
  while (last > first && PixelKind(row[(last - 1) / scale]) == SPRITE_RUN_TRANSPARENT) --last;
  if (first == last) return 0;
  size_t size = first < 255 ? 1 : 3;
  if (destination) {
    *destination++ = IntMin(first, 255);
    if (first >= 255) { *destination++ = first; *destination++ = first >> 8; }
  }
  for (int x = first; x < last;) {
    SpriteRunKind kind = PixelKind(row[x / scale]);
    int next = x + 1, limit = IntMin(last, x + SPRITE_RUN_LIMIT);
    while (next < limit && PixelKind(row[next / scale]) == kind) ++next;
    size += SpriteRunHeaderSize(next - x) + (size_t)(next - x) * SpriteRunPayloadBytes(kind);
    if (destination) {
      destination = WriteSpriteRun(destination, kind, next - x);
      if (kind != SPRITE_RUN_TRANSPARENT) for (int i = x; i < next; ++i) {
        SpritePixel pixel = row[i / scale];
        if (SpriteRunIndexed(kind)) *destination++ = pixel.index;
        else {
          *destination++ = pixel.rgba >> 16;
          *destination++ = pixel.rgba >> 8;
          *destination++ = pixel.rgba;
        }
        if (SpriteRunHasAlpha(kind)) *destination++ = pixel.rgba >> 24;
      }
    }
    x = next;
  }
  return size;
}

static size_t EncodeSpriteRow(const SpritePixel *row, int width, int scale, u8 *destination) {
  return EncodeSpriteRowRange(row, 0, width, scale, destination);
}

// Sample directly from the packed source. Downscaling skips unused source rows
// and pixels, and needs only one output row rather than a full decoded image.
static void SampleSpriteRow(const RunSprite *sprite, int y, int left, SpritePixel *row, int width,
    int numerator, int denominator, const u32 *palette, int *first_pixel, int *last_pixel) {
  if (sprite->palette) palette = sprite->palette->colors;
  *first_pixel = *last_pixel = 0;
  if (y < 0 || y >= sprite->height) return;
  const u32 *rows = RunSpriteRows(sprite);
  const u8 *data = RunSpriteData(sprite), *at = data + rows[y], *end = data + rows[y + 1];
  if (at == end) return;
  int source_x = ReadSpriteInitialSkip(&at) - left, x = 0;
  int step = denominator / numerator, fraction = denominator % numerator;
  *first_pixel = IntMax(0, IntMin(width,
      (int)(((i64)source_x * numerator + denominator - 1) / denominator)));
  while (at < end) {
    int kind, count = ReadSpriteRun(&at, &kind), bytes = SpriteRunPayloadBytes(kind);
    int first = IntMax(0, IntMin(width,
        (int)(((i64)source_x * numerator + denominator - 1) / denominator)));
    int last = IntMax(0, IntMin(width,
        (int)(((i64)(source_x + count) * numerator + denominator - 1) / denominator)));
    x = IntMax(x, first);
    if (kind == SPRITE_RUN_TRANSPARENT) {
      memset(row + x, 0, (size_t)(last - x) * sizeof(*row));
      x = last;
    } else {
      int sample = (int)((i64)x * denominator / numerator) - source_x;
      i64 phase = (i64)x * denominator % numerator;
      for (; x < last; ++x) {
        const u8 *pixel = at + (size_t)sample * bytes;
        row[x].kind = kind;
        if (SpriteRunIndexed(kind)) {
          row[x].index = pixel[0];
          row[x].rgba = (SpriteRunAnimated(kind) ? 0 : palette[pixel[0]]) & 0xFFFFFF;
          row[x].rgba |= (u32)(SpriteRunHasAlpha(kind) ? pixel[1] : 255) << 24;
        } else {
          row[x].rgba = (pixel[0] << 16) | (pixel[1] << 8) | pixel[2];
          row[x].rgba |= kind == SPRITE_RUN_RGBA ? (u32)pixel[3] << 24 : 0xFF000000;
        }
        sample += step;
        phase += fraction;
        if (phase >= numerator) { ++sample; phase -= numerator; }
      }
    }
    if (x == width) break;
    source_x += count;
    at += (size_t)count * bytes;
  }
  *last_pixel = x;
}

// Interpolate fixed image colors or zoning color/opacity within the nearest
// sample's footprint. Transparent neighbors never expand the sprite boundary.
static void SmoothSpriteRowColors(const RunSprite *source, SpritePixel *row, SpritePixel *samples,
    CityRect content, int y, CityPoint numerator, CityPoint denominator, const u32 *palette,
    int first, int last, bool image) {
  int width = content.right - content.left, height = content.bottom - content.top;
  int sy = IntClamp((int)((i64)(2 * y + 1) * denominator.y * 128 / numerator.y - 128), 0, (height - 1) * 256);
  int y0 = sy >> 8, fy = sy & 255;
  for (int side = 0; side < 2; ++side) {
    SpritePixel *sample = samples + side * width;
    memset(sample, 0, (size_t)width * sizeof(*sample));
    int begin, end;
    SampleSpriteRow(source, content.top + IntMin(y0 + side, height - 1), content.left,
        sample, width, 1, 1, palette, &begin, &end);
  }
  for (int x = first; x < last; ++x) {
    if (image && SpriteRunIndexed(row[x].kind) && !SpriteRunAnimated(row[x].kind))
      row[x].kind = SpriteRunHasAlpha(row[x].kind) ? SPRITE_RUN_RGBA : SPRITE_RUN_RGB;
    if (row[x].kind != SPRITE_RUN_RGB && row[x].kind != SPRITE_RUN_RGBA) continue;
    int sx = IntClamp((int)((i64)(2 * x + 1) * denominator.x * 128 / numerator.x - 128), 0, (width - 1) * 256);
    int x0 = sx >> 8, fx = sx & 255;
    u32 red = 0, green = 0, blue = 0, alpha = 0, total = 0;
    for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
      SpritePixel pixel = samples[dy * width + IntMin(x0 + dx, width - 1)];
      int weight = (dx ? fx : 256 - fx) * (dy ? fy : 256 - fy);
      if (image) total += weight;
      if (pixel.kind != SPRITE_RUN_RGB && pixel.kind != SPRITE_RUN_RGBA &&
          !(image && SpriteRunIndexed(pixel.kind) && !SpriteRunAnimated(pixel.kind))) continue;
      int color_weight = image ? weight * (pixel.rgba >> 24) : weight;
      red += ((pixel.rgba >> 16) & 255) * color_weight;
      green += ((pixel.rgba >> 8) & 255) * color_weight;
      blue += (pixel.rgba & 255) * color_weight;
      alpha += (pixel.rgba >> 24) * weight;
      if (!image) total += weight;
    }
    // Interpolate image colors with premultiplied weights, then encode straight
    // alpha again. Invisible RGB must not darken antialiased icon and text edges.
    u32 colors = image ? alpha : total;
    if (total && colors) row[x].rgba = ((alpha + total / 2) / total) << 24 |
        ((red + colors / 2) / colors) << 16 | ((green + colors / 2) / colors) << 8 | (blue + colors / 2) / colors;
    else if (image) row[x].rgba = 0;
  }
}

RunSprite *ResampleRunSprite(SpritePreparationWorkspace *workspace, const RunSprite *source,
    CityRect content, CityPoint numerator, CityPoint denominator, const u32 *palette, SpriteFilter filter) {
  bool smooth_colors = filter != SPRITE_FILTER_NEAREST;
  if (numerator.x == denominator.x && numerator.y == denominator.y && !content.left && !content.top &&
      content.right == source->width && content.bottom == source->height) {
    return RetainRunSprite(source);
  }
  i64 width = ((i64)(content.right - content.left) * numerator.x + denominator.x - 1) / denominator.x;
  i64 height = ((i64)(content.bottom - content.top) * numerator.y + denominator.y - 1) / denominator.y;
  if (width > 65535 || height > INT_MAX) return NULL;
  size_t sample_count = smooth_colors ? 2 * (size_t)(content.right - content.left) : 0;
  SpritePixel *row = SpritePreparationPixels(workspace, (size_t)width + sample_count);
  if (!row) return NULL;
  SpritePixel *samples = row + width;
  RunSprite *sprite = NULL;
  const u32 *source_rows = RunSpriteRows(source);
  const u8 *source_data = RunSpriteData(source);
  u64 stream_size = 0;
  for (int pass = 0; pass < 2; ++pass) {
    u32 *rows = sprite ? (u32 *)(sprite + 1) : NULL;
    u8 *data = rows ? (u8 *)(rows + height + 1) : NULL;
    size_t row_size = 0;
    u64 offset = 0;
    int previous_y = -1;
    for (int y = 0; y < height; ++y) {
      int source_y = content.top + (int)((i64)y * denominator.y / numerator.y);
      if (rows) rows[y] = (u32)offset;
      size_t source_size = source_y >= 0 && source_y < source->height ?
          source_rows[source_y + 1] - source_rows[source_y] : 0;
      bool repeated = !smooth_colors && source_size && previous_y >= 0 && previous_y < source->height &&
          (source_y == previous_y ||
          (source_size == source_rows[previous_y + 1] - source_rows[previous_y] &&
          !memcmp(source_data + source_rows[source_y], source_data + source_rows[previous_y], source_size)));
      if (!source_size) {
        row_size = 0;
      } else if (!repeated) {
        int first, last;
        SampleSpriteRow(source, source_y, content.left, row, (int)width,
            numerator.x, denominator.x, palette, &first, &last);
        if (smooth_colors && first < last)
          SmoothSpriteRowColors(source, row, samples, content, y, numerator, denominator, palette, first, last,
              filter == SPRITE_FILTER_IMAGE);
        // Initial skips and omitted trailing pixels already describe the transparent
        // margins. Neither clear nor scan those pixels in the temporary row.
        row_size = EncodeSpriteRowRange(row, first, last, 1, data ? data + offset : NULL);
      } else if (data) {
        memcpy(data + offset, data + rows[y - 1], row_size);
      }
      previous_y = source_y;
      offset += row_size;
      if (offset > UINT32_MAX) { FreeRunSprite(sprite); return NULL; }
    }
    if (!pass) {
      stream_size = offset;
      u64 allocation = sizeof(*sprite) + (height + 1) * sizeof(u32) + stream_size;
      if (allocation > SIZE_MAX) return NULL;
      sprite = malloc((size_t)allocation);
      if (!sprite) return NULL;
      *sprite = (RunSprite){.width = (int)width, .height = (int)height, .stream_size = (u32)stream_size,
          .references = 1, .palette = source->palette, .mirrored = source->mirrored};
      RetainSpritePalette(sprite->palette);
    } else rows[height] = (u32)offset;
  }
  return sprite;
}

// Prepare a fixed image at its final pixel dimensions through the shared resampler.
// Only row workspace is temporary; the returned normal sprite owns one reference.
RunSprite *ResizeRunSprite(const RunSprite *source, int width, int height) {
  return ResizeRunSpriteRegion(source, (CityRect){0, 0, source->width, source->height}, width, height);
}

// Crop coordinates refer to the source canvas, preserving shared preview alignment.
static RunSprite *ResizeRunSpriteRegion(const RunSprite *source, CityRect content, int width, int height) {
  assert(source && width > 0 && height > 0 && content.left >= 0 && content.top >= 0 &&
      content.left < content.right && content.top < content.bottom);
  SpritePreparationWorkspace workspace = {0};
  RunSprite *sprite = ResampleRunSprite(&workspace, source, content,
      (CityPoint){width, height}, (CityPoint){content.right - content.left, content.bottom - content.top},
      NULL, SPRITE_FILTER_IMAGE);
  FreeSpritePreparationWorkspace(&workspace);
  return sprite;
}

RunSprite *EncodeRunSprite(const SpritePixel *pixels, int width, int height, int stride, int scale) {
  if (!pixels || width <= 0 || height <= 0 || stride < width || scale <= 0) return NULL;
  i64 w = (i64)width * scale, h = (i64)height * scale;
  // The initial skip and extended run lengths are u16.
  if (w > 65535 || h > INT_MAX) return NULL;
  int output_width = (int)w, output_height = (int)h;
  u64 stream_size = 0;
  size_t row_size = 0;
  for (int y = 0; y < output_height; ++y) {
    if (!y || y / scale != (y - 1) / scale)
      row_size = EncodeSpriteRow(pixels + (size_t)(y / scale) * stride, output_width, scale, NULL);
    stream_size += row_size;
    if (stream_size > UINT32_MAX) return NULL;
  }
  u64 allocation = sizeof(RunSprite) + ((u64)output_height + 1) * sizeof(u32) + stream_size;
  if (allocation > SIZE_MAX) return NULL;
  RunSprite *sprite = malloc((size_t)allocation);
  if (!sprite) return NULL;
  *sprite = (RunSprite){.width = output_width, .height = output_height,
      .stream_size = (u32)stream_size, .references = 1};
  u32 *rows = (u32 *)(sprite + 1);
  u8 *data = (u8 *)(rows + output_height + 1), *destination = data;
  for (int y = 0; y < output_height; ++y) {
    rows[y] = (u32)(destination - data);
    if (y && y / scale == (y - 1) / scale) {
      size_t size = rows[y] - rows[y - 1];
      memcpy(destination, data + rows[y - 1], size);
      destination += size;
    } else destination += EncodeSpriteRow(pixels + (size_t)(y / scale) * stride, output_width, scale, destination);
  }
  rows[output_height] = (u32)(destination - data);
  return sprite;
}

// Encode generated straight-alpha pixels without keeping a second decoded image.
RunSprite *EncodeRgbaRunSprite(const u32 *pixels, int width, int height, int stride) {
  assert(pixels && width > 0 && width <= SPRITE_RUN_LIMIT && height > 0 && stride >= width);
  SpritePixel *row = calloc(width, sizeof(*row));
  if (!row) return NULL;
  RunSprite *sprite = NULL;
  for (int pass = 0; pass < 2; ++pass) {
    u32 *rows = sprite ? (u32 *)(sprite + 1) : NULL;
    u8 *data = rows ? (u8 *)(rows + height + 1) : NULL;
    u64 offset = 0;
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) row[x].rgba = pixels[(size_t)y * stride + x];
      if (rows) rows[y] = offset;
      offset += EncodeSpriteRow(row, width, 1, data ? data + offset : NULL);
      if (offset > UINT32_MAX) { FreeRunSprite(sprite); free(row); return NULL; }
    }
    if (!pass) {
      u64 allocation = sizeof(*sprite) + ((u64)height + 1) * sizeof(u32) + offset;
      sprite = allocation <= SIZE_MAX ? malloc(allocation) : NULL;
      if (!sprite) { free(row); return NULL; }
      *sprite = (RunSprite){.width = width, .height = height, .stream_size = offset, .references = 1};
    } else rows[height] = offset;
  }
  free(row);
  return sprite;
}

// SDL needs straight-alpha pixels rather than framebuffer palette identities.
// Scaling stays in ResizeRunSprite; this boundary only exports already prepared rows.
bool CopyRunSpriteRgba(const RunSprite *source, u32 *pixels, int stride,
    const u32 *palette, const u32 *animation_palette) {
  assert(source && pixels && stride >= source->width);
  SpritePixel *row = calloc(source->width, sizeof(*row));
  if (!row) return false;
  for (int y = 0; y < source->height; ++y) {
    memset(row, 0, source->width * sizeof(*row));
    int first, last;
    SampleSpriteRow(source, y, 0, row, source->width, 1, 1, palette, &first, &last);
    for (int x = 0; x < source->width; ++x) {
      u32 color = row[x].rgba;
      if (SpriteRunAnimated(row[x].kind))
        color = (color & 0xFF000000) | (animation_palette[row[x].index] & 0xFFFFFF);
      pixels[(size_t)y * stride + (source->mirrored ? source->width - 1 - x : x)] = color;
    }
  }
  free(row);
  return true;
}

// Stream full-screen images through row scratch and the same sprite drawer;
// no full-size scaled backdrop or duplicate screen buffer is retained.
static bool DrawScaledRunSprite(SpriteSurface *surface, const RunSprite *source, CityRect bounds, u32 flags) {
  int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
  CityRect clip = IntersectCityRects(surface->canvas.clip, bounds);
  clip = IntersectCityRects(clip, (CityRect){0, 0, surface->canvas.width, surface->canvas.height});
  if (clip.left >= clip.right || clip.top >= clip.bottom) return true;
  if (width > SPRITE_RUN_LIMIT) return false;
  if (width == source->width && height == source->height) {
    DrawRunSprite(surface, source, bounds.left, bounds.top, flags, NULL);
    return true;
  }
  SpritePreparationWorkspace workspace = {0};
  SpritePixel *row = SpritePreparationPixels(&workspace, (size_t)width + 2 * source->width);
  RunSprite *line = malloc(sizeof(*line) + 2 * sizeof(u32) + (size_t)width * 6 + 3);
  if (!row || !line) { free(line); FreeSpritePreparationWorkspace(&workspace); return false; }
  *line = (RunSprite){.width = width, .height = 1, .references = 1,
      .palette = RetainSpritePalette(source->palette), .mirrored = source->mirrored};
  u32 *rows = (u32 *)(line + 1);
  u8 *data = (u8 *)(rows + 2);
  rows[0] = 0;
  CityRect content = {0, 0, source->width, source->height};
  CityPoint numerator = {width, height}, denominator = {source->width, source->height};
  for (int y = clip.top; y < clip.bottom; ++y) {
    int first, last, relative_y = y - bounds.top;
    SampleSpriteRow(source, (i64)relative_y * source->height / height, 0, row, width,
        width, source->width, source->palette ? source->palette->colors : surface->palette, &first, &last);
    if (first == last) continue;
    SmoothSpriteRowColors(source, row, row + width, content, relative_y, numerator, denominator,
        surface->palette, first, last, true);
    line->stream_size = rows[1] = EncodeSpriteRowRange(row, first, last, 1, data);
    DrawRunSprite(surface, line, bounds.left, y, flags, NULL);
  }
  FreeRunSprite(line);
  FreeSpritePreparationWorkspace(&workspace);
  return true;
}

void ReleaseSpriteImage(SpriteImage *image) {
  FreeRunSprite(image->source);
  FreeRunSprite(image->prepared);
  *image = (SpriteImage){0};
}

// Logical canvas bounds supply the position and clip. Already prepared images hit
// DrawScaledRunSprite's direct draw path; uncached backdrops use only row scratch.
bool DrawCanvasRunSprite(Canvas *canvas, const RunSprite *sprite, CityRect bounds, u32 flags) {
  CityRect box = CanvasRasterImageBounds(canvas, bounds);
  CanvasRaster raster = CanvasRasterView(canvas);
  SpriteSurface surface = {.canvas = *canvas, .indices = canvas->palette_indices,
      .index_stride = canvas->index_stride, .row_flags = canvas->palette_rows};
  surface.canvas.width = raster.width;
  surface.canvas.height = raster.height;
  surface.canvas.clip = IntersectCityRects(box, CanvasRasterRect(canvas, canvas->clip));
  surface.canvas.raster = (CanvasRaster){0};
  return DrawScaledRunSprite(&surface, sprite, box, flags);
}

bool DrawCanvasSpriteImage(Canvas *canvas, SpriteImage *image, const RunSprite *source,
    CityRect content, CityRect bounds, u32 flags) {
  assert(image);
  if (!source) return true;
  CityRect box = CanvasRasterImageBounds(canvas, bounds);
  CityRect clip = IntersectCityRects(box, CanvasRasterRect(canvas, canvas->clip));
  if (clip.left >= clip.right || clip.top >= clip.bottom) return true;
  int width = box.right - box.left, height = box.bottom - box.top;
  if (image->source != source || !image->prepared ||
      image->prepared->width != width || image->prepared->height != height ||
      memcmp(&image->content, &content, sizeof(content))) {
    RunSprite *prepared = ResizeRunSpriteRegion(source, content, width, height);
    if (!prepared) return false;
    RunSprite *retained = RetainRunSprite(source);
    ReleaseSpriteImage(image);
    *image = (SpriteImage){retained, prepared, content};
  }
  return DrawCanvasRunSprite(canvas, image->prepared, bounds, flags);
}

// Generated bitmaps use ordinary runs. Release temporary pixels immediately; repeated paints reuse the image.
bool DrawCachedCanvasBitmap(Canvas *canvas, SpriteBitmapCache *cache, const CanvasBitmapPaint *paint) {
  CityRect box = CanvasRasterImageBounds(canvas, paint->bounds);
  CityRect clip = IntersectCityRects(box, CanvasRasterRect(canvas, canvas->clip));
  if (clip.left >= clip.right || clip.top >= clip.bottom) return true;
  int width = box.right - box.left, height = box.bottom - box.top;
  if (width > SCREEN_DAMAGE_MAX_WIDTH || height > SCREEN_DAMAGE_MAX_HEIGHT) return false;
  if (!cache->valid || !cache->sprite || cache->sprite->width != width || cache->sprite->height != height) {
    u32 *pixels = calloc((size_t)width * height, sizeof(*pixels));
    if (!pixels) return false;
    Canvas target = {.pixels = pixels, .width = width, .height = height, .stride = width,
        .clip = {0, 0, width, height}};
    paint->build(&target, paint->context);
    RunSprite *sprite = EncodeRgbaRunSprite(pixels, width, height, width);
    free(pixels);
    if (!sprite) return false;
    FreeRunSprite(cache->sprite);
    cache->sprite = sprite;
    cache->valid = true;
  }
  return DrawCanvasRunSprite(canvas, cache->sprite, paint->bounds, 0);
}
