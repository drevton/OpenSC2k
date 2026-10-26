#include "drawing/startup.h"

#include <assert.h>
#include <stdio.h>

#include "assets/graphics.h"
#include "city/maps/kinds.h"
#include "city/maps/tile_storage.h"
#include "city/terrain/terrain.h"
#include "drawing/bitmaps.h"
#include "drawing/canvas.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/raster/canvas_raster_internal.h"
#include "drawing/sprites/preparation.h"

_Static_assert((int)CITY_INTRO_STAGE_COUNT == CITY_INTRO_DIGIT_FIRST, "Intro stage labels must precede the digits");

int CityStartupContentTop(int width, int height) {
  // The baker puts the 170-pixel identity at y=12 in this same background.
  return IntMax(width * 182 / 1024, height * 182 / 683) + 8;
}

// Loading artwork uses the shared row resampler and sprite drawer. Streaming
// avoids retaining a scaled full-screen copy of the backdrop.
static bool DrawIntroImage(const CityIntroArt *art, Canvas *canvas, int id, int left, int top, int scale) {
  const RunSprite *image = art->images[id];
  CityRect bounds = {left, top, left + IntMax(1, image->width * scale / 65536),
      top + IntMax(1, image->height * scale / 65536)};
  return DrawCanvasRunSprite(canvas, image, bounds, 0);
}

bool DrawCityStartupBackdrop(const CityIntroArt *art, Canvas *canvas, CityRect area) {
  const RunSprite *image = art->images[CITY_INTRO_CITY_ART];
  int scale = IntMax((int)(((i64)canvas->width * 65536 + image->width - 1) / image->width),
      (int)(((i64)canvas->height * 65536 + image->height - 1) / image->height));
  Canvas clipped = *canvas;
  clipped.clip = IntersectCityRects(canvas->clip, area);
  return DrawIntroImage(art, &clipped, CITY_INTRO_CITY_ART,
      (canvas->width - image->width * scale / 65536) / 2, 0, scale);
}

CityLoadingPaint PaintCityLoadingScreen(Canvas *canvas, const CityIntroArt *art, CityIntroStage stage,
    int completed, int total, bool initial) {
  assert(art->images[0] && stage >= 0 && stage < CITY_INTRO_STAGE_COUNT);
  // Both screens share the still backdrop and the same baked game identity.
  int scale = IntClamp(IntMin(canvas->width * 65536 / 704, canvas->height * 65536 / 404), 16384, 98304);
  int panel_width = 620 * scale / 65536, panel_height = 160 * scale / 65536;
  int left = (canvas->width - panel_width) / 2;
  int top = (CityStartupContentTop(canvas->width, canvas->height) + canvas->height - panel_height) / 2;
  CityRect panel = {left, top, left + panel_width, top + panel_height};
  CityRect dirty = initial ? canvas->clip : panel;
  bool valid = DrawCityStartupBackdrop(art, canvas, dirty);
  DrawCanvasFrame(canvas, panel, 0xFF97835B, 0xFFD3BB93, 0xFF674F2B, true);
  int label = stage;
  if (!DrawIntroImage(art, canvas, label, (canvas->width - (int)art->images[label]->width * scale / 65536) / 2,
      top + 32 * scale / 65536, scale)) valid = false;
  int percent = total > 0 ? (int)((i64)IntClamp(completed, 0, total) * 100 / total) : 0;
  int bar_width = 360 * scale / 65536;
  int bar_left = (canvas->width - bar_width) / 2, bar_top = top + 82 * scale / 65536;
  DrawCanvasLoadingProgress(canvas,
      (CityRect){bar_left, bar_top, bar_left + bar_width, bar_top + IntMax(3, 4 * scale / 65536)},
      percent, 0xFFFCB030, 0xFF67532B);
  char text[8]; snprintf(text, sizeof(text), "%d%%", percent);
  int percent_width = 0;
  for (const char *c = text; *c; ++c) {
    int image = CITY_INTRO_DIGIT_FIRST + (*c == '%' ? 10 : *c - '0');
    percent_width += art->images[image]->width * scale / 65536;
  }
  int x = (canvas->width - percent_width) / 2;
  for (const char *c = text; *c; ++c) {
    int image = CITY_INTRO_DIGIT_FIRST + (*c == '%' ? 10 : *c - '0');
    if (!DrawIntroImage(art, canvas, image, x, top + 108 * scale / 65536, scale)) valid = false;
    x += art->images[image]->width * scale / 65536;
  }
  return (CityLoadingPaint){dirty, !valid};
}

