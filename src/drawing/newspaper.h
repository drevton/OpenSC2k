#ifndef SC2K_DRAWING_NEWSPAPER_H
#define SC2K_DRAWING_NEWSPAPER_H

#include "city/types.h"

typedef struct {
  CityRect panel[11];
} CityNewspaperLayout;

typedef struct {
  CityNewspaperLayout style[3];
} CityNewspaperLayouts;

typedef struct {
  i16 size[11];
} CityNewspaperFonts;

typedef struct {
  CityNewspaperFonts style[3];
} CityNewspaperFontTable;

typedef struct {
  i16 alignment[11];
} CityNewspaperAlignments;

typedef struct {
  CityNewspaperAlignments style[3];
} CityNewspaperAlignmentTable;

typedef struct {
  CityNewspaperLayouts layouts;
  CityNewspaperFontTable fonts;
  CityNewspaperAlignmentTable alignments;
} CityNewspaperDrawing;

typedef struct Drawing Drawing;
typedef struct Canvas Canvas;
// Release the temporary opening image after its current paint has finished. Safe to repeat.
void ReleaseCityNewspaperOpening(Drawing *drawing);
// Replace the opening image with zeroed physical pixels. Releases the previous image/key/bounds first, even on failure.
// Invalid dimensions or failed allocation leave an empty cache. Success leaves Drawing owning the new pixels;
// UI captures the page into them and publishes the window key afterward. No window callbacks or capture occur here.
bool ResetCityNewspaperOpening(Drawing *drawing, int width, int height);
// Physical bounds of a newspaper image scaled and rotated about its center. Scale is positive; angle is radians.
// Includes the existing one-pixel damage margin. No window, animation clock or mutable state is consulted.
CityRect CityNewspaperOpeningBounds(CityRect bounds, double scale, double angle);
// Paint the prepared Drawing-owned opening image into the target's physical raster clip and remember its bounds.
// pixels locates the untransformed image and matches its dimensions. Scale/angle come from UI's opening keyframes.
// Requires a valid cached image and positive scale.
// No allocation, window access or timing occurs; source and canvas configuration stay unchanged.
void PaintCityNewspaperOpening(Drawing *drawing, Canvas *canvas, CityRect pixels, double scale, double angle);

#endif
