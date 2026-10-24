#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "assets/state.h"
#include "base/version.h"
#include "city/engine/command_types.h"
#include "city/maps/kinds.h"
#include "city/maps/tile_storage.h"
#include "drawing/bitmaps.h"
#include "drawing/fonts/fonts.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/raster/canvas_raster_internal.h"
#include "drawing/sprites/preparation.h"
#include "drawing/state.h"
#include "game/graphics.h"
#include "game/state.h"
#include "ui/menus/menu.h"
#include "ui/startup/startup.h"
#include "ui/sdl.h"
#include "ui/state.h"
#include "ui/windows/controls.h"

// Resource-backed canvas controls. All raster writes target the final framebuffer.

static const u32 kTerrainFace = 0xFF97835B, kTerrainLight = 0xFFD3BB93, kTerrainDark = 0xFF674F2B;
static const u32 kTerrainAmber = 0xFFFCB030, kTerrainAmberLight = 0xFFFFDC80, kTerrainAmberDark = 0xFFD88C00;
static const u32 kTerrainGreen = 0xFF609C30, kTerrainGreenLight = 0xFF98C85C, kTerrainGreenDark = 0xFF386018;
static const u32 kTerrainFreshWater = 0xFF488CBC;

static int CityStartupMenuIcon(int control) {
  switch (control) {
    case CITY_STARTUP_CONTROL_NEW_CITY: return CITY_TOPBAR_MAP;
    case CITY_STARTUP_CONTROL_LOAD_CITY: return CITY_TOPBAR_FILE;
    case CITY_STARTUP_CONTROL_WEBSITE: return CITY_TOOLBAR_WEBSITE_ICON;
    case CITY_STARTUP_CONTROL_SETTINGS: return CITY_TOPBAR_SETTINGS;
    case CITY_STARTUP_CONTROL_JUKEBOX: return CITY_TOOLBAR_JUKEBOX_ICON;
    case CITY_STARTUP_CONTROL_LOAD_SCENARIO: return CITY_TOPBAR_REWARDS;
    case CITY_STARTUP_CONTROL_EDIT_MAP: return CITY_TOPBAR_LANDSCAPE;
    default: return -1;
  }
}

bool PrepareCityStartupIcons(Game *game) {
  for (int i = 0; i < CITY_STARTUP_WINDOW_LIMIT; ++i) {
    const CityCanvasStartupWindow *layout = &game->ui->startup_windows[i].layout;
    if (!game->ui->startup_windows[i].dialog || !layout->main_menu) continue;
    for (int j = 0; j < layout->control_count; ++j) {
      int icon = CityStartupMenuIcon(layout->controls[j].id);
      if (icon >= 0 && !PrepareCityToolbarIconSize(game, icon, 28)) return false;
    }
  }
  return true;
}

typedef struct {
  const CityCanvasStartupWindow *layout;
  CityRect bounds;
  CanvasRaster raster;
} TerrainPreviewPixels;

static void BuildTerrainPreview(Canvas *pixels, const void *context) {
  const TerrainPreviewPixels *preview = context;
  const CityTerrainMap *map = preview->layout->terrain_preview;
  CanvasRaster raster = preview->raster;
  int width = preview->bounds.right - preview->bounds.left, height = preview->bounds.bottom - preview->bounds.top;
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
      color = map->flags[tile] & TF_SALTWATER ?
          preview->layout->water_colors[SPRITE_WATER_COLORS / 2] : kTerrainFreshWater;
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
    int left = CanvasRasterEdge(px, 0, raster.numerator, raster.denominator);
    int right = CanvasRasterEdge(px + 1, 0, raster.numerator, raster.denominator);
    int top = CanvasRasterEdge(py, 0, raster.numerator, raster.denominator);
    int bottom = CanvasRasterEdge(py + 1, 0, raster.numerator, raster.denominator);
    for (int y = top; y < bottom; ++y)
      for (int x = left; x < right; ++x) pixels->pixels[(size_t)y * pixels->stride + x] = color;
  }
}

