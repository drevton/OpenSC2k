#ifndef SC2K_UI_TOOL_COST_LABELS_H
#define SC2K_UI_TOOL_COST_LABELS_H

#include "drawing/canvas.h"

typedef struct CityMapViewConfig CityMapViewConfig;

// The view is required for zoom and placement. A NULL canvas retains the main-map label until repaint;
// otherwise pixels are drawn immediately. Text is borrowed for the call and copied into retained state when needed.
// NULL text formats the supplied cost using the city's currency convention.
typedef struct CityZoneCostPresentation {
  Canvas *canvas;
  const char *text;
  CityMapViewConfig *view;
} CityZoneCostPresentation;

void DrawCityZoneCostLabel(City *city, const CityZoneCostPresentation *presentation, int cost,
    int screen_x, int screen_y);
void DrawCityCanvasZoneCostLabel(City *city, Canvas *canvas, CityMapViewConfig *view, int cost, const char *text,
    int screen_x, int screen_y);
// Draw the main map's retained label into its bound Drawing target, clipped to the current physical repaint.
// Auxiliary map windows do not display this label; capture traversals have no UI composition.
void DrawRetainedCityZoneCostLabel(City *city);
void Sc2kShowZoneDragCost(City *city, CityMapViewConfig *view, u32 cost, int screen_x, int screen_y);

#endif
