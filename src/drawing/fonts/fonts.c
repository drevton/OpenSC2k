#include "base/byte_order.h"
#include "drawing/fonts/fonts.h"
#include "base/text.h"

#include <stdlib.h>
#include <string.h>

#include "drawing/raster/canvas_raster_internal.h"

// The baked canvas atlas uses Windows-1252 glyph indices; native strings remain UTF-8.
u8 ReadCityFontCharacter(const char **text) {
  return EncodeWindows1252Character(ReadUtf8Character(text));
}

// Measure the fixed canvas font in logical pixels; scaling and wrapping belong to the caller.
i32 CanvasFontTextWidth(const char *text, const CityCanvasGlyph *font) {
  i32 width = 0;
  while (*text != '\0')
    width += font[ReadCityFontCharacter(&text)].advance;
  return width;
}

i32 CanvasTextWidth(const char *text) {
  return CanvasFontTextWidth(text, kCanvasFont);
}

static const CityCanvasGlyph *CanvasNextGlyph(const char **text) {
  return &kCanvasFont[ReadCityFontCharacter(text)];
}

void CanvasTextFont(Canvas *canvas, i32 x, i32 y, const char *text, u32 color, i32 scale,
    const CityCanvasGlyph *font) {
  if (font == kCanvasFont && scale == 1 && canvas->interface_font && canvas->interface_font->ready) {
    DrawCityInterfaceText(canvas->interface_font, canvas, x, y, text, color, false);
    return;
  }
  while (*text != '\0') {
    const CityCanvasGlyph *glyph = &font[ReadCityFontCharacter(&text)];
    for (i32 row = 0; row < glyph->height; ++row)
      for (i32 bit = 0; bit < glyph->width; ++bit)
        if ((glyph->rows[row] & (1u << bit)) != 0) {
          i32 left = x + (glyph->offset_x + bit) * scale;
          i32 top = y + (glyph->offset_y + row) * scale;
          CanvasFill(canvas, left, top, left + scale, top + scale, color);
        }
    x += glyph->advance * scale;
  }
}

static void CanvasTextScaled(Canvas *canvas, i32 x, i32 y, const char *text, u32 color, i32 scale) {
  CanvasTextFont(canvas, x, y, text, color, scale, kCanvasFont);
}

void CanvasTextAt(Canvas *canvas, i32 x, i32 y, const char *text, u32 color) {
  CanvasTextScaled(canvas, x, y, text, color, 1);
}

i32 CanvasTextWrappedScaled(Canvas *canvas, i32 x, i32 y, i32 right, i32 bottom,
    const char *text, u32 color, i32 scale, i32 line_height, i32 alignment) {
  i32 available = (right - x) / scale;
  if (available < 1)
    return 0;
  i32 start_y = y;
  CityRect previous_clip = {0};
  if (canvas != NULL) {
    previous_clip = canvas->clip;
    canvas->clip = IntersectCityRects(canvas->clip, (CityRect){x, y, right, bottom});
  }
  // Leave room for accents above the font's ordinary eight-pixel ascender.
  y += 2 * scale;
  line_height = IntMax(line_height, 12 * scale);
  while (*text != '\0' && y + 10 * scale <= bottom) {
    char line[256];
    i32 length = 0;
    i32 width = 0;
    const char *last_space = NULL;
    i32 last_space_length = 0;
    while (*text == ' ') ++text;
    while (*text != '\0' && *text != '\n' && *text != '\r') {
      const char *next = text;
      const CityCanvasGlyph *glyph = CanvasNextGlyph(&next);
      i32 bytes = (i32)(next - text);
      if (width + glyph->advance > available || length + bytes >= (i32)sizeof(line)) {
        if (last_space != NULL) {
          text = last_space + 1;
          length = last_space_length;
        } else if (length == 0) {
          text = next;
        }
        break;
      }
      if (*text == ' ') {
        last_space = text;
        last_space_length = length;
      }
      memcpy(line + length, text, (size_t)bytes);
      length += bytes;
      width += glyph->advance;
      text = next;
    }
    line[length] = '\0';
    i32 line_x = x;
    i32 line_width = CanvasTextWidth(line) * scale;
    if (alignment == CITY_CANVAS_TEXT_CENTER) line_x += (right - x - line_width) / 2;
    else if (alignment == CITY_CANVAS_TEXT_RIGHT) line_x = right - line_width;
    if (canvas != NULL)
      CanvasTextScaled(canvas, line_x, y, line, color, scale);
    y += line_height;
    if (*text == '\r') {
      ++text;
      if (*text == '\n') ++text;
    } else if (*text == '\n') {
      ++text;
    }
  }
  if (canvas != NULL)
    canvas->clip = previous_clip;
  return y - start_y;
}

