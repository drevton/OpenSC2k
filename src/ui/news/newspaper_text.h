#ifndef SC2K_UI_NEWSPAPER_TEXT_H
#define SC2K_UI_NEWSPAPER_TEXT_H

#include "city/types.h"
#include "ui/news/newspaper_state.h"

typedef struct Game Game;
typedef struct Drawing Drawing;
typedef struct CityNewspaperResources CityNewspaperResources;

// Initialize the regular capture grid and its fonts.
void InitializeCityNewspaperLayouts(Drawing *drawing);
// Paint updates the supplied City's calendar and CRT RNG. Preview callers supply their scoped City copy;
// comparison may supply no capture for observed drawing.
void PaintCityNewspaperDialog(City *city, NativeNewspaperDialog *capture, CityNewspaperDialog *dialog);
bool DrawNewspaperTextBlock(Game *game, NativeNewspaperDialog *capture, char *text, const CityRect *bounds,
    int font_size, int alignment);
void DrawNewspaperArticleText(Game *game, NativeNewspaperDialog *capture, char *text, CityRect *bounds, int font_size,
    int alignment);
void DrawNewspaperArticleWithFiller(City *city, NativeNewspaperDialog *capture, char *text, CityRect *bounds,
    int font_size, int alignment);
int FindNewspaperTextWrapPoint(const char *text);
// Output must fit the expanded text. Returns its last occupied byte index; callers add the terminator.
int ExpandCityNewspaperTemplate(City *city, const CityNewspaperResources *resources, char *output, int template_id,
    int article_index, int half);
bool ComposeCityNewspaperArticle(City *city, const CityNewspaperResources *resources, int article_index, char *output);
int ComposeCityNewspaperArticleBody(City *city, const CityNewspaperResources *resources, int article_index,
    char *output);
void DrawNewspaperFillerLines(City *city, NativeNewspaperDialog *capture, const CityRect *bounds);
void FormatCityNewspaperWeather(City *city, char *output);
// Context is a borrowed int text scale, or null for the default metric; comparison may supply its observer context.
void TrimNewspaperHeadlineToWidth(char *text, i32 width, void *drawing_context);

#endif