static CityRect ControlBounds(const Window *window, const CityCanvasStartupControl *control) {
  CityRect rect = control->bounds;
  rect.left += window->bounds.left + 2; rect.right += window->bounds.left + 2;
  rect.top += window->bounds.top + 16; rect.bottom += window->bounds.top + 16;
  return rect;
}
i32 Sc2kCanvasStartupControlAt(const CityCanvasStartupWindow *layout,
    const Window *window, i32 x, i32 y) {
  for (i32 i = layout->control_count - 1; i >= 0; --i) {
    const CityCanvasStartupControl *control = &layout->controls[i];
    if (!control->visible ||
        (control->control_class != CITY_RESOURCE_CONTROL_BUTTON &&
          control->control_class != CITY_RESOURCE_CONTROL_EDIT &&
          control->control_class != CITY_RESOURCE_CONTROL_SLIDER &&
          control->control_class != CITY_RESOURCE_CONTROL_DROPDOWN) ||
        (control->control_class == CITY_RESOURCE_CONTROL_BUTTON &&
          (control->style & CITY_RESOURCE_BUTTON_TYPE_MASK) == CITY_RESOURCE_BUTTON_GROUP) ||
        (control->style & CITY_RESOURCE_STYLE_DISABLED)) continue;
    if (CanvasPointInRect(ControlBounds(window, control), x, y)) return control->id;
  }
  return 0;
}
static void DrawStartupMenu(Game *game, Canvas *canvas, const Window *window) {
  const CityCanvasStartupWindow *layout = window->startup_layout;
  CityRect b = window->bounds;
  DrawCanvasFrame(canvas, b, kTerrainFace, kTerrainLight, kTerrainDark, true);
  for (int i = 0; i < layout->control_count; ++i) {
    const CityCanvasStartupControl *control = &layout->controls[i];
    if (!control->visible) continue;
    CityRect button = ControlBounds(window, control);
    bool pressed = layout->pressed_control == control->id;
    pressed |= DrawCanvasButtonFrame(canvas, button, kTerrainAmber, kTerrainAmberLight, kTerrainAmberDark, !pressed);
    if (layout->focused_control == control->id)
      DrawCanvasFrame(canvas, (CityRect){button.left + 2, button.top + 2, button.right - 2, button.bottom - 2},
          kTerrainAmber, kTerrainAmberDark, kTerrainAmberDark, true);
    int icon = CityStartupMenuIcon(control->id);
    CityRect text = {button.left + pressed, button.top + pressed, button.right + pressed, button.bottom + pressed};
    if (icon >= 0) {
      DrawCityToolbarIcon(game, canvas, (CityRect){button.left + 3, button.top + 3, button.left + 33, button.top + 33},
          icon, false, false);
      text.left += 40;
      text.right = text.left + (CityInterfaceTextWidth(canvas->interface_font, control->text) * 14 + 9) / 10;
    }
    DrawCityInterfaceTextCentered(canvas->interface_font, canvas, text, control->text, 0xFF101010, false, 14);
  }
  DrawCityInterfaceTextCentered(canvas->interface_font, canvas,
      (CityRect){b.left + 8, b.bottom - 40, b.right - 8, b.bottom - 22},
      SC2K_VERSION_DISPLAY, 0xFF101010, false, 10);
  char revision[SC2K_GIT_REVISION_CAPACITY];
  ReadBuildRevision(revision, sizeof(revision));
  CityRect revision_bounds = {b.left + 8, b.bottom - 23, b.right - 8, b.bottom - 5};
  Canvas local = *canvas;
  local.clip = IntersectCityRects(canvas->clip, revision_bounds);
  DrawCityInterfaceTextCentered(canvas->interface_font, &local, revision_bounds, revision, 0xFF101010, false, 8);
}

