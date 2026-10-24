#include "ui/news/newspaper_capture.h"
#include "ui/news/newspaper_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/catalog.h"
#include "assets/resource_types.h"
#include "assets/resources.h"
#include "assets/state.h"
#include "base/text.h"
#include "city/engine/clock.h"
#include "city/engine/random.h"
#include "city/events/news.h"
#include "city/state.h"
#include "drawing/bitmaps.h"
#include "drawing/state.h"
#include "game/errors.h"
#include "game/state.h"

// Newspaper page capture, preview composition and retained first-paint effects.

enum { kNewspaperColorStartYear = 1990 };

static const i16 kNewspaperPictureByGroup[68] = {
  0, 0, 404, 404, 405, 402, 406, 0, 0, 0, 0, 404,
  406, 408, 0, 0, 410, 401, 411, 0, 0, 0, 408, 0,
  408, 409, 400, 406, 408, 408, 408, 411, 411, 409, 403, 411,
  410, 404, 402, 403, 407, 0, 0, 0, 0, 411, 403, 410,
  408, 403, 403, 403, 411, 411, 403, 403, 403, 403, 411, 410,
  401, 411, 404, 407, 407, 0, 0, 0,
};

// Native capture pages share a regular grid. The publication window
// independently lays out the complete articles with the bundled publication fonts.
void InitializeCityNewspaperLayouts(Drawing *drawing) {
  CityNewspaperDrawing *newspaper = &drawing->newspaper;
  for (int style = 0; style < 3; ++style) {
    CityNewspaperLayout *layout = &newspaper->layouts.style[style];
    CityNewspaperFonts *fonts = &newspaper->fonts.style[style];
    CityNewspaperAlignments *alignments = &newspaper->alignments.style[style];
    *layout = (CityNewspaperLayout){{
      {12, 10, 628, 46}, {12, 54, 368, 70}, {400, 54, 628, 70},
      {12, 138, 167, 238}, {12, 82, 628, 126}, {12, 348, 420, 388},
      {432, 250, 628, 388}, {180, 138, 396, 238}, {408, 138, 628, 238},
      {12, 250, 210, 336}, {222, 250, 420, 336},
    }};
    for (int panel = 0; panel < 11; ++panel) {
      fonts->size[panel] = 12;
      alignments->alignment[panel] = 0;
    }
    fonts->size[0] = 32;
    fonts->size[1] = fonts->size[2] = 10;
    fonts->size[3] = 0;
    fonts->size[4] = 20;
    fonts->size[5] = fonts->size[6] = 10;
    alignments->alignment[0] = alignments->alignment[4] = 1;
    alignments->alignment[2] = 2;
    // Alternate the photograph's side without changing the editorial structure.
    if (style == 1) {
      layout->panel[3] = (CityRect){473, 138, 628, 238};
      layout->panel[7] = (CityRect){12, 138, 230, 238};
      layout->panel[8] = (CityRect){242, 138, 461, 238};
    }
  }
}

// Discard whole trailing words until the platform's measured width fits.
void TrimNewspaperHeadlineToWidth(char *text, int width, void *drawing_context) {
  int length = (int)strlen(text);
  int measured_width = MeasureCityNewspaperText(drawing_context, text, length);
  while (measured_width > width) {
    while (length > 0 && text[length] != ' ')
      --length;
    text[length] = '\0';
    length = (int)strlen(text);
    measured_width = MeasureCityNewspaperText(drawing_context, text, length);
  }
}