void CanvasTextWrapped(Canvas *canvas, i32 x, i32 y, i32 right, i32 bottom,
    const char *text, u32 color) {
  CanvasTextWrappedScaled(canvas, x, y, right, bottom, text, color, 1, 11, 0);
}

void CanvasTextRight(Canvas *canvas, i32 right, i32 y, const char *value, u32 color) {
  CanvasTextAt(canvas, right - CityInterfaceTextWidth(canvas->interface_font, value), y, value, color);
}

// Interface Font

int CityInterfaceTextWidth(const CityInterfaceFont *font, const char *text) {
  if (!font || !font->ready) return CanvasTextWidth(text);
  int width = 0;
  while (*text) width += font->glyphs[0][ReadCityFontCharacter(&text)].advance;
  return width;
}

static int CityInterfaceCharacterWidth(const CityInterfaceFont *font, int character) {
  if (font && font->ready) return font->glyphs[0][character].advance;
  // ReadCityFontCharacter already resolved the Windows-1252 glyph index.
  return kCanvasFont[character].advance;
}

int CityInterfaceTextPrefixWidth(const CityInterfaceFont *font, const char *text, int bytes) {
  const char *start = text;
  int width = 0;
  while (*text && text - start < bytes) {
    int character = ReadCityFontCharacter(&text);
    if (text - start > bytes) break;
    width += CityInterfaceCharacterWidth(font, character);
  }
  return width;
}

int CityInterfaceTextCaretAt(const CityInterfaceFont *font, const char *text, int x) {
  const char *start = text;
  int width = 0;
  while (*text) {
    const char *cell = text;
    width += CityInterfaceCharacterWidth(font, ReadCityFontCharacter(&text));
    if (x < width) return cell - start;
  }
  return text - start;
}

void DrawCityInterfaceText(const CityInterfaceFont *font, Canvas *canvas, int x, int y,
    const char *text, u32 color, bool shadow) {
  DrawCityInterfaceTextSized(font, canvas, x, y, text, color, shadow, 10);
}

void DrawCityInterfaceTextCentered(const CityInterfaceFont *font, Canvas *canvas, CityRect bounds,
    const char *text, u32 color, bool shadow, int size) {
  if (CityCanvasButtonPressed(canvas, bounds)) {
    ++bounds.left; ++bounds.right; ++bounds.top; ++bounds.bottom;
  }
  int first = 11, last = 0;
  if (font && font->ready) {
    const char *character = text;
    while (*character) {
      const CityInterfaceGlyph *glyph = &font->glyphs[0][ReadCityFontCharacter(&character)];
      for (int row = 0; row < glyph->height; ++row) {
        if (!(glyph->ink[row] | (shadow ? glyph->shadow[row] : 0))) continue;
        first = IntMin(first, row);
        last = IntMax(last, row + 1);
      }
    }
  }
  if (!last) { first = 0; last = 10; }
  int width = (CityInterfaceTextWidth(font, text) * size + 9) / 10;
  int height = ((last - first) * size + 9) / 10;
  int y = bounds.top + bounds.bottom - height - first * size * 2 / 10;
  // Keep half a logical pixel in both axes when caption and text have different parity.
  Canvas centered = *canvas;
  centered.raster = CanvasRasterView(canvas);
  centered.raster.origin_x *= 2;
  centered.raster.origin_y *= 2;
  centered.raster.denominator *= 2;
  centered.width *= 2;
  centered.height *= 2;
  centered.clip = (CityRect){canvas->clip.left * 2, canvas->clip.top * 2,
      canvas->clip.right * 2, canvas->clip.bottom * 2};
  DrawCityInterfaceTextSized(font, &centered, bounds.left + bounds.right - width, y,
      text, color, shadow, size * 2);
}

void DrawCityInterfaceTextInRow(const CityInterfaceFont *font, Canvas *canvas, CityRect bounds,
    const char *text, u32 color) {
  bounds.right = bounds.left + CityInterfaceTextWidth(font, text);
  DrawCityInterfaceTextCentered(font, canvas, bounds, text, color, false, 10);
}

