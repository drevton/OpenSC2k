#include "drawing/maps/map_effects.h"
#include "ui/map.h"
#include "city/infrastructure/bridges.h"
#include "city/maps/map_types.h"
#include "city/maps/tile_storage.h"
#include "drawing/maps/map_view.h"
#include "city/maps/layers.h"
#include "city/maps/map.h"
#include "drawing/drawing.h"
#include "drawing/drawing_internal.h"
#include "drawing/fonts/fonts.h"
#include "drawing/maps/map_render.h"

// Map Overlay
// Drawing subsystem: map overlay.

void SetCityMapLabelStyle(Drawing *drawing, bool enabled) {
  drawing->label_style = enabled;
}

static u8 CityOverlayColorIndex(Drawing *drawing, u32 color) {
  int red = (int)(color & 0xFF);
  int green = (int)((color >> 8) & 0xFF);
  int blue = (int)((color >> 16) & 0xFF);
  int best_distance = 0x7FFFFFFF;
  int best_index = 0;

  for (int index = 0; index < 256; ++index) {
    const CityPaletteEntry *entry = &drawing->palette.current[index];
    int red_delta = (int)entry->red - red;
    int green_delta = (int)entry->green - green;
    int blue_delta = (int)entry->blue - blue;
    int distance = red_delta * red_delta + green_delta * green_delta + blue_delta * blue_delta;
    if (distance < best_distance) {
      best_distance = distance;
      best_index = index;
    }
  }
  return best_index;
}

static void FillMapTarget(Drawing *drawing, CityRect rect, u8 color) {
  int scale = drawing->target_scale, x = drawing->target_x, y = drawing->target_y;
  FillSpriteSurface(&drawing->map_target, (CityRect){x + (int)((i64)rect.left * scale / 100), y + (int)((i64)rect.top * scale / 100),
      x + (int)(((i64)rect.right * scale + 99) / 100), y + (int)(((i64)rect.bottom * scale + 99) / 100)}, color);
}

static void CityOverlayPutPixel(Drawing *drawing, int x, int y, u8 color) {
  FillMapTarget(drawing, (CityRect){x, y, x + 1, y + 1}, color);
}

static void CityOverlayFillRect(Drawing *drawing, CityRect rect, u8 color) {
  FillMapTarget(drawing, rect, color);
}

// Rasterize the original two-pixel pen paths and their end caps.
static void DrawCityMapOverlayBeveledRect(Drawing *drawing, CityRect rect, u8 fill, u8 light, u8 dark, u8 corner) {
  CityOverlayFillRect(drawing, rect, fill);
  CityOverlayFillRect(drawing, (CityRect){rect.left - 1, rect.top - 1, rect.right - 1, rect.top + 1}, dark);
  CityOverlayFillRect(drawing, (CityRect){rect.left - 1, rect.top - 1, rect.left + 1, rect.bottom - 1}, dark);
  CityOverlayFillRect(drawing, (CityRect){rect.left, rect.bottom - 3, rect.right - 1, rect.bottom - 1}, corner);
  CityOverlayFillRect(drawing, (CityRect){rect.right - 3, rect.top, rect.right - 1, rect.bottom - 1}, corner);
  // LineTo excludes its final pixel; the last two stock calls are both MoveTo.
  CityOverlayPutPixel(drawing, rect.left, rect.bottom - 1, light);
}

static void CityOverlayDrawText(Drawing *drawing, int x, int y, int zoom, const char *text, u8 color) {
  const CityOverlayGlyph *font = CityOverlayFontGlyphs(&drawing->overlay_font, zoom);
  for (int character = 0; text[character] != 0; ++character) {
    int code = (u8)text[character];
    if (code < CITY_OVERLAY_FONT_FIRST ||
        code >= CITY_OVERLAY_FONT_FIRST + CITY_OVERLAY_GLYPH_COUNT)
      continue;
    const CityOverlayGlyph *glyph = &font[code - CITY_OVERLAY_FONT_FIRST];
    for (int y_offset = 0; y_offset < CITY_OVERLAY_GLYPH_HEIGHT; ++y_offset) {
      u32 row = glyph->rows[y_offset];
      for (int x_offset = 0; x_offset < 32; ++x_offset)
        if ((row & (1u << x_offset)) != 0)
          CityOverlayPutPixel(drawing, x + x_offset - glyph->origin_offset, y + y_offset, color);
    }
    x += glyph->paint_advance;
  }
}

