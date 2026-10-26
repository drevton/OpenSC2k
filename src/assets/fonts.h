#ifndef SC2K_ASSET_FONTS_H
#define SC2K_ASSET_FONTS_H

#include "city/types.h"
#include "base/byte_order.h"

// Immutable canvas glyphs borrow shared, tightly stored row masks.
typedef struct {
  u8 width, height, advance;
  i8 offset_x, offset_y;
  const u16 *rows;
} CityCanvasGlyph;

extern const CityCanvasGlyph kCanvasFont[256];
extern const CityCanvasGlyph kCanvasHeadingFont[256];

// Interface Font

enum { CITY_INTERFACE_FONT_VARIANTS = 3, CITY_INTERFACE_FONT_GLYPHS = 257, CITY_INTERFACE_FONT_CLOSE = 256,
       CITY_INTERFACE_FONT_HEIGHT = 44, CITY_COVERAGE_FONT_MIN = 5, CITY_COVERAGE_FONT_MAX = 55,
       CITY_COVERAGE_FONT_SIZES = CITY_COVERAGE_FONT_MAX - CITY_COVERAGE_FONT_MIN + 1 };
typedef struct {
  u8 width, height, advance;
  u64 ink[CITY_INTERFACE_FONT_HEIGHT], shadow[CITY_INTERFACE_FONT_HEIGHT];
} CityInterfaceGlyph;

typedef struct {
  u32 offset;
  u8 width, height;
} CityCoverageGlyph;

typedef struct CityInterfaceFont {
  CityInterfaceGlyph glyphs[CITY_INTERFACE_FONT_VARIANTS][CITY_INTERFACE_FONT_GLYPHS];
  CityCoverageGlyph coverage[CITY_COVERAGE_FONT_SIZES][256];
  u8 *coverage_pixels;
  u8 *newspaper_atlas; // Immutable coverage glyphs, owned by Assets.
  bool ready;
  u8 max_advance; // Largest logical advance in glyphs[0], including retained glyphs after a failed load.
} CityInterfaceFont;

// Load retained bytes, or an explicit loose source for diagnostics. Failure to open the base font preserves it.
// After opening, clear ready and retain glyph/coverage writes on later failure. Each pixel atlas replaces its
// previous allocation only after validation; ready requires base glyphs, coverage and publication fonts to load.
bool LoadCityInterfaceFont(CityAssets *assets, const char *graphics_root);
// Release both dynamic atlases and clear ready. Inline glyph metadata and its maximum advance remain in Assets.
void ReleaseCityInterfaceFont(CityInterfaceFont *font);
typedef enum CityPublicationFace {
  CITY_PUBLICATION_BODY,
  CITY_PUBLICATION_HEADLINE,
  CITY_PUBLICATION_MASTHEAD,
} CityPublicationFace;

enum { kPublicationSizeCount = 22, kPublicationGlyphBytes = 12,
       kPublicationMetadataBytes = 3 * kPublicationSizeCount * 256 * kPublicationGlyphBytes,
       CITY_PUBLICATION_MASTER_VARIANT = 10, CITY_PUBLICATION_MASTER_UNITS = 32 * 64 };
extern const u8 kPublicationSizes[kPublicationSizeCount];

// One borrowed glyph from the validated publication atlas. Advance uses 1/64 pixels at that variant's size;
// text layout uses the 32-pixel master variant. Coverage is tightly packed width-by-height, valid until replacement.
typedef struct CityPublicationGlyph {
  const u8 *coverage;
  i16 offset_x, offset_y;
  u16 advance;
  u8 width, height;
} CityPublicationGlyph;

// Font must have a publication atlas and face/variant/code must be valid. Decode only this record;
// Keep the reader inline so advance-only measurement omits unused field decoding in size-optimized builds too.
static SC2K_FORCE_INLINE CityPublicationGlyph ReadCityPublicationGlyph(const CityInterfaceFont *font,
    CityPublicationFace face, int variant, int code) {
  const u8 *record = font->newspaper_atlas +
      ((face * kPublicationSizeCount + variant) * 256 + code) * kPublicationGlyphBytes;
  return (CityPublicationGlyph){
      .coverage = font->newspaper_atlas + kPublicationMetadataBytes + ReadLE32(record),
      .offset_x = (i16)ReadLE16(record + 6), .offset_y = (i16)ReadLE16(record + 8),
      .advance = ReadLE16(record + 10), .width = record[4], .height = record[5]};
}

enum { CITY_OVERLAY_FONT_FIRST = 32, CITY_OVERLAY_FONT_COUNT = 3,
       CITY_OVERLAY_GLYPH_COUNT = 224, CITY_OVERLAY_GLYPH_HEIGHT = 32 };
typedef struct {
  u8 advance;
  u8 paint_advance;
  i8 origin_offset;
  u32 rows[CITY_OVERLAY_GLYPH_HEIGHT];
} CityOverlayGlyph;

// SC2KFNT1: eight-byte magic, seven little-endian u32 fields, then font-major
// glyph records: u8 advance, u8 paint advance, i8 origin, 32 little-endian rows.
// No host structure padding is stored. Bake with tools/build_overlay_font.py.
#define SC2K_OVERLAY_FONT_ASSET_PATH "fonts/overlay-font.dat"
#define SC2K_OVERLAY_FONT_ASSET_MAGIC "SC2KFNT1"
enum { SC2K_OVERLAY_FONT_ASSET_VERSION = 1, SC2K_OVERLAY_FONT_CODE_PAGE = 1252,
       SC2K_OVERLAY_FONT_HEADER_SIZE = 36, SC2K_OVERLAY_FONT_RECORD_SIZE = 131 };

typedef struct {
  CityOverlayGlyph glyphs[CITY_OVERLAY_FONT_COUNT * CITY_OVERLAY_GLYPH_COUNT];
  u8 max_advance[CITY_OVERLAY_FONT_COUNT]; // Largest logical advance in each zoom's glyph bank.
} CityOverlayFont;

// Initialize bounded fallback glyphs before loading. Glyph data belongs to Assets and stays valid until replaced.
void InitializeCityOverlayFont(CityOverlayFont *font);
// Missing, malformed or incomplete input preserves prior glyphs. Success replaces the entire inline font.
bool LoadCityOverlayFont(CityAssets *assets, const char *graphics_root);

#endif
