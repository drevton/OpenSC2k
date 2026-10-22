#ifndef SC2K_UI_NEWSPAPER_H
#define SC2K_UI_NEWSPAPER_H

#include "city/presentation.h"
#include "city/types.h"
#include "ui/news/newspaper_state.h"

typedef struct City City;
typedef struct Game Game;
typedef struct Drawing Drawing;
typedef struct Canvas Canvas;

// Capture borrows paint for this call; the captured page owns its copied storage and retained picture.
bool NativeCaptureNewspaper(City *city, CityNewspaperDialog *paint, NativeNewspaperDialog *dialog);
void NativeReleaseNewspaperPage(NativeNewspaperDialog *dialog);

bool ComposeCityNewspaperPreview(const City *city, int edition, NativeNewspaperDialog *dialog);

// UI owns the dialog lifetime; callers retain the opening notice until the City initialization tail can run once.
CityNewspaperInitialization BeginCityNewspaperDialog(City *city, CityNewspaperDialog *dialog);
void InitializeCityNewspaperDialog(City *city, CityNewspaperDialog *dialog);
void DestroyCityNewspaperWindow(City *city, CityNewspaperDialog *dialog);

i32 RunPausedCityNewspaperDialog(City *city, CityNewspaperDialog *dialog);
bool ChooseCityNewspaperModal(City *city, CityNewspaperDialog *dialog);
// Retained disaster presentation advances the pending effects, then waits for its newspaper.
// CITY_OPERATION_PENDING keeps the same effect work; it must be continued rather than restarted.
i64 ProcessPendingCityDisaster(City *city, bool terrain_water_allowed);
// Copies the requested edition and arms delivery after the first rendered map frame.
void ScheduleCityStartupNewspaper(City *city);
// True transfers callback delivery through opening/window completion, which may occur before return.
// False retains no callback/context and leaves cleanup to the caller.
bool OpenCityNewspaperWithCallback(City *city, int edition, bool automatic,
    void (*callback)(City *city, void *context), void *context);
// Resume/cancel one newspaper-initialization token. City finishes its tail before UI opens the page.
// True transfers callback delivery, possibly before return; false retains no callback or context.
// Page-creation failure consumes the City token; it does not restart or roll back initialization.
bool ResumeCityNewspaperOpening(City *city, int token, bool resume,
    void (*callback)(City *city, void *context), void *context);
bool InitializeCityNewspaperWindow(City *city, CityNewspaperDialog *paint);
// Returns the captured window ID or zero; the window owns its page, and paint is borrowed only for this call.
// Pause reconciliation may close or reorder windows before return; reacquire the ID before accessing the window.
u32 OpenCityNewspaperPage(City *city, CityNewspaperDialog *paint, int edition, double close_at);
void OpenCityManualNewspaper(City *city, int edition);

void ShowCityNewspaper(City *city, i32 edition_index);
void PresentCityNewspaperEdition(City *city, i32 edition_index);
i32 MeasureCityNewspaperText(void *context, const char *text, i32 length);
typedef struct Window Window;
void DrawCityNewspaperWindow(Canvas *canvas, const Window *window);
bool CityNewspaperOpening(const Window *window);
CityRect CityNewspaperPaintBounds(const Window *window, CityRect bounds);
bool DrawCityNewspaperOpening(Drawing *drawing, Canvas *canvas, const Window *window);
bool UpdateCityNewspaperOpening(Game *game, double now);
void DrawCityNewspaperRectangle(Game *game, NativeNewspaperDialog *capture, const CityRect *bounds);
void PresentCityNewspaperText(Game *game, NativeNewspaperDialog *capture, const char *text, const CityRect *bounds,
    int size, int alignment);

void Sc2kCanvasTrimNewspaperHeadline(char *text, i32 width, i32 font_size, i32 page_scale);
void Sc2kCanvasDrawNewspaper(Canvas *canvas, CityRect bounds,
    const char *headline, const char *article);
i32 Sc2kCanvasNewspaperLayoutStyle(City *city, int edition_index);
void Sc2kCanvasNewspaperPageLayout(const Drawing *drawing, CityCanvasNewspaperPage *page, i32 layout_style,
    const char *const *text);

#endif