// Native presentation: a two-pixel inside bevel, with visible glyph pixels centered in the face.
static void CanvasOverlayLabel(Drawing *drawing, CityRect rect, int zoom, const char *text) {
  int light = CityOverlayColorIndex(drawing, 0x00BFBFBF);
  int dark = CityOverlayColorIndex(drawing, 0x00555555);
  CityOverlayFillRect(drawing, rect, CityOverlayColorIndex(drawing, 0x00838383));
  CityOverlayFillRect(drawing, (CityRect){rect.left, rect.top, rect.right, rect.top + 2}, light);
  CityOverlayFillRect(drawing, (CityRect){rect.left, rect.top, rect.left + 2, rect.bottom}, light);
  CityOverlayFillRect(drawing, (CityRect){rect.left, rect.bottom - 2, rect.right, rect.bottom}, dark);
  CityOverlayFillRect(drawing, (CityRect){rect.right - 2, rect.top, rect.right, rect.bottom}, dark);
  if (text == NULL)
    return;
  const CityOverlayGlyph *font = CityOverlayFontGlyphs(&drawing->overlay_font, zoom);
  int advance = 0;
  CityRect ink = {INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN};
  for (int index = 0; text[index]; ++index) {
    int code = (u8)text[index];
    if (code < CITY_OVERLAY_FONT_FIRST || code >= CITY_OVERLAY_FONT_FIRST + CITY_OVERLAY_GLYPH_COUNT)
      continue;
    const CityOverlayGlyph *glyph = &font[code - CITY_OVERLAY_FONT_FIRST];
    for (int y = 0; y < CITY_OVERLAY_GLYPH_HEIGHT; ++y) {
      u32 row = glyph->rows[y];
      if (row == 0)
        continue;
      int left = advance + (int)__builtin_ctz(row) - glyph->origin_offset;
      int right = advance + 32 - (int)__builtin_clz(row) - glyph->origin_offset;
      ink.left = IntMin(ink.left, left);
      ink.right = IntMax(ink.right, right);
      ink.top = IntMin(ink.top, y);
      ink.bottom = IntMax(ink.bottom, y + 1);
    }
    advance += glyph->paint_advance;
  }
  if (ink.left != INT32_MAX)
    CityOverlayDrawText(drawing, rect.left + (rect.right - rect.left - (ink.right - ink.left)) / 2 - ink.left,
        rect.top + (rect.bottom - rect.top - (ink.bottom - ink.top)) / 2 - ink.top,
        zoom, text, CityOverlayColorIndex(drawing, 0));
}

static void DrawOverlayLabelSoftware(Drawing *drawing, int zoom, int screen_x, int screen_y, const char *text) {
  int label_x = screen_x;
  int label_y = screen_y;
  if (zoom == 0) {
    label_x += 4;
    label_y -= 2;
  } else if (zoom == 1) {
    label_x += 8;
    label_y -= 4;
  } else if (zoom == 2) {
    label_x += 16;
    label_y -= 8;
  }

  const CityOverlayGlyph *font = CityOverlayFontGlyphs(&drawing->overlay_font, zoom);
  int text_width = 0;
  int length = (int)strlen(text);
  for (int index = 0; index < length; ++index) {
    int code = (u8)text[index];
    if (code >= CITY_OVERLAY_FONT_FIRST &&
        code < CITY_OVERLAY_FONT_FIRST + CITY_OVERLAY_GLYPH_COUNT)
      text_width += font[code - CITY_OVERLAY_FONT_FIRST].advance;
  }

  CityRect rect;
  rect.bottom = label_y - 15 * zoom - 20;
  rect.top = rect.bottom - drawing->map_state.label_heights[zoom] - 5;
  rect.left = label_x - (text_width >> 1) - 8;
  rect.right = rect.left + text_width + 16;
  int fill = CityOverlayColorIndex(drawing, 0x00BBBBBB);
  int light = CityOverlayColorIndex(drawing, drawing->map_state.border_light_color);
  int dark = CityOverlayColorIndex(drawing, drawing->map_state.border_dark_color);
  int corner = CityOverlayColorIndex(drawing, drawing->map_state.border_corner_color);
  if (drawing->label_style)
    CanvasOverlayLabel(drawing, rect, zoom, text);
  else {
    DrawCityMapOverlayBeveledRect(drawing, rect, fill, light, dark, corner);
    CityOverlayDrawText(drawing, rect.left + 4, rect.top + 2, zoom, text,
        CityOverlayColorIndex(drawing, drawing->map_state.label_text_color));
  }
  rect.top = rect.bottom;
  rect.bottom = label_y;
  rect.left = label_x - 2;
  rect.right = label_x + 2;
  if (drawing->label_style)
    CanvasOverlayLabel(drawing, rect, zoom, NULL);
  else
    DrawCityMapOverlayBeveledRect(drawing, rect, CityOverlayColorIndex(drawing, 0x009F9F9F), light, dark, corner);
}

