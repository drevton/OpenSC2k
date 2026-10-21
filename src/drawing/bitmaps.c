#include "city/maps/layers.h"
#include "city/maps/buildings.h"
#include "city/maps/trees.h"
#include "city/engine/city_types.h"
#include "city/maps/tile_storage.h"
#include "city/tools/types.h"
#include "game/application.h"
#include "ui/tools/types.h"
#include "ui/tools/tools.h"
#include "base/byte_order.h"
#include "drawing/bitmaps.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/assets.h"
#include "game/state.h"
#include "city/engine/commands.h"
#include "city/tools/tools.h"
#include "city/state.h"
#include "game/errors.h"
#include "city/saveload/services.h"
#include "ui/map.h"
#include "drawing/drawing.h"
#include "drawing/drawing_internal.h"
#include "drawing/maps/map_render.h"
#include "drawing/maps/tables.h"
#include "drawing/sprites/sprites.h"
#include "drawing/state.h"
#include "assets/graphics.h"
#include "assets/state.h"
#include "ui/charts/charts.h"
#include "ui/commands.h"




enum { CITY_PALETTE_ENTRY_RESERVED = 1 };

// Drawing Resources
// Native indexed map storage and presentation. Original DIB lifecycle lives in comparison.

bool EnsureCityMapViewSurface(Drawing *drawing, CityMapViewConfig *view) {
  if (view->pixels != NULL) return true;
  int width = drawing->client_rect.right - drawing->client_rect.left;
  int height = drawing->client_rect.bottom - drawing->client_rect.top;
  if (width <= 0 || width > 600 || height <= 0 || height > 490) return false;
  view->pixels = drawing->map_framebuffer;
  view->paint_pixel_stride = 600;
  view->paint_mode_word = height;
  return true;
}

// Present indexed map pixels through the host canvas; City state is untouched.
void Sc2kCanvasBlitCitySurface(Canvas *canvas, CityMapViewConfig *view,
    CityRect destination, CityRect source, const u32 *palette) {
  const u8 *bits = view->pixels;
  int width = view->paint_pixel_stride, height = view->paint_mode_word;
  int direction = -1;
  if (bits == NULL) return;
  i64 dw = (i64)destination.right - destination.left;
  i64 dh = (i64)destination.bottom - destination.top;
  i64 sw = (i64)source.right - source.left;
  i64 sh = (i64)source.bottom - source.top;
  if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0)
    return;
  int left = IntMax(destination.left, canvas->clip.left);
  int top = IntMax(destination.top, canvas->clip.top);
  int right = IntMin(destination.right, canvas->clip.right);
  int bottom = IntMin(destination.bottom, canvas->clip.bottom);
  left = IntMax(left, 0);
  top = IntMax(top, 0);
  right = IntMin(right, canvas->width);
  bottom = IntMin(bottom, canvas->height);
  size_t stride = ((size_t)width + 3) & ~(size_t)3;
  for (int y = top; y < bottom; ++y) {
    i64 sy = source.top + (i64)((u64)((i64)y - destination.top) * (u64)sh / (u64)dh);
    if (sy < 0 || sy >= height)
      continue;
    if (direction > 0)
      sy = height - 1 - sy;
    for (int x = left; x < right; ++x) {
      i64 sx = source.left + (i64)((u64)((i64)x - destination.left) * (u64)sw / (u64)dw);
      if (sx >= 0 && sx < width)
        CanvasStore(canvas, x, y, palette[bits[(size_t)sy * stride + (size_t)sx]]);
    }
  }
}

CityPaintBuffer BorrowCityMapViewPixels(Drawing *drawing, CityMapViewConfig *view) {
  if (!EnsureCityMapViewSurface(drawing, view)) return NULL;
  return view->pixels;
}

CityMapImmediatePaintState ReadCityMapImmediatePaint(const CityMapViewConfig *view) {
  return (CityMapImmediatePaintState){view->pixels, view->paint_pixel_stride, view->paint_mode_word, view->view_rect};
}

