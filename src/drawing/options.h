#ifndef SC2K_DRAWING_OPTIONS_H
#define SC2K_DRAWING_OPTIONS_H

#include "base/types.h"

// Rendering preferences and host display information belong to Drawing.
typedef struct DrawingOptions {
  i32 scale, automatic, snapping, fullscreen;
  bool scale_bevels, sprite_font, font_antialias, save_view_position, black_background;
  u8 demolition_elevation;
  u32 fixes;
  i32 window_width, window_height, refresh_rate;
  char driver[48];
} DrawingOptions;

typedef enum DrawingFix {
  // IDs0–8 were retired sprite repairs, hangar variants and dark underground.
  DRAWING_FIX_CITY_CENTER = 9,
  // IDs10–14 were palette effects for retired indexed artwork.
  // ID15 was the full-date host title option.
  DRAWING_FIX_PIXEL_SCROLLING = 16,
  // ID17 was the removed pipe endpoint option; do not reuse it.
  DRAWING_FIX_COUNT,
  DRAWING_FIX_ACTIVE_MASK = (1 << DRAWING_FIX_CITY_CENTER) | (1 << DRAWING_FIX_PIXEL_SCROLLING),
  DRAWING_FIX_DEFAULT_MASK = (1 << DRAWING_FIX_PIXEL_SCROLLING)
} DrawingFix;

#endif
