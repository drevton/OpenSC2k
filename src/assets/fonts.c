#include "assets/fonts.h"
#include "base/byte_order.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "assets/asset_file.h"
#include "assets/assets.h"
#include "assets/state.h"

#include "assets/canvas_font.inc"
#include "assets/canvas_heading_font.inc"

static bool LoadCityPublicationFont(CityInterfaceFont *font, CityAssets *assets, const char *root) {
  AssetFile file;
  int opened = OpenAssetSource(&file, assets, CITY_ASSET_NEWSPAPER_FONT, root);
  if (opened != 1) return false;
  u8 header[24], sizes[kPublicationSizeCount];
  bool valid = ReadAssetFile(&file, header, sizeof(header)) == sizeof(header) &&
      memcmp(header, "SC2KNPF1", 8) == 0 && ReadLE32(header + 8) == 3 &&
      ReadLE32(header + 12) == kPublicationSizeCount &&
      ReadAssetFile(&file, sizes, sizeof(sizes)) == sizeof(sizes) && !memcmp(sizes, kPublicationSizes, sizeof(sizes));
  u32 bytes = valid ? ReadLE32(header + 16) : 0, packed_size = valid ? ReadLE32(header + 20) : 0;
  valid = valid && bytes > kPublicationMetadataBytes && bytes <= 64 * 1024 * 1024 &&
      packed_size > 0 && packed_size <= bytes;
  u8 *atlas = valid ? malloc(bytes) : NULL, *packed = valid ? malloc(packed_size) : NULL;
  valid = atlas && packed && ReadAssetFile(&file, packed, packed_size) == packed_size &&
      ReadAssetByte(&file) == EOF && !file.failed;
  if (!CloseAssetFile(&file)) valid = false;
  uLongf expanded = bytes;
  valid = valid && uncompress(atlas, &expanded, packed, packed_size) == Z_OK && expanded == bytes;
  free(packed);
  for (int i = 0; valid && i < kPublicationMetadataBytes; i += kPublicationGlyphBytes) {
    const u8 *glyph = atlas + i;
    u32 offset = ReadLE32(glyph), count = glyph[4] * glyph[5];
    valid = offset <= bytes - kPublicationMetadataBytes && count <= bytes - kPublicationMetadataBytes - offset;
  }
  if (!valid) { free(atlas); return false; }
  free(font->newspaper_atlas);
  font->newspaper_atlas = atlas;
  return true;
}

static bool LoadCityCoverageFont(CityInterfaceFont *font, CityAssets *assets, const char *graphics_root) {
  AssetFile file;
  int opened = OpenAssetSource(&file, assets, CITY_ASSET_COVERAGE_FONT, graphics_root);
  if (opened != 1) return false;
  u8 header[16];
  bool valid = ReadAssetFile(&file, header, sizeof(header)) == sizeof(header) && memcmp(header, "SC2KAAF1", 8) == 0;
  u32 bytes = valid ? ReadLE32(header + 8) : 0;
  u32 encoded = valid ? ReadLE32(header + 12) : 0;
  valid = valid && bytes > 0 && bytes <= 16 * 1024 * 1024 && encoded > 0 && encoded <= bytes * 2 && !(encoded & 1);
  u8 *pixels = valid ? malloc(bytes) : NULL;
  valid = valid && pixels != NULL;
  u32 offset = 0;
  for (int size = CITY_COVERAGE_FONT_MIN; valid && size <= CITY_COVERAGE_FONT_MAX; ++size)
    for (int code = 0; valid && code < 256; ++code) {
      int width = ReadAssetByte(&file), height = ReadAssetByte(&file);
      valid = width == (font->glyphs[0][code].advance * size + 9) / 10 + 2 &&
          height == (11 * size + 9) / 10 && offset <= bytes && (u32)(width * height) <= bytes - offset;
      if (!valid) break;
      font->coverage[size - CITY_COVERAGE_FONT_MIN][code] = (CityCoverageGlyph){offset, width, height};
      offset += width * height;
    }
  valid = valid && offset == bytes;
  offset = 0;
  for (u32 input = 0; valid && input < encoded; input += 2) {
    int count = ReadAssetByte(&file), value = ReadAssetByte(&file);
    valid = count > 0 && value >= 0 && (u32)count <= bytes - offset;
    if (!valid) break;
    memset(pixels + offset, value, count);
    offset += count;
  }
  valid = valid && offset == bytes && ReadAssetByte(&file) == EOF && !file.failed;
  if (!CloseAssetFile(&file)) valid = false;
  if (!valid) { free(pixels); return false; }
  free(font->coverage_pixels);
  font->coverage_pixels = pixels;
  return true;
}

void ReleaseCityInterfaceFont(CityInterfaceFont *font) {
  free(font->coverage_pixels);
  free(font->newspaper_atlas);
  font->newspaper_atlas = NULL;
  font->coverage_pixels = NULL;
  font->ready = false;
}