// Drawing Palette
// Drawing subsystem: drawing palette.

// Snapshot the active palette and initialize its two animation ranges.
void InitializeCityPaletteAnimation(Drawing *drawing) {
  CityPaletteState *state = &drawing->palette;
  for (int index = 0; index < 256; ++index) {
    CityPaletteEntry *entry = &state->source.entry[index];
    u32 color = drawing->display_palette[index];
    entry->red = (u8)(color >> 16);
    entry->green = (u8)(color >> 8);
    entry->blue = (u8)color;
    entry->flags = 0;
  }
  state->fast_tick = 0;
  state->slow_tick = 0;
  for (int index = 0; index < 49; ++index) {
    state->fast[index] = state->source.entry[0xAB + index];
    state->fast[index].flags = CITY_PALETTE_ENTRY_RESERVED;
  }
  for (int index = 0; index < 16; ++index) {
    state->slow[index] = state->source.entry[0xE0 + index];
    state->slow[index].flags = CITY_PALETTE_ENTRY_RESERVED;
  }
}

// A project-authored compatibility palette. Most native artwork carries its own RGB or palette.
void InitializeNativeCityPalette(Drawing *drawing) {
  static const u32 kBase[] = {0x101418, 0x304838, 0x283C60, 0x706028,
      0x703830, 0x68486C, 0x387878, 0x787C78, 0xB0B4AC, 0x70A060,
      0x6498D0, 0xD8BC58, 0xC85C48, 0xAC80B4, 0x78B8B0, 0xF4F0DC};
  for (int index = 0; index < 256; ++index) {
    u32 base = kBase[index / 16];
    int shade = 64 + (index % 16) * 12;
    int red = IntMin(255, (int)(base >> 16) * shade / 160);
    int green = IntMin(255, (int)((base >> 8) & 255) * shade / 160);
    int blue = IntMin(255, (int)(base & 255) * shade / 160);
    drawing->colors[index] = 0xFF000000 | red << 16 | green << 8 | blue;
  }
  drawing->colors[0] = 0xFF000000;
  drawing->colors[255] = 0xFFFFFFFF;
}

// Apply the stock entry-index permutation to one palette block.
u8 ReorderCityPaletteEntries(CityPaletteState *palette, int palette_49_entries) {
  static const u8 kFastOrder[49] = {
    1, 2, 3, 4, 5, 6, 7, 0, 9, 10, 11, 12, 13, 14, 15, 8,
    17, 18, 19, 20, 21, 22, 23, 16, 25, 26, 27, 24, 28, 36, 29, 30,
    31, 32, 33, 34, 35, 40, 37, 38, 39, 48, 41, 42, 43, 44, 45, 46,
    47
  };
  static const u8 kSlowOrder[16] = {
    1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 14, 0
  };
  CityPaletteEntry temporary[49];
  CityPaletteEntry *entries = palette_49_entries != 0 ? palette->fast :
      palette->slow;
  const u8 *order = palette_49_entries != 0 ? kFastOrder : kSlowOrder;
  int count = palette_49_entries != 0 ? 49 : 16;
  for (int index = 0; index < count; ++index)
    temporary[index] = entries[index];
  for (int index = 0; index < count; ++index)
    entries[index] = temporary[order[index]];
  return entries[count - 1].flags;
}

// Update the active palette and its guest-visible 256-entry snapshot.
int AnimateCityPalette(City *city, int slow_palette) {
  Drawing *drawing = city->game->drawing;
  CityPaletteState *state = &drawing->palette;
  int idle_state = city->game->idle_state;
  if (city->settings.speed == CITY_SPEED_PAUSED && state->direct_animation == 0 &&
      idle_state == CITY_IDLE_PLAY)
    return (int)0xFFFFFFFFu;
  if (drawing->display_palette != NULL)
    CaptureCityPalette(state, drawing->display_palette);
  ReorderCityPaletteEntries(state, slow_palette == 0);
  if (drawing->display_palette != NULL)
    ApplyAnimatedCityPalette(state, drawing->display_palette);
  return 0;
}