// Fill unused newspaper space using the saved layout and random generator.
void DrawNewspaperFillerLines(City *city, NativeNewspaperDialog *capture, const CityRect *bounds) {
  if (bounds->top >= bounds->bottom)
    return;
  int line = 0;
  int line_count = (bounds->bottom - bounds->top) / 5;
  int tail_width = 2 * (bounds->right - bounds->left) / 3;
  do {
    int top = bounds->top + 5 * line;
    DrawCityNewspaperRectangle(city->game, capture, &(CityRect){bounds->left + 13,
        top + 2, bounds->right - 3, top + 5});
    int run = Sc2kCrtRandRange(city, 6);
    int remaining = run - 1;
    if (run > 0) {
      do {
        ++line;
        if (line >= line_count)
          break;
        top = bounds->top + 5 * line;
        DrawCityNewspaperRectangle(city->game, capture, &(CityRect){bounds->left + 3,
            top + 2, bounds->right - 3, top + 5});
        int previous = remaining--;
        if (previous <= 0)
          break;
      } while (1);
    }
    int next = (int)(line + 1);
    if (next < line_count) {
      int tail = Sc2kCrtRandRange(city, tail_width);
      top = bounds->top + 5 * next;
      DrawCityNewspaperRectangle(city->game, capture, &(CityRect){bounds->left + 3,
          top + 2, bounds->right - tail - 3,
          top + 5});
    }
    line = next + 1;
  } while (line < line_count);
}

// Choose the closest space around the article's midpoint.
static int FindNewspaperTextWrapPoint(const char *text) {
  int midpoint = (i8)text[0] / 2;
  int result = midpoint;

  if (text[midpoint] != ' ' && result > 1) {
    for (int distance = 1; ; ++distance) {
      if (text[midpoint + distance] == ' ')
        return (result + distance);
      if (text[midpoint - distance] == ' ')
        return (result - distance);
      if (distance + 1 >= result)
        return result;
    }
  }
  return result;
}

// Platform font/DC work is supplied by the canvas; the original returns 1 unconditionally.
bool DrawNewspaperTextBlock(Game *game, NativeNewspaperDialog *capture, char *text, const CityRect *bounds,
    int font_size, int alignment) {
  PresentCityNewspaperText(game, capture, text, bounds, font_size, alignment);
  return true;
}

// The fallback is unreachable with normal text-block returns.
void DrawNewspaperArticleText(Game *game, NativeNewspaperDialog *capture, char *text, CityRect *bounds, int font_size,
    int alignment) {
  if (DrawNewspaperTextBlock(game, capture, text, bounds, font_size, alignment)) {
    bounds->top += 5 * font_size / 4;
  } else {
    int split = FindNewspaperTextWrapPoint(text);
    i8 length = (i8)text[0];
    text[0] = (char)split;
    int drawn;
    do {
      drawn = DrawNewspaperTextBlock(game, capture, text, bounds, font_size, alignment);
      if (drawn) {
        int advance = 5 * font_size / 4;
        bounds->top += advance;
        text[split] = (char)(length - split);
        if (DrawNewspaperTextBlock(game, capture, text + split, bounds, font_size, alignment))
          bounds->top += advance;
      } else {
        font_size = 6 * font_size / 7;
      }
    } while (!drawn);
  }
}

// Draw the article line, then fill the remaining columns.
void DrawNewspaperArticleWithFiller(City *city, NativeNewspaperDialog *capture, char *text, CityRect *bounds,
    int font_size, int alignment) {
  CityRect remaining = *bounds;
  DrawNewspaperArticleText(city->game, capture, text, &remaining, font_size, alignment);
  int width = (int)(bounds->right - bounds->left);
  int columns = width / 130 + 1;
  for (int column = 0; column < columns; ++column) {
    CityRect fill = remaining;
    fill.left = (int)(bounds->left) + column * width / columns;
    fill.right = (int)(bounds->left) + (column + 1) * width / columns;
    DrawNewspaperFillerLines(city, capture, &fill);
  }
}

