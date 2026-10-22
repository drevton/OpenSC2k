#include "drawing/maps/placement.h"

#include "assets/graphics.h"
#include "city/maps/layers.h"
#include "drawing/maps/map_render.h"
#include "drawing/state.h"
#include "game/state.h"

// Tool placement markers, footprints and map damage.

static CityRect CanvasPreviewBounds(const int points[8]) {
  int left = points[0], right = points[0];
  int top = points[1], bottom = points[1];
  for (int point = 1; point < 4; ++point) {
    int x = points[2 * point];
    int y = points[2 * point + 1];
    left = IntMin(left, x);
    right = IntMax(right, x);
    top = IntMin(top, y);
    bottom = IntMax(bottom, y);
  }
  return (CityRect){left - 2, top - 2, right + 3, bottom + 3};
}

CityRect CityMapPlacementBounds(const City *city, TileIndex tile, bool point) {
  const Drawing *drawing = city->game->drawing;
  const CityMapViewConfig *view = drawing->default_map_view;
  int points[8];
  CityMapTileCorners(city, tile, view->zoom, points);
  int magnify = view->magnified_large_zoom ? 2 : 1;
  for (int i = 0; i < 8; ++i) points[i] *= magnify;
  int id = MAP_POINT_SPRITE_FIRST + view->zoom;
  const MapSpriteCache *cache = &drawing->sprite_cache;
  const RunSprite *sprite = cache->base[id];
  if (point && sprite) {
    // Match the cached sprite's anchor and rounded dimensions at the final
    // pixel scale. A fixed margin around the tile misses magnified dot rows.
    int scale = drawing->map_pixel_scale;
    int numerator = magnify * scale, denominator = cache->base_scales[id];
    int left = points[0] * scale + cache->base_offsets_x[id] * numerator / denominator;
    int top = points[1] * scale - cache->base_baselines[id] * numerator / denominator;
    int right = left + (sprite->width * numerator + denominator - 1) / denominator;
    int bottom = top + (sprite->height * numerator + denominator - 1) / denominator;
    // Round outward when returning to map coordinates, including offscreen points.
    return (CityRect){left / scale - (left % scale < 0), top / scale - (top % scale < 0),
        right / scale + (right % scale > 0), bottom / scale + (bottom % scale > 0)};
  }
  return CanvasPreviewBounds(points);
}

// Damage a main-map marker's existing footprint. Invalid tiles or an unprepared view have no screen footprint.
void InvalidateCityMapPlacement(const City *city, TileIndex tile) {
  Drawing *drawing = city->game->drawing;
  if (!IsMapTileValid(city, tile) || !drawing->default_map_view) return;
  CityRect bounds = CityMapPlacementBounds(city, tile, false);
  int scale = drawing->map_pixel_scale;
  MarkScreenDirty(&drawing->screen_damage,
      (CityRect){bounds.left * scale + drawing->scroll_pixels.x, bounds.top * scale + drawing->scroll_pixels.y,
          bounds.right * scale + drawing->scroll_pixels.x, bounds.bottom * scale + drawing->scroll_pixels.y});
}

