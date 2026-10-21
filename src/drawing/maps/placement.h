#ifndef SC2K_DRAWING_MAPS_PLACEMENT_H
#define SC2K_DRAWING_MAPS_PLACEMENT_H

#include "drawing/maps/map_view.h"

// Marker bounds in the main map's logical pixels, including magnification and cached sprite anchors.
// Requires a valid tile and prepared default view. Reads Drawing resources without changing selection state.
CityRect CityMapPlacementBounds(const City *city, TileIndex tile, bool point);

CityMapImmediatePaintState *Sc2kCityLinePreviewView(Drawing *drawing);
// Borrow preview pixels temporarily, restoring the view's previous target afterwards.
typedef struct {
  CityMapImmediatePaintState saved;
  bool bound;
} CityLinePreviewPaint;
CityLinePreviewPaint BeginCityLinePreviewPaint(Drawing *drawing, CityMapViewConfig *view);
void EndCityLinePreviewPaint(CityMapViewConfig *view, const CityLinePreviewPaint *paint);

#endif