// Newspaper paint's persistent data and CRT random effects.
void PaintCityNewspaperDialog(City *city, NativeNewspaperDialog *capture, CityNewspaperDialog *dialog) {
  Drawing *drawing = city->drawing;
  CityResourceState *resources = city->game->resources;
  const CityNewspaperEdition *edition = &city->newspaper_editions.edition[dialog->edition_index];
  const CityNewspaperLayout *layout = &drawing->newspaper.layouts.style[edition->layout_style];
  const CityNewspaperFonts *fonts = &drawing->newspaper.fonts.style[edition->layout_style];
  const CityNewspaperAlignments *alignments = &drawing->newspaper.alignments.style[edition->layout_style];
  int months = UpdateCityCalendar(city, (int)city->clock.tick);
  u32 seed = (u32)(int)city->newspaper.random_base_seed + (u32)months + 500 * (u32)dialog->edition_index;
  char article[2048] = {0};
  CityRect rect;

  const char *month_name = resources->calendar_names.full[city->clock.month];
  int day = (city->clock.month + city->clock.quarter + city->clock.year +
      city->geography.start_year) % 7 + 1;
  snprintf(article, sizeof(article), "%s%s%d%s%s%s%d", "Sunday ",
      " ",
      day, ", ", month_name, " ",
      city->geography.start_year + city->clock.year);
  DrawNewspaperTextBlock(city->game, capture, article, &layout->panel[1], fonts->size[1], alignments->alignment[1]);
  int era = (city->geography.start_year - 1900) / 50 + city->clock.year / 50;
  era = IntMin(era, 4);
  snprintf(article, sizeof(article), "%s", CityNewspaperPriceText(edition->era_kind, era));
  DrawNewspaperTextBlock(city->game, capture, article, &layout->panel[2], fonts->size[2], alignments->alignment[2]);
  if (city->unlocks.disaster_level / 2 <= dialog->edition_index)
    snprintf(article, sizeof(article), "%s%s%s", (CityNameText(&city->name)), " ",
        kCityNewspaperTitles[edition->title_kind]);
  else
    snprintf(article, sizeof(article), "%s%s", "The ",
        kCityNewspaperTitles[edition->title_kind]);
  DrawNewspaperTextBlock(city->game, capture, article, &layout->panel[0], fonts->size[0], alignments->alignment[0]);

  city->random.crt = seed + 28;
  ComposeCityNewspaperArticle(city, &resources->newspaper, 0, article);
  rect = layout->panel[4];
  TrimNewspaperHeadlineToWidth(article, (rect.right - rect.left), NULL);
  if (fonts->size[4] <= 15)
    DrawNewspaperArticleWithFiller(city, capture, article, &rect, fonts->size[4], alignments->alignment[4]);
  else
    DrawNewspaperTextBlock(city->game, capture, article, &rect, fonts->size[4], alignments->alignment[4]);

  if (!dialog->picture_attempted) {
    city->random.crt = seed + 21;
    int picture = kNewspaperPictureByGroup[city->news_records.record[0].id];
    if (picture == 0)
      picture = Sc2kCrtRandRange(city, 12) + 400;
    dialog->picture_id = picture;
    dialog->picture_attempted = 1;
    dialog->picture = ReadCityPicture(city->game->resources, picture);
  }

  if (fonts->size[3] > 0)
    DrawNewspaperFillerLines(city, capture, &layout->panel[3]);

  for (int panel = 7; panel <= 10; ++panel) {
    city->random.crt = seed + 7u * panel;
    ComposeCityNewspaperArticle(city, &resources->newspaper, (panel - 6), article);
    rect = layout->panel[panel];
    TrimNewspaperHeadlineToWidth(article, (rect.right - rect.left), NULL);
    DrawNewspaperArticleWithFiller(city, capture, article, &rect, fonts->size[panel], alignments->alignment[panel]);
  }

  rect = layout->panel[6];
  int font_size = fonts->size[6];
  if (font_size > 0) {
    snprintf(article, sizeof(article), "%s",
        kCityNewspaperWeatherHeadings[edition->subtitle_kind]);
    DrawNewspaperTextBlock(city->game, capture, article, &rect, font_size, alignments->alignment[6]);
    rect.top += 5 * font_size / 4;
    city->random.crt = seed + 42;
    ComposeCityNewspaperArticle(city, &resources->newspaper, 7, article);
    DrawNewspaperTextBlock(city->game, capture, article, &rect, font_size, alignments->alignment[6]);
    rect.top += 5 * font_size / 4;
    FormatCityNewspaperWeather(city, article);
    DrawNewspaperTextBlock(city->game, capture, article, &rect, font_size, alignments->alignment[6]);
    rect.top += 5 * font_size / 4;
  }
  DrawNewspaperFillerLines(city, capture, &rect);

  rect = layout->panel[5];
  font_size = fonts->size[5];
  if (font_size > 0) {
    snprintf(article, sizeof(article), "%s",
        kCityNewspaperEditorialHeadings[edition->report_kind]);
    DrawNewspaperTextBlock(city->game, capture, article, &rect, font_size, alignments->alignment[5]);
    rect.top += 5 * font_size / 4;
    city->random.crt = seed + 35;
    ComposeCityNewspaperArticle(city, &resources->newspaper, 8, article);
    DrawNewspaperTextBlock(city->game, capture, article, &rect, font_size, alignments->alignment[5]);
    rect.top += 5 * font_size / 4;
    if (edition->report_kind <= 2) {
      if (city->infrastructure.mayor_rating != 0) {
        snprintf(article, sizeof(article), "Mayor approval %d%%",
            (int)city->infrastructure.mayor_rating);
        DrawNewspaperTextBlock(city->game, capture, article, &rect, font_size, alignments->alignment[5]);
        rect.top += 5 * font_size / 4;
      }
    } else if (edition->report_kind <= 4) {
      for (int problem = 0; problem < 4 && city->newspaper.problems.scores[problem] != 0; ++problem) {
        snprintf(article, sizeof(article),
            kCityNewspaperStatistics[city->newspaper.problems.category_order[problem]],
            city->newspaper.problems.scores[problem]);
        DrawNewspaperTextBlock(city->game, capture, article, &rect, font_size, alignments->alignment[5]);
        rect.top += 5 * font_size / 4;
      }
    }
  }
  DrawNewspaperFillerLines(city, capture, &rect);
}