// Cached coverage glyphs are composited directly into the final framebuffer.
// Sprite and coverage fonts share logical advances; only the raster detail changes.
static void DrawCityCoverageText(const CityInterfaceFont *font, Canvas *canvas, int x, int y,
    const char *text, u32 color, bool shadow, int size, bool bold) {
  CanvasRaster r = CanvasRasterView(canvas);
  CityRect clip = CanvasRasterRect(canvas, canvas->clip);
  int pixels = (r.numerator * size + r.denominator / 2) / r.denominator;
  pixels = IntMax(CITY_COVERAGE_FONT_MIN, IntMin(CITY_COVERAGE_FONT_MAX, pixels));
  int top = CanvasRasterEdge(y, r.origin_y, r.numerator, r.denominator) + r.offset_y;
  int weight = bold ? IntMax(1, pixels / 16) : 0;
  int shade = bold ? 2 : IntMax(1, (r.numerator * size + r.denominator * 10 - 1) / (r.denominator * 10));
  for (int pass = shadow ? 0 : 1; pass < 2; ++pass) {
    const char *character = text;
    int position = x * 10;
    u32 ink = pass ? color : 0xFF101010;
    while (*character) {
      int code = ReadCityFontCharacter(&character);
      const CityCoverageGlyph *glyph = &font->coverage[pixels - CITY_COVERAGE_FONT_MIN][code];
      int left = CanvasRasterEdge(position, r.origin_x * 10, r.numerator, r.denominator * 10) + r.offset_x;
      int dx = left + (pass ? 0 : shade), dy = top + (pass ? 0 : shade);
      int first_x = IntMax(0, clip.left - dx), last_x = IntMin(glyph->width + weight, clip.right - dx);
      int first_y = IntMax(0, clip.top - dy), last_y = IntMin(glyph->height, clip.bottom - dy);
      const u8 *coverage = font->coverage_pixels + glyph->offset;
      for (int row = first_y; row < last_y; ++row)
        for (int col = first_x; col < last_x; ++col) {
          // Widen the cached coverage once, preserving the original glyph advances.
          int alpha = col < glyph->width ? coverage[row * glyph->width + col] : 0;
          if (weight)
            for (int sample = IntMax(0, col - weight); sample < IntMin(col, glyph->width); ++sample)
              alpha = IntMax(alpha, coverage[row * glyph->width + sample]);
          if (!canvas->font_antialias) alpha = alpha >= 128 ? 255 : 0;
          if (!alpha) continue;
          int px = dx + col, py = dy + row;
          size_t index = (size_t)py * canvas->stride + px;
          u32 value = ink;
          if (alpha != 255) {
            u32 background = canvas->pixels[index];
            if (canvas->palette_indices)
              RecordCanvasPaletteComposite(canvas, py, px, px + 1,
                  CanvasPaletteIndexMask(canvas->palette_indices[(size_t)py * canvas->index_stride + px]));
            value = 0xFF000000;
            for (int shift = 0; shift <= 16; shift += 8)
              value |= (((ink >> shift & 255) * alpha + (background >> shift & 255) * (255 - alpha) + 127) / 255)
                  << shift;
          }
          canvas->pixels[index] = value;
          if (canvas->palette_indices && (!canvas->palette_rows || canvas->palette_rows[py]))
            canvas->palette_indices[(size_t)py * canvas->index_stride + px] = 0;
        }
      position += font->glyphs[0][code].advance * size;
    }
  }
}

