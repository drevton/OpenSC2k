#ifndef SC2K_UI_TOOL_COST_LABELS_H
#define SC2K_UI_TOOL_COST_LABELS_H

#include "drawing/maps/cost_labels.h"

// The view is required for zoom and placement. A NULL canvas retains the main-map label until repaint;
// otherwise pixels are drawn immediately. Text is borrowed for the call and copied into retained state when needed.
// NULL text formats the supplied cost using the city's currency convention.
void DrawCityZoneCostLabel(const City *city, const CityZoneCostPresentation *presentation, int cost,
    int screen_x, int screen_y);
void DrawCityCanvasZoneCostLabel(const City *city, Canvas *canvas, CityMapViewConfig *view, int cost, const char *text,
    int screen_x, int screen_y);
// Draw the main map's retained label into its bound Drawing target, clipped to the current physical repaint.
// Auxiliary map windows do not display this label; capture traversals have no UI composition.
void DrawRetainedCityZoneCostLabel(const City *city);
// Start the existing floating cost label for a positive completed charge on a valid tile; otherwise do nothing.
// Formats currency in UI, samples host time once and asks Drawing to retain the label. No City writes or RNG.
void ShowCityConstructionCost(const City *city, TileIndex tile, i64 cost);

#endif
