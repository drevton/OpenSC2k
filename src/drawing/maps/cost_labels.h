#ifndef SC2K_DRAWING_MAPS_COST_LABELS_H
#define SC2K_DRAWING_MAPS_COST_LABELS_H

#include "drawing/types.h"

typedef struct Drawing Drawing;
typedef struct CityMapViewConfig CityMapViewConfig;

// A NULL canvas retains the main-map label until repaint; otherwise draw immediately into the borrowed canvas.
// View and formatted text are required. Retention copies the text, so the caller may use a temporary buffer.
typedef struct CityZoneCostPresentation {
  Canvas *canvas;
  const char *text;
  CityMapViewConfig *view;
} CityZoneCostPresentation;

// Position uses the supplied canvas's coordinates, or magnified map coordinates when retaining a label.
// Cost is retained for the UI's later affordability query; Drawing only uses the supplied affordability style.
void PaintCityZoneCostLabel(Drawing *drawing, const CityZoneCostPresentation *presentation, int cost,
    CityPoint position, bool affordable);
// Compose the retained main-map label into the bound target, respecting its physical repaint clip.
// UI supplies affordability from the live treasury on each repaint. Auxiliary map windows omit this label.
void PaintRetainedCityZoneCostLabel(Drawing *drawing, bool affordable);

// Draw text at a baseline in canvas pixels, clipping to bounds and restoring the caller's clip afterwards.
// Color uses the label's BGR convention; conversion to opaque ARGB belongs to Drawing. Text/bounds are borrowed.
void DrawCityZoneCostCanvasText(Canvas *canvas, const char *text, int x, int baseline_y,
    u32 color, const CityRect *bounds);
// Erase the retained label through normal map damage.
void ClearCityZoneCostLabel(Drawing *drawing, const CityMapViewConfig *view);

#endif