static const u32 kTerrainFreshWater = 0xFF488CBC;

typedef struct {
  const CityTerrainMap *map;
  CityPoint size;
  int numerator, denominator;
  u32 saltwater_color;
} TerrainPreviewPixels;

static void BuildTerrainPreview(Canvas *pixels, const void *context) {
  const TerrainPreviewPixels *preview = context;
  const CityTerrainMap *map = preview->map;
  int numerator = preview->numerator, denominator = preview->denominator;
  int width = preview->size.x, height = preview->size.y;
  int elevation_range = IntMax(4, map->maximum_height - map->minimum_height);
  for (int py = 0; py < height; ++py) for (int px = 0; px < width; ++px) {
    int x = px * map->width / width, y = py * map->height / height;
    int tile = y * map->width + x;
    int elevation = map->altitude[tile] & CITY_ALTITUDE_LAND_MASK;
    int climate = CityTerrainTileClimate(map, tile);
    static const u32 kClimateColors[] = {
      0xFF9B8747, 0xFF588438, 0xFF648C54, 0xFF859F7F, 0xFFA5B3A7, 0xFFC5D0CF, 0xFFE5EBF7,
      0xFF809C38, 0xFF609030, 0xFFBEBB6E, 0xFFFCDCA4, 0xFF5E8846, 0xFF6C9038, 0xFF729446,
      0xFFA2BAA3, 0xFFACBFA3
    };
    u32 color = kClimateColors[climate];
    if (IsTileKindTree(map->buildings[tile]))
      color = climate >= CLIMATE_DESERT_50 && climate <= CLIMATE_DESERT_100 ? 0xFF608C38 : 0xFF287030;
    if (IsTileKindExposedWater(map->buildings[tile])) {
      color = map->flags[tile] & TF_SALTWATER ? preview->saltwater_color : kTerrainFreshWater;
      color |= 0xFF000000;
    } else {
      // Brighten high ground and light slopes from the upper left. Scale the
      // height contrast to the preview's range so low hills remain visible;
      // the four local samples keep redraw work bounded by preview pixels.
      int west = map->altitude[y * map->width + IntMax(0, x - 1)] & CITY_ALTITUDE_LAND_MASK;
      int east = map->altitude[y * map->width + IntMin(map->width - 1, x + 1)] & CITY_ALTITUDE_LAND_MASK;
      int north = map->altitude[IntMax(0, y - 1) * map->width + x] & CITY_ALTITUDE_LAND_MASK;
      int south = map->altitude[IntMin(map->height - 1, y + 1) * map->width + x] & CITY_ALTITUDE_LAND_MASK;
      int relief = IntMax(-56, IntMin(56, (east + south - west - north) * 14));
      int shade = -20 + (elevation - map->minimum_height) * 84 / elevation_range + relief;
      int red = IntMax(0, IntMin(255, (int)((color >> 16) & 255) + shade));
      int green = IntMax(0, IntMin(255, (int)((color >> 8) & 255) + shade));
      int blue = IntMax(0, IntMin(255, (int)(color & 255) + shade));
      color = 0xFF000000 | red << 16 | green << 8 | blue;
    }
    int left = CanvasRasterEdge(px, 0, numerator, denominator);
    int right = CanvasRasterEdge(px + 1, 0, numerator, denominator);
    int top = CanvasRasterEdge(py, 0, numerator, denominator);
    int bottom = CanvasRasterEdge(py + 1, 0, numerator, denominator);
    for (int y = top; y < bottom; ++y)
      for (int x = left; x < right; ++x) pixels->pixels[(size_t)y * pixels->stride + x] = color;
  }
}

bool DrawCityTerrainPreview(Canvas *canvas, CityTerrainPreviewDrawing *drawing,
    const CityTerrainMap *map, CityRect bounds, u32 saltwater_color) {
  CanvasRaster raster = CanvasRasterView(canvas);
  TerrainPreviewPixels preview = {map, {bounds.right - bounds.left, bounds.bottom - bounds.top},
      raster.numerator, raster.denominator, saltwater_color};
  CanvasBitmapPaint paint = {.bounds = bounds, .build = BuildTerrainPreview, .context = &preview};
  return DrawCachedCanvasBitmap(canvas, &drawing->preview, &paint);
}
