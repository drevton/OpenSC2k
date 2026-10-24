#include "city/events/events.h"
#include "city/maps/labels.h"
#include "drawing/sprites/surface.h"
#include "drawing/maps/map_labels.h"

#include <stdio.h>
#include <stdlib.h>

#include "assets/state.h"
#include "city/maps/layers.h"
#include "city/maps/map.h"
#include "city/state.h"
#include "drawing/fonts/fonts.h"
#include "drawing/maps/map_animation.h"
#include "drawing/maps/map_events.h"
#include "drawing/maps/map_render.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/raster/canvas_raster_internal.h"
#include "drawing/state.h"
#include "game/state.h"

// Map labels, sign composition and effect dispatch.

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

static void CityOverlayDrawText(const City *city, int x, int y, int zoom, const char *text, u8 color) {
  Drawing *drawing = city->drawing;
  const CityOverlayGlyph *font = CityOverlayFontGlyphs(&city->game->resources->overlay_font, zoom);
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
static void CanvasOverlayLabel(const City *city, CityRect rect, int zoom, const char *text) {
  Drawing *drawing = city->drawing;
  int light = CityOverlayColorIndex(drawing, 0x00BFBFBF);
  int dark = CityOverlayColorIndex(drawing, 0x00555555);
  CityOverlayFillRect(drawing, rect, CityOverlayColorIndex(drawing, 0x00838383));
  CityOverlayFillRect(drawing, (CityRect){rect.left, rect.top, rect.right, rect.top + 2}, light);
  CityOverlayFillRect(drawing, (CityRect){rect.left, rect.top, rect.left + 2, rect.bottom}, light);
  CityOverlayFillRect(drawing, (CityRect){rect.left, rect.bottom - 2, rect.right, rect.bottom}, dark);
  CityOverlayFillRect(drawing, (CityRect){rect.right - 2, rect.top, rect.right, rect.bottom}, dark);
  if (text == NULL)
    return;
  const CityOverlayGlyph *font = CityOverlayFontGlyphs(&city->game->resources->overlay_font, zoom);
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
    CityOverlayDrawText(city, rect.left + (rect.right - rect.left - (ink.right - ink.left)) / 2 - ink.left,
        rect.top + (rect.bottom - rect.top - (ink.bottom - ink.top)) / 2 - ink.top,
        zoom, text, CityOverlayColorIndex(drawing, 0));
}

static void DrawOverlayLabelSoftware(const City *city, int zoom, int screen_x, int screen_y, const char *text) {
  Drawing *drawing = city->drawing;
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

  const CityOverlayGlyph *font = CityOverlayFontGlyphs(&city->game->resources->overlay_font, zoom);
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
    CanvasOverlayLabel(city, rect, zoom, text);
  else {
    DrawCityMapOverlayBeveledRect(drawing, rect, fill, light, dark, corner);
    CityOverlayDrawText(city, rect.left + 4, rect.top + 2, zoom, text,
        CityOverlayColorIndex(drawing, drawing->map_state.label_text_color));
  }
  rect.top = rect.bottom;
  rect.bottom = label_y;
  rect.left = label_x - 2;
  rect.right = label_x + 2;
  if (drawing->label_style)
    CanvasOverlayLabel(city, rect, zoom, NULL);
  else
    DrawCityMapOverlayBeveledRect(drawing, rect, CityOverlayColorIndex(drawing, 0x009F9F9F), light, dark, corner);
}

// Host damage bounds, including the label border and its stem.
static CityRect CityOverlayLabelBounds(const City *city, int zoom, int screen_x, int screen_y,
    const char *text) {
  const Drawing *drawing = city->drawing;
  const CityOverlayGlyph *font = CityOverlayFontGlyphs(&city->game->resources->overlay_font, zoom);
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
CityRect CitySignPixelBounds(const CityInterfaceFont *font, int scale, CityPoint anchor, const char *text) {
  int width = (CityInterfaceTextWidth(font, CitySignTextName(text)) + 4) * scale;
  int height = 12 * scale;
  width = (width + 99) / 100;
  height = (height + 99) / 100;
  return (CityRect){anchor.x - (width >> 1), anchor.y - height, anchor.x - (width >> 1) + width, anchor.y};
}

CityRect CitySignMapBounds(const City *city, int zoom, CityPoint tile_point, const char *text) {
  const Drawing *drawing = city->drawing;
  const CityInterfaceFont *font = &city->game->resources->interface_font;
  int screen_x = tile_point.x, screen_y = tile_point.y;
  if (drawing->label_style) {
    CityRect pixels = CitySignPixelBounds(font, drawing->options.scale, (CityPoint){0}, text);
    int scale = drawing->target_scale;
    int left = pixels.left * 100, top = pixels.top * 100;
    int right = pixels.right * 100;
    int x = screen_x + (4 << zoom), y = screen_y - (2 << zoom);
    // Round outward, allowing the physical anchor's fractional-pixel rounding.
    return (CityRect){x + left / scale - (left % scale < 0) - 2,
        y + top / scale - (top % scale < 0) - 2,
        x + right / scale + (right % scale > 0) + 2, y + 2};
  }
  CityRect legacy = CityOverlayLabelBounds(city, zoom, screen_x, screen_y, text);
  // One source pixel always occupies at least one physical pixel. This conservative
  // source-space cover works for the main camera and independently scaled extra cameras.
  CityRect native = CitySignPixelBounds(font, drawing->options.scale,
      (CityPoint){screen_x + (4 << zoom), screen_y - (2 << zoom)}, text);
  return (CityRect){IntMin(legacy.left, native.left) - 1, IntMin(legacy.top, native.top) - 1,
      IntMax(legacy.right, native.right) + 1, IntMax(legacy.bottom, native.bottom) + 1};
}

static void DrawNativeCityMapSign(const City *city, int zoom, CityPoint tile_point, const char *text) {
  Drawing *drawing = city->drawing;
  const CityInterfaceFont *font = &city->game->resources->interface_font;
  int scale = drawing->target_scale;
  CityPoint anchor = {drawing->target_x + (tile_point.x + (4 << zoom)) * scale / 100,
      drawing->target_y + (tile_point.y - (2 << zoom)) * scale / 100};
  CityRect bounds = CitySignPixelBounds(font, drawing->options.scale, anchor, text);
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
  canvas.width = CityInterfaceTextWidth(font, text) + 4;
  canvas.height = 12;
  canvas.clip = (CityRect){0, 0, canvas.width, canvas.height};
  canvas.interface_font = font;
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
static void DrawCityConnectionSigns(const City *city) {
  Drawing *drawing = city->drawing;
  if (!drawing->label_style || !drawing->map_state.label_display || drawing->map_state.events_suppressed ||
      (drawing->map_state.data_overlay_enabled && drawing->map_state.data_mode != CITY_DATA_MODE_NONE)) return;
  int zoom = drawing->default_map_view->zoom, scale = 1 << zoom;
  CityRect area = VisibleCityMapTileBounds(city, drawing->map_state.origin_x, drawing->map_state.origin_y,
      zoom, CityMapCameraRotation(drawing), drawing->map_state.paint_rect);
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
        DrawOverlayLabelSoftware(city, zoom, screen_x, screen_y, text);
      }
    }
  }
}