void DrawCityInterfaceTextSized(const CityInterfaceFont *font, Canvas *canvas, int x, int y,
    const char *text, u32 color, bool shadow, int size) {
  if (!font || !font->ready) {
    if (shadow) CanvasTextAt(canvas, x + 1, y + 1, text, 0xFF101010);
    CanvasTextAt(canvas, x, y, text, color);
    return;
  }
  if (canvas->scalable_font && font->coverage_pixels) {
    DrawCityCoverageText(font, canvas, x, y, text, color, shadow, size, false);
    return;
  }
  CanvasRaster raster = CanvasRasterView(canvas);
  int numerator = raster.numerator * size, denominator = raster.denominator * 10;
  int variant = numerator <= denominator ? 0 : numerator <= denominator * 2 ? 1 : 2;
  int scale = 1 << variant;
  Canvas glyph_canvas = *canvas;
  glyph_canvas.raster = raster;
  int units = scale * 10;
  glyph_canvas.raster.origin_x *= units;
  glyph_canvas.raster.origin_y *= units;
  glyph_canvas.raster.denominator *= units;
  glyph_canvas.width *= units;
  glyph_canvas.height *= units;
  glyph_canvas.clip = (CityRect){canvas->clip.left * units, canvas->clip.top * units,
      canvas->clip.right * units, canvas->clip.bottom * units};
  x *= units; y *= units;
  while (*text) {
    const CityInterfaceGlyph *glyph = &font->glyphs[variant][ReadCityFontCharacter(&text)];
    for (int row = 0; row < glyph->height; ++row) {
      u64 ink = glyph->ink[row], shade = shadow ? glyph->shadow[row] : 0;
      for (int bit = 0; bit < glyph->width; ++bit) {
        if (ink & ((u64)1 << bit))
          CanvasFill(&glyph_canvas, x + bit * size, y + row * size, x + (bit + 1) * size, y + (row + 1) * size, color);
        else if (shade & ((u64)1 << bit))
          CanvasFill(&glyph_canvas, x + bit * size, y + row * size,
              x + (bit + 1) * size, y + (row + 1) * size, 0xFF101010);
      }
    }
    x += glyph->advance * scale * size;
  }
}

void DrawCityInterfaceTextBold(const CityInterfaceFont *font, Canvas *canvas, int x, int y,
    const char *text, u32 color, int size) {
  if (font && font->ready && font->coverage_pixels)
    DrawCityCoverageText(font, canvas, x, y, text, color, true, size, true);
  else DrawCityInterfaceTextSized(font, canvas, x, y, text, color, true, size);
}

void DrawCityInterfaceClose(const CityInterfaceFont *font, Canvas *canvas, CityRect bounds, u32 color) {
  if (!font || !font->ready) {
    static const u8 rows[8] = {0xC6, 0xEE, 0x7C, 0x38, 0x38, 0x7C, 0xEE, 0xC6};
    int left = bounds.left + (bounds.right - bounds.left - 7) / 2;
    int top = bounds.top + (bounds.bottom - bounds.top - 8) / 2;
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < 7; ++x)
        if (rows[y] & (0x80 >> x))
          CanvasFill(canvas, left + x, top + y, left + x + 1, top + y + 1, color);
    return;
  }
  CanvasRaster raster = CanvasRasterView(canvas);
  int variant = raster.numerator <= raster.denominator ? 0 :
      raster.numerator <= raster.denominator * 2 ? 1 : 2;
  int scale = 1 << variant;
  const CityInterfaceGlyph *glyph = &font->glyphs[variant][CITY_INTERFACE_FONT_CLOSE];
  Canvas pixels = *canvas;
  pixels.raster = raster;
  pixels.raster.origin_x *= scale;
  pixels.raster.origin_y *= scale;
  pixels.raster.denominator *= scale;
  pixels.width *= scale;
  pixels.height *= scale;
  pixels.clip = (CityRect){canvas->clip.left * scale, canvas->clip.top * scale,
      canvas->clip.right * scale, canvas->clip.bottom * scale};
  int left = (bounds.left + (bounds.right - bounds.left - 7) / 2) * scale;
  int top = (bounds.top + (bounds.bottom - bounds.top - 8) / 2) * scale;
  for (int y = 0; y < glyph->height; ++y)
    for (int x = 0; x < glyph->width; ++x)
      if (glyph->ink[y] & ((u64)1 << x))
        CanvasFill(&pixels, left + x, top + y, left + x + 1, top + y + 1, color);
}

// Overlay Font

const CityOverlayGlyph *CityOverlayFontGlyphs(const CityOverlayFont *font, int zoom) {
  if (zoom < 0 || zoom >= CITY_OVERLAY_FONT_COUNT) zoom = 0;
  return font->glyphs + zoom * CITY_OVERLAY_GLYPH_COUNT;
}

// Publication typography uses baked coverage at the final framebuffer scale.
// Advances come from the 32-pixel master so wrapping is independent of display scale.
static const u8 *PublicationGlyph(const CityInterfaceFont *font, CityPublicationFace face, int variant, int code) {
  return font->newspaper_atlas + ((face * kPublicationSizeCount + variant) * 256 + code) * kPublicationGlyphBytes;
}

