#ifndef SC2K_UI_NEWSPAPER_CAPTURE_H
#define SC2K_UI_NEWSPAPER_CAPTURE_H

#include "city/types.h"

typedef struct Game Game;
typedef struct Drawing Drawing;
typedef struct CityNewspaperDialog CityNewspaperDialog;
typedef struct NativeNewspaperDialog NativeNewspaperDialog;
typedef struct CityCanvasNewspaperPage CityCanvasNewspaperPage;

// Capture borrows paint for this call and replaces a zero-initialized or previously used capture.
// The capture owns its copied storage and retained picture. Failure leaves a releasable partial capture.
bool NativeCaptureNewspaper(City *city, CityNewspaperDialog *paint, NativeNewspaperDialog *dialog);
// Release only the retained picture; embedded text and geometry need no separate cleanup. Safe to repeat.
void NativeReleaseNewspaperPage(NativeNewspaperDialog *dialog);

// Compose through a scalar City copy. Host errors still report through Game.
bool ComposeCityNewspaperPreview(const City *city, int edition, NativeNewspaperDialog *dialog);

// Reset a page's borrowed text/geometry references using the chosen capture layout.
void InitializeCityNewspaperPageLayout(const Drawing *drawing, CityCanvasNewspaperPage *page, int layout_style,
    const char *const *text);

// Initialize the regular capture grid and its fonts.
void InitializeCityNewspaperLayouts(Drawing *drawing);
void PaintCityNewspaperDialog(City *city, NativeNewspaperDialog *capture, CityNewspaperDialog *dialog);
void PresentCityNewspaperText(Game *game, NativeNewspaperDialog *capture, const char *text, const CityRect *bounds,
    int size, int alignment);
void DrawCityNewspaperRectangle(Game *game, NativeNewspaperDialog *capture, const CityRect *bounds);
i32 MeasureCityNewspaperText(void *context, const char *text, i32 length);
bool DrawNewspaperTextBlock(Game *game, NativeNewspaperDialog *capture, char *text, const CityRect *bounds,
    int font_size, int alignment);
void DrawNewspaperArticleText(Game *game, NativeNewspaperDialog *capture, char *text, CityRect *bounds, int font_size,
    int alignment);
void DrawNewspaperArticleWithFiller(City *city, NativeNewspaperDialog *capture, char *text, CityRect *bounds,
    int font_size, int alignment);
void DrawNewspaperFillerLines(City *city, NativeNewspaperDialog *capture, const CityRect *bounds);
void TrimNewspaperHeadlineToWidth(char *text, i32 width, void *drawing_context);

#endif
