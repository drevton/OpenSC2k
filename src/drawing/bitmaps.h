#ifndef SC2K_RENDER_BITMAPS_H
#define SC2K_RENDER_BITMAPS_H

#include "city/infrastructure/types.h"
#include "drawing/canvas.h"
#include "assets/graphics.h"
#include "city/types.h"

// Native map toolbar artwork, in assets/ui/map-data/icons.svg and icons.dat order.
typedef enum CityMapIcon {
  CITY_MAP_ICON_STRUCTURES, CITY_MAP_ICON_ROADS, CITY_MAP_ICON_POWER, CITY_MAP_ICON_WATER,
  CITY_MAP_ICON_POPULATION, CITY_MAP_ICON_SAFETY, CITY_MAP_ICON_POLLUTION, CITY_MAP_ICON_LAND_VALUE,
  CITY_MAP_ICON_CIVIC, CITY_MAP_ICON_ZONES, CITY_MAP_ICON_RAIL, CITY_MAP_ICON_TRAFFIC,
  CITY_MAP_ICON_DENSITY, CITY_MAP_ICON_GROWTH, CITY_MAP_ICON_CRIME, CITY_MAP_ICON_POLICE_COVERAGE,
  CITY_MAP_ICON_POLICE_STATION, CITY_MAP_ICON_FIRE_COVERAGE, CITY_MAP_ICON_FIRE_STATION,
  CITY_MAP_ICON_SCHOOL, CITY_MAP_ICON_COLLEGE, CITY_MAP_ICON_OVERLAY, CITY_MAP_ICON_ISOMETRIC,
  CITY_MAP_ICON_COUNT
} CityMapIcon;

void InitializeNativeCityPalette(Drawing *drawing);

// Generated map previews keep one Drawing-owned sprite per control. Windows
// invalidate content explicitly; changing raster size rebuilds the preview.
typedef struct Game Game;
typedef struct Drawing Drawing;
typedef struct CityBitmapCache {
  struct RunSprite *sprite;
  u32 palette_buckets;
  bool valid;
} CityBitmapCache;
typedef struct {
  struct RunSprite *source, *prepared;
  CityRect content;
} CitySpriteImage;
void ReleaseCitySpriteImage(CitySpriteImage *image);
// One cache slot prepares a source region at its final raster dimensions.
// A null slot streams a full source image through row scratch instead of caching it.
bool DrawCanvasSpriteImage(Canvas *canvas, CitySpriteImage *image, const struct RunSprite *source,
    CityRect content, CityRect bounds, u32 flags);

// Specialized window drawing objects declare exactly the caches they need.
typedef enum CityWindowDrawingKind {
  CITY_WINDOW_DRAWING_MAP_DATA, CITY_WINDOW_DRAWING_BRIDGE_CHOICES,
  CITY_WINDOW_DRAWING_TREE_PICKER, CITY_WINDOW_DRAWING_TERRAIN_PREVIEW,
  CITY_WINDOW_DRAWING_SPRITE, CITY_WINDOW_DRAWING_NEIGHBORS, CITY_WINDOW_DRAWING_MUSIC
} CityWindowDrawingKind;
typedef struct CityWindowDrawing {
  Drawing *owner;
  struct CityWindowDrawing *next;
  CityWindowDrawingKind kind;
} CityWindowDrawing;
enum { CITY_MAP_BITMAP_CATEGORIES = 10, CITY_MAP_BITMAP_OPTIONS = 3,
  CITY_BRIDGE_BITMAP_COUNT = CITY_BRIDGE_CHOICE_ICON_COUNT, CITY_TREE_BITMAP_COUNT = 12 };
typedef struct CityMapDataWindowDrawing {
  CityWindowDrawing base;
  CityBitmapCache preview;
  CitySpriteImage categories[CITY_MAP_BITMAP_CATEGORIES], options[CITY_MAP_BITMAP_OPTIONS], overlay, projection;
} CityMapDataWindowDrawing;
typedef struct CityBridgeWindowDrawing {
  CityWindowDrawing base;
  CitySpriteImage icons[CITY_BRIDGE_BITMAP_COUNT];
} CityBridgeWindowDrawing;
typedef struct CityTreePickerDrawing {
  CityWindowDrawing base;
  CitySpriteImage trees[CITY_TREE_BITMAP_COUNT];
} CityTreePickerDrawing;
typedef struct CityTerrainPreviewDrawing {
  CityWindowDrawing base;
  CityBitmapCache preview;
} CityTerrainPreviewDrawing;
// Drawing owns a bounded set of prepared sprites for an image-bearing window.
typedef struct CitySpriteDrawing {
  CityWindowDrawing base;
  u8 count, next;
  CitySpriteImage images[];
} CitySpriteDrawing;
bool DrawCanvasSprite(Canvas *canvas, CitySpriteDrawing *drawing, const struct RunSprite *source, CityRect bounds);

typedef struct {
  CityRect bounds;
  u32 (*build)(Canvas *pixels, const void *context); // Returns animated palette dependencies.
  const void *context;
} CityBitmapPaint;
bool DrawCityCachedBitmap(Canvas *canvas, CityBitmapCache *cache, const CityBitmapPaint *paint);
CityWindowDrawing *CreateCityWindowDrawing(Drawing *drawing, CityWindowDrawingKind kind);
void InvalidateCityWindowBitmaps(CityWindowDrawing *state);
void InvalidateCityWindowPaletteBitmaps(Drawing *drawing, u32 changed_buckets);
void ReleaseCityWindowDrawing(CityWindowDrawing *state);
// Borrowed resolution variants; prepared sprites are released with their window.
bool DrawCityInterfaceIcon(Canvas *canvas, CitySpriteImage *cache, const CityInterfaceIcons *icons,
    int icon, CityRect bounds);
// Painted atlas icons, shared by construction toolbars and extra viewport controls.
// Prepared toolbar sprites belong to Drawing, not the immutable asset atlas.
enum { CITY_TOOLBAR_ICON_BUTTON_VARIANTS = 1, CITY_TOOLBAR_ICON_PREVIEW_VARIANTS = 4,
    CITY_TOOLBAR_ICON_CACHE_VARIANTS = CITY_TOOLBAR_ICON_BUTTON_VARIANTS + CITY_TOOLBAR_ICON_PREVIEW_VARIANTS };
bool PrepareCityToolbarIcons(Game *game);
bool PrepareCityToolbarIconSize(Game *game, int icon, int size);
int CityToolbarIconColumns(int icon);
void ReleaseCityToolbarIcons(Drawing *drawing);
void DrawCityToolbarIcon(Game *game, Canvas *canvas, CityRect bounds, int icon, bool disabled, bool flipped);
int CityToolSpriteTile(int tool);
void DrawCityTreePreview(Game *game, Canvas *canvas, CitySpriteImage *cache, CityRect bounds, int species);
enum { CITY_BUILDING_PREVIEW_WIDTH = 144, CITY_BUILDING_PREVIEW_HEIGHT = 150 };
void DrawCityBuildingPreview(Game *game, Canvas *canvas, CityRect bounds, int tool);

// Palette

typedef struct __attribute__((packed)) {
  u8 red;
  u8 green;
  u8 blue;
  u8 flags;
} CityPaletteEntry;

typedef struct __attribute__((packed)) {
  CityPaletteEntry entry[256];
} CityPaletteEntries;

#endif