// Host damage bounds, including the label border and its stem.
CityRect Sc2kCityOverlayLabelBounds(const Drawing *drawing, int zoom, int screen_x, int screen_y,
    const char *text) {
  const CityOverlayGlyph *font = CityOverlayFontGlyphs(&drawing->overlay_font, zoom);
  int text_width = 0;
  for (int index = 0; text[index]; ++index) {
    int code = (u8)text[index];
    if (code >= CITY_OVERLAY_FONT_FIRST && code < CITY_OVERLAY_FONT_FIRST + CITY_OVERLAY_GLYPH_COUNT)
      text_width += font[code - CITY_OVERLAY_FONT_FIRST].advance;
  }
  int label_x = screen_x + (4 << zoom), label_y = screen_y - (2 << zoom);
  return (CityRect){label_x - (text_width >> 1) - 9,
      label_y - 15 * zoom - 26 - drawing->map_state.label_heights[zoom],
      label_x - (text_width >> 1) + text_width + 9, label_y};
}

// The color index is persistent: retain this ordering when adding presentation styles.
CitySignColors CitySignColorStyle(int color) {
  static const u32 kFaces[CITY_SIGN_COLOR_COUNT] = {
    0xFF549A2A, 0xFF4068B0, 0xFF989898, 0xFFB8A0D0, 0xFF5898D0,
    0xFFF0B858, 0xFFC85050, 0xFF509870, 0xFF90C858, 0xFF6088C8,
    0xFFB88868, 0xFFB0B8D0, 0xFFF89038, 0xFFB858B0, 0xFFD8B840,
    0xFFF8F0C0, 0xFF70C8C8, 0xFF283858, 0xFF505868, 0xFF784898,
    0xFF286880, 0xFF886030, 0xFF882838, 0xFF286040, 0xFF587830,
    0xFF384878, 0xFF784830, 0xFFD888A0, 0xFFFFD888, 0xFFD8B8E8,
    0xFFD0D8E8, 0xFFA8D8A0
  };
  if (!color) return (CitySignColors){kFaces[0], 0xFF70AC48, 0xFF385C18, 0xFF101010};
  u32 face = kFaces[color];
  int red = face >> 16 & 255, green = face >> 8 & 255, blue = face & 255;
  u32 light = 0xFF000000 | (red + ((255 - red) >> 2)) << 16 |
      (green + ((255 - green) >> 2)) << 8 | (blue + ((255 - blue) >> 2));
  u32 dark = 0xFF000000 | (red * 3 >> 2) << 16 | (green * 3 >> 2) << 8 | (blue * 3 >> 2);
  u32 ink = red * 299 + green * 587 + blue * 114 < 128000 ? 0xFFFCFCFC : 0xFF101010;
  return (CitySignColors){face, light, dark, ink};
}

// Native signs use interface detail and size independently of the map zoom.
CityRect CitySignPixelBounds(const City *city, CityPoint anchor, const char *text) {
  int scale = city->game->drawing->options.scale;
  int width = (CityInterfaceTextWidth(&city->game->resources->interface_font, CitySignTextName(text)) + 4) * scale;
  int height = 12 * scale;
  width = (width + 99) / 100;
  height = (height + 99) / 100;
  return (CityRect){anchor.x - (width >> 1), anchor.y - height, anchor.x - (width >> 1) + width, anchor.y};
}

CityRect CitySignMapBounds(const City *city, int zoom, int screen_x, int screen_y, const char *text) {
  Drawing *drawing = city->game->drawing;
  if (drawing->label_style) {
    CityRect pixels = CitySignPixelBounds(city, (CityPoint){0}, text);
    int scale = drawing->target_scale;
    int left = pixels.left * 100, top = pixels.top * 100;
    int right = pixels.right * 100;
    int x = screen_x + (4 << zoom), y = screen_y - (2 << zoom);
    // Round outward, allowing the physical anchor's fractional-pixel rounding.
    return (CityRect){x + left / scale - (left % scale < 0) - 2,
        y + top / scale - (top % scale < 0) - 2,
        x + right / scale + (right % scale > 0) + 2, y + 2};
  }
  CityRect legacy = Sc2kCityOverlayLabelBounds(drawing, zoom, screen_x, screen_y, text);
  // One source pixel always occupies at least one physical pixel. This conservative
  // source-space cover works for the main camera and independently scaled extra cameras.
  CityRect native = CitySignPixelBounds(city, (CityPoint){screen_x + (4 << zoom), screen_y - (2 << zoom)}, text);
  return (CityRect){IntMin(legacy.left, native.left) - 1, IntMin(legacy.top, native.top) - 1,
      IntMax(legacy.right, native.right) + 1, IntMax(legacy.bottom, native.bottom) + 1};
}