// SC2KUIF3: 1x/2x/4x, 256 text glyphs plus the close icon. Three metadata bytes followed by
// height pairs of little-endian 64-bit ink/shadow masks. Startup I/O only.
bool LoadCityInterfaceFont(CityAssets *assets, const char *graphics_root) {
  CityInterfaceFont *font = &assets->interface_font;
  AssetFile file;
  int opened = OpenAssetSource(&file, assets, CITY_ASSET_INTERFACE_FONT, graphics_root);
  if (opened != 1) return false;
  font->ready = false;
  u8 header[8];
  bool valid = ReadAssetFile(&file, header, sizeof(header)) == sizeof(header) && memcmp(header, "SC2KUIF3", 8) == 0;
  for (int variant = 0; valid && variant < CITY_INTERFACE_FONT_VARIANTS; ++variant) {
    int scale = 1 << variant;
    for (int code = 0; valid && code < CITY_INTERFACE_FONT_GLYPHS; ++code) {
      u8 metadata[3], rows[CITY_INTERFACE_FONT_HEIGHT * 16];
      valid = ReadAssetFile(&file, metadata, sizeof(metadata)) == sizeof(metadata);
      if (!valid) break;
      int width = metadata[0], height = metadata[1], advance = metadata[2];
      valid = width <= 64 && height == 11 * scale && width == advance * scale && advance > 0;
      if (!valid) break;
      size_t count = height * 16;
      valid = ReadAssetFile(&file, rows, count) == count;
      if (!valid) break;
      CityInterfaceGlyph *glyph = &font->glyphs[variant][code];
      *glyph = (CityInterfaceGlyph){.width = width, .height = height, .advance = advance};
      for (int row = 0; row < height; ++row) {
        for (int byte = 0; byte < 8; ++byte) {
          glyph->ink[row] |= (u64)rows[row * 16 + byte] << (byte * 8);
          glyph->shadow[row] |= (u64)rows[row * 16 + 8 + byte] << (byte * 8);
        }
        if (width < 64 && ((glyph->ink[row] | glyph->shadow[row]) >> width)) valid = false;
        if (glyph->ink[row] & glyph->shadow[row]) valid = false;
      }
    }
  }
  valid = valid && ReadAssetByte(&file) == EOF && !file.failed;
  if (!CloseAssetFile(&file)) valid = false;
  font->ready = valid && LoadCityCoverageFont(font, assets, graphics_root) &&
      LoadCityPublicationFont(font, assets, graphics_root);
  return font->ready;
}

bool LoadCityOverlayFont(CityOverlayFont *font, CityAssets *assets, const char *graphics_root) {
  AssetFile file;
  int opened = OpenAssetSource(&file, assets, CITY_ASSET_OVERLAY_FONT, graphics_root);
  if (opened != 1) return false;
  u8 header[SC2K_OVERLAY_FONT_HEADER_SIZE];
  bool valid = ReadAssetFile(&file, header, sizeof(header)) == sizeof(header) &&
      memcmp(header, SC2K_OVERLAY_FONT_ASSET_MAGIC, 8) == 0 &&
      ReadLE32(header + 8) == SC2K_OVERLAY_FONT_ASSET_VERSION &&
      ReadLE32(header + 12) == SC2K_OVERLAY_FONT_CODE_PAGE &&
      ReadLE32(header + 16) == CITY_OVERLAY_FONT_FIRST &&
      ReadLE32(header + 20) == CITY_OVERLAY_FONT_COUNT &&
      ReadLE32(header + 24) == CITY_OVERLAY_GLYPH_COUNT &&
      ReadLE32(header + 28) == CITY_OVERLAY_GLYPH_HEIGHT &&
      ReadLE32(header + 32) == SC2K_OVERLAY_FONT_RECORD_SIZE;
  const size_t glyph_count = CITY_OVERLAY_FONT_COUNT * CITY_OVERLAY_GLYPH_COUNT;
  const size_t byte_count = glyph_count * SC2K_OVERLAY_FONT_RECORD_SIZE;
  u8 *bytes = valid ? malloc(byte_count) : NULL;
  valid = bytes != NULL && ReadAssetFile(&file, bytes, byte_count) == byte_count &&
      ReadAssetByte(&file) == EOF && !file.failed;
  if (!CloseAssetFile(&file)) valid = false;
  if (!valid) { free(bytes); return false; }
  CityOverlayGlyph *glyphs = font->glyphs;
  memset(font->glyphs, 0, sizeof(font->glyphs));
  for (size_t index = 0; index < glyph_count; ++index) {
    const u8 *record = bytes + index * SC2K_OVERLAY_FONT_RECORD_SIZE;
    glyphs[index].advance = record[0];
    glyphs[index].paint_advance = record[1];
    glyphs[index].origin_offset = (i8)(record[2] < 128 ? record[2] : (i32)record[2] - 256);
    for (int row = 0; row < CITY_OVERLAY_GLYPH_HEIGHT; ++row)
      glyphs[index].rows[row] = ReadLE32(record + 3 + row * 4);
  }
  free(bytes);
  return true;
}

const u8 kPublicationSizes[kPublicationSizeCount] = {
  8, 10, 12, 14, 16, 18, 20, 22, 24, 28, 32, 36, 40, 44, 48, 56, 64, 72, 80, 96, 112, 128
};
