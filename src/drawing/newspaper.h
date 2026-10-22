#ifndef SC2K_DRAWING_NEWSPAPER_H
#define SC2K_DRAWING_NEWSPAPER_H

#include "city/types.h"

typedef struct {
  i8 choice[250];
} CityNewspaperSharedChoices;

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

// Shared newspaper composition scratch and prepared capture layouts, separate from map-view drawing state.
// Comparison transports the four records individually, excluding padding between them.
typedef struct {
  CityNewspaperSharedChoices choices;
  CityNewspaperLayouts layouts;
  CityNewspaperFontTable fonts;
  CityNewspaperAlignmentTable alignments;
} CityNewspaperDrawing;

typedef struct Drawing Drawing;
// Release the temporary opening image after its current paint has finished. Safe to repeat.
void ReleaseCityNewspaperOpening(Drawing *drawing);

#endif