// Select the software display palette without changing guest animation state.
void SetCityDisplayPalette(Drawing *drawing, u32 *colors) {
  drawing->display_palette = colors;
}

// Initialize the headless renderer's active palette and the two stock animation blocks.
void InitializeCityDisplayPalette(Drawing *drawing, u32 *colors) {
  drawing->display_palette = colors;
  CaptureCityPalette(&drawing->palette, colors);
  InitializeCityPaletteAnimation(drawing);
}

// Mirror GetPaletteEntries before the timer callback mutates its active palette block.
void CaptureCityPalette(CityPaletteState *palette, const u32 *colors) {
  for (int index = 0; index < 256; ++index) {
    CityPaletteEntry *entry = &palette->current[index];
    u32 color = colors[index];
    entry->red = (u8)(color >> 16);
    entry->green = (u8)(color >> 8);
    entry->blue = (u8)color;
    entry->flags = 0;
  }
}

// Apply the active guest animation blocks to the indexed renderer's RGBA palette.
void ApplyAnimatedCityPalette(const CityPaletteState *palette, u32 *colors) {
  for (int index = 0; index < 49; ++index) {
    const CityPaletteEntry *entry = &palette->fast[index];
    colors[0xAB + index] = 0xFF000000u | (u32)entry->red << 16 |
        (u32)entry->green << 8 | entry->blue;
  }
  for (int index = 0; index < 16; ++index) {
    const CityPaletteEntry *entry = &palette->slow[index];
    colors[0xE0 + index] = 0xFF000000u | (u32)entry->red << 16 |
        (u32)entry->green << 8 | entry->blue;
  }
}

CityMapSprite *CityDrawingSpriteCatalog(const Drawing *drawing) { return drawing->map_state.sprite_catalog; }

void ReleaseCityToolbarIcons(Drawing *drawing) {
  for (int icon = 0; icon < CITY_TOOLBAR_ICON_COUNT; ++icon)
    for (int variant = 0; variant < CITY_TOOLBAR_ICON_CACHE_VARIANTS; ++variant)
      FreeRunSprite(drawing->toolbar_icons[icon][variant]);
  memset(drawing->toolbar_icons, 0, sizeof(drawing->toolbar_icons));
  memset(drawing->toolbar_icon_next, 0, sizeof(drawing->toolbar_icon_next));
  drawing->toolbar_icon_source = NULL;
  drawing->toolbar_icon_data = NULL;
  drawing->toolbar_icon_scale = 0;
}

static const RunSprite *FindCityToolbarIcon(Drawing *drawing, int icon, int width, int height) {
  RunSprite **variants = drawing->toolbar_icons[icon];
  for (int i = 0; i < CITY_TOOLBAR_ICON_CACHE_VARIANTS; ++i)
    if (variants[i] && variants[i]->width == width && variants[i]->height == height)
      return variants[i];
  return NULL;
}

static const RunSprite *PrepareCityToolbarIcon(Drawing *drawing, int icon, int width, int height, bool button) {
  const RunSprite *cached = FindCityToolbarIcon(drawing, icon, width, height);
  if (cached) return cached;
  RunSprite **variants = drawing->toolbar_icons[icon], **slot = NULL;
  int first = button ? 0 : CITY_TOOLBAR_ICON_BUTTON_VARIANTS;
  int end = button ? CITY_TOOLBAR_ICON_BUTTON_VARIANTS : CITY_TOOLBAR_ICON_CACHE_VARIANTS;
  for (int i = first; i < end; ++i)
    if (!variants[i]) { slot = &variants[i]; break; }
  bool flipped = false;
  const RunSprite *source = GetInterfaceSprite(drawing->toolbar_icon_source, icon,
      InterfaceSpriteLevel(IntMax(drawing->toolbar_icon_scale, (height * 100 + 31) / 32)), &flipped);
  RunSprite *reference = ReferenceRunSprite(source, source->palette, flipped);
  if (!reference) return NULL;
  RunSprite *prepared = ResizeRunSprite(reference, width, height);
  FreeRunSprite(reference);
  if (!prepared) return NULL;
  // Preview resizing does not evict button sizes. Flipping is a draw flag.
  if (!slot) slot = &variants[first + drawing->toolbar_icon_next[icon]++ % (end - first)];
  FreeRunSprite(*slot);
  *slot = prepared;
  return prepared;
}

