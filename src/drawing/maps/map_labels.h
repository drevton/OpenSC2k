#ifndef SC2K_DRAWING_MAPS_MAP_LABELS_H
#define SC2K_DRAWING_MAPS_MAP_LABELS_H

#include "city/types.h"

typedef struct Drawing Drawing;
typedef struct CityInterfaceFont CityInterfaceFont;

typedef struct { u32 face, light, dark, ink; } CitySignColors;

// Persistent sign colors range from zero to CITY_SIGN_COLOR_COUNT - 1.
CitySignColors CitySignColorStyle(int color);

// Physical framebuffer bounds, anchored at the bottom center of the sign. Scale is a positive percentage;
// font and text are borrowed for this query. A leading City sign-color byte does not contribute to the width.
CityRect CitySignPixelBounds(const CityInterfaceFont *font, int scale, CityPoint anchor, const char *text);

// Conservative logical map bounds at the tile's sprite origin, using the bound target's scale and label style.
// Covers the selected label style, including the palette label's stem. Fonts come from this City's Assets.
CityRect CitySignMapBounds(const City *city, int zoom, CityPoint tile_point, const char *text);

// Select native signs for a map paint; false selects the palette-font labels used by immediate previews.
void SetCityMapLabelStyle(Drawing *drawing, bool enabled);
// Paint the current pass's captured signs, then visible connection labels, into the bound target/camera.
// Captured text is borrowed from City until painting completes. Only Drawing state and target pixels change.
void DrawCapturedCitySigns(const City *city);

#endif