void InitializeCityNewspaperPageLayout(const Drawing *drawing, CityCanvasNewspaperPage *page, int layout_style,
    const char *const *text) {
  static const u8 kLayoutPanelIndex[7] = {4, 7, 8, 9, 10, 6, 5};
  const CityNewspaperLayout *layout = &drawing->newspaper.layouts.style[layout_style];
  const CityNewspaperFonts *fonts = &drawing->newspaper.fonts.style[layout_style];
  page->picture_bounds = layout->panel[3];
  page->masthead_bounds = layout->panel[0];
  page->picture_visible = fonts->size[3] <= 0;
  page->picture = NULL;
  page->filler = NULL;
  page->filler_count = 0;
  page->text_runs = NULL;
  page->text_run_count = 0;
  for (i32 index = 0; index < 7; ++index) {
    int panel_index = kLayoutPanelIndex[index];
    page->panel[index].bounds = layout->panel[panel_index];
    page->panel[index].font_size = fonts->size[panel_index];
    page->panel[index].text = text[index];
  }
}

// Newspaper Capture
// Host capture of one authoritative newspaper paint; no cloned city or guessed bitmap selection.

void NativeReleaseNewspaperPage(NativeNewspaperDialog *dialog) {
  FreeRunSprite(dialog->picture);
  dialog->picture = NULL;
  dialog->page.picture = NULL;
}

