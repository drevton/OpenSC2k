#include "drawing/drawing_internal.h"
#include <stdio.h>

#include "ui/frontend.h"
#include "game/errors.h"
#include "ui/news/newspaper.h"

// Each paint receives its destination page explicitly.

void PresentCityNewspaperText(Game *game, NativeNewspaperDialog *dialog, const char *text, const CityRect *bounds,
    int size, int alignment) {
  if (dialog == NULL) {
    return;
  }
  i32 index = dialog->page.text_run_count;
  if (index == 32) {
    SetGameError(game, -3);
    return;
  }
  snprintf(dialog->text_run_buffers[index], sizeof(dialog->text_run_buffers[index]), "%s", text);
  dialog->text_runs[index] = (CityCanvasNewspaperTextRun){*bounds, size, alignment, dialog->text_run_buffers[index]};
  ++dialog->page.text_run_count;
}

void DrawCityNewspaperRectangle(Game *game, NativeNewspaperDialog *dialog, const CityRect *bounds) {
  if (dialog == NULL) {
    return;
  }
  CityCanvasNewspaperPage *page = &dialog->page;
  if (page->filler_count == (i32)(sizeof(dialog->filler) / sizeof(CityRect))) {
    SetGameError(game, -3);
    return;
  }
  dialog->filler[page->filler_count++] = *bounds;
}

i32 MeasureCityNewspaperText(void *context, const char *text, i32 length) {
  (void)length;
  int scale = context != NULL ? *(const int *)context : 1;
  return CanvasTextWidth(text) * scale;
}