int CityToolbarIconColumns(int icon) {
  return icon == CITY_TOPBAR_ICON_COUNT + CITY_CANVAS_TOOL_ROAD_BRIDGE ||
      icon == CITY_TOPBAR_ICON_COUNT + CITY_CANVAS_TOOL_RAIL_BRIDGE ? 2 : 1;
}

bool PrepareCityToolbarIcons(Game *game) {
  Drawing *drawing = game->drawing;
  const CityInterfaceIcons *source = &game->resources->main_toolbar_icons;
  int scale = drawing->options.scale;
  if (drawing->toolbar_icon_scale == scale && drawing->toolbar_icon_data == source->data.data) return true;
  ReleaseCityToolbarIcons(drawing);
  if (!source->data.data) return true;
  drawing->toolbar_icon_source = source;
  drawing->toolbar_icon_data = source->data.data;
  drawing->toolbar_icon_scale = scale;
  // Position-independent image dimensions need one prepared size per UI scale.
  int size = (20 * scale + 99) / 100;
  for (int icon = 0; icon < CITY_TOOLBAR_ICON_COUNT; ++icon) {
    int width = (20 * CityToolbarIconColumns(icon) * scale + 99) / 100;
    if (!PrepareCityToolbarIcon(drawing, icon, width, size, true)) return false;
  }
  return true;
}

bool PrepareCityToolbarIconSize(Game *game, int icon, int size) {
  Drawing *drawing = game->drawing;
  if (!drawing->toolbar_icon_source || size <= 0) return true;
  int pixels = (size * drawing->options.scale + 99) / 100;
  int width = (size * CityToolbarIconColumns(icon) * drawing->options.scale + 99) / 100;
  return PrepareCityToolbarIcon(drawing, icon, width, pixels, false) != NULL;
}

typedef enum BitmapCacheOperation { BITMAP_INVALIDATE, BITMAP_INVALIDATE_PALETTE, BITMAP_RELEASE } BitmapCacheOperation;

static void UpdateBitmapArray(CityBitmapCache *images, int count, BitmapCacheOperation operation, u32 buckets) {
  for (int i = 0; i < count; ++i) {
    if (operation == BITMAP_RELEASE) FreeRunSprite(images[i].sprite);
    else if (operation == BITMAP_INVALIDATE || (images[i].palette_buckets & buckets)) images[i].valid = false;
  }
}

static void UpdateSpriteImages(CitySpriteImage *images, int count, BitmapCacheOperation operation) {
  if (operation == BITMAP_INVALIDATE_PALETTE) return;
  for (int i = 0; i < count; ++i) ReleaseCitySpriteImage(&images[i]);
}

void ReleaseCitySpriteImage(CitySpriteImage *image) {
  FreeRunSprite(image->source);
  FreeRunSprite(image->prepared);
  *image = (CitySpriteImage){0};
}