static void DrawNativeCityMapSign(City *city, int zoom, int screen_x, int screen_y, const char *text) {
  Drawing *drawing = city->game->drawing;
  int scale = drawing->target_scale;
  CityPoint anchor = {drawing->target_x + (screen_x + (4 << zoom)) * scale / 100,
      drawing->target_y + (screen_y - (2 << zoom)) * scale / 100};
  CityRect bounds = CitySignPixelBounds(city, anchor, text);
  CitySignColors colors = CitySignColorStyle(CitySignTextColor(text));
  text = CitySignTextName(text);
  Canvas canvas = drawing->map_target.canvas;
  canvas.raster = CanvasRasterView(&canvas);
  canvas.raster.clip = IntersectCityRects(canvas.raster.clip, canvas.clip);
  canvas.raster.numerator = drawing->options.scale;
  canvas.raster.denominator = 100;
  canvas.raster.origin_x = canvas.raster.origin_y = 0;
  canvas.raster.offset_x = bounds.left;
  canvas.raster.offset_y = bounds.top;
  canvas.width = CityInterfaceTextWidth(&city->game->resources->interface_font, text) + 4;
  canvas.height = 12;
  canvas.clip = (CityRect){0, 0, canvas.width, canvas.height};
  canvas.interface_font = &city->game->resources->interface_font;
  canvas.unscaled_bevels = !drawing->options.scale_bevels;
  canvas.scalable_font = !drawing->options.sprite_font;
  canvas.font_antialias = drawing->options.font_antialias;
  DrawCanvasFrame(&canvas, canvas.clip, colors.face, colors.light, colors.dark, true);
  DrawCityInterfaceText(canvas.interface_font, &canvas, 2, 1, text, colors.ink, false);
}

static void FormatCityConnectionSign(const City *city, int x, int y, char text[CITY_OVERLAY_NAME_BYTES]) {
  int direction = 0, position = 0;
  if (x < 2) { direction = 3; position = y; }
  if (y > city->map_height - 3) { direction = 0; position = x; }
  if (x > city->map_width - 3) { direction = 1; position = city->map_height - 1 - y; }
  if (y < 2) { direction = 2; position = city->map_width - 1 - x; }
  const char *name = (const char *)city->geography.neighbor_names.name[(direction + city->geography.rotation) & 3];
  snprintf(text, CITY_OVERLAY_NAME_BYTES, "%.20s %c", name, position % 5 + '5');
}

// Connection labels have no fixed count. Visit only the visible border strips
// after traffic, without consuming the bounded pool reserved for ordinary signs.
static void DrawCityConnectionSigns(City *city) {
  Drawing *drawing = city->game->drawing;
  if (!drawing->label_style || !drawing->map_state.label_display || drawing->map_state.events_suppressed ||
      (drawing->map_state.data_overlay_enabled && drawing->map_state.data_mode != CITY_DATA_MODE_NONE)) return;
  int zoom = drawing->default_map_view->zoom, scale = 1 << zoom;
  CityRect area = VisibleCityMapTileBounds(city, drawing->map_state.origin_x, drawing->map_state.origin_y,
      zoom, CityMapCameraRotation(drawing), drawing->map_state.paint_parameters.rect);
  int width = city->map_width, height = city->map_height;
  CityRect edges[] = {
    {area.left, area.top, area.right, IntMin(area.bottom, 2)},
    {area.left, IntMax(area.top, height - 2), area.right, area.bottom},
    {area.left, IntMax(area.top, 2), IntMin(area.right, 2), IntMin(area.bottom, height - 2)},
    {IntMax(area.left, width - 2), IntMax(area.top, 2), area.right, IntMin(area.bottom, height - 2)}
  };
  for (int edge = 0; edge < 4; ++edge) {
    CityRect bounds = edges[edge];
    for (int y = bounds.top; y < bounds.bottom; ++y) {
      for (int x = bounds.left; x < bounds.right; ++x) {
        TileIndex tile = MapTileXY(city, x, y);
        if (ReadOverlay(city, tile) != CITY_OVERLAY_BORDER_CONNECTION) continue;
        CityPoint point = CityMapViewTilePoint(city, tile);
        int screen_x = drawing->map_state.origin_x + (point.y - point.x) * 4 * scale;
        int screen_y = drawing->map_state.origin_y + (point.x + point.y) * 2 * scale -
            ReadCitySurfaceHeight(&city->tile[tile]) * CityMapHeightStep(zoom);
        char text[CITY_OVERLAY_NAME_BYTES];
        FormatCityConnectionSign(city, x, y, text);
        DrawOverlayLabelSoftware(drawing, zoom, screen_x, screen_y, text);
      }
    }
  }
}

static int ComparePaintedSigns(const void *left, const void *right) {
  const MapSignPaint *a = left, *b = right;
  return a->diagonal != b->diagonal ? a->diagonal - b->diagonal : a->row - b->row;
}