void Sc2kCanvasDrawStartupWindow(Game *game, Canvas *canvas, const Window *window) {
  const CityCanvasStartupWindow *layout = window->startup_layout;
  if (layout && layout->main_menu) {
    DrawStartupMenu(game, canvas, window);
    return;
  }
  bool setup = layout && !layout->year_query;
  if (setup) {
    CityRect body = window->bounds;
    body.top += 14;
    DrawCanvasFrame(canvas, body, kTerrainFace, kTerrainLight, kTerrainDark, true);
    DrawCityWindowCaption(canvas, window->bounds, layout->title, kTerrainFace, kTerrainLight, kTerrainDark, 0);
  } else if (layout && layout->year_query) {
    CityRect body = window->bounds;
    body.top += 14;
    DrawCanvasFrame(canvas, body, canvas_face, canvas_light, canvas_dark, true);
    DrawCityWindowCaption(canvas, window->bounds, layout->title, canvas_face, canvas_light, canvas_dark, 0);
  } else {
    CanvasPanelFrame(canvas, window->bounds, layout != NULL ? layout->title : window->title);
  }
  if (layout == NULL) return;
  CityRect saved = canvas->clip;
  for (i32 i = 0; i < layout->control_count; ++i) {
    const CityCanvasStartupControl *control = &layout->controls[i];
    if (!control->visible) continue;
    canvas->clip = saved;
    CityRect rect = ControlBounds(window, control);
    if (control->control_class == CITY_RESOURCE_CONTROL_DROPDOWN) {
      DrawCityDropdownControl(canvas, &kCityDropdownGoldColors, rect, control->text,
          layout->pressed_control == control->id, (control->style & CITY_RESOURCE_STYLE_DISABLED) != 0);
      continue;
    }
    if (control->control_class == CITY_RESOURCE_CONTROL_EDIT) {
      DrawCityTextBox(canvas, rect, control->text, control->text_caret, control->text_scroll,
          layout->focused_control == control->id, layout->selected_text, layout->caret_visible);
      continue;
    }
    if (control->control_class == CITY_RESOURCE_CONTROL_SLIDER) {
      if (setup)
        DrawCanvasFrame(canvas, rect, kTerrainFace, kTerrainLight, kTerrainDark, false);
      else if (layout->focused_control == control->id)
        CanvasBeveledBox(canvas, rect.left, rect.top, rect.right, rect.bottom, 0xFFD0D0D0, false);
      int y = (rect.top + rect.bottom) / 2;
      int x = rect.left + control->checked * (rect.right - rect.left - 1) / 47;
      CanvasFill(canvas, rect.left + 2, y - 1, rect.right - 2, y + 1, setup ? kTerrainDark : 0xFF606060);
      if (setup)
        DrawCanvasFrame(canvas, (CityRect){x - 2, rect.top + 2, x + 3, rect.bottom - 2},
            kTerrainAmber, kTerrainAmberLight, kTerrainAmberDark, true);
      else
        CanvasBeveledBox(canvas, x - 3, rect.top + 2, x + 4, rect.bottom - 2, 0xFFE0E0E0, true);
      continue;
    }
    i32 button = control->control_class == CITY_RESOURCE_CONTROL_BUTTON;
    i32 type = control->style & CITY_RESOURCE_BUTTON_TYPE_MASK;
    i32 radio = button && type == CITY_RESOURCE_BUTTON_RADIO;
    bool checkbox = button && type == CITY_RESOURCE_BUTTON_CHECKBOX;
    i32 group = button && type == CITY_RESOURCE_BUTTON_GROUP;
    i32 edit = control->control_class == CITY_RESOURCE_CONTROL_EDIT;
    i32 pressed = button && control->id == layout->pressed_control;
    bool generate = setup && control->id == CITY_DIALOG_OK;
    bool disabled = control->style & CITY_RESOURCE_STYLE_DISABLED;
    if (button && !group && !disabled) pressed |= RegisterCityCanvasButton(canvas, rect);
    if (checkbox && setup) {
      int top = rect.top + (rect.bottom - rect.top - 10) / 2;
      DrawCityCheckbox(canvas, (CityRect){rect.left, top, rect.left + 20, top + 10}, control->checked,
          kTerrainFace, kTerrainLight, kTerrainDark);
      Canvas label = *canvas;
      label.clip = IntersectCityRects(label.clip, (CityRect){rect.left + 24, rect.top, rect.right, rect.bottom});
      DrawCityInterfaceTextCentered(label.interface_font, &label,
          (CityRect){rect.left + 24, rect.top,
              rect.left + 24 + CityInterfaceTextWidth(label.interface_font, control->text), rect.bottom},
          control->text, disabled ? kTerrainDark : 0xFF101010, false, 10);
      continue;
    }
    if (!setup && button && type == CITY_RESOURCE_BUTTON_DEFAULT)
      CanvasBeveledBox(canvas, rect.left - 2, rect.top - 2, rect.right + 2, rect.bottom + 2, 0xFFC0C0C0u, !pressed);
    if (setup && (button || edit)) {
      u32 fill = edit ? 0xFF101010 : generate ? kTerrainGreen : kTerrainAmber;
      u32 light = edit ? kTerrainLight : generate ? kTerrainGreenLight : kTerrainAmberLight;
      u32 dark = edit ? kTerrainDark : generate ? kTerrainGreenDark : kTerrainAmberDark;
      if (disabled) { fill = kTerrainFace; light = kTerrainLight; dark = kTerrainDark; }
      else if (pressed || (radio && control->checked)) fill = generate ? 0xFF84B850 : 0xFFFCC868;
      if (button && !disabled)
        DrawCanvasButtonFrame(canvas, rect, fill, light, dark, !pressed && !(radio && control->checked));
      else DrawCanvasFrame(canvas, rect, fill, light, dark, !edit && !pressed);
    } else if (group) {
      CanvasBeveledBox(canvas, rect.left, rect.top, rect.right, rect.bottom, 0xFFC0C0C0u, 0);
      CanvasFill(canvas, rect.left + 6, rect.top - 1, rect.left + 12 + CanvasTextWidth(control->text), rect.top + 12,
          0xFFC0C0C0u);
    } else if (radio) {
      i32 y = rect.top + (rect.bottom - rect.top - 12) / 2;
      CanvasBeveledBox(canvas, rect.left, y, rect.left + 12, y + 12, pressed ? 0xFFC0C0C0u : 0xFFFFFFFFu, 0);
      if (control->checked)
        CanvasFill(canvas, rect.left + 4, y + 4, rect.left + 8, y + 8, 0xFF101010u);
    } else if (button || edit)
      CanvasBeveledBox(canvas, rect.left, rect.top, rect.right, rect.bottom,
          edit ? 0xFFFFFFFFu : 0xFFC0C0C0u, button && !pressed);
    if (setup && control->id == CITY_SETUP_RANDOM_NAME) {
      int x = (rect.left + rect.right) / 2 + pressed, y = (rect.top + rect.bottom) / 2 + pressed;
      CanvasFill(canvas, x - 5, y - 5, x + 5, y + 5, 0xFF101010);
      CanvasFill(canvas, x - 4, y - 4, x + 4, y + 4, 0xFFFCFCFC);
      for (int row = -2; row <= 2; row += 4)
        for (int column = -2; column <= 2; column += 4)
          CanvasFill(canvas, x + column, y + row, x + column + 1, y + row + 1, 0xFF101010);
      CanvasFill(canvas, x, y, x + 1, y + 1, 0xFF101010);
      continue;
    }
    if (setup && (control->id == CITY_SETUP_YEAR_PREVIOUS || control->id == CITY_SETUP_YEAR_NEXT)) {
      int x = (rect.left + rect.right) / 2 + pressed;
      int y = (rect.top + rect.bottom) / 2 - 2 + pressed;
      for (int row = 0; row < 4; ++row) {
        int span = control->id == CITY_SETUP_YEAR_NEXT ? row : 3 - row;
        CanvasFill(canvas, x - span - 1, y + row, x + span + 1, y + row + 1, 0xFF101010);
      }
      continue;
    }
    if (setup || (layout->year_query && button)) {
      CityRect label = {rect.left + 2 + pressed, rect.top + pressed, rect.right - 2 + pressed, rect.bottom + pressed};
      if (!button) label.right = label.left + CityInterfaceTextWidth(canvas->interface_font, control->text);
      DrawCityInterfaceTextCentered(canvas->interface_font, canvas, label, control->text,
          generate ? 0xFFFFFFFF : disabled ? kTerrainDark : 0xFF101010, generate, 10);
      continue;
    }
    canvas->clip = saved;
    canvas->clip = IntersectCityRects(canvas->clip,
        (CityRect){rect.left + 2, rect.top + 2, rect.right - 2, rect.bottom - 2});
    char text[CITY_STARTUP_CANVAS_TEXT];
    memcpy(text, control->text, sizeof(text));
    i32 y = button && !group ? rect.top + (rect.bottom - rect.top - 10) / 2 : rect.top + 2;
    for (char *line = text; line != NULL;) {
      char *next = strchr(line, '\n');
      if (next != NULL) *next++ = 0;
      i32 x = radio && !setup ? rect.left + 16 : group ? rect.left + 8 :
          button ? rect.left + (rect.right - rect.left - CanvasTextWidth(line)) / 2 : rect.left + 2;
      CanvasTextAt(canvas, x + pressed, y + pressed, line,
          generate ? 0xFFFFFFFF : disabled ? (setup ? kTerrainDark : 0xFF808080) :
          setup && edit ? 0xFFFFFF80 : 0xFF101010);
      line = next; y += 12;
    }
  }
  canvas->clip = saved;
  if (layout->terrain_preview) {
    const CityTerrainMap *map = layout->terrain_preview;
    const int preview_size = 200;
    int left = window->bounds.left + 282, top = window->bounds.top + 24;
    DrawCanvasFrame(canvas, (CityRect){left - 2, top - 2, left + preview_size + 2, top + preview_size + 2},
        0xFF101010, kTerrainLight, kTerrainDark, false);
    int edge = IntMax(layout->map_width, layout->map_height);
    int width = IntMax(1, preview_size * layout->map_width / edge);
    int height = IntMax(1, preview_size * layout->map_height / edge);
    int preview_left = left + (preview_size - width) / 2, preview_top = top + (preview_size - height) / 2;
    CanvasFill(canvas, left, top, left + preview_size, top + preview_size, kTerrainDark);
    CityRect preview_bounds = {preview_left, preview_top, preview_left + width, preview_top + height};
    CanvasRaster raster = CanvasRasterView(canvas);
    TerrainPreviewPixels preview = {layout, {0, 0, width, height}, raster};
    CanvasBitmapPaint paint = {.bounds = preview_bounds, .build = BuildTerrainPreview, .context = &preview};
    CityTerrainPreviewDrawing *pixels = layout->drawing;
    if (!DrawCachedCanvasBitmap(canvas, &pixels->preview, &paint))
      game->execution_failed = true;
    char summary[128];
    snprintf(summary, sizeof(summary), "Water: %.1f%%  Trees: %.1f%%  Height: %d-%d",
        100.0 * map->water_tiles / map->cell_count, 100.0 * map->tree_tiles / map->cell_count,
        map->minimum_height, map->maximum_height);
    DrawCityInterfaceTextCentered(canvas->interface_font, canvas,
        (CityRect){left, top + 224, left + preview_size, top + 238}, summary, 0xFF101010, false, 8);
  }
  canvas->clip = saved;
}

