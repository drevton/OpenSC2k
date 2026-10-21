#include "city/events/news.h"
#include "ui/windows/state.h"
#include "city/presentation.h"
#include "ui/state.h"
#include "game/application_internal.h"
#include "assets/resources.h"
#include "ui/news/newspaper.h"

#include "ui/input.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui/frontend.h"
#include "game/errors.h"
#include "assets/catalog.h"
#include "drawing/drawing_internal.h"
#include "drawing/state.h"
#include "drawing/bitmaps.h"

#include "city/state.h"

// Newspaper
// Drawing subsystem: newspaper.

enum { kNewspaperOpeningFrames = 45, kNewspaperOpeningRate = 60, kNewspaperColorStartYear = 1990 };

void Sc2kCanvasTrimNewspaperHeadline(char *text, i32 width, i32 font_size, i32 page_scale) {
  i32 scale = font_size * page_scale / 8000;
  scale = IntClamp(scale, 1, 3);
  TrimNewspaperHeadlineToWidth(text, width, &scale);
}

static void CanvasNewspaperText(Canvas *canvas, CityRect bounds, const char *text,
    i32 font_size, i32 page_scale, int alignment) {
  if (text == NULL || bounds.left >= bounds.right || bounds.top >= bounds.bottom)
    return;
  i32 scale = font_size * page_scale / 8000;
  scale = IntClamp(scale, 1, 3);
  i32 line_height = (5 * font_size * page_scale + 3999) / 4000;
  line_height = IntMax(line_height, 11 * scale);
  CanvasTextWrappedScaled(canvas, bounds.left, bounds.top, bounds.right, bounds.bottom,
      text, canvas_text, scale, line_height, alignment);
}

static void CanvasDrawNewspaperFiller(Canvas *canvas, const CityCanvasNewspaperPage *page,
    i32 origin_x, i32 origin_y, i32 scale) {
  CityRect clip = IntersectCityRects(canvas->clip, (CityRect){0, 0, canvas->width, canvas->height});
  for (i32 index = 0; index < page->filler_count; ++index) {
    const CityRect *rectangle = &page->filler[index];
    i64 left = origin_x + (i64)rectangle->left * scale / 1000;
    i64 top = origin_y + (i64)rectangle->top * scale / 1000;
    i64 right = origin_x + (i64)rectangle->right * scale / 1000;
    i64 bottom = origin_y + (i64)rectangle->bottom * scale / 1000;
    left = IntMax(left, clip.left);
    top = IntMax(top, clip.top);
    right = IntMin(right, clip.right);
    bottom = IntMin(bottom, clip.bottom);
    if (left < right && top < bottom)
      CanvasFill(canvas, (i32)left, (i32)top, (i32)right, (i32)bottom, canvas_text);
  }
}

void CanvasDrawNewspaperPage(Canvas *canvas, const Window *window) {
  CityRect bounds = window->bounds;
  const CityCanvasNewspaperPage *page = &window->newspaper_page;
  CanvasPanelFrame(canvas, bounds, "City Newspaper");
  CityRect paper = {bounds.left + 8, bounds.top + 25, bounds.right - 8, bounds.bottom - 8};
  CanvasBeveledBox(canvas, paper.left, paper.top, paper.right, paper.bottom, 0xFFF4F0E4u, 0);
  i32 available_width = paper.right - paper.left - 10;
  i32 available_height = paper.bottom - paper.top - 10;
  i32 scale = available_width * 1000 / 640;
  i32 scale_y = available_height * 1000 / 400;
  scale = IntMin(scale, scale_y);
  scale = IntMin(scale, 1000);
  i32 page_width = 640 * scale / 1000;
  i32 page_height = 400 * scale / 1000;
  i32 origin_x = paper.left + (paper.right - paper.left - page_width) / 2;
  i32 origin_y = paper.top + (paper.bottom - paper.top - page_height) / 2;
  if (page->picture_visible && page->picture && scale > 0) {
    int left = origin_x + page->picture_bounds.left * scale / 1000;
    int top = origin_y + page->picture_bounds.top * scale / 1000;
    DrawCanvasSprite(canvas, window->picture_drawing, page->picture,
        (CityRect){left, top, left + page->picture->width * scale / 1000,
            top + page->picture->height * scale / 1000});
  }
  CanvasDrawNewspaperFiller(canvas, page, origin_x, origin_y, scale);
  // Original paint brackets the masthead with two horizontal rules. LineTo excludes its endpoint.
  i32 rule_left = origin_x + (page->masthead_bounds.left + 3) * scale / 1000;
  i32 rule_right = origin_x + (page->masthead_bounds.right - 4) * scale / 1000;
  i32 rule_top = origin_y + (page->masthead_bounds.top - 2) * scale / 1000;
  i32 rule_bottom = origin_y + (page->masthead_bounds.bottom - 2) * scale / 1000;
  CanvasFill(canvas, rule_left, rule_top, rule_right, rule_top + 1, 0xFF000000u);
  CanvasFill(canvas, rule_left, rule_bottom, rule_right, rule_bottom + 1, 0xFF000000u);
  for (i32 index = 0; index < page->text_run_count; ++index) {
    const CityCanvasNewspaperTextRun *run = &page->text_runs[index];
    CityRect cell = {origin_x + run->bounds.left * scale / 1000,
        origin_y + run->bounds.top * scale / 1000,
        origin_x + run->bounds.right * scale / 1000,
        origin_y + run->bounds.bottom * scale / 1000};
    CanvasNewspaperText(canvas, cell, run->text, run->font_size, scale, run->alignment);
  }
  for (i32 index = 0; page->text_runs == NULL && index < 7; ++index) {
    const CityCanvasNewspaperPanel *panel = &page->panel[index];
    CityRect cell = {
      origin_x + panel->bounds.left * scale / 1000 + 2,
      origin_y + panel->bounds.top * scale / 1000 + 2,
      origin_x + panel->bounds.right * scale / 1000 - 2,
      origin_y + panel->bounds.bottom * scale / 1000 - 2,
    };
    if (cell.left >= cell.right || cell.top >= cell.bottom) continue;
    if (index != 0)
      CanvasFill(canvas, cell.left, cell.top, cell.right, cell.top + 1, canvas_edge);
    const char *text = panel->text;
    char headline[2048];
    if (index < 5 && text != NULL) {
      snprintf(headline, sizeof(headline), "%s", text);
      Sc2kCanvasTrimNewspaperHeadline(headline, cell.right - cell.left, panel->font_size, scale);
      text = headline;
    }
    CanvasNewspaperText(canvas, cell, text, panel->font_size, scale, 0);
  }
  DrawCanvasButtonBox(canvas, bounds.right - 51, bounds.bottom - 24, bounds.right - 10,
      bounds.bottom - 10, canvas_light, 1);
  CanvasTextAt(canvas, bounds.right - 43, bounds.bottom - 20, "CLOSE", canvas_text);
}