int CityPublicationTextWidth(const CityInterfaceFont *font, const char *text, int bytes,
    CityPublicationFace face, int size) {
  if (!font || !font->newspaper_atlas)
    return CityInterfaceTextPrefixWidth(font, text, bytes < 0 ? (int)strlen(text) : bytes);
  const char *start = text;
  int width = 0;
  while (*text && (bytes < 0 || text - start < bytes)) {
    int code = ReadCityFontCharacter(&text);
    if (bytes >= 0 && text - start > bytes) break;
    const u8 *glyph = PublicationGlyph(font, face, 10, code);
    width += glyph[10] | glyph[11] << 8;
  }
  return (width * size + 2047) / 2048;
}

void DrawCityPublicationText(const CityInterfaceFont *font, Canvas *canvas, int x, int y,
    const char *text, int bytes, CityPublicationFace face, int size, u32 color) {
  if (!font || !font->newspaper_atlas) {
    char line[2048];
    int remaining = bytes;
    // CanvasTextAt selects the canvas font when the supplied interface font is unavailable.
    const CityInterfaceFont *fallback = font && font->ready ? font : canvas->interface_font;
    while (*text && remaining != 0) {
      int limit = sizeof(line) - 1;
      if (remaining > 0) limit = IntMin(limit, remaining);
      int length = Utf8PrefixBytes(text, limit);
      if (!length) break;
      memcpy(line, text, length);
      line[length] = 0;
      DrawCityInterfaceText(font, canvas, x, y, line, color, false);
      text += length;
      if (remaining > 0) remaining -= length;
      if (*text && remaining != 0) x += CityInterfaceTextWidth(fallback, line);
    }
    return;
  }
  CanvasRaster r = CanvasRasterView(canvas);
  CityRect clip = CanvasRasterRect(canvas, canvas->clip);
  int pixels = IntMax(1, (size * r.numerator + r.denominator / 2) / r.denominator);
  int variant = 0;
  for (int i = 1; i < kPublicationSizeCount; ++i)
    if (abs(kPublicationSizes[i] - pixels) < abs(kPublicationSizes[variant] - pixels)) variant = i;
  int source_size = kPublicationSizes[variant];
  int baseline = CanvasRasterEdge(y + size, r.origin_y, r.numerator, r.denominator) + r.offset_y;
  int position = 0;
  const char *start = text;
  while (*text && (bytes < 0 || text - start < bytes)) {
    int code = ReadCityFontCharacter(&text);
    if (bytes >= 0 && text - start > bytes) break;
    const u8 *glyph = PublicationGlyph(font, face, variant, code);
    int left = CanvasRasterEdge(x * 2048 + position * size, r.origin_x * 2048,
        r.numerator, r.denominator * 2048) + r.offset_x + (i16)(glyph[6] | glyph[7] << 8) * pixels / source_size;
    int top = baseline + (i16)(glyph[8] | glyph[9] << 8) * pixels / source_size;
    int width = (glyph[4] * pixels + source_size - 1) / source_size;
    int height = (glyph[5] * pixels + source_size - 1) / source_size;
    const u8 *coverage = font->newspaper_atlas + kPublicationMetadataBytes + ReadLE32(glyph);
    for (int row = IntMax(0, clip.top - top); row < IntMin(height, clip.bottom - top); ++row)
      for (int col = IntMax(0, clip.left - left); col < IntMin(width, clip.right - left); ++col) {
        int alpha = coverage[(row * source_size / pixels) * glyph[4] + col * source_size / pixels];
        if (!alpha) continue;
        size_t index = (size_t)(top + row) * canvas->stride + left + col;
        u32 result = color;
        if (alpha != 255) {
          u32 background = canvas->pixels[index];
          if (canvas->palette_indices)
            RecordCanvasPaletteComposite(canvas, top + row, left + col, left + col + 1,
                CanvasPaletteIndexMask(canvas->palette_indices[
                    (size_t)(top + row) * canvas->index_stride + left + col]));
          result = 0xFF000000;
          for (int shift = 0; shift <= 16; shift += 8)
            result |= (((color >> shift & 255) * alpha + (background >> shift & 255) * (255 - alpha) + 127) / 255)
                << shift;
        }
        canvas->pixels[index] = result;
        if (canvas->palette_indices && (!canvas->palette_rows || canvas->palette_rows[top + row]))
          canvas->palette_indices[(size_t)(top + row) * canvas->index_stride + left + col] = 0;
      }
    const u8 *advance = PublicationGlyph(font, face, 10, code);
    position += advance[10] | advance[11] << 8;
  }
}
