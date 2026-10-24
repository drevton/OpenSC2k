#include "drawing/maps/map_terrain.h"
#include "city/maps/labels.h"
#include <stdio.h>

#include "assets/state.h"
#include "city/events/events.h"
#include "city/growth/growth.h"
#include "city/maps/buildings.h"
#include "city/maps/layers.h"
#include "city/maps/map_types.h"
#include "city/maps/tile_storage.h"
#include "drawing/fonts/fonts.h"
#include "drawing/maps/map_animation.h"
#include "drawing/maps/map_events.h"
#include "drawing/maps/map_depth.h"
#include "drawing/maps/map_labels.h"
#include "drawing/maps/map_render.h"
#include "drawing/maps/map_view.h"
#include "drawing/maps/tables.h"
#include "drawing/state.h"
#include "game/state.h"

static bool Intersects(CityRect a, CityRect b) {
  return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

static int FloorDivide(int value, int divisor) {
  return value / divisor - (value < 0 && value % divisor != 0);
}

static int CeilDivide(int value, int divisor) {
  return -FloorDivide(-value, divisor);
}

// Sprite extents depend on assets/graphics configuration, never map edits or
// camera position. Bounds-only callers must not prepare or sort moving events.
static void PrepareCityMapTileBounds(const City *city, int zoom, bool labels) {
  Drawing *drawing = city->drawing;
  MapTileTraversal *traversal = &drawing->tile_traversal;
  const MapSpriteMetrics *catalog = drawing->map_state.sprite_metrics;
  bool native = (city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) != 0;
  u32 revision = city->game->resources->sprite_revision;
  if (traversal->extent_metrics != catalog || traversal->extent_revision != revision ||
      traversal->extent_native != native) {
    traversal->extent_valid = 0;
    traversal->extent_metrics = catalog;
    traversal->extent_revision = revision;
    traversal->extent_native = native;
  }
  int scale = 1 << zoom;
  if (!traversal->extent_valid) {
    // Prepare every zoom together; changing zoom never starts a new scan.
    for (int detail = 0; detail < 3; ++detail) {
      CityPoint limit = {16 << detail, 32 << detail};
      for (int sprite = 0; sprite < CITY_MAP_SPRITES_PER_ZOOM; ++sprite) {
        CityPoint extent = CityBuildingSpriteExtent(city, sprite, detail);
        limit.x = IntMax(limit.x, extent.x);
        limit.y = IntMax(limit.y, extent.y);
      }
      traversal->sprite_extents[detail] = limit;
    }
    traversal->extent_valid = 1;
  }
  traversal->width = traversal->sprite_extents[zoom].x;
  traversal->height = traversal->sprite_extents[zoom].y;
  traversal->left_extent = 8 * scale;
  if (labels && drawing->map_state.label_display && drawing->label_style) {
    const CityInterfaceFont *font = &city->game->resources->interface_font;
    int advance = 0;
    for (int glyph = 0; glyph < CITY_INTERFACE_FONT_GLYPHS; ++glyph)
      advance = IntMax(advance, font->glyphs[0][glyph].advance);
    int width = ((advance * CITY_OVERLAY_NAME_MAX_LENGTH + 4) * drawing->options.scale + 99) / 100;
    int height = (12 * drawing->options.scale + 99) / 100;
    // Interface text is sized in physical pixels; traversal uses logical map pixels.
    width = (width * 100 + drawing->target_scale - 1) / drawing->target_scale;
    height = (height * 100 + drawing->target_scale - 1) / drawing->target_scale;
    traversal->left_extent = IntMax(traversal->left_extent, (width + 1) / 2 + 2);
    traversal->width = IntMax(traversal->width, (width + 1) / 2 + 4 * scale + 2);
    traversal->height = IntMax(traversal->height, height + 2 * scale + 2);
  } else if (labels && drawing->map_state.label_display) {
    const CityOverlayGlyph *font = CityOverlayFontGlyphs(&city->game->resources->overlay_font, zoom);
    int advance = 0;
    for (int glyph = 0; glyph < CITY_OVERLAY_GLYPH_COUNT; ++glyph)
      advance = IntMax(advance, font[glyph].advance);
    const CityInterfaceFont *interface = &city->game->resources->interface_font;
    int native_advance = 0;
    for (int glyph = 0; glyph < CITY_INTERFACE_FONT_GLYPHS; ++glyph)
      native_advance = IntMax(native_advance, interface->glyphs[0][glyph].advance);
    int native_width = ((native_advance * CITY_OVERLAY_NAME_MAX_LENGTH + 4) * drawing->options.scale + 99) / 100;
    traversal->left_extent = IntMax(traversal->left_extent, (native_width >> 1) + 4);
    traversal->width = IntMax(traversal->width, (native_width >> 1) + 4 * scale);
    traversal->height = IntMax(traversal->height, (12 * drawing->options.scale + 99) / 100 + 4 * scale);
    int half_label = (advance * CITY_OVERLAY_NAME_MAX_LENGTH >> 1) + 16;
    traversal->left_extent = IntMax(traversal->left_extent, half_label);
    traversal->width = IntMax(traversal->width, half_label + 4 * scale);
    traversal->height = IntMax(traversal->height, 15 * zoom + 26 + drawing->map_state.label_heights[zoom]);
  }
}

// Retain sprite bounds and event anchors, without a map-wide index or rebuild after tile edits.
static void PrepareMapTileTraversal(City *city, const CityMapViewConfig *view, int zoom) {
  Drawing *drawing = city->drawing;
  MapTileTraversal *traversal = &drawing->tile_traversal;
  // Object collection also paints ground and captures signs in this traversal.
  bool labels = drawing->animation.mode != ANIMATION_CAPTURE;
  PrepareCityMapTileBounds(city, zoom, labels);
  if (!PrepareCityEventDrawingStorage(city)) { traversal->event_count = 0; return; }
  int rotation = CityMapViewRotation(view);
  drawing->map_state.zone_display_mask = kTileScanMaskTable[city->geography.rotation];
  traversal->event_count = 0;
  for (int slot = CITY_EVENT_SLOT_TOOL_PREVIEW; slot < city->events.capacity; ++slot) {
    const CityMapEvent *event = &city->events.event[slot];
    if (event->type == CITY_MAP_EVENT_UNUSED) continue;
    TileIndex tile = event->tile;
    if (tile == INVALID_TILE) continue;
    int overlay = ReadOverlay(city, tile);
    if (!IsCityEventOverlay(overlay) || CityEventOverlaySlot(overlay) != slot) continue;
    // Airborne and interpolated sprites can lie outside the ground-tile range.
    // Merge these few anchors into the regular painter traversal.
    int position = traversal->event_count;
    CityPoint location = ReadCityMapEventPosition(city, event);
    CityPoint point = CityMapCameraPoint(city, rotation, location.x, location.y);
    int order = point.x + point.y;
    while (position > 0) {
      TileIndex previous = traversal->event_cells[position - 1];
      CityPoint prior = CityMapCameraPoint(city, rotation, MapTileX(city, previous), MapTileY(city, previous));
      int previous_order = prior.x + prior.y;
      if (previous_order < order || (previous_order == order && prior.y <= point.y)) break;
      traversal->event_cells[position] = previous;
      --position;
    }
    traversal->event_cells[position] = tile;
    ++traversal->event_count;
  }
}

CityRect VisibleCityMapTileBounds(const City *city, int origin_x, int origin_y, int zoom, int rotation, CityRect area) {
  Drawing *drawing = city->drawing;
  PrepareCityMapTileBounds(city, zoom, true);
  const MapTileTraversal *traversal = &drawing->tile_traversal;
  int scale = 1 << zoom;
  int first = CeilDivide(area.top - origin_y - (traversal->width >> 2) - 20 * scale, 2 * scale);
  int last = FloorDivide(area.bottom - origin_y +
      CITY_ALTITUDE_LAND_MASK * CITY_MAP_HEIGHT_UNIT * scale + traversal->height + 20 * scale, 2 * scale);
  int left = CeilDivide(area.left - origin_x - traversal->width - 20 * scale, 4 * scale);
  int right = FloorDivide(area.right - origin_x + traversal->left_extent + 20 * scale, 4 * scale);
  CityPoint size = CityMapCameraSize(city, rotation);
  CityRect bounds = {IntMax(0, CeilDivide(first - right, 2)), IntMax(0, CeilDivide(first + left, 2)),
      IntMin(size.x, FloorDivide(last - left, 2) + 1), IntMin(size.y, FloorDivide(last + right, 2) + 1)};
  if (bounds.left >= bounds.right || bounds.top >= bounds.bottom) return (CityRect){0};
  CityPoint a = CityMapWorldPoint(city, rotation, bounds.left, bounds.top);
  CityPoint b = CityMapWorldPoint(city, rotation, bounds.right - 1, bounds.bottom - 1);
  return (CityRect){IntMin(a.x, b.x), IntMin(a.y, b.y), IntMax(a.x, b.x) + 1, IntMax(a.y, b.y) + 1};
}

static CityRect TilePaintBounds(City *city, Drawing *drawing, int zoom, TileIndex cell) {
  int scale = 1 << zoom;
  int x = MapTileX(city, cell), y = MapTileY(city, cell), tile = CityMapViewTileSpriteIndex(city, cell);
  CityPoint point = CityMapViewTilePoint(city, cell);
  CityPoint extent = CityBuildingSpriteExtent(city, tile, zoom);
  int width = extent.x, height = extent.y;
  width = IntMax(width, 16 * scale);
  height = IntMax(height, 32 * scale);
  const TileInfo *tp = &city->tile[cell];
  int land = ReadCityLandHeight(tp), surface = ReadCitySurfaceHeight(tp);
  int screen_x = drawing->map_state.origin_x + (point.y - point.x) * 4 * scale;
  int screen_y = drawing->map_state.origin_y + (point.x + point.y) * 2 * scale;
  CityRect bounds = {screen_x - 8 * scale,
      screen_y - IntMax(land, surface) * CITY_MAP_HEIGHT_UNIT * scale - height - 8 * scale,
      screen_x + width + 8 * scale, screen_y + (width >> 2) + 8 * scale};
  int overlay = ReadOverlay(city, cell);
  CityRect extra = {0};
  if (IsCityMapOverlayAnimated(city, overlay)) {
    extra = CapturedCityMapAnimationBounds(drawing, cell);
  } else if (IsCityEventOverlay(overlay)) {
    for (int slot = CityEventOverlaySlot(overlay); slot >= 0; slot = NextCityMapEventOnTile(city, slot)) {
      CityRect event = CityMapEventSpriteBounds(city, cell, zoom, slot);
      if (event.left < event.right && event.top < event.bottom)
        extra = extra.left < extra.right && extra.top < extra.bottom ? UnionCityRects(extra, event) : event;
    }
  }
  int sign = ReadCitySignOverlay(city, cell);
  if (drawing->map_state.label_display && (sign || overlay == CITY_OVERLAY_BORDER_CONNECTION)) {
    int label = sign ? sign : overlay;
    int elevation = surface;
    const char *text = (const char *)ReadCityOverlayName(city, label)->character;
    char border_name[CITY_OVERLAY_NAME_BYTES];
    if (overlay == CITY_OVERLAY_BORDER_CONNECTION) {
      int direction = x < 2 ? 3 : 0;
      if (y > city->map_height - 3) direction = 0;
      if (x > city->map_width - 3) direction = 1;
      if (y < 2) direction = 2;
      snprintf(border_name, sizeof(border_name), "%.20s 9",
          city->geography.neighbor_names.name[(direction + city->geography.rotation) & 3]);
      text = border_name;
    }
    CityRect sign_bounds = CitySignMapBounds(city, zoom,
        (CityPoint){screen_x, screen_y - elevation * CITY_MAP_HEIGHT_UNIT * scale}, text);
    extra = extra.left < extra.right && extra.top < extra.bottom ? UnionCityRects(extra, sign_bounds) : sign_bounds;
    extra.left -= 4; extra.top -= 4; extra.right += 4; extra.bottom += 4;
  }
  if (extra.left < extra.right && extra.top < extra.bottom) {
    bounds.left = IntMin(bounds.left, extra.left); bounds.top = IntMin(bounds.top, extra.top);
    bounds.right = IntMax(bounds.right, extra.right); bounds.bottom = IntMax(bounds.bottom, extra.bottom);
  }
  return bounds;
}

void PaintVisibleCityMapTiles(City *city, const CityMapViewConfig *view, int zoom) {
  Drawing *drawing = city->drawing;
  bool capture = drawing->animation.mode == ANIMATION_CAPTURE;
  CityRect area = capture ? drawing->map_state.scan_clip : drawing->map_state.paint_rect;
  if (area.left >= area.right || area.top >= area.bottom) return;
  PrepareMapTileTraversal(city, view, zoom);
  MapTileTraversal *traversal = &drawing->tile_traversal;
  int event_count = traversal->event_count;
  int scale = 1 << zoom;
  // Invert the isometric projection, expanding for the largest possible sprite,
  // elevation and label. Both diagonal and within-diagonal ranges are bounded
  // by the clip, not by the number of map cells.
  int first = IntMax(0, CeilDivide(area.top - drawing->map_state.origin_y - (traversal->width >> 2) - 8 * scale,
      2 * scale));
  int last = IntMin(city->map_width + city->map_height - 2, FloorDivide(area.bottom - drawing->map_state.origin_y +
      CITY_ALTITUDE_LAND_MASK * CITY_MAP_HEIGHT_UNIT * scale + traversal->height + 8 * scale, 2 * scale));
  int left = CeilDivide(area.left - drawing->map_state.origin_x - traversal->width - 8 * scale, 4 * scale);
  int right = FloorDivide(area.right - drawing->map_state.origin_x + traversal->left_extent, 4 * scale);
  int rotation = CityMapViewRotation(view);
  CityPoint size = CityMapCameraSize(city, rotation);
  int diagonal = first, event = 0;
  while (diagonal <= last || event < event_count) {
    CityPoint event_point = {INT32_MAX, 0};
    if (event < event_count) event_point = CityMapViewTilePoint(city, traversal->event_cells[event]);
    int event_diagonal = event_point.x + event_point.y;
    int current = diagonal <= last ? IntMin(diagonal, event_diagonal) : event_diagonal;
    int first_y = IntMax(IntMax(0, current - size.x + 1), CeilDivide(current + left, 2));
    int last_y = IntMin(IntMin(current, size.y - 1), FloorDivide(current + right, 2));
    if (current != diagonal || diagonal > last) first_y = last_y + 1;
    int y = first_y;
    for (;;) {
      int event_y = INT32_MAX;
      if (event < event_count) {
        TileIndex event_tile = traversal->event_cells[event];
        CityPoint candidate = CityMapViewTilePoint(city, event_tile);
        if (candidate.x + candidate.y == current) event_y = candidate.y;
      }
      if (y > last_y && event_y == INT32_MAX) break;
      int cell_y = y <= last_y ? IntMin(y, event_y) : event_y;
      TileIndex cell = CityMapCameraTile(city, rotation, current - cell_y, cell_y);
      if (cell_y == y) ++y;
      while (event < event_count && traversal->event_cells[event] == cell) ++event;
      if (drawing->frame_stats.calls.active) ++drawing->frame_stats.tile_visits;
      int x = current - cell_y;
      int screen_x = drawing->map_state.origin_x + (cell_y - x) * 4 * scale;
      int screen_y = drawing->map_state.origin_y + current * 2 * scale;
      if (capture) {
        if (!IsCityMapOverlayAnimated(city, ReadOverlay(city, cell)) &&
            (!(city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) ||
                (CityBuildingSmokeIndex(ReadTile(city, cell)) < 0 &&
                    ReadTile(city, cell) != TK_WIND_POWER))) continue;
      } else if (!(screen_x >= area.left && screen_x < area.right &&
          screen_y >= area.top && screen_y < area.bottom) &&
          !Intersects(area, TilePaintBounds(city, drawing, zoom, cell))) {
        continue;
      }
      if (drawing->frame_stats.calls.active) ++drawing->frame_stats.tile_visible_visits;
      bool collecting = drawing->map_depth.phase == MAP_DEPTH_COLLECT;
      if (drawing->map_state.data_overlay_enabled && drawing->map_state.data_mode != CITY_DATA_MODE_NONE) {
        DrawCityMapDataTile(city, zoom, cell, screen_x, screen_y);
      } else if (drawing->map_state.events_suppressed) {
        DrawCityMapUndergroundTile(city, zoom, cell, screen_x, screen_y);
      } else {
        DrawCityMapTile(city, zoom, cell, screen_x, screen_y);
      }
      if (collecting) {
        drawing->map_depth.phase = MAP_DEPTH_COLLECT;
        drawing->map_depth.preparing = true;
      }
    }
    if (current == diagonal) ++diagonal;
  }
}