void Sc2kCanvasDrawNewspaper(Canvas *canvas, CityRect bounds,
    const char *headline, const char *article) {
  CanvasPanelFrame(canvas, bounds, "City Newspaper");
  CanvasBeveledBox(canvas, bounds.left + 9, bounds.top + 27, bounds.right - 9,
      bounds.bottom - 28, canvas_light, 0);
  CanvasTextWrapped(canvas, bounds.left + 16, bounds.top + 40, bounds.right - 16,
      bounds.bottom - 34, article, canvas_text);
  DrawCanvasButtonBox(canvas, bounds.right - 51, bounds.bottom - 24, bounds.right - 10,
      bounds.bottom - 10, canvas_light, 1);
  CanvasTextAt(canvas, bounds.right - 43, bounds.bottom - 20, "CLOSE", canvas_text);
}

i32 Sc2kCanvasNewspaperLayoutStyle(City *city, int edition_index) {
  return city->newspaper_editions.edition[edition_index].layout_style;
}

void Sc2kCanvasNewspaperPageLayout(const Drawing *drawing, CityCanvasNewspaperPage *page, i32 layout_style,
    const char *const *text) {
  static const u8 layout_panel_index[7] = {4, 7, 8, 9, 10, 6, 5};
  const CityNewspaperLayout *layout = &drawing->map_state.newspaper_layouts.style[layout_style];
  const CityNewspaperFonts *fonts = &drawing->map_state.newspaper_fonts.style[layout_style];
  page->picture_bounds = layout->panel[3];
  page->masthead_bounds = layout->panel[0];
  page->picture_visible = fonts->size[3] <= 0;
  page->picture = NULL;
  page->filler = NULL;
  page->filler_count = 0;
  page->text_runs = NULL;
  page->text_run_count = 0;
  for (i32 index = 0; index < 7; ++index) {
    int panel_index = layout_panel_index[index];
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

// Keep full text outside City. Re-expansion uses only scalar preview state and
// restores Drawing's shared choices; clicking a story never advances simulation RNG.
static void CaptureReadableNewspaper(City *city, int edition_index, NativeNewspaperDialog *dialog) {
  City preview = *city;
  Drawing *drawing = city->game->drawing;
  CityNewspaperSharedChoices saved = drawing->map_state.newspaper_shared_choices;
  const CityNewspaperEdition *edition = &city->newspaper_editions.edition[edition_index];
  static const u8 kReportTypes[] = {CITY_NEWS_FREDS_OPINION, CITY_NEWS_EDITOR_COMMENTARY,
      CITY_NEWS_EDITOR_COMMENTARY, CITY_NEWS_OPINION_SURVEY, CITY_NEWS_OPINION_SURVEY, CITY_NEWS_ADVICE};
  static const u8 kSlots[] = {0, 1, 2, 3, 4, 7, 8};
  static const u8 kSeeds[] = {28, 49, 56, 63, 70, 42, 35};
  preview.facility_rankings.record[7].id = CITY_NEWS_WEATHER;
  preview.facility_rankings.record[7].event_data = city->weather.state;
  preview.facility_rankings.record[8].id = kReportTypes[edition->report_kind];
  preview.facility_rankings.record[8].event_data = city->newspaper.problems.category_order[0];
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
  drawing->map_state.newspaper_shared_choices = saved;
  dialog->reading = dialog->hovered = -1;
  dialog->opening_frame = kNewspaperOpeningFrames;
}

bool NativeCaptureNewspaper(City *city, CityNewspaperDialog *paint, NativeNewspaperDialog *dialog) {
  Drawing *drawing = city->game->drawing;
  NativeReleaseNewspaperPage(dialog);
  const char *text[7] = {0};
  Sc2kCanvasNewspaperPageLayout(drawing, &dialog->page,
      Sc2kCanvasNewspaperLayoutStyle(city, paint->edition_index), text);
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

// Newspaper Host
// Manual previews use isolated scalar City state and borrow map layers for reads.

bool ComposeCityNewspaperPreview(const City *city, int edition, NativeNewspaperDialog *dialog) {
  City preview = *city;
  Drawing *drawing = city->game->drawing;
  CityNewspaperSharedChoices previous_choices = drawing->map_state.newspaper_shared_choices;
  CityNewspaperDialog paint = {0};
  paint.edition_index = edition;
  ClearGameError(city->game);
  bool captured = NativeCaptureNewspaper(&preview, &paint, dialog);
  drawing->map_state.newspaper_shared_choices = previous_choices;
  return captured;
}
// Direct/headless commands run the same page effects without opening a canvas window.
void ApplyCityNewspaperPageEffects(City *city, int edition) {
  CityNewspaperDialog paint = {.edition_index = edition};
  if (BeginCityNewspaperDialog(city, &paint) != CITY_NEWSPAPER_INITIALIZATION_SKIPPED)
    FinishCityNewspaperInitialization(city, edition);
  if (!city->game->resources->facility_resources_unavailable) {
    NativeNewspaperDialog *page = calloc(1, sizeof(*page));
    if (page) {
      NativeCaptureNewspaper(city, &paint, page);
      NativeReleaseNewspaperPage(page);
      free(page);
    } else SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
  }
  DestroyCityNewspaperWindow(city, &paint);
}

// Native publication layout: retain text, wrap at the current window size, and
// draw coverage glyphs into the same framebuffer as the other canvas windows.
typedef struct {
  const char *text;
  int bytes, width;
  bool last;
} PublicationLine;

typedef struct {
  CityRect paper, masthead, banner, stories[7], photo, footer;
  int body_size, heading_size;
} PublicationLayout;

static const u32 kPublicationPaper = 0xFFDCD8CC, kPublicationInk = 0xFF24231F, kPublicationRule = 0xFFAAA69C;

static int WrapPublicationText(const CityInterfaceFont *font, const char *text, int width,
    CityPublicationFace face, int size, PublicationLine *lines, int capacity) {
  int count = 0;
  while (*text && count < capacity) {
    while (*text == ' ' || *text == '\t' || *text == '\r') ++text;
    if (*text == '\n') { ++text; continue; }
    const char *start = text, *end = text;
    int measured = 0;
    while (*text && *text != '\n') {
      const char *word = text;
      while (*text && *text != ' ' && *text != '\n' && *text != '\r' && *text != '\t') ++text;
      int candidate = CityPublicationTextWidth(font, start, (int)(text - start), face, size);
      if (candidate > width && end != start) { text = word; break; }
      if (candidate > width) {
        text = word;
        do {
          const char *next = text;
          ReadCityFontCharacter(&next);
          candidate = CityPublicationTextWidth(font, start, (int)(next - start), face, size);
          if (candidate > width && text != start) break;
          text = next;
        } while (*text && *text != ' ' && *text != '\n');
        measured = CityPublicationTextWidth(font, start, (int)(text - start), face, size);
        end = text;
        break;
      }
      end = text;
      measured = candidate;
      while (*text == ' ' || *text == '\t' || *text == '\r') ++text;
    }
    bool last = !*text || *text == '\n';
    if (end > start) lines[count++] = (PublicationLine){start, (int)(end - start), measured, last};
    if (*text == '\n') ++text;
  }
  return count;
}

static int DrawPublicationLines(Canvas *canvas, CityRect bounds, const char *text,
    CityPublicationFace face, int size, int alignment, bool justify) {
  PublicationLine lines[512];
  int count = WrapPublicationText(canvas->interface_font, text, bounds.right - bounds.left, face, size,
      lines, sizeof(lines) / sizeof(lines[0]));
  int pitch = size * 3 / 2;
  int shown = IntMin(count, IntMax(0, (bounds.bottom - bounds.top) / pitch));
  Canvas local = *canvas;
  local.clip = IntersectCityRects(local.clip, bounds);
  for (int row = 0; row < shown; ++row) {
    PublicationLine *line = &lines[row];
    int x = bounds.left, y = bounds.top + row * pitch;
    int spare = bounds.right - bounds.left - line->width;
    if (alignment == 1) x += spare / 2;
    if (alignment == 2) x += spare;
    int spaces = 0;
    for (int i = 0; i < line->bytes; ++i) spaces += line->text[i] == ' ';
    if (!justify || line->last || !spaces || spare > size * spaces * 2) {
      DrawCityPublicationText(local.interface_font, &local, x, y, line->text, line->bytes, face, size, kPublicationInk);
      continue;
    }
    int first = 0, added = 0;
    for (int i = 0; i <= line->bytes; ++i) {
      if (i != line->bytes && line->text[i] != ' ') continue;
      int offset = CityPublicationTextWidth(local.interface_font, line->text, first, face, size);
      DrawCityPublicationText(local.interface_font, &local, x + offset + spare * added / spaces, y,
          line->text + first, i - first, face, size, kPublicationInk);
      first = i + 1;
      ++added;
    }
  }
  return count;
}

static PublicationLayout NewspaperPageLayout(CityRect bounds, const NativeNewspaperDialog *dialog,
    const CityInterfaceFont *font) {
  PublicationLayout layout = {0};
  layout.paper = (CityRect){bounds.left + 3, bounds.top + 18, bounds.right - 3, bounds.bottom - 3};
  int left = layout.paper.left + 14, right = layout.paper.right - 14;
  int width = right - left;
  layout.body_size = width >= 780 ? 12 : width >= 540 ? 11 : 10;
  layout.heading_size = layout.body_size + 5;
  layout.masthead = (CityRect){left, layout.paper.top + 4, right, layout.paper.top + 48};
  PublicationLine headline[8];
  int banner_size = layout.body_size + 12;
  int rows = WrapPublicationText(font, dialog->articles[0].headline, width, CITY_PUBLICATION_HEADLINE,
      banner_size, headline, 8);
  int height = IntMin(rows, 2) * (banner_size * 3 / 2);
  layout.banner = (CityRect){left, layout.masthead.bottom + 23, right, layout.masthead.bottom + 23 + height};
  layout.footer = (CityRect){left, bounds.bottom - 24, right, bounds.bottom - 6};
  int top = layout.banner.bottom + 10, bottom = layout.footer.top - 8;
  int column = (width - 28) / 3, middle = left + column + 14, last = middle + column + 14;
  int half = (bottom - top) / 2;
  int third = (bottom - top) / 3;
  layout.stories[1] = (CityRect){left, top, left + column, top + third};
  layout.stories[6] = (CityRect){left, top + third + 6, left + column, top + third * 2};
  layout.stories[4] = (CityRect){left, top + third * 2 + 6, left + column, bottom};
  layout.stories[5] = (CityRect){last, top, right, top + third};
  layout.stories[2] = (CityRect){last, top + third + 6, right, top + third * 2};
  layout.stories[3] = (CityRect){last, top + third * 2 + 6, right, bottom};
  int photo_height = dialog->page.picture ? IntMin(110, half) : 0;
  layout.photo = (CityRect){middle, top, middle + column, top + photo_height};
  layout.stories[0] = (CityRect){middle, top + photo_height + (photo_height ? 8 : 0), middle + column, bottom};
  return layout;
}

static CityRect NewspaperReaderTextBounds(const Window *window) {
  CityRect b = window->bounds;
  int margin = IntMax(24, (b.right - b.left - 650) / 2);
  return (CityRect){b.left + margin, b.top + 102, b.right - margin - 12, b.bottom - 38};
}

static int NewspaperReaderRows(const Window *window, const CityInterfaceFont *font, int *visible) {
  CityRect area = NewspaperReaderTextBounds(window);
  PublicationLine lines[512];
  *visible = IntMax(1, (area.bottom - area.top) / 21);
  return WrapPublicationText(font, window->newspaper->articles[window->newspaper->reading].body,
      area.right - area.left, CITY_PUBLICATION_BODY, 14, lines, sizeof(lines) / sizeof(lines[0]));
}

static void DrawNewspaperArticleReader(Canvas *canvas, const Window *window) {
  NativeNewspaperDialog *dialog = window->newspaper;
  CityRect b = window->bounds;
  CityRect title = {b.left + 24, b.top + 28, b.right - 24, b.top + 94};
  DrawPublicationLines(canvas, title, dialog->articles[dialog->reading].headline,
      CITY_PUBLICATION_HEADLINE, 22, 1, false);
  CityRect body = NewspaperReaderTextBounds(window);
  CanvasFill(canvas, body.left, body.top - 8, body.right + 12, body.top - 7, kPublicationRule);
  PublicationLine lines[512];
  const char *text = dialog->articles[dialog->reading].body;
  int count = WrapPublicationText(canvas->interface_font, text, body.right - body.left,
      CITY_PUBLICATION_BODY, 14, lines, sizeof(lines) / sizeof(lines[0]));
  int visible = IntMax(1, (body.bottom - body.top) / 21);
  int first = IntMin(dialog->scroll, IntMax(0, count - visible));
  Canvas local = *canvas;
  local.clip = IntersectCityRects(canvas->clip, body);
  for (int row = first; row < IntMin(count, first + visible); ++row)
    DrawCityPublicationText(local.interface_font, &local, body.left, body.top + (row - first) * 21,
        lines[row].text, lines[row].bytes, CITY_PUBLICATION_BODY, 14, kPublicationInk);
  if (count > visible) {
    CityRect track = {body.right + 4, body.top, body.right + 12, body.bottom};
    CanvasFill(canvas, track.left, track.top, track.right, track.bottom, 0xFFC5C0B5);
    CityRect thumb = ReadCityScrollbar(track, count, visible, first, 12).thumb;
    CanvasFill(canvas, thumb.left, thumb.top, thumb.right, thumb.bottom, 0xFF8C877B);
  }
  DrawCityPublicationText(canvas->interface_font, canvas, b.left + 20, b.bottom - 24,
      "< Back to newspaper", -1, CITY_PUBLICATION_BODY, 11, kPublicationInk);
}

void DrawCityNewspaperWindow(Canvas *canvas, const Window *window) {
  const NativeNewspaperDialog *dialog = window->newspaper;
  if (!dialog || !dialog->articles[0].headline[0]) {
    CanvasDrawNewspaperPage(canvas, window);
    return;
  }
  CityRect b = window->bounds;
  CanvasPanelFrame(canvas, b, "City Newspaper");
  PublicationLayout layout = NewspaperPageLayout(b, dialog, canvas->interface_font);
  CanvasFill(canvas, layout.paper.left, layout.paper.top, layout.paper.right, layout.paper.bottom, kPublicationPaper);
  if (dialog->reading >= 0) DrawNewspaperArticleReader(canvas, window);
  else {
    DrawPublicationLines(canvas, layout.masthead, dialog->text_run_buffers[2], CITY_PUBLICATION_MASTHEAD, 28, 1, false);
    CityRect meta = {layout.masthead.left, layout.masthead.bottom,
        layout.masthead.right, layout.masthead.bottom + 15};
    DrawPublicationLines(canvas, meta, dialog->text_run_buffers[1], CITY_PUBLICATION_BODY, 10, 0, false);
    DrawPublicationLines(canvas, meta, dialog->text_run_buffers[0], CITY_PUBLICATION_BODY, 10, 2, false);
    CanvasFill(canvas, meta.left, meta.bottom + 1, meta.right, meta.bottom + 2, kPublicationRule);
    DrawPublicationLines(canvas, layout.banner, dialog->articles[0].headline,
        CITY_PUBLICATION_HEADLINE, layout.body_size + 12, 1, false);
    CityCanvasNewspaperPage picture = dialog->page;
    picture.picture_bounds = layout.photo;
    if (picture.picture && layout.photo.bottom > layout.photo.top) {
      int w = picture.picture->width, h = picture.picture->height;
      CityRect photo = layout.photo;
      int target_w = IntMin(photo.right - photo.left, (photo.bottom - photo.top) * w / h);
      int target_h = target_w * h / w;
      photo.left += (photo.right - photo.left - target_w) / 2;
      photo.top += (photo.bottom - photo.top - target_h) / 2;
      photo.right = photo.left + target_w;
      photo.bottom = photo.top + target_h;
      DrawCanvasSprite(canvas, window->picture_drawing, picture.picture, photo);
    }
    for (int i = 0; i < 7; ++i) {
      if (!dialog->articles[i].headline[0]) continue;
      CityRect area = layout.stories[i];
      int heading_size = layout.heading_size, pitch = heading_size * 3 / 2;
      if (i) CanvasFill(canvas, area.left, area.top - 2, area.right, area.top - 1, kPublicationRule);
      if (dialog->articles[i].heading[0]) {
        int rows = DrawPublicationLines(canvas, (CityRect){area.left, area.top, area.right, area.top + pitch},
            dialog->articles[i].heading, CITY_PUBLICATION_BODY, layout.body_size, 1, false);
        area.top += IntMin(rows, 1) * (layout.body_size * 3 / 2) + 2;
      }
      if (i) {
        CityRect heading = {area.left, area.top, area.right, IntMin(area.bottom, area.top + pitch * 2)};
        int rows = DrawPublicationLines(canvas, heading, dialog->articles[i].headline,
            CITY_PUBLICATION_HEADLINE, heading_size, 1, false);
        area.top += IntMin(rows, 2) * pitch + 2;
      }
      int bottom = area.bottom;
      area.bottom -= layout.body_size * 3 / 2;
      DrawPublicationLines(canvas, area, dialog->articles[i].body, CITY_PUBLICATION_BODY, layout.body_size, 0, true);
      CityRect more = {area.left, bottom - layout.body_size * 3 / 2, area.right, bottom};
      DrawPublicationLines(canvas, more, "Read article >", CITY_PUBLICATION_BODY, layout.body_size, 2, false);
      if (dialog->hovered == i)
        CanvasFill(canvas, more.right - 66, more.bottom - 1, more.right, more.bottom, kPublicationInk);
    }
    for (int i = 1; i <= 2; ++i) {
      int x = layout.stories[1].left + i * (layout.stories[1].right - layout.stories[1].left + 14) - 7;
      CanvasFill(canvas, x, layout.photo.top, x + 1, layout.footer.top - 8, kPublicationRule);
    }
    DrawPublicationLines(canvas, layout.footer, "Click an article to read the full story.",
        CITY_PUBLICATION_BODY, 10, 0, false);
  }
  DrawCanvasButtonBox(canvas, b.right - 51, b.bottom - 24, b.right - 10, b.bottom - 10, canvas_light, 1);
  CanvasTextAt(canvas, b.right - 43, b.bottom - 20, "CLOSE", canvas_text);
}

bool CityNewspaperOpening(const Window *window) {
  return window->newspaper && window->newspaper->articles[0].headline[0] &&
      window->newspaper->opening_frame < kNewspaperOpeningFrames;
}

// A short spin followed by two settling steps. Only host presentation time advances it.
static void NewspaperOpeningTransform(const Window *window, double *scale, double *angle) {
  static const double kKeys[][3] = {{0, 0.03, -720}, {0.76, 1.04, 8}, {0.9, 0.98, -3}, {1, 1, 0}};
  double progress = (double)window->newspaper->opening_frame / kNewspaperOpeningFrames;
  int step = progress <= kKeys[1][0] ? 0 : progress <= kKeys[2][0] ? 1 : 2;
  double t = (progress - kKeys[step][0]) / (kKeys[step + 1][0] - kKeys[step][0]);
  double rest = 1 - t, eased = 1 - rest * rest * rest;
  *scale = kKeys[step][1] + (kKeys[step + 1][1] - kKeys[step][1]) * eased;
  *angle = (kKeys[step][2] + (kKeys[step + 1][2] - kKeys[step][2]) * eased) * 0.017453292519943295;
}

CityRect CityNewspaperPaintBounds(const Window *window, CityRect bounds) {
  if (!CityNewspaperOpening(window)) return bounds;
  double scale, angle;
  NewspaperOpeningTransform(window, &scale, &angle);
  double width = (bounds.right - bounds.left) * scale, height = (bounds.bottom - bounds.top) * scale;
  double x = (bounds.left + bounds.right) * 0.5, y = (bounds.top + bounds.bottom) * 0.5;
  double extent_x = (fabs(cos(angle)) * width + fabs(sin(angle)) * height) * 0.5;
  double extent_y = (fabs(sin(angle)) * width + fabs(cos(angle)) * height) * 0.5;
  return (CityRect){(int)floor(x - extent_x) - 1, (int)floor(y - extent_y) - 1,
      (int)ceil(x + extent_x) + 1, (int)ceil(y + extent_y) + 1};
}

void ReleaseCityNewspaperOpening(Drawing *drawing) {
  free(drawing->newspaper_opening.pixels);
  drawing->newspaper_opening = (Canvas){0};
  drawing->newspaper_opening_window = 0;
  drawing->newspaper_opening_bounds = (CityRect){0};
}

static void DamageNewspaperOpening(Game *game, CityRect bounds) {
  MarkScreenDirty(&game->drawing->screen_damage, bounds);
  Sc2kDirtyMarkRect(&game->ui->desktop.window_damage, bounds);
}

static void SetNewspaperOpeningFrame(Game *game, const Window *window, int frame) {
  CityRect pixels = Sc2kCanvasDesktopPixelBounds(&game->ui->desktop, window->window_id);
  DamageNewspaperOpening(game, CityNewspaperPaintBounds(window, pixels));
  window->newspaper->opening_frame = frame;
  DamageNewspaperOpening(game, CityNewspaperPaintBounds(window, pixels));
}

bool UpdateCityNewspaperOpening(Game *game, double now) {
  CityCanvasDesktop *desktop = &game->ui->desktop;
  bool opening = false, dirty = false;
  for (int i = 0; i < desktop->count; ++i) {
    Window *window = &desktop->windows[i];
    if (!CityNewspaperOpening(window)) continue;
    double elapsed = now - window->newspaper->opening_started;
    double frames = elapsed * kNewspaperOpeningRate;
    int frame = window->newspaper->opening_skipped || frames >= kNewspaperOpeningFrames ?
        kNewspaperOpeningFrames : IntMax(0, (int)frames);
    if (frame != window->newspaper->opening_frame) {
      SetNewspaperOpeningFrame(game, window, frame);
      dirty = true;
    }
    opening |= CityNewspaperOpening(window);
  }
  if (!opening && game->drawing->newspaper_opening_window) {
    DamageNewspaperOpening(game, game->drawing->newspaper_opening_bounds);
    ReleaseCityNewspaperOpening(game->drawing);
    dirty = true;
  }
  return dirty;
}

bool DrawCityNewspaperOpening(Drawing *drawing, Canvas *canvas, const Window *window) {
  if (!CityNewspaperOpening(window)) return false;
  CanvasRaster raster = CanvasRasterView(canvas);
  CityRect b = window->bounds;
  CityRect pixels = {
      CanvasRasterEdge(b.left, raster.origin_x, raster.numerator, raster.denominator) + raster.offset_x,
      CanvasRasterEdge(b.top, raster.origin_y, raster.numerator, raster.denominator) + raster.offset_y,
      CanvasRasterEdge(b.right, raster.origin_x, raster.numerator, raster.denominator) + raster.offset_x,
      CanvasRasterEdge(b.bottom, raster.origin_y, raster.numerator, raster.denominator) + raster.offset_y};
  int width = pixels.right - pixels.left, height = pixels.bottom - pixels.top;
  Canvas *image = &drawing->newspaper_opening;
  if (drawing->newspaper_opening_window != window->window_id || image->width != width || image->height != height) {
    ReleaseCityNewspaperOpening(drawing);
    u32 *storage = width > 0 && height > 0 && (size_t)width <= SIZE_MAX / height / sizeof(u32) ?
        calloc((size_t)width * height, sizeof(u32)) : NULL;
    if (!storage) {
      window->newspaper->opening_skipped = true; // The next frame presents the ordinary page.
      return true;
    }
    *image = (Canvas){.pixels = storage, .width = width, .height = height, .stride = width};
    Canvas source = *canvas;
    source.pixels = storage;
    source.stride = width;
    source.width = b.right - b.left; source.height = b.bottom - b.top;
    source.clip = (CityRect){0, 0, source.width, source.height};
    source.raster = (CanvasRaster){storage, raster.numerator, raster.denominator, 0, 0,
        width, height, {0, 0, width, height}, 0, 0};
    source.palette_indices = NULL; source.palette_rows = NULL;
    Window page = *window;
    page.bounds = source.clip;
    DrawCityNewspaperWindow(&source, &page);
    drawing->newspaper_opening_window = window->window_id;
  }
  CityRect area = CityNewspaperPaintBounds(window, pixels);
  drawing->newspaper_opening_bounds = area;
  area = IntersectCityRects(area, raster.clip);
  area = IntersectCityRects(area, (CityRect){0, 0, raster.width, raster.height});
  double scale, angle;
  NewspaperOpeningTransform(window, &scale, &angle);
  double inverse_cos = cos(angle) / scale, inverse_sin = sin(angle) / scale;
  double center_x = (pixels.left + pixels.right) * 0.5, center_y = (pixels.top + pixels.bottom) * 0.5;
  int step_x = (int)llround(inverse_cos * 65536), step_y = (int)llround(-inverse_sin * 65536);
  // Increment inverse coordinates along each scanline; no trigonometry or divisions per pixel.
  for (int y = area.top; y < area.bottom; ++y) {
    i64 sx = (i64)llround((width * 0.5 + inverse_cos * (area.left + 0.5 - center_x) +
        inverse_sin * (y + 0.5 - center_y)) * 65536);
    i64 sy = (i64)llround((height * 0.5 - inverse_sin * (area.left + 0.5 - center_x) +
        inverse_cos * (y + 0.5 - center_y)) * 65536);
    for (int x = area.left; x < area.right; ++x, sx += step_x, sy += step_y) {
      int source_x = (int)(sx >> 16), source_y = (int)(sy >> 16);
      if ((u32)source_x >= (u32)width || (u32)source_y >= (u32)height) continue;
      size_t index = (size_t)y * canvas->stride + x;
      canvas->pixels[index] = image->pixels[(size_t)source_y * width + source_x];
      if (canvas->palette_indices && (!canvas->palette_rows || canvas->palette_rows[y]))
        canvas->palette_indices[(size_t)y * canvas->index_stride + x] = 0;
    }
  }
  return true;
}

// Handles this window's painting and input; paint coordinates are local to the window.
bool NewspaperWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    Canvas *canvas = event->canvas;
    if (!DrawCityNewspaperOpening(game->drawing, canvas, window)) {
      if (window->newspaper) DrawCityNewspaperWindow(canvas, window);
      else if (window->newspaper_page.text_runs || window->newspaper_page.panel[0].text)
        CanvasDrawNewspaperPage(canvas, window);
      else Sc2kCanvasDrawNewspaper(canvas, window->bounds, window->title, window->message);
    }
    return true;
  }
  if (event->kind != WINDOW_INPUT || !window->newspaper) return false;
  NativeNewspaperDialog *dialog = window->newspaper;
  if (!dialog->articles[0].headline[0]) return false;
  CityCanvasDesktop *desktop = &game->ui->desktop;
  const CanvasSDLEvent *input = event->input;
  switch (input->kind) {
  case CANVAS_SDL_POINTER_DOWN:
  case CANVAS_SDL_POINTER_MOVE:
  case CANVAS_SDL_POINTER_LEAVE: {
    bool pressed = input->kind == CANVAS_SDL_POINTER_DOWN;
    if (pressed && input->button != CANVAS_SDL_BUTTON_LEFT) return false;
    int x = event->x, y = event->y;
    if (desktop->dragging != CITY_CANVAS_DRAG_NONE) return false;
    if (CityNewspaperOpening(window)) {
      if (pressed) SetNewspaperOpeningFrame(game, window, kNewspaperOpeningFrames);
      return pressed;
    }
    if (dialog->reading >= 0) {
      CityRect b = window->bounds;
      if (pressed && CanvasPointInRect((CityRect){b.left + 12, b.bottom - 30, b.left + 190, b.bottom - 5}, x, y)) {
        dialog->reading = -1;
        Sc2kDirtyMarkRect(&desktop->dirty, b);
        return true;
      }
      CityRect body = NewspaperReaderTextBounds(window);
      if (pressed && CanvasPointInRect((CityRect){body.right, body.top, body.right + 16, body.bottom}, x, y)) {
        int visible, count = NewspaperReaderRows(window, desktop->interface_font, &visible);
        int desired = (y - body.top) * count / IntMax(1, body.bottom - body.top) - visible / 2;
        dialog->scroll = IntClamp(desired, 0, count - visible);
        Sc2kDirtyMarkRect(&desktop->dirty, b);
        return true;
      }
      return false;
    }
    PublicationLayout layout = NewspaperPageLayout(window->bounds, dialog, desktop->interface_font);
    int hovered = -1;
    for (int i = 0; i < 7; ++i)
      if (dialog->articles[i].headline[0] && CanvasPointInRect(layout.stories[i], x, y)) hovered = i;
    if (CanvasPointInRect(layout.banner, x, y) || CanvasPointInRect(layout.photo, x, y)) hovered = 0;
    if (hovered != dialog->hovered) {
      dialog->hovered = hovered;
      Sc2kDirtyMarkRect(&desktop->dirty, window->bounds);
    }
    if (!pressed || hovered < 0) return false;
    dialog->reading = hovered;
    dialog->scroll = 0;
    dialog->wheel_remainder = 0;
    dialog->close_at = 0; // An article stays open until the reader closes it.
    Sc2kDirtyMarkRect(&desktop->dirty, window->bounds);
    return true;
  }
  case CANVAS_SDL_KEY_DOWN: {
    CityHotkey key = input->key;
    if (CityNewspaperOpening(window)) {
      if (key != CITY_KEY_ESCAPE && key != CITY_KEY_RETURN && key != CITY_KEY_SPACE) return false;
      SetNewspaperOpeningFrame(game, window, kNewspaperOpeningFrames);
      return true;
    }
    if (dialog->reading < 0) return false;
    int visible, count = NewspaperReaderRows(window, desktop->interface_font, &visible);
    if (key == CITY_KEY_ESCAPE || key == CITY_KEY_BACKSPACE) dialog->reading = -1;
    else if (key == CITY_KEY_UP) --dialog->scroll;
    else if (key == CITY_KEY_DOWN) ++dialog->scroll;
    else if (key == CITY_KEY_PAGE_UP) dialog->scroll -= visible;
    else if (key == CITY_KEY_PAGE_DOWN) dialog->scroll += visible;
    else if (key == CITY_KEY_HOME) dialog->scroll = 0;
    else if (key == CITY_KEY_END) dialog->scroll = count;
    else return false;
    dialog->scroll = IntClamp(dialog->scroll, 0, count - visible);
    Sc2kDirtyMarkRect(&desktop->dirty, window->bounds);
    return true;
  }
  case CANVAS_SDL_SCROLL: {
    int delta = input->y;
    if (dialog->reading < 0) return true;
    int visible, count = NewspaperReaderRows(window, desktop->interface_font, &visible);
    dialog->wheel_remainder -= (i64)delta * 3;
    int previous = dialog->scroll;
    dialog->scroll = IntClamp(dialog->scroll + (int)(dialog->wheel_remainder / 1024), 0, count - visible);
    dialog->wheel_remainder %= 1024;
    if (previous != dialog->scroll) Sc2kDirtyMarkRect(&desktop->dirty, window->bounds);
    return true;
  }
  default: return false;
  }
}

CityNewspaperInitialization BeginCityNewspaperOpeningPresentation(City *city, int edition) {
  CityNewspaperDialog paint = {.edition_index = edition};
  return BeginCityNewspaperDialog(city, &paint);
}

// Retain the original newspaper caller for comparison. Returns the retained dispatch result; pending effects must
// continue through their existing work rather than being restarted after presentation.
i64 ProcessPendingCityDisaster(City *city, bool terrain_water_allowed) {
  i64 result = AdvanceCityDisasterEffects(city, terrain_water_allowed);
  if (result == CITY_OPERATION_PENDING) return result;
  if (!result) return GetGameError(city->game) == 0;
  CityNewspaperDialog newspaper = {0};
  RunPausedCityNewspaperDialog(city, &newspaper);
  return GetGameError(city->game) == 0;
}