// Lifecycle dispatch visits the actual fields of each specialized drawing object.
static void UpdateWindowBitmaps(CityWindowDrawing *state, BitmapCacheOperation operation, u32 buckets) {
  switch (state->kind) {
    case CITY_WINDOW_DRAWING_MAP_DATA: {
      CityMapDataWindowDrawing *pixels = (CityMapDataWindowDrawing *)state;
      UpdateBitmapArray(&pixels->preview, 1, operation, buckets);
      UpdateSpriteImages(pixels->categories, CITY_MAP_BITMAP_CATEGORIES, operation);
      UpdateSpriteImages(pixels->options, CITY_MAP_BITMAP_OPTIONS, operation);
      UpdateSpriteImages(&pixels->overlay, 1, operation);
      UpdateSpriteImages(&pixels->projection, 1, operation);
      break;
    }
    case CITY_WINDOW_DRAWING_BRIDGE_CHOICES:
      UpdateSpriteImages(((CityBridgeWindowDrawing *)state)->icons, CITY_BRIDGE_BITMAP_COUNT, operation);
      break;
    case CITY_WINDOW_DRAWING_TREE_PICKER:
      UpdateSpriteImages(((CityTreePickerDrawing *)state)->trees, CITY_TREE_BITMAP_COUNT, operation);
      break;
    case CITY_WINDOW_DRAWING_SPRITE:
    case CITY_WINDOW_DRAWING_NEIGHBORS:
    case CITY_WINDOW_DRAWING_MUSIC: {
      if (operation == BITMAP_RELEASE) {
        CitySpriteDrawing *drawing = (CitySpriteDrawing *)state;
        UpdateSpriteImages(drawing->images, drawing->count, operation);
      }
      break;
    }
    case CITY_WINDOW_DRAWING_TERRAIN_PREVIEW:
      UpdateBitmapArray(&((CityTerrainPreviewDrawing *)state)->preview, 1, operation, buckets);
      break;
  }
}

void InvalidateCityWindowBitmaps(CityWindowDrawing *state) {
  if (state) UpdateWindowBitmaps(state, BITMAP_INVALIDATE, 0);
}

void InvalidateCityWindowPaletteBitmaps(Drawing *drawing, u32 changed_buckets) {
  for (CityWindowDrawing *state = drawing->window_drawing; state; state = state->next)
    UpdateWindowBitmaps(state, BITMAP_INVALIDATE_PALETTE, changed_buckets);
}

void ReleaseCityWindowDrawing(CityWindowDrawing *state) {
  CityWindowDrawing **link = &state->owner->window_drawing;
  while (*link != state) link = &(*link)->next;
  *link = state->next;
  UpdateWindowBitmaps(state, BITMAP_RELEASE, 0);
  free(state);
}

CityWindowDrawing *CreateCityWindowDrawing(Drawing *drawing, CityWindowDrawingKind kind) {
  static const size_t kDrawingSizes[] = {sizeof(CityMapDataWindowDrawing), sizeof(CityBridgeWindowDrawing),
      sizeof(CityTreePickerDrawing), sizeof(CityTerrainPreviewDrawing), sizeof(CitySpriteDrawing),
      sizeof(CitySpriteDrawing), sizeof(CitySpriteDrawing)};
  int images = kind == CITY_WINDOW_DRAWING_MUSIC ? CITY_MUSIC_SPRITE_COUNT :
      kind == CITY_WINDOW_DRAWING_NEIGHBORS ? CITY_CANVAS_NEIGHBOR_ICON_COUNT :
      kind == CITY_WINDOW_DRAWING_SPRITE ? 1 : 0;
  CityWindowDrawing *state = calloc(1, kDrawingSizes[kind] + images * sizeof(CitySpriteImage));
  if (!state) return NULL;
  *state = (CityWindowDrawing){.owner = drawing, .next = drawing->window_drawing, .kind = kind};
  drawing->window_drawing = state;
  if (images) ((CitySpriteDrawing *)state)->count = images;
  return state;
}

// Window-local bounds supply the position and clip. Already prepared images hit
// DrawScaledRunSprite's direct draw path; uncached backdrops use only row scratch.
static bool DrawCanvasRunSprite(Canvas *canvas, const RunSprite *sprite, CityRect bounds, u32 flags) {
  CityRect box = CanvasRasterImageBounds(canvas, bounds);
  CanvasRaster raster = CanvasRasterView(canvas);
  SpriteSurface surface = {.canvas = *canvas, .indices = canvas->palette_indices,
      .index_stride = canvas->index_stride, .row_flags = canvas->palette_rows};
  surface.canvas.width = raster.width;
  surface.canvas.height = raster.height;
  surface.canvas.clip = IntersectCityRects(box, CanvasRasterRect(canvas, canvas->clip));
  surface.canvas.raster = (CanvasRaster){0};
  return DrawScaledRunSprite(&surface, sprite, box, flags);
}

