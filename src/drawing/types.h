#ifndef SC2K_DRAWING_TYPES_H
#define SC2K_DRAWING_TYPES_H

#include "city/types.h"

// Shared drawing vocabulary. Pixel buffers and raster state belong to canvas.h.
typedef struct Canvas Canvas;

// Text-block alignment used by the software canvas, not GDI flags.
typedef enum CityCanvasTextAlignment {
  CITY_CANVAS_TEXT_LEFT = 0, CITY_CANVAS_TEXT_CENTER = 1, CITY_CANVAS_TEXT_RIGHT = 2,
} CityCanvasTextAlignment;

typedef void (*CityCanvasVisiblePaint)(CityRect area, void *context);

typedef void (*CanvasScaledPaint)(Canvas *canvas, void *context);

#endif