// Paint ordinary signs and visible connection labels after all map objects.
void DrawCapturedCitySigns(City *city) {
  Drawing *drawing = city->game->drawing;
  if (drawing->sign_count > 1)
    qsort(drawing->signs, drawing->sign_count, sizeof(MapSignPaint), ComparePaintedSigns);
  for (int index = 0; index < drawing->sign_count; ++index) {
    if (index && !ComparePaintedSigns(&drawing->signs[index - 1], &drawing->signs[index])) continue;
    const MapSignPaint *sign = &drawing->signs[index];
    DrawNativeCityMapSign(city, drawing->default_map_view->zoom, sign->anchor.x, sign->anchor.y, sign->text);
  }
  DrawCityConnectionSigns(city);
}

// City-map overlay dispatch.
void DrawCityMapOverlay(City *city, int zoom, int x, int y, int screen_x, int screen_y) {
  Drawing *drawing = city->game->drawing;
  TileIndex tile = MapTileXY(city, x, y);
  int overlay = ReadOverlay(city, tile);
  int sign = ReadCitySignOverlay(city, tile);
  if (drawing->map_state.label_display && (sign || overlay == CITY_OVERLAY_BORDER_CONNECTION)) {
    int label = sign ? sign : overlay;
    if (label == CITY_OVERLAY_BORDER_CONNECTION) {
      if (drawing->label_style && !drawing->map_state.events_suppressed) return;
      FormatCityConnectionSign(city, x, y, (char *)CityOverlayNameAt(city, label)->character);
    }
    const char *text = (const char *)ReadCityOverlayName(city, label)->character;
    if (drawing->label_style && sign) {
      if (drawing->sign_count == drawing->sign_capacity) {
        int capacity = IntMax(64, drawing->sign_capacity * 2);
        MapSignPaint *signs = realloc(drawing->signs, capacity * sizeof(*signs));
        if (signs) { drawing->signs = signs; drawing->sign_capacity = capacity; }
      }
      if (drawing->sign_count < drawing->sign_capacity) {
        CityPoint point = CityMapViewTilePoint(city, tile);
        drawing->signs[drawing->sign_count++] = (MapSignPaint){.anchor = {screen_x, screen_y}, .text = text,
            .diagonal = point.x + point.y, .row = point.y};
      }
    } else {
      DrawOverlayLabelSoftware(drawing, zoom, screen_x, screen_y, text);
    }
  }
  bool animated = BeginCityMapOverlayAnimation(city, drawing, x, y, overlay);
  if (overlay >= CITY_OVERLAY_POLLUTION && overlay < 256) {
    DrawCityMapAnimatedOverlay(city, zoom, x, y, overlay);
  } else if (IsCityEventOverlay(overlay)) {
    for (int slot = FirstCityMapEventOnTile(city, tile); slot >= 0;
        slot = NextCityMapEventOnTile(city, slot))
      DrawCityMapEventOverlay(city, zoom, x, y, slot);
  }
  if (animated) EndCityMapOverlayAnimation(city, drawing);
}

// Map Overlays

// Shared background for all 128-pixel data-map overlays. History deliberately
// uses the rubble color for trees while the other overlays use green.
static u8 CityMapOverlayGroundColor(const City *city, TileIndex tile, int tree_color) {
  int kind = ReadTile(city, tile);
  if (kind != TK_CLEAR && !IsTileKindExposedWater(kind))
    return !IsTileKindUnbuilt(kind) ? 0 : IsTileKindTree(kind) ? tree_color : 53;
  if (IsTileKindExposedWater(kind)) return 98;
  int elevation = IntMin(ReadCityLandHeight(&city->tile[tile]), 16);
  return (u8)(-124 - ((3 * elevation >> 2) + 4));
}

static void BuildCityMapMetricOverlay(const City *city, u8 *pixels, int row_stride, const u8 *values, int shift) {
  for (int y = 0; y < 128; ++y) {
    for (int x = 0; x < 128; ++x) {
      int map_x = y * city->map_width >> 7, map_y = x * city->map_height >> 7;
      int value = values[((map_y >> shift) * city->map_width / (1 << shift) + (map_x >> shift)) * 4] >> 4;
      pixels[y * row_stride + x] = value ? (u8)(value - 101u) :
          CityMapOverlayGroundColor(city, MapTileXY(city, map_x, map_y), 67);
    }
  }
}

void BuildCityMapCrimeOverlay(const City *city, u8 *pixels, int row_stride) {
  BuildCityMapMetricOverlay(city, pixels, row_stride, &city->half[0].land_value, 1);
}

typedef enum CityDataOverlayKind {
  CITY_DATA_OVERLAY_GROUND, CITY_DATA_OVERLAY_ZONES, CITY_DATA_OVERLAY_ROADS, CITY_DATA_OVERLAY_RAIL,
  CITY_DATA_OVERLAY_TRAFFIC, CITY_DATA_OVERLAY_POWER, CITY_DATA_OVERLAY_WATER, CITY_DATA_OVERLAY_HISTORY,
  CITY_DATA_OVERLAY_TILE,
} CityDataOverlayKind;