bool DrawCanvasSpriteImage(Canvas *canvas, CitySpriteImage *image, const RunSprite *source,
    CityRect content, CityRect bounds, u32 flags) {
  if (!source) return true;
  CityRect box = CanvasRasterImageBounds(canvas, bounds);
  CityRect clip = IntersectCityRects(box, CanvasRasterRect(canvas, canvas->clip));
  if (clip.left >= clip.right || clip.top >= clip.bottom) return true;
  int width = box.right - box.left, height = box.bottom - box.top;
  if (!image) {
    assert(content.left == 0 && content.top == 0 && content.right == source->width && content.bottom == source->height);
    return DrawCanvasRunSprite(canvas, source, bounds, flags);
  }
  if (image->source != source || !image->prepared ||
      image->prepared->width != width || image->prepared->height != height ||
      memcmp(&image->content, &content, sizeof(content))) {
    RunSprite *prepared = ResizeRunSpriteRegion(source, content, width, height);
    if (!prepared) return false;
    RunSprite *retained = RetainRunSprite(source);
    ReleaseCitySpriteImage(image);
    *image = (CitySpriteImage){retained, prepared, content};
  }
  return DrawCanvasRunSprite(canvas, image->prepared, bounds, flags);
}

// Windows with several fixed pictures retain a bounded set of normal preparations.
bool DrawCanvasSprite(Canvas *canvas, CitySpriteDrawing *drawing, const RunSprite *source, CityRect bounds) {
  if (!source) return true;
  CitySpriteImage *image = NULL;
  for (int i = 0; i < drawing->count; ++i)
    if (drawing->images[i].source == source) { image = &drawing->images[i]; break; }
  if (!image) {
    image = &drawing->images[drawing->next];
    drawing->next = (drawing->next + 1) % drawing->count;
  }
  return DrawCanvasSpriteImage(canvas, image, source, (CityRect){0, 0, source->width, source->height}, bounds, 0);
}

// Procedural map previews keep their existing builder. Store its result as ordinary
// runs, releasing temporary pixels immediately; repeated paints share the sprite drawer.
bool DrawCityCachedBitmap(Canvas *canvas, CityBitmapCache *cache, const CityBitmapPaint *paint) {
  CityRect box = CanvasRasterImageBounds(canvas, paint->bounds);
  CityRect clip = IntersectCityRects(box, CanvasRasterRect(canvas, canvas->clip));
  if (clip.left >= clip.right || clip.top >= clip.bottom) return true;
  int width = box.right - box.left, height = box.bottom - box.top;
  if (width > SCREEN_DAMAGE_MAX_WIDTH || height > SCREEN_DAMAGE_MAX_HEIGHT) return false;
  if (!cache->valid || !cache->sprite || cache->sprite->width != width || cache->sprite->height != height) {
    u32 *pixels = calloc((size_t)width * height, sizeof(*pixels));
    if (!pixels) return false;
    Canvas target = {.pixels = pixels, .width = width, .height = height, .stride = width,
        .clip = {0, 0, width, height}};
    u32 buckets = paint->build(&target, paint->context);
    RunSprite *sprite = EncodeRgbaRunSprite(pixels, width, height, width);
    free(pixels);
    if (!sprite) return false;
    FreeRunSprite(cache->sprite);
    cache->sprite = sprite;
    cache->palette_buckets = buckets;
    cache->valid = true;
  }
  for (int y = clip.top; y < clip.bottom; ++y)
    RecordCanvasPaletteComposite(canvas, y, clip.left, clip.right, cache->palette_buckets);
  return DrawCanvasRunSprite(canvas, cache->sprite, paint->bounds, 0);
}

