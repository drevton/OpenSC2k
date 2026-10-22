#ifndef SC2K_DRAWING_MAPS_COST_LABELS_H
#define SC2K_DRAWING_MAPS_COST_LABELS_H

#include "drawing/canvas.h"

typedef struct Drawing Drawing;
typedef struct CityMapViewConfig CityMapViewConfig;

// Draw text at a baseline in canvas pixels, clipping to bounds and restoring the caller's clip afterwards.
// Color uses the label's BGR convention; conversion to opaque ARGB belongs to Drawing. Text/bounds are borrowed.
void DrawCityZoneCostCanvasText(Canvas *canvas, const char *text, int x, int baseline_y,
    u32 color, const CityRect *bounds);
// Copy formatted text and map-pixel layout into Drawing, invalidating old bounds before replacement and new after.
// The view and bounds must match this main-map label. No currency or affordability policy is evaluated here.
void RetainCityZoneCostLabel(Drawing *drawing, const CityMapViewConfig *view, CityRect bounds, int cost,
    const char *text, CityPoint position);
// Erase the retained label through normal map damage.
void ClearCityZoneCostLabel(Drawing *drawing, const CityMapViewConfig *view);

#endif
