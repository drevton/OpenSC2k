#ifndef SC2K_UI_NEWSPAPER_H
#define SC2K_UI_NEWSPAPER_H

#include "city/presentation.h"

#include "ui/input.h"
#include "drawing/canvas.h"
#include "city/types.h"

#include "ui/news/newspaper_state.h"


typedef struct City City;
typedef struct Game Game;
typedef struct Drawing Drawing;

i32 RunPausedCityNewspaperDialog(City *city, CityNewspaperDialog *dialog);
bool ChooseCityNewspaperModal(City *city, CityNewspaperDialog *dialog);
void OpenCityManualNewspaper(City *city, int edition);

void PresentCityNewspaperEdition(City *city, i32 edition_index);
i32 MeasureCityNewspaperText(void *context, const char *text, i32 length);
typedef struct NativeNewspaperDialog NativeNewspaperDialog;
typedef struct Window Window;
typedef struct CityCanvasDesktop CityCanvasDesktop;
void DrawCityNewspaperWindow(Canvas *canvas, const Window *window);
bool CityNewspaperOpening(const Window *window);
CityRect CityNewspaperPaintBounds(const Window *window, CityRect bounds);
bool DrawCityNewspaperOpening(Drawing *drawing, Canvas *canvas, const Window *window);
bool UpdateCityNewspaperOpening(Game *game, double now);
void ReleaseCityNewspaperOpening(Drawing *drawing);
void DrawCityNewspaperRectangle(Game *game, NativeNewspaperDialog *capture, const CityRect *bounds);
void PresentCityNewspaperText(Game *game, NativeNewspaperDialog *capture, const char *text, const CityRect *bounds,
    int size, int alignment);

#endif