bool DrawCityInterfaceIcon(Canvas *canvas, CitySpriteImage *cache, const CityInterfaceIcons *icons,
    int icon, CityRect bounds) {
  CityRect box = CanvasRasterImageBounds(canvas, bounds);
  int height = box.bottom - box.top, logical = bounds.bottom - bounds.top;
  int scale = logical > 0 ? height * 100 / logical : 100;
  bool flipped = false;
  const RunSprite *source = GetInterfaceSprite(icons, icon,
      InterfaceSpriteLevel(IntMax(scale, (height * 100 + 31) / 32)), &flipped);
  return DrawCanvasSpriteImage(canvas, cache, source, (CityRect){0, 0, source->width, source->height},
      bounds, flipped ? SPRITE_FLIPPED : 0);
}

// Every species uses its climate's common crop, keeping tree sizes and ground alignment.
void DrawCityTreePreview(Game *game, Canvas *canvas, CitySpriteImage *cache, CityRect bounds, int species) {
  int art = kCityTreeArtLayouts[species * 4][0];
  const RunSprite *sprite = game->resources->landscape[CITY_LANDSCAPE_TREE_FIRST + art * 7 + CITY_TREE_GROWN].sprite;
  if (!sprite || bounds.right <= bounds.left || bounds.bottom <= bounds.top) return;
  CityRect source = game->resources->tree_preview_bounds[species >= 20 ? 2 : species >= 12 ? 1 : 0];
  int source_width = source.right - source.left, source_height = source.bottom - source.top;
  if (source_width <= 0 || source_height <= 0) return;
  int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
  if ((i64)width * source_height > (i64)height * source_width) width = height * source_width / source_height;
  else height = width * source_height / source_width;
  bounds.left += (bounds.right - bounds.left - width) >> 1;
  bounds.right = bounds.left + width;
  bounds.top = bounds.bottom - height;
  if (!DrawCanvasSpriteImage(canvas, cache, sprite, source, bounds, 0)) game->execution_failed = true;
}

// Construction uses prepared runs and the ordinary sprite renderer.
void DrawCityBuildingPreview(Game *game, Canvas *canvas, CityRect bounds, int tool) {
  Drawing *drawing = game->drawing;
  int kind = CityToolSpriteTile(tool);
  int orientation = (game->ui->placement_rotation + game->city->geography.rotation +
      CityMapViewRotation(drawing->default_map_view)) & 3;
  int art = CityBuildingSprite(game->city, kind, orientation);
  int view = art < 0 ? CITY_CIVIC_VIEWS : (art - CityBuildingSpriteFirst(kind)) & 3;
  int preview = kCityMapKindSprites[kind] - CITY_CIVIC_KIND_FIRST;
  const RunSprite *sprite = drawing->sprite_cache.building_previews[preview][view];
  if (kind == TK_WIND_POWER && art >= 0 &&
      game->city->geography.start_year + game->city->clock.year >= CITY_WIND_MODERN_YEAR)
    sprite = drawing->sprite_cache.modern_wind_previews[view];
  if (!sprite) { game->execution_failed = true; return; }
  CityRect box = CanvasRasterImageBounds(canvas, bounds);
  CanvasRaster raster = CanvasRasterView(canvas);
  SpriteSurface surface = drawing->screen_surface;
  surface.canvas = *canvas;
  surface.canvas.width = raster.width;
  surface.canvas.height = raster.height;
  surface.canvas.clip = CanvasRasterRect(canvas, IntersectCityRects(canvas->clip, bounds));
  surface.canvas.raster = (CanvasRaster){0};
  bool flipped = art < 0 && (orientation & 1);
  DrawRunSprite(&surface, sprite, box.left + (box.right - box.left - sprite->width) / 2,
      box.top + (box.bottom - box.top - sprite->height) / 2, SPRITE_COPY | (flipped ? SPRITE_FLIPPED : 0), NULL);
}