int CityStartupContentTop(int width, int height) {
  // The baker puts the 170-pixel identity at y=12 in this same background.
  return IntMax(width * 182 / 1024, height * 182 / 683) + 8;
}

// Loading artwork uses the shared row resampler and sprite drawer. Streaming
// avoids retaining a scaled full-screen copy of the backdrop.
static void DrawIntroImage(Game *game, Canvas *canvas, int id, int left, int top, int scale) {
  const RunSprite *image = game->resources->intro.images[id];
  CityRect bounds = {left, top, left + IntMax(1, image->width * scale / 65536),
      top + IntMax(1, image->height * scale / 65536)};
  if (!DrawCanvasRunSprite(canvas, image, bounds, 0)) game->execution_failed = true;
}

void DrawCityStartupBackdrop(Game *game, Canvas *canvas, CityRect area) {
  const RunSprite *image = game->resources->intro.images[CITY_INTRO_CITY_ART];
  int scale = IntMax((int)(((i64)canvas->width * 65536 + image->width - 1) / image->width),
      (int)(((i64)canvas->height * 65536 + image->height - 1) / image->height));
  Canvas clipped = *canvas;
  clipped.clip = IntersectCityRects(canvas->clip, area);
  DrawIntroImage(game, &clipped, CITY_INTRO_CITY_ART, (canvas->width - image->width * scale / 65536) / 2, 0, scale);
}

