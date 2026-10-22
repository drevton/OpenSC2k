#ifndef SC2K_ASSET_CURSORS_H
#define SC2K_ASSET_CURSORS_H

#include "city/types.h"

// Cursor Assets

// canvas-cursors.dat: eight-byte SC2KCUR1 magic, five little-endian u32
// dimensions/count, then little-endian ARGB pixels. Badge, white arrow,
// gold arrow and generated OpenGFX2 crop order. No padding or trailing bytes.
#define SC2K_CURSOR_ASSET_MAGIC "SC2KCUR1"
#define SC2K_CURSOR_ASSET_PATH "ui/opengfx2/cursors/canvas-cursors.dat"

// OpenGFX2 crop order from tools/generate_cursor_sprites.py. Unidentified
// artwork keeps its source sheet cell coordinates rather than a guessed name.
// Cell8,6 is deliberately present twice in the generated crop catalog.
typedef enum CityCanvasCursorIcon {
  CITY_CANVAS_CURSOR_ICON_RAIL = 0, CITY_CANVAS_CURSOR_ICON_ROAD = 1,
  CITY_CANVAS_CURSOR_ICON_DEMOLISH = 2, CITY_CANVAS_CURSOR_ICON_RAISE = 3,
  CITY_CANVAS_CURSOR_ICON_LOWER = 4, CITY_CANVAS_CURSOR_ICON_LEVEL = 5,
  CITY_CANVAS_CURSOR_ICON_TREE = 6, CITY_CANVAS_CURSOR_ICON_WATER = 7,
  CITY_CANVAS_CURSOR_ICON_CELL_7_7 = 8, CITY_CANVAS_CURSOR_ICON_CELL_6_7 = 9,
  CITY_CANVAS_CURSOR_ICON_CELL_8_6 = 10, CITY_CANVAS_CURSOR_ICON_CELL_2_7 = 11,
  CITY_CANVAS_CURSOR_ICON_QUERY = 12, CITY_CANVAS_CURSOR_ICON_SIGN = 13,
  CITY_CANVAS_CURSOR_ICON_CELL_4_5 = 14, CITY_CANVAS_CURSOR_ICON_WAREHOUSE = 15,
  CITY_CANVAS_CURSOR_ICON_CELL_6_6 = 16, CITY_CANVAS_CURSOR_ICON_CELL_8_6_DUPLICATE = 17,
  CITY_CANVAS_CURSOR_ICON_CELL_2_8 = 18, CITY_CANVAS_CURSOR_ICON_CELL_7_8 = 19,
  CITY_CANVAS_CURSOR_ICON_CELL_5_13 = 20, CITY_CANVAS_CURSOR_ICON_CELL_8_8 = 21,
  CITY_CANVAS_CURSOR_ICON_FAST_FORWARD = 22, CITY_CANVAS_CURSOR_ICON_BUOY = 23,
  CITY_CANVAS_CURSOR_ICON_COUNT = 24,
} CityCanvasCursorIcon;

enum {
  SC2K_CURSOR_ASSET_HEADER_SIZE = 28,
  SC2K_CURSOR_ASSET_SOURCE_SIZE = 72,
  SC2K_CURSOR_ASSET_ICON_SIZE = 40,
  SC2K_CURSOR_ASSET_ICON_COUNT = 24,
  SC2K_CURSOR_ASSET_SPRITE_PIXELS = 72 * 72,
  SC2K_CURSOR_ASSET_ICON_PIXELS = 40 * 40,
  SC2K_CURSOR_ASSET_BADGE_OFFSET = 0,
  SC2K_CURSOR_ASSET_ARROW_OFFSET = SC2K_CURSOR_ASSET_SPRITE_PIXELS,
  SC2K_CURSOR_ASSET_DEFAULT_OFFSET = 2 * SC2K_CURSOR_ASSET_SPRITE_PIXELS,
  SC2K_CURSOR_ASSET_ICONS_OFFSET = 3 * SC2K_CURSOR_ASSET_SPRITE_PIXELS,
  SC2K_CURSOR_ASSET_PIXEL_COUNT = SC2K_CURSOR_ASSET_ICONS_OFFSET +
      SC2K_CURSOR_ASSET_ICON_COUNT * SC2K_CURSOR_ASSET_ICON_PIXELS,
};

// Cursor State

// Immutable source artwork belongs to Assets; composed palette-dependent
// tool artwork belongs to Drawing. Neither is saved City state.
typedef struct {
  u32 *pixels;
  u32 fallback[SC2K_CURSOR_ASSET_SPRITE_PIXELS];
  bool fallback_ready;
} CityCursorAssets;

// Source loading replaces only Assets-owned pixels after validation. Game coordinates the composed Drawing cache.
bool LoadCityCursorArtwork(CityAssets *assets, const char *graphics_root);
void ReleaseCityCursorArtwork(CityAssets *assets);

#endif