int CityToolSpriteTile(int tool) {
  if ((tool >= CITY_CANVAS_TOOL_POLICE && tool <= CITY_CANVAS_TOOL_NUCLEAR) ||
      (tool >= CITY_CANVAS_TOOL_SUBWAY_STATION && tool <= CITY_CANVAS_TOOL_WATER_PUMP)) {
    int facility = Sc2kCanvasMapPointerTools(tool, 0);
    return GetCityFacilityTile(facility & 255, (facility >> 8) & 255);
  }

  static const u16 kTiles[CITY_CANVAS_TOOL_COUNT] = {
    [CITY_CANVAS_TOOL_PIPES] = CITY_SPRITE_CATALOG_PIPE_EW,
    [CITY_CANVAS_TOOL_SUBWAY] = CITY_SPRITE_CATALOG_SUBWAY_EW,
    [CITY_CANVAS_TOOL_POWER_LINES] = TK_POWER_LINE,
    [CITY_CANVAS_TOOL_SUBWAY_RAIL_CONNECTION] = TK_SUBWAY_TO_RAIL,
    [CITY_CANVAS_TOOL_HYDRO] = TK_HYDRO_POWER,
    [CITY_CANVAS_TOOL_DISPATCH_POLICE] = TK_POLICE_STATION,
    [CITY_CANVAS_TOOL_DISPATCH_FIRE] = TK_FIRE_STATION,
    [CITY_CANVAS_TOOL_DISPATCH_MILITARY] = TK_BRAUN_LLAMA_DOME,
    [CITY_CANVAS_TOOL_MARINA] = TK_MARINA
  };
  return tool >= 0 && tool < CITY_CANVAS_TOOL_COUNT ? kTiles[tool] : 0;
}

void DrawCityToolbarIcon(Game *game, Canvas *canvas, CityRect bounds, int icon, bool disabled, bool flipped) {
  Drawing *drawing = game->drawing;
  if (!drawing->toolbar_icon_source) return;
  CityRect clip = canvas->clip;
  canvas->clip = IntersectCityRects(clip,
      (CityRect){bounds.left + 1, bounds.top + 1, bounds.right - 1, bounds.bottom - 1});
  int columns = CityToolbarIconColumns(icon);
  int size = IntMin((bounds.right - bounds.left - 2 * columns) / columns, bounds.bottom - bounds.top - 2);
  int pressed = !disabled && CityCanvasButtonPressed(canvas, bounds);
  int left = bounds.left + ((bounds.right - bounds.left - size * columns) >> 1) + pressed;
  int top = bounds.top + ((bounds.bottom - bounds.top - size) >> 1) + pressed;
  bounds = (CityRect){left, top, left + size * columns, top + size};
  CityRect box = CanvasRasterImageBounds(canvas, bounds);
  int width = box.right - box.left, height = box.bottom - box.top;
  CityRect area = IntersectCityRects(box, CanvasRasterRect(canvas, canvas->clip));
  if (area.left < area.right && area.top < area.bottom) {
    const RunSprite *sprite = FindCityToolbarIcon(drawing, icon, width, height);
    if (!sprite) game->execution_failed = true;
    else if (!DrawCanvasRunSprite(canvas, sprite, bounds,
        (flipped ? SPRITE_FLIPPED : 0) | (disabled ? SPRITE_DISABLED : 0))) game->execution_failed = true;
  }
  canvas->clip = clip;
}

// Animate an enabled background palette while marking its refresh callback lifetime. Returns false when background
// animation is disabled; requires Drawing's initialized palette state.
bool RefreshActiveCityPalette(City *city) {
  CityPaletteState *palette = &city->game->drawing->palette;
  if (palette->background_enabled == 0)
    return false;
  palette->refresh_active = 1;
  AnimateCityPalette(city, 0);
  palette->refresh_active = 0;
  return true;
}