static void BuildCityMapDataOverlay(const City *city, u8 *pixels, int row_stride,
    CityDataOverlayKind mode, int tile_id) {
  static const u8 kZonePalette[15] = {';', ';', '\\', '\\', '2', '2', 0, 0, 0, 0, 0, 0, 0, 0, 0};
  for (int y = 0; y < 128; ++y) {
    for (int x = 0; x < 128; ++x) {
      int map_x = y * city->map_width >> 7, map_y = x * city->map_height >> 7;
      TileIndex cell_index = MapTileXY(city, map_x, map_y);
      const TileInfo *tp = &city->tile[cell_index];
      int tile = tp->kind, pixel = -1, tree_color = 67;
      u32 classes = ReadCityTileClasses(tile);
      switch (mode) {
      case CITY_DATA_OVERLAY_GROUND:
        break;
      case CITY_DATA_OVERLAY_ZONES: {
        int zone = ReadCityZoneKind(tp);
        if (zone != 0) {
          pixel = kZonePalette[zone - 1u];
        }
        break;
      }
      case CITY_DATA_OVERLAY_ROADS:
        if (tile != TK_CLEAR && ((classes & TCLASS_ROAD) ||
            ((classes & TCLASS_NATIVE_BRIDGE) && !(classes & TCLASS_RAIL_BRIDGE)) ||
                ReadCityBridgeUnderpass(tp) == CITY_BRIDGE_UNDERPASS_ROAD ||
                IsTileKindMultiHighway(tile) ||
                IsTileKindStraightHighwayFamily(tile))) {
          pixel = 255;
        }
        break;
      case CITY_DATA_OVERLAY_RAIL:
        if ((classes & (TCLASS_RAIL | TCLASS_RAIL_BRIDGE)) ||
            ReadCityBridgeUnderpass(tp) == CITY_BRIDGE_UNDERPASS_RAIL) {
          pixel = 255;
        }
        break;
      case CITY_DATA_OVERLAY_TRAFFIC: {
        int traffic_density = ReadTrafficDensity(city, MapTileXY(city, map_x & ~1, map_y & ~1)) >> 4;
        if (traffic_density != 0) {
          pixel = (u8)(traffic_density - 101u);
        } else if (classes & (TCLASS_ROAD | TCLASS_RAIL |
            TCLASS_NATIVE_BRIDGE | TCLASS_HIGHWAY)) {
          pixel = 255;
        }
        break;
      }
      case CITY_DATA_OVERLAY_POWER: {
        int flags = tp->flags;
        if (classes & TCLASS_POWER) {
          pixel = 255;
        } else if (tile != TK_CLEAR && !IsTileKindExposedWater(tile) && (flags & TF_POWERED) != 0) {
          pixel = 50;
        } else if (tile != TK_CLEAR && !IsTileKindExposedWater(tile) && (i8)flags < 0) {
          pixel = 29;
        }
        break;
      }
      case CITY_DATA_OVERLAY_WATER: {
        int underground = ReadUnderground(city, cell_index);
        int flags = tp->flags;
        if (underground >= TUK_PIPE_FIRST && underground <= TUK_SUBWAY_ACCESS) {
          pixel = 255;
        } else if ((flags & TF_WATER_SERVICE) == TF_WATER_SERVICE) {
          pixel = 50;
        } else if ((flags & TF_PIPED) != 0) {
          pixel = 29;
        }
        break;
      }
      case CITY_DATA_OVERLAY_HISTORY: {
        int history = ReadDensityHistory(city, MapTileXY(city, map_x & ~3, map_y & ~3));
        if (history > 124 && history < 131) {
          tree_color = 53;
        } else if (history > 130) {
          pixel = 67;
        } else {
          pixel = 29;
        }
        break;
      }
      case CITY_DATA_OVERLAY_TILE:
        if (tile != TK_CLEAR && tile == (u8)tile_id) {
          pixel = 255;
        }
        break;
      }
      pixels[y * row_stride + x] = pixel >= 0 ? pixel : CityMapOverlayGroundColor(city, cell_index, tree_color);
    }
  }
}

void BuildCityMapDefaultOverlay(const City *city, u8 *pixels, int row_stride) {
  BuildCityMapDataOverlay(city, pixels, row_stride, CITY_DATA_OVERLAY_GROUND, 0);
}

void BuildCityMapZoneOverlay(const City *city, u8 *pixels, int row_stride) {
  BuildCityMapDataOverlay(city, pixels, row_stride, CITY_DATA_OVERLAY_ZONES, 0);
}

