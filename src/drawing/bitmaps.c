#include "drawing/sprites/preparation.h"
#include "drawing/sprites/surface.h"
#include "drawing/sprites/sources.h"
#include "drawing/bitmaps.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "assets/state.h"
#include "city/maps/buildings.h"
#include "city/maps/tile_storage.h"
#include "city/maps/trees.h"
#include "city/state.h"
#include "drawing/maps/map_camera.h"
#include "drawing/maps/tables.h"
#include "drawing/raster/canvas_raster_internal.h"
#include "drawing/raster/screen_damage.h"
#include "drawing/state.h"
#include "game/state.h"

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

const RunSprite *FindCityToolbarIcon(const Drawing *drawing, int icon, int width, int height) {
  RunSprite *const *variants = drawing->toolbar_icons[icon];
  for (int i = 0; i < CITY_TOOLBAR_ICON_CACHE_VARIANTS; ++i)
    if (variants[i] && variants[i]->width == width && variants[i]->height == height)
      return variants[i];
  return NULL;
}

const RunSprite *PrepareCityToolbarIcon(Drawing *drawing, int icon, int width, int height, bool button) {
  const RunSprite *cached = FindCityToolbarIcon(drawing, icon, width, height);
  if (cached) return cached;
  RunSprite **variants = drawing->toolbar_icons[icon], **slot = NULL;
  int first = button ? 0 : CITY_TOOLBAR_ICON_BUTTON_VARIANTS;
  int end = button ? CITY_TOOLBAR_ICON_BUTTON_VARIANTS : CITY_TOOLBAR_ICON_CACHE_VARIANTS;
  for (int i = first; i < end; ++i)
    if (!variants[i]) { slot = &variants[i]; break; }
  InterfaceSpriteSource source = GetInterfaceSprite(drawing->toolbar_icon_source, icon,
      InterfaceSpriteLevel(IntMax(drawing->toolbar_icon_scale, (height * 100 + 31) / 32)));
  RunSprite *reference = ReferenceRunSprite(source.image, source.image->palette, source.flipped);
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

typedef enum BitmapCacheOperation { BITMAP_INVALIDATE, BITMAP_RELEASE } BitmapCacheOperation;

static void UpdateBitmapCache(SpriteBitmapCache *image, BitmapCacheOperation operation) {
  if (operation == BITMAP_RELEASE) FreeRunSprite(image->sprite);
  else image->valid = false;
}

static void ReleaseSpriteImages(SpriteImage *images, int count) {
  for (int i = 0; i < count; ++i) ReleaseSpriteImage(&images[i]);
}

// Lifecycle dispatch visits the actual fields of each specialized drawing object.
static void UpdateWindowBitmaps(CityWindowDrawing *state, BitmapCacheOperation operation) {
  switch (state->kind) {
  case CITY_WINDOW_DRAWING_MAP_DATA: {
    CityMapDataWindowDrawing *pixels = (CityMapDataWindowDrawing *)state;
    UpdateBitmapCache(&pixels->preview, operation);
    ReleaseSpriteImages(pixels->categories, CITY_MAP_BITMAP_CATEGORIES);
    ReleaseSpriteImages(pixels->options, CITY_MAP_BITMAP_OPTIONS);
    ReleaseSpriteImages(&pixels->overlay, 1);
    ReleaseSpriteImages(&pixels->projection, 1);
    break;
  }
  case CITY_WINDOW_DRAWING_BRIDGE_CHOICES:
    ReleaseSpriteImages(((CityBridgeWindowDrawing *)state)->icons, CITY_BRIDGE_BITMAP_COUNT);
    break;
  case CITY_WINDOW_DRAWING_TREE_PICKER:
    ReleaseSpriteImages(((CityTreePickerDrawing *)state)->trees, CITY_TREE_BITMAP_COUNT);
    break;
  case CITY_WINDOW_DRAWING_SPRITE: {
    if (operation == BITMAP_RELEASE) {
      CitySpriteDrawing *drawing = (CitySpriteDrawing *)state;
      ReleaseSpriteImages(drawing->images, drawing->count);
    }
    break;
  }
  case CITY_WINDOW_DRAWING_TERRAIN_PREVIEW:
    UpdateBitmapCache(&((CityTerrainPreviewDrawing *)state)->preview, operation);
    break;
  }
}

void InvalidateCityWindowBitmaps(CityWindowDrawing *state) {
  if (state) UpdateWindowBitmaps(state, BITMAP_INVALIDATE);
}

void ReleaseCityWindowDrawing(CityWindowDrawing *state) {
  CityWindowDrawing **link = &state->owner->window_drawing;
  while (*link != state) link = &(*link)->next;
  *link = state->next;
  UpdateWindowBitmaps(state, BITMAP_RELEASE);
  free(state);
}

CityWindowDrawing *CreateCityWindowDrawing(Drawing *drawing, CityWindowDrawingKind kind, int images) {
  assert(images >= 0 && images <= UINT8_MAX);
  assert((kind == CITY_WINDOW_DRAWING_SPRITE) == (images > 0));
  static const size_t kDrawingSizes[] = {
    [CITY_WINDOW_DRAWING_MAP_DATA] = sizeof(CityMapDataWindowDrawing),
    [CITY_WINDOW_DRAWING_BRIDGE_CHOICES] = sizeof(CityBridgeWindowDrawing),
    [CITY_WINDOW_DRAWING_TREE_PICKER] = sizeof(CityTreePickerDrawing),
    [CITY_WINDOW_DRAWING_TERRAIN_PREVIEW] = sizeof(CityTerrainPreviewDrawing),
    [CITY_WINDOW_DRAWING_SPRITE] = sizeof(CitySpriteDrawing),
  };
  CityWindowDrawing *state = calloc(1, kDrawingSizes[kind] + images * sizeof(SpriteImage));
  if (!state) return NULL;
  *state = (CityWindowDrawing){.owner = drawing, .next = drawing->window_drawing, .kind = kind};
  drawing->window_drawing = state;
  if (images) ((CitySpriteDrawing *)state)->count = images;
  return state;
}

// Windows with several fixed pictures retain a bounded set of normal preparations.
bool DrawCanvasSprite(Canvas *canvas, CitySpriteDrawing *drawing, const RunSprite *source, CityRect bounds) {
  if (!source) return true;
  SpriteImage *image = NULL;
  for (int i = 0; i < drawing->count; ++i)
    if (drawing->images[i].source == source) { image = &drawing->images[i]; break; }
  if (!image) {
    image = &drawing->images[drawing->next];
    drawing->next = (drawing->next + 1) % drawing->count;
  }
  return DrawCanvasSpriteImage(canvas, image, source, (CityRect){0, 0, source->width, source->height}, bounds, 0);
}

bool DrawCityInterfaceIcon(Canvas *canvas, SpriteImage *cache, const CityInterfaceIcons *icons,
    int icon, CityRect bounds) {
  CityRect box = CanvasRasterImageBounds(canvas, bounds);
  int height = box.bottom - box.top, logical = bounds.bottom - bounds.top;
  int scale = logical > 0 ? height * 100 / logical : 100;
  InterfaceSpriteSource source = GetInterfaceSprite(icons, icon,
      InterfaceSpriteLevel(IntMax(scale, (height * 100 + 31) / 32)));
  return DrawCanvasSpriteImage(canvas, cache, source.image, (CityRect){0, 0, source.image->width, source.image->height},
      bounds, source.flipped ? SPRITE_FLIPPED : 0);
}

// Every species uses its climate's common crop, keeping tree sizes and ground alignment.
bool DrawCityTreePreview(const CityAssets *assets, Canvas *canvas, SpriteImage *cache, CityRect bounds,
    int species) {
  int art = kCityTreeArtLayouts[species * 4][0];
  const RunSprite *sprite = assets->landscape[CITY_LANDSCAPE_TREE_FIRST + art * 7 + CITY_TREE_GROWN].sprite;
  if (!sprite || bounds.right <= bounds.left || bounds.bottom <= bounds.top) return true;
  CityRect source = assets->tree_preview_bounds[species >= 20 ? 2 : species >= 12 ? 1 : 0];
  int source_width = source.right - source.left, source_height = source.bottom - source.top;
  if (source_width <= 0 || source_height <= 0) return true;
  int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
  if ((i64)width * source_height > (i64)height * source_width) width = height * source_width / source_height;
  else height = width * source_height / source_width;
  bounds.left += (bounds.right - bounds.left - width) >> 1;
  bounds.right = bounds.left + width;
  bounds.top = bounds.bottom - height;
  return DrawCanvasSpriteImage(canvas, cache, sprite, source, bounds, 0);
}

// Construction uses prepared runs and the ordinary sprite renderer.
bool DrawCityBuildingPreview(const City *city, Canvas *canvas, CityRect bounds, TileKind kind, int orientation) {
  const Drawing *drawing = city->drawing;
  int art = CityBuildingSprite(city, kind, orientation);
  int view = art < 0 ? CITY_CIVIC_VIEWS : (art - CityBuildingSpriteFirst(kind)) & 3;
  int preview = kCityMapKindSprites[kind] - CITY_CIVIC_KIND_FIRST;
  const RunSprite *sprite = drawing->sprite_cache.building_previews[preview][view];
  if (kind == TK_WIND_POWER && art >= 0 &&
      city->geography.start_year + city->clock.year >= CITY_WIND_MODERN_YEAR)
    sprite = drawing->sprite_cache.modern_wind_previews[view];
  if (!sprite) return false;
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
  return true;
}