static void DrawIntroProgress(Canvas *canvas, int left, int top, int width, int height, int percent) {
  double radius = height / 2.0;
  for (int y = IntMax(0, top - 1); y < IntMin(canvas->height, top + height + 1); ++y)
    for (int x = IntMax(0, left - 1); x < IntMin(canvas->width, left + width + 1); ++x) {
      double dx = fmax(left + radius - x - .5, x + .5 - (left + width - radius));
      double dy = y + .5 - (top + radius);
      double coverage = fmax(0, fmin(1, radius + .5 - hypot(fmax(0, dx), dy)));
      if (!coverage) continue;
      u32 color = x < left + width * percent / 100 ? 0xFFFCB030 : 0xFF67532B;
      size_t index = (size_t)y * canvas->stride + x;
      u32 background = canvas->pixels[index], result = 0xFF000000;
      for (int shift = 0; shift <= 16; shift += 8)
        result |= (u32)((color >> shift & 255) * coverage + (background >> shift & 255) * (1 - coverage) + .5)
            << shift;
      canvas->pixels[index] = result;
    }
}

bool DrawCityLoadingScreen(Game *game, CanvasSDL *display, CityIntroStage stage,
    int completed, int total, bool initial) {
  Drawing *drawing = game->drawing;
  int width = drawing->framebuffer.width, height = drawing->framebuffer.height;
  if (!PrepareCityScreenBuffer(game, display) || Sc2kSDLConfigureFramebuffer(display, 100)) return false;
  Canvas canvas = drawing->framebuffer;
  const CityIntroArt *art = &game->resources->intro;
  assert(art->images[0] && stage >= 0 && stage < CITY_INTRO_STAGE_COUNT);
  initial |= width != canvas.width || height != canvas.height;
  // Both screens share the still backdrop and the same baked game identity.
  int scale = IntClamp(IntMin(canvas.width * 65536 / 704, canvas.height * 65536 / 404), 16384, 98304);
  int panel_width = 620 * scale / 65536, panel_height = 160 * scale / 65536;
  int left = (canvas.width - panel_width) / 2;
  int top = (CityStartupContentTop(canvas.width, canvas.height) + canvas.height - panel_height) / 2;
  CityRect panel = {left, top, left + panel_width, top + panel_height};
  CityRect dirty = initial ? canvas.clip : panel;
  DrawCityStartupBackdrop(game, &canvas, dirty);
  DrawCanvasFrame(&canvas, panel, 0xFF97835B, 0xFFD3BB93, 0xFF674F2B, true);
  int label = stage;
  DrawIntroImage(game, &canvas, label, (canvas.width - (int)art->images[label]->width * scale / 65536) / 2,
      top + 32 * scale / 65536, scale);
  int percent = total > 0 ? (int)((i64)IntClamp(completed, 0, total) * 100 / total) : 0;
  int bar_width = 360 * scale / 65536;
  DrawIntroProgress(&canvas, (canvas.width - bar_width) / 2, top + 82 * scale / 65536,
      bar_width, IntMax(3, 4 * scale / 65536), percent);
  char text[8]; snprintf(text, sizeof(text), "%d%%", percent);
  int percent_width = 0;
  for (const char *c = text; *c; ++c) {
    int image = CITY_INTRO_DIGIT_FIRST + (*c == '%' ? 10 : *c - '0');
    percent_width += art->images[image]->width * scale / 65536;
  }
  int x = (canvas.width - percent_width) / 2;
  for (const char *c = text; *c; ++c) {
    int image = CITY_INTRO_DIGIT_FIRST + (*c == '%' ? 10 : *c - '0');
    DrawIntroImage(game, &canvas, image, x, top + 108 * scale / 65536, scale);
    x += art->images[image]->width * scale / 65536;
  }
  if (game->execution_failed) return false;
  ClearScreenDamage(&drawing->upload_damage);
  MarkScreenDirty(&drawing->upload_damage, dirty);
  return Sc2kSDLPresentFramebuffer(display, &drawing->framebuffer, &drawing->upload_damage, (CityRect){0}) == 0;
}
