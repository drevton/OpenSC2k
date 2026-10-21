#ifndef SC2K_ASSET_FONTS_H
#define SC2K_ASSET_FONTS_H

#include "city/types.h"

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
} CityInterfaceFont;

// Load retained bytes, or an explicit loose source for diagnostics. Missing or invalid input fails.
bool LoadCityInterfaceFont(CityAssets *assets, const char *graphics_root);
void ReleaseCityInterfaceFont(CityInterfaceFont *font);
typedef enum CityPublicationFace {
  CITY_PUBLICATION_BODY,
  CITY_PUBLICATION_HEADLINE,
  CITY_PUBLICATION_MASTHEAD,
} CityPublicationFace;
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
} CityOverlayFont;

void InitializeCityOverlayFont(CityOverlayFont *font);
bool LoadCityOverlayFont(CityOverlayFont *font, CityAssets *assets, const char *graphics_root);

enum { kPublicationSizeCount = 22, kPublicationGlyphBytes = 12,
       kPublicationMetadataBytes = 3 * kPublicationSizeCount * 256 * kPublicationGlyphBytes };
extern const u8 kPublicationSizes[kPublicationSizeCount];

#endif