// Keep full text outside City. Re-expansion uses a scalar City copy, including its
// shared token choices; clicking a story never advances simulation RNG.
static void CaptureReadableNewspaper(City *city, int edition_index, NativeNewspaperDialog *dialog) {
  City preview = *city;
  const CityNewspaperEdition *edition = &city->newspaper_editions.edition[edition_index];
  static const u8 kReportTypes[] = {CITY_NEWS_FREDS_OPINION, CITY_NEWS_EDITOR_COMMENTARY,
      CITY_NEWS_EDITOR_COMMENTARY, CITY_NEWS_OPINION_SURVEY, CITY_NEWS_OPINION_SURVEY, CITY_NEWS_ADVICE};
  static const u8 kSlots[] = {0, 1, 2, 3, 4, 7, 8};
  static const u8 kSeeds[] = {28, 49, 56, 63, 70, 42, 35};
  preview.news_records.record[7].id = CITY_NEWS_WEATHER;
  preview.news_records.record[7].event_data = city->weather.state;
  preview.news_records.record[8].id = kReportTypes[edition->report_kind];
  preview.news_records.record[8].event_data = city->newspaper.problems.category_order[0];
  u32 seed = city->newspaper.random_base_seed + city->clock.tick / 25 + 500 * edition_index;
  int next_ranked = 0;
  for (int i = 0; i < 7; ++i) {
    bool selected = false;
    int first = i < 5 ? next_ranked : kSlots[i], end = i < 5 ? 7 : first + 1;
    for (int slot = first; slot < end && !selected; ++slot) {
      if (i < 5) next_ranked = slot + 1;
      preview.random.crt = seed + kSeeds[i];
      ComposeCityNewspaperArticle(&preview, &city->game->resources->newspaper, slot, dialog->articles[i].headline);
      preview.random.crt = seed + kSeeds[i];
      ComposeCityNewspaperArticleBody(&preview, &city->game->resources->newspaper, slot, dialog->articles[i].body);
      selected = true;
      // Different ranked topics can resolve to the same authored story. Compare the rendered text too.
      for (int previous = 0; previous < i; ++previous)
        if ((dialog->articles[i].headline[0] &&
                !strcmp(dialog->articles[i].headline, dialog->articles[previous].headline)) ||
            (dialog->articles[i].body[0] &&
                !strcmp(dialog->articles[i].body, dialog->articles[previous].body))) selected = false;
    }
    if (!selected) dialog->articles[i].headline[0] = dialog->articles[i].body[0] = 0;
  }
  CopyText(dialog->articles[5].heading, sizeof(dialog->articles[5].heading),
      kCityNewspaperWeatherHeadings[edition->subtitle_kind]);
  CopyText(dialog->articles[6].heading, sizeof(dialog->articles[6].heading),
      kCityNewspaperEditorialHeadings[edition->report_kind]);
  dialog->reading = dialog->hovered = -1;
  dialog->opening_frame = CITY_NEWSPAPER_OPENING_FRAMES;
}

bool NativeCaptureNewspaper(City *city, CityNewspaperDialog *paint, NativeNewspaperDialog *dialog) {
  Drawing *drawing = city->drawing;
  NativeReleaseNewspaperPage(dialog);
  const char *text[7] = {0};
  InitializeCityNewspaperPageLayout(drawing, &dialog->page,
      city->newspaper_editions.edition[paint->edition_index].layout_style, text);
  dialog->page.filler = dialog->filler;
  dialog->page.text_runs = dialog->text_runs;
  PaintCityNewspaperDialog(city, dialog, paint);
  if (GetGameError(city->game))
    return false;
  CaptureReadableNewspaper(city, paint->edition_index, dialog);
  if (!paint->picture) return true;
  // Preserve the issue's color choice without copying image pixels. Grayscale
  // issues own only a palette and a reference to the immutable source rows.
  bool monochrome = city->geography.start_year + city->clock.year < kNewspaperColorStartYear;
  if (monochrome) {
    SpritePalette *palette = calloc(1, sizeof(*palette));
    if (!palette) { SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE); return false; }
    palette->references = 1;
    for (int i = 0; i < 256; ++i) {
      u32 rgb = paint->picture->palette->colors[i];
      int gray = ((rgb >> 16 & 255) * 77 + (rgb >> 8 & 255) * 150 + (rgb & 255) * 29) / 256;
      palette->colors[i] = gray * 0x010101;
    }
    dialog->picture = ReferenceRunSprite(paint->picture, palette, false);
    FreeSpritePalette(palette);
    if (!dialog->picture) { SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE); return false; }
  } else dialog->picture = RetainRunSprite(paint->picture);
  dialog->page.picture = dialog->picture;
  return true;
}

// Manual previews use isolated scalar City state and borrow map layers for reads.
bool ComposeCityNewspaperPreview(const City *city, int edition, NativeNewspaperDialog *dialog) {
  City preview = *city;
  CityNewspaperDialog paint = {0};
  paint.edition_index = edition;
  ClearGameError(city->game);
  return NativeCaptureNewspaper(&preview, &paint, dialog);
}
