#ifndef SC2K_DRAWING_STARTUP_H
#define SC2K_DRAWING_STARTUP_H

#include "city/types.h"

typedef struct Canvas Canvas;
typedef struct CityIntroArt CityIntroArt;
typedef struct CityTerrainMap CityTerrainMap;
typedef struct CityTerrainPreviewDrawing CityTerrainPreviewDrawing;

// Labels occupy the first four intro-art slots in this order.
typedef enum CityIntroStage {
  CITY_INTRO_LOADING_CITY, CITY_INTRO_LOADING_GRAPHICS, CITY_INTRO_PREPARING_GRAPHICS,
  CITY_INTRO_PREPARING_SPRITES, CITY_INTRO_STAGE_COUNT
} CityIntroStage;

typedef struct {
  CityRect dirty;
  bool failed;
} CityLoadingPaint;

// Reserve the baked identity area when positioning startup content.
int CityStartupContentTop(int width, int height);
// Stream the backdrop into the target clip, borrowing loaded artwork. No display or Game state is accessed.
// Failure leaves any pixels already painted in place.
bool DrawCityStartupBackdrop(const CityIntroArt *art, Canvas *canvas, CityRect area);
// Paint loading artwork into an already prepared physical canvas. An initial paint includes the whole backdrop;
// later paints include only the loading panel. All sprites are attempted even if an earlier draw fails.
// Return the affected area and accumulated failure; the caller owns damage publication and presentation.
CityLoadingPaint PaintCityLoadingScreen(Canvas *canvas, const CityIntroArt *art, CityIntroStage stage,
    int completed, int total, bool initial);

// Paint fixed terrain colors and shaded relief through the existing window bitmap cache.
// The map and positive logical bounds are prepared by UI; saltwater_color is its selected saltwater RGB color.
// Only explicit invalidation or a physical-size change rebuilds the image; a cache hit does not resample terrain.
// Map data remains read-only. A failed rebuild retains the prior sprite; a final draw can fail after replacement.
bool DrawCityTerrainPreview(Canvas *canvas, CityTerrainPreviewDrawing *drawing,
    const CityTerrainMap *map, CityRect bounds, u32 saltwater_color);

#endif
