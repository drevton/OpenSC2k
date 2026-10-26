#ifndef SC2K_RENDER_BITMAPS_H
#define SC2K_RENDER_BITMAPS_H

#include "city/infrastructure/types.h"
#include "drawing/types.h"
#include "drawing/sprites/preparation.h"
#include "assets/graphics.h"
#include "city/types.h"

// Generated map previews keep one Drawing-owned sprite per control. Windows
// invalidate content explicitly; changing raster size rebuilds the preview.
typedef struct Drawing Drawing;

// Specialized window drawing objects declare exactly the caches they need.
typedef enum CityWindowDrawingKind {
  CITY_WINDOW_DRAWING_MAP_DATA, CITY_WINDOW_DRAWING_BRIDGE_CHOICES,
  CITY_WINDOW_DRAWING_TREE_PICKER, CITY_WINDOW_DRAWING_TERRAIN_PREVIEW,
  CITY_WINDOW_DRAWING_SPRITE
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
  SpriteBitmapCache preview;
  SpriteImage categories[CITY_MAP_BITMAP_CATEGORIES], options[CITY_MAP_BITMAP_OPTIONS], overlay, projection;
} CityMapDataWindowDrawing;
typedef struct CityBridgeWindowDrawing {
  CityWindowDrawing base;
  SpriteImage icons[CITY_BRIDGE_BITMAP_COUNT];
} CityBridgeWindowDrawing;
typedef struct CityTreePickerDrawing {
  CityWindowDrawing base;
  SpriteImage trees[CITY_TREE_BITMAP_COUNT];
} CityTreePickerDrawing;
typedef struct CityTerrainPreviewDrawing {
  CityWindowDrawing base;
  SpriteBitmapCache preview;
} CityTerrainPreviewDrawing;
// Drawing owns a bounded set of prepared sprites for an image-bearing window.
typedef struct CitySpriteDrawing {
  CityWindowDrawing base;
  u8 count, next;
  SpriteImage images[];
} CitySpriteDrawing;
bool DrawCanvasSprite(Canvas *canvas, CitySpriteDrawing *drawing, const struct RunSprite *source, CityRect bounds);

// UI supplies the required sprite-cache capacity: 1..255 images for SPRITE, zero for fixed drawing kinds.
// A failed allocation leaves Drawing's list unchanged. The owner releases successful results at window close.
CityWindowDrawing *CreateCityWindowDrawing(Drawing *drawing, CityWindowDrawingKind kind, int images);
void InvalidateCityWindowBitmaps(CityWindowDrawing *state);
void ReleaseCityWindowDrawing(CityWindowDrawing *state);
// Borrowed resolution variants; prepared sprites are released with their window.
bool DrawCityInterfaceIcon(Canvas *canvas, SpriteImage *cache, const CityInterfaceIcons *icons,
    int icon, CityRect bounds);
// Painted atlas icons, shared by construction toolbars and extra viewport controls.
// Prepared toolbar sprites belong to Drawing, not the immutable asset atlas.
enum { CITY_TOOLBAR_ICON_BUTTON_VARIANTS = 1, CITY_TOOLBAR_ICON_PREVIEW_VARIANTS = 4,
    CITY_TOOLBAR_ICON_CACHE_VARIANTS = CITY_TOOLBAR_ICON_BUTTON_VARIANTS + CITY_TOOLBAR_ICON_PREVIEW_VARIANTS };
// Look up a valid icon's borrowed preparation at exact physical dimensions; a miss returns NULL.
const struct RunSprite *FindCityToolbarIcon(const Drawing *drawing, int icon, int width, int height);
// Prepare a valid icon at positive physical dimensions from the installed source. Button and preview slots have
// independent eviction ranges. Failure retains existing entries; returned sprites are borrowed from Drawing.
const struct RunSprite *PrepareCityToolbarIcon(Drawing *drawing, int icon, int width, int height, bool button);
void ReleaseCityToolbarIcons(Drawing *drawing);

#endif