void ApplyCityMapSelectionPalette(SpriteSurface *surface, int style) {
  // OpenGFX2 point markers use the OpenTTD grayscale ramp and palette 772 for red.
  // Diamonds use the same offline-projected shapes as the map ground.
  static const u32 kSelectionPalette[256] = {
    [1] = 0xFF101010, [2] = 0xFF202020, [3] = 0xFF303030, [4] = 0xFF404040, [5] = 0xFF505050,
    [6] = 0xFF646464, [7] = 0xFF747474, [8] = 0xFF848484, [9] = 0xFF949494, [10] = 0xFFA8A8A8,
    [11] = 0xFFB8B8B8, [12] = 0xFFC8C8C8, [13] = 0xFFD8D8D8, [14] = 0xFFE8E8E8, [15] = 0xFFFCFCFC,
    [23] = 0xFFCCD0DC,
    [0xA9] = 0xFFFCF4EC, // OpenGFX2 point marker's cream center.
    [0xB2] = 0xFF3C0000, [0xB3] = 0xFF5C0000, [0xB4] = 0xFF800000, [0xB5] = 0xFFA00000,
    [0xA3] = 0xFFD43434, [0xA4] = 0xFFFC3434,
  };
  static const u32 kPointWhitePalette[256] = {
    [1] = 0xFFFCFCFC, [2] = 0xFFFCFCFC, [3] = 0xFFFCFCFC, [4] = 0xFFFCFCFC, [5] = 0xFFFCFCFC,
    [6] = 0xFFFCFCFC, [7] = 0xFFFCFCFC, [8] = 0xFFFCFCFC, [9] = 0xFFFCFCFC, [10] = 0xFFFCFCFC,
    [11] = 0xFFFCFCFC, [12] = 0xFFFCFCFC, [13] = 0xFFFCFCFC, [14] = 0xFFFCFCFC, [15] = 0xFFFCFCFC,
    [23] = 0xFFFCFCFC, [0xA9] = 0xFFFCFCFC,
  };
  static const u8 kSelectionRedRemap[256] = {
    [1] = 0xB2, [2] = 0xB3, [3] = 0xB3, [4] = 0xB3, [5] = 0xB3, [6] = 0xB4, [7] = 0xB4,
    [8] = 0xB5, [9] = 0xB5, [10] = 0xA3, [11] = 0xA3, [12] = 0xA3, [13] = 0xA3,
    [14] = 0xA4, [15] = 0xA4,
    [0xA9] = 0xA4,
  };
  // Bitmap indices encode coverage. Both 256-entry palettes preserve that
  // alpha, so changing selection color cannot change its shape or weight.
  static const u32 kDiamondWhitePalette[256] = {
    [1] = 0x11FCFCFC, [2] = 0x22FCFCFC, [3] = 0x33FCFCFC, [4] = 0x44FCFCFC, [5] = 0x55FCFCFC,
    [6] = 0x66FCFCFC, [7] = 0x77FCFCFC, [8] = 0x88FCFCFC, [9] = 0x99FCFCFC, [10] = 0xAAFCFCFC,
    [11] = 0xBBFCFCFC, [12] = 0xCCFCFCFC, [13] = 0xDDFCFCFC, [14] = 0xEEFCFCFC, [15] = 0xFFFCFCFC,
  };
  static const u32 kDiamondRedPalette[256] = {
    [1] = 0x11FC3434, [2] = 0x22FC3434, [3] = 0x33FC3434, [4] = 0x44FC3434, [5] = 0x55FC3434,
    [6] = 0x66FC3434, [7] = 0x77FC3434, [8] = 0x88FC3434, [9] = 0x99FC3434, [10] = 0xAAFC3434,
    [11] = 0xBBFC3434, [12] = 0xCCFC3434, [13] = 0xDDFC3434, [14] = 0xEEFC3434, [15] = 0xFFFC3434,
  };
  surface->palette = style >= 2 ? (style == 3 ? kDiamondRedPalette : kDiamondWhitePalette) :
      style == 0 ? kPointWhitePalette : kSelectionPalette;
  surface->remap = style == 1 ? kSelectionRedRemap : NULL;
}

void DrawCityMapPlacementMarker(const City *city, TileIndex tile, bool point, bool blocked) {
  Drawing *drawing = city->game->drawing;
  int zoom = drawing->default_map_view->zoom, tile_scale = 1 << zoom;
  int points[8];
  CityMapTileCorners(city, tile, zoom, points);
  SpriteSurface surface = drawing->map_target;
  int style = (point ? 0 : 2) + blocked;
  ApplyCityMapSelectionPalette(&surface, style);
  int id, left, bottom;
  if (point) {
    id = MAP_POINT_SPRITE_FIRST + zoom;
    left = points[0];
    bottom = points[1];
  } else {
    CityMapUnderworldPatch patch = {.elevation = ReadCitySurfaceHeight(&city->tile[tile])};
    CityPoint camera = CityMapViewTilePoint(city, tile);
    int x = camera.x, y = camera.y;
    if (drawing->map_state.events_suppressed) patch = CityMapUnderworldSurface(city, tile);
    else {
      int base = drawing->map_state.origin_y + 2 * tile_scale * (y + x) -
          CityMapHeightStep(zoom) * patch.elevation;
      double north = (double)(base - 4 * tile_scale - points[1]) / CityMapHeightStep(zoom);
      double east = (double)(base - 2 * tile_scale - points[3]) / CityMapHeightStep(zoom);
      double south = (double)(base - points[5]) / CityMapHeightStep(zoom);
      double west = (double)(base - 2 * tile_scale - points[7]) / CityMapHeightStep(zoom);
      patch.coefficient[0][0] = north;
      patch.coefficient[1][0] = west - north;
      patch.coefficient[0][1] = east - north;
      patch.coefficient[1][1] = north - east - west + south;
    }
    left = drawing->map_state.origin_x + 4 * tile_scale * (y - x);
    bottom = drawing->map_state.origin_y + 2 * tile_scale * (y + x) - CityMapHeightStep(zoom) * patch.elevation;
    id = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM -
        CITY_LANDSCAPE_GROUND_FIRST + CITY_LANDSCAPE_BAKED_SELECTION_FIRST + CityMapUnderworldShape(&patch);
  }
  int slot = drawing->target_cache;
  const RunSprite *sprite = drawing->sprite_cache.sprites[slot][id];
  if (!sprite) return;
  int px = drawing->target_x + left * drawing->target_scale / 100 + drawing->sprite_cache.offsets_x[slot][id];
  int py = drawing->target_y + bottom * drawing->target_scale / 100 - drawing->sprite_cache.baselines[slot][id];
  if (drawing->map_depth.preparing)
    PrepareCityMapSprite(drawing, id, px, py, SPRITE_FIXED_PALETTE, MAP_PALETTE_SELECTION_FIRST + style);
  else DrawRunSprite(&surface, sprite, px, py, SPRITE_FIXED_PALETTE, NULL);
}