// Native previews use semantic colors, independently of the compatibility palette.
static u32 CityMapPreviewGround(const City *city, TileIndex tile) {
  const TileInfo *tp = &city->tile[tile];
  if (HasTileWater(tp)) return 0xFF285A80;
  if (IsTileKindTree(tp->kind)) return 0xFF304E38;
  if (!IsTileKindUnbuilt(tp->kind)) return 0xFF505860;
  CityTileGeometry geometry = ReadCityTileGeometry(city, tile);
  int shade = 40 + IntMin(geometry.land_height, 31);
  return 0xFF000000 | shade << 16 | (shade + 8) << 8 | shade;
}

static u32 CityMapPreviewZone(const TileInfo *tp, u32 ground) {
  static const u32 kZoneColors[ZONE_RESERVED_FIRST] = {
    [ZONE_RESIDENTIAL_LOW] = 0xFF54B85C, [ZONE_RESIDENTIAL_HIGH] = 0xFF278C39,
    [ZONE_COMMERCIAL_LOW] = 0xFF559DE0, [ZONE_COMMERCIAL_HIGH] = 0xFF286DBD,
    [ZONE_INDUSTRIAL_LOW] = 0xFFF0D05A, [ZONE_INDUSTRIAL_HIGH] = 0xFFCBA527,
    [ZONE_MILITARY] = 0xFFCB6556, [ZONE_AIRPORT] = 0xFFAD7BC9, [ZONE_SEAPORT] = 0xFF55BFC5
  };
  int zone = ReadCityZoneKind(tp);
  return zone > ZONE_NONE && zone < ZONE_RESERVED_FIRST ? kZoneColors[zone] : ground;
}

// Preserve the sixteen data levels; zero leaves the background visible.
static u32 CityMapPreviewMetric(int value, u32 low, u32 high, u32 ground) {
  int level = value >> 4;
  if (!level) return ground;
  u32 color = 0xFF000000;
  for (int shift = 0; shift < 24; shift += 8)
    color |= (((low >> shift & 255) * (15 - level) + (high >> shift & 255) * (level - 1)) / 14) << shift;
  return color;
}

static u32 CityMapPreviewColor(const City *city, TileIndex tile, int selection) {
  const TileInfo *tp = &city->tile[tile];
  u32 ground = CityMapPreviewGround(city, tile), classes = ReadCityTileClasses(tp->kind);
  const u32 highlight = 0xFFF4F0DC, supplied = 0xFF65D67B, missing = 0xFFE65C50;
  const u32 low_warning = 0xFFC9B85B, high_warning = 0xFFE74232;
  const u32 low_benefit = 0xFF344D3C, high_benefit = 0xFF65D67B;
  switch (selection) {
  case CITY_DATA_SELECTION_STRUCTURES: return ground;
  case CITY_DATA_SELECTION_ZONES: return CityMapPreviewZone(tp, ground);
  case CITY_DATA_SELECTION_ROADS:
    return tp->kind != TK_CLEAR && ((classes & TCLASS_ROAD) ||
        ((classes & TCLASS_NATIVE_BRIDGE) && !(classes & TCLASS_RAIL_BRIDGE)) ||
        ReadCityBridgeUnderpass(tp) == CITY_BRIDGE_UNDERPASS_ROAD ||
        IsTileKindMultiHighway(tp->kind) || IsTileKindStraightHighwayFamily(tp->kind)) ? highlight : ground;
  case CITY_DATA_SELECTION_RAIL:
    return (classes & (TCLASS_RAIL | TCLASS_RAIL_BRIDGE)) ||
        ReadCityBridgeUnderpass(tp) == CITY_BRIDGE_UNDERPASS_RAIL ? highlight : ground;
  case CITY_DATA_SELECTION_TRAFFIC: {
    u32 background = classes & (TCLASS_ROAD | TCLASS_RAIL | TCLASS_NATIVE_BRIDGE | TCLASS_HIGHWAY) ? highlight : ground;
    return CityMapPreviewMetric(ReadTrafficDensity(city, tile), low_warning, high_warning, background);
  }
  case CITY_DATA_SELECTION_POWER:
    if (classes & TCLASS_POWER) return highlight;
    if (tp->kind != TK_CLEAR && !IsTileKindExposedWater(tp->kind)) {
      if (tp->flags & TF_POWERED) return supplied;
      if (tp->flags & TF_POWERABLE) return missing;
    }
    return ground;
  case CITY_DATA_SELECTION_WATER: {
    int underground = ReadUnderground(city, tile);
    if (underground >= TUK_PIPE_FIRST && underground <= TUK_SUBWAY_ACCESS) return highlight;
    if ((tp->flags & TF_WATER_SERVICE) == TF_WATER_SERVICE) return 0xFF55BFC5;
    return tp->flags & TF_PIPED ? missing : ground;
  }
  case CITY_DATA_SELECTION_DENSITY:
    return CityMapPreviewMetric(ReadDensity(city, tile), 0xFF3B5474, 0xFF9FDBFF, ground);
  case CITY_DATA_SELECTION_GROWTH: {
    int history = ReadDensityHistory(city, tile);
    if (history <= 124) return missing;
    if (history > 130) return supplied;
    return IsTileKindTree(tp->kind) ? 0xFF505860 : ground;
  }
  case CITY_DATA_SELECTION_CRIME:
    return CityMapPreviewMetric(ReadCrime(city, tile), low_warning, high_warning, ground);
  case CITY_DATA_SELECTION_POLLUTION:
    return CityMapPreviewMetric(ReadPollution(city, tile), low_warning, high_warning, ground);
  case CITY_DATA_SELECTION_LAND_VALUE:
    return CityMapPreviewMetric(ReadLandValue(city, tile), low_benefit, high_benefit, ground);
  case CITY_DATA_SELECTION_POLICE_COVERAGE:
    return CityMapPreviewMetric(ReadPoliceCoverage(city, tile), low_benefit, high_benefit, ground);
  case CITY_DATA_SELECTION_FIRE_COVERAGE:
    return CityMapPreviewMetric(ReadFireCoverage(city, tile), low_benefit, high_benefit, ground);
  case CITY_DATA_SELECTION_POLICE_STATIONS: return tp->kind == TK_POLICE_STATION ? highlight : ground;
  case CITY_DATA_SELECTION_FIRE_STATIONS: return tp->kind == TK_FIRE_STATION ? highlight : ground;
  case CITY_DATA_SELECTION_SCHOOLS: return tp->kind == TK_SCHOOL ? highlight : ground;
  case CITY_DATA_SELECTION_COLLEGES: return tp->kind == TK_COLLEGE ? highlight : ground;
  default: return ground;
  }
}