static int ComparePaintedSigns(const void *left, const void *right) {
  const MapSignPaint *a = left, *b = right;
  return a->diagonal != b->diagonal ? a->diagonal - b->diagonal : a->row - b->row;
}

// Paint ordinary signs and visible connection labels after all map objects.
void DrawCapturedCitySigns(const City *city) {
  Drawing *drawing = city->drawing;
  if (drawing->sign_count > 1)
    qsort(drawing->signs, drawing->sign_count, sizeof(MapSignPaint), ComparePaintedSigns);
  for (int index = 0; index < drawing->sign_count; ++index) {
    if (index && !ComparePaintedSigns(&drawing->signs[index - 1], &drawing->signs[index])) continue;
    const MapSignPaint *sign = &drawing->signs[index];
    DrawNativeCityMapSign(city, drawing->default_map_view->zoom, sign->anchor, sign->text);
  }
  DrawCityConnectionSigns(city);
}

// City-map overlay dispatch.
void DrawCityMapOverlay(City *city, TileIndex tile, int zoom, CityPoint screen) {
  Drawing *drawing = city->drawing;
  int x = MapTileX(city, tile), y = MapTileY(city, tile);
  int screen_x = screen.x, screen_y = screen.y;
  int overlay = ReadOverlay(city, tile);
  int sign = ReadCitySignOverlay(city, tile);
  if (drawing->map_state.label_display && (sign || overlay == CITY_OVERLAY_BORDER_CONNECTION)) {
    if (!sign) {
      if (drawing->label_style && !drawing->map_state.events_suppressed) return;
      // Generated connection text belongs to this paint, never City's saved label storage.
      char connection_text[CITY_OVERLAY_NAME_BYTES];
      FormatCityConnectionSign(city, x, y, connection_text);
      DrawOverlayLabelSoftware(city, zoom, screen_x, screen_y, connection_text);
    } else {
      const char *text = (const char *)ReadCityOverlayName(city, sign)->character;
      if (drawing->label_style) {
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
      } else
        DrawOverlayLabelSoftware(city, zoom, screen_x, screen_y, text);
    }
  }
  bool animated = BeginCityMapOverlayAnimation(city, tile, overlay);
  if (overlay >= CITY_OVERLAY_POLLUTION && overlay < 256) {
    DrawCityMapAnimatedOverlay(city, tile, zoom, overlay);
  } else if (IsCityEventOverlay(overlay)) {
    for (int slot = FirstCityMapEventOnTile(city, tile); slot >= 0;
        slot = NextCityMapEventOnTile(city, slot))
      DrawCityMapEventOverlay(city, tile, zoom, slot);
  }
  if (animated) EndCityMapSpriteAnimation(city);
}