bool BuildCityMapDataPreview(const City *city, int selection, u32 *pixels) {
  bool changed = false;
  for (int y = 0; y < 128; ++y)
    for (int x = 0; x < 128; ++x) {
      TileIndex tile = MapTileXY(city, y * city->map_width / 128, x * city->map_height / 128);
      u32 color = CityMapPreviewColor(city, tile, selection);
      int index = y * 128 + x;
      changed |= pixels[index] != color;
      pixels[index] = color;
    }
  return changed;
}

void BuildCityMapResidentialOverlay(const City *city, u8 *pixels, int row_stride) {
  BuildCityMapDataOverlay(city, pixels, row_stride, CITY_DATA_OVERLAY_ROADS, 0);
}

void BuildCityMapCommercialOverlay(const City *city, u8 *pixels, int row_stride) {
  BuildCityMapDataOverlay(city, pixels, row_stride, CITY_DATA_OVERLAY_RAIL, 0);
}

void BuildCityMapTrafficDensityOverlay(const City *city, u8 *pixels, int row_stride) {
  BuildCityMapDataOverlay(city, pixels, row_stride, CITY_DATA_OVERLAY_TRAFFIC, 0);
}

void BuildCityMapIndustrialOverlay(const City *city, u8 *pixels, int row_stride) {
  BuildCityMapDataOverlay(city, pixels, row_stride, CITY_DATA_OVERLAY_POWER, 0);
}

void BuildCityMapUndergroundOverlay(const City *city, u8 *pixels, int row_stride) {
  BuildCityMapDataOverlay(city, pixels, row_stride, CITY_DATA_OVERLAY_WATER, 0);
}

void BuildCityMapDensityOverlay(const City *city, u8 *pixels, int row_stride) {
  BuildCityMapMetricOverlay(city, pixels, row_stride, &city->quarter[0].density, 2);
}

void BuildCityMapDensityHistoryOverlay(const City *city, u8 *pixels, int row_stride) {
  BuildCityMapDataOverlay(city, pixels, row_stride, CITY_DATA_OVERLAY_HISTORY, 0);
}

void BuildCityMapPollutionOverlay(const City *city, u8 *pixels, int row_stride) {
  BuildCityMapMetricOverlay(city, pixels, row_stride, &city->half[0].crime, 1);
}

void BuildCityMapPoliceCoverageOverlay(const City *city, u8 *pixels, int row_stride) {
  BuildCityMapMetricOverlay(city, pixels, row_stride, &city->quarter[0].police_coverage, 2);
}

void BuildCityMapLandValueOverlay(const City *city, u8 *pixels, int row_stride) {
  BuildCityMapMetricOverlay(city, pixels, row_stride, &city->half[0].pollution, 1);
}

void BuildCityMapFireCoverageOverlay(const City *city, u8 *pixels, int row_stride) {
  BuildCityMapMetricOverlay(city, pixels, row_stride, &city->quarter[0].fire_coverage, 2);
}

void BuildCityMapTileTypeHighlight(const City *city, u8 *pixels, int row_stride, int tile_id) {
  BuildCityMapDataOverlay(city, pixels, row_stride, CITY_DATA_OVERLAY_TILE, tile_id);
}
