#include "assets/resource_types.h"
#include "audio/types.h"
#include "city/events/news.h"
#include "city/presentation.h"
#include "city/saveload/legacy_text.h"
#include "city/economy/economy.h"
#include "city/engine/simulation.h"
#include "game/errors.h"
#include "assets/catalog.h"
#include "ui/map.h"
#include "ui/news/newspaper.h"
#include "drawing/drawing_internal.h"
#include "assets/resources.h"



// Drawing subsystem: drawing newspaper.

static const i16 kNewspaperPictureByGroup[68] = {
  0, 0, 404, 404, 405, 402, 406, 0, 0, 0, 0, 404,
  406, 408, 0, 0, 410, 401, 411, 0, 0, 0, 408, 0,
  408, 409, 400, 406, 408, 408, 408, 411, 411, 409, 403, 411,
  410, 404, 402, 403, 407, 0, 0, 0, 0, 411, 403, 410,
  408, 403, 403, 403, 411, 411, 403, 403, 403, 403, 411, 410,
  401, 411, 404, 407, 407, 0, 0, 0,
};

static inline const u8 *NewspaperTemplate(const CityNewspaperResources *resources, int index) {
  return resources->text + resources->offsets[index];
}

// Initialize the four coordinates of one guest CityRect.
void ConstructCityRect(CityRect *rectangle, int left, int top, int right, int bottom) {
  const CityRect value = {left, top, right, bottom};
  // Some remaining packed transport records do not align their rectangles.
  memcpy(rectangle, &value, sizeof(value));
}

// Native capture pages share a regular grid. The publication window
// independently lays out the complete articles with the bundled publication fonts.
void InitializeCityNewspaperLayouts(Drawing *drawing) {
  CityMapDrawingState *map = &drawing->map_state;
  for (int style = 0; style < 3; ++style) {
    CityNewspaperLayout *layout = &map->newspaper_layouts.style[style];
    CityNewspaperFonts *fonts = &map->newspaper_fonts.style[style];
    CityNewspaperAlignments *alignments = &map->newspaper_alignments.style[style];
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

// Release the picture, cancel its window timer, then forward the default message.
void DestroyCityNewspaperWindow(City *city, CityNewspaperDialog *dialog) {
  dialog->picture = NULL;
  CityInterfaceState *ui = city->game->ui;
  if (ui->waiting_newspaper != NULL && ui->newspaper_modal_owner == ui->waiting_newspaper)
    ui->waiting_newspaper_timer_active = false;
  if (dialog == ui->waiting_newspaper)
    for (int index = 0; index < ui->desktop.count; ++index)
      if (ui->desktop.windows[index].window_id == ui->waiting_newspaper_window)
        Sc2kDirtyMarkRect(&ui->desktop.dirty, ui->desktop.windows[index].bounds);
  ApplicationUpdateDialogPause(city);
}

// Initialize newspaper article state before the first paint.
CityNewspaperInitialization BeginCityNewspaperDialog(City *city, CityNewspaperDialog *dialog) {
  CityResourceState *resources = city->game->resources;
  if (resources->facility_resources_unavailable != 0) {
    CityInterfaceState *ui = city->game->ui;
    if (dialog == ui->initializing_newspaper)
      ui->newspaper_closed_during_initialization = true;
    if (dialog == ui->waiting_newspaper)
      Sc2kCanvasDesktopDismissWindow(&ui->desktop, ui->waiting_newspaper_window);
  }
  dialog->picture = NULL;
  dialog->picture_id = 0;
  dialog->picture_attempted = 0;
  if (Sc2kMainWindowIsIconic(city->game->ui))
    return CITY_NEWSPAPER_INITIALIZATION_SKIPPED;
  StopCityMusicSequence(city->game);
  return BeginCityNewspaperProblemRankings(city) ? CITY_NEWSPAPER_INITIALIZATION_NOTICE :
      CITY_NEWSPAPER_INITIALIZATION_READY;
}

void FinishCityNewspaperInitialization(City *city, int edition_index) {
  static const u8 kSpecialReportGroup[6] = {
      CITY_NEWS_FREDS_OPINION, CITY_NEWS_EDITOR_COMMENTARY, CITY_NEWS_EDITOR_COMMENTARY,
      CITY_NEWS_OPINION_SURVEY, CITY_NEWS_OPINION_SURVEY, CITY_NEWS_ADVICE};
  static const u8 kMusicTrack[5] = {9, 15, 14, 6, 2};
  const CityNewspaperEdition *edition = &city->newspaper_editions.edition[edition_index];
  FacilityRankings *rankings = (&city->facility_rankings);
  rankings->record[7].id = CITY_NEWS_WEATHER;
  rankings->record[7].event_data = city->weather.state;
  rankings->record[8].id = kSpecialReportGroup[edition->report_kind];
  rankings->record[8].event_data = (u8)city->newspaper.problems.category_order[0];
  int music = NextCityMapRandomModulo(city, 5);
  PlayCityEventMusicById(city->game, CITY_MUSIC_TRACK_00 + kMusicTrack[music]);
}

void InitializeCityNewspaperDialog(City *city, CityNewspaperDialog *dialog) {
  CityNewspaperInitialization state = BeginCityNewspaperDialog(city, dialog);
  if (state == CITY_NEWSPAPER_INITIALIZATION_NOTICE)
    ShowCityResourceAlertDialog(city, CITY_BITMAP_NOTICE_407, CITY_NOTICE_MAYOR_PARADE);
  if (state != CITY_NEWSPAPER_INITIALIZATION_SKIPPED)
    FinishCityNewspaperInitialization(city, dialog->edition_index);
}

// Newspaper data is token bytecode. Only literal bytes carry the stock codepage.
static int WriteNewspaperLiteral(char *output, u8 byte) {
  if (byte == 0xD4 || byte == 0xD5) byte = '\'';
  if (byte == 0xD2 || byte == 0xD3) byte = '"';
  return WriteUtf8Character(output, DecodeLegacyCityCharacter(byte, CITY_TEXT_OEM_437));
}

// Copy an authored UTF-8 headline or body, substituting the city name for '='. Comparison resources instead expand
// stock article bytecode below. Output must fit the expanded text; the result is its last occupied byte index.
int ExpandCityNewspaperTemplate(City *city, const CityNewspaperResources *resources, char *output, int template_id,
    int article_index, int half) {
  if (resources->articles) {
    assert(template_id >= 0 && template_id < CITY_NEWS_EVENT_COUNT);
    const CityNewspaperArticle *article = &resources->articles[template_id];
    const char *source = half ? article->body : article->headline;
    const char *name = CityNameText(&city->name);
    size_t name_length = strlen(name);
    int written = 0;
    for (; *source; ++source) {
      if (*source == '=') {
        memcpy(output + written, name, name_length);
        written += name_length;
      } else {
        output[written++] = *source;
      }
    }
    return written - 1;
  }
  Drawing *drawing = city->game->drawing;
  const u8 *source = NewspaperTemplate(resources, template_id);
  FacilityRankingRecord *article = &city->facility_rankings.record[article_index];
  int written = 0;
  int position = 0;
  if (half == 1) {
    while (source[position] != 0 && source[position] != '+')
      ++position;
    if (source[position] == 0)
      return 0;
    ++position;
    half = 2;
  }
  while (source[position] != 0) {
    u8 token = source[position];
    if (half == 0 && token == '+')
      break;
    if (token == '$') {
      int value = article->event_data;
      if (value > 99) output[written++] = (char)('0' + value / 100 % 10);
      if (value > 9) output[written++] = (char)('0' + value / 10 % 10);
      output[written] = (char)('0' + value % 10);
    } else if (token == '%' || token == '<') {
      int maximum = source[++position];
      int chosen = 0;
      if (maximum >= 2)
        chosen = Sc2kCrtRandRange(city, maximum - 1) + 2;
      if (token == '<') {
        if (article->article_cache[2] == 0xFF)
          article->article_cache[2] = (u8)chosen;
        chosen = article->article_cache[2];
      }
      if (chosen > 99) output[written++] = (char)('0' + chosen / 100 % 10);
      if (chosen > 9) output[written++] = (char)('0' + chosen / 10 % 10);
      output[written] = (char)('0' + chosen % 10);
    } else if (token == '&') {
      int group = source[++position];
      if (resources->group_counts.value[group] <= article->event_data)
        article->event_data = 0;
      int next = resources->group_starts.value[group] + article->event_data;
      written += ExpandCityNewspaperTemplate(city, resources, output + written, next, article_index, half);
    } else if (token == '*' || token == '@' || token == '^' || token == '[') {
      u8 group = source[++position];
      int count = resources->group_counts.value[group];
      int choice = Sc2kCrtRandRange(city, count);
      if (token == '@' || token == '^') {
        u8 *cached = &article->article_cache[token == '@' ? 1 : 0];
        if (count <= *cached)
          *cached = (u8)choice;
        choice = *cached;
      } else if (token == '[') {
        i8 *cached = &drawing->map_state.newspaper_shared_choices.choice[group];
        if (*cached == -1)
          *cached = (i8)choice;
        choice = (u8)*cached;
      }
      int next = resources->group_starts.value[group] + choice;
      written += ExpandCityNewspaperTemplate(city, resources, output + written, next, article_index, half);
    } else if (token == '-') {
      output[written++] = '\n';
      for (int space = 0; space < 4; ++space)
        output[written++] = ' ';
      output[written] = ' ';
    } else if (token == '=') {
      memcpy(output + written, (CityNameText(&city->name)), (city->name.length) + 1);
      written += ((city->name.length) - 1);
    } else if (token == '>' || token == '~') {
      const char *name = token == '~' ? (const char *)city->overlay_names.entry[0].character :
          (const char *)city->overlay_names.entry[article->event_data + 251].character;
      int length = strlen(name);
      memcpy(output + written, name, (size_t)length + 1);
      written += length - 1;
    } else if (token == '\\') {
      written += WriteNewspaperLiteral(output + written, source[++position]) - 1;
    } else {
      int expansion = resources->token_expansions.template_id[token];
      if (expansion != 0)
        written += ExpandCityNewspaperTemplate(city, resources, output + written, expansion, article_index, half);
      else
        written += WriteNewspaperLiteral(output + written, token) - 1;
    }
    ++position;
    ++written;
  }
  return written - 1;
}

static int ChooseCityNewspaperTemplate(City *city, const CityNewspaperResources *resources, int article_index) {
  int group = city->facility_rankings.record[article_index].id;
  int count = resources->group_counts.value[group];
  int choice = Sc2kCrtRandRange(city, count);
  return resources->group_starts.value[group] + choice;
}

// Choose and compose a ranked newspaper article.
bool ComposeCityNewspaperArticle(City *city, const CityNewspaperResources *resources, int article_index, char *output) {
  Drawing *drawing = city->game->drawing;
  memset(drawing->map_state.newspaper_shared_choices.choice, -1, sizeof(drawing->map_state.newspaper_shared_choices.choice));
  int template_id = ChooseCityNewspaperTemplate(city, resources, article_index);
  int length = ExpandCityNewspaperTemplate(city, resources, output, template_id, article_index, 0);
  output[length + 1] = '\0';
  int previous_alphanumeric = 0;
  for (int index = 0; index < length; ++index) {
    int character = (u8)output[index];
    if (character == '\'') {
      if (index != 0 && output[index - 1] == 'L')
        previous_alphanumeric = 0;
    } else if (previous_alphanumeric || character < 'a' || character > 'z') {
      previous_alphanumeric = (character >= '0' && character <= '9') ||
          (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
    } else {
      output[index] = (char)(character - 'a' + 'A');
      previous_alphanumeric = 1;
    }
  }
  return true;
}

// Compose the second half of a ranked article.
int ComposeCityNewspaperArticleBody(City *city, const CityNewspaperResources *resources, int article_index,
    char *output) {
  int template_id = ChooseCityNewspaperTemplate(city, resources, article_index);
  int length = ExpandCityNewspaperTemplate(city, resources, output, template_id, article_index, 1);
  int size = length + 1;
  output[size] = '\0';
  int sentence_state = 0;
  for (int index = 0; index < size; ++index) {
    int character = (u8)output[index];
    if (sentence_state < 2 || character < 'a' || character > 'z') {
      if (character == ' ' || character == '.' || character == '!' || character == '?' ||
          character == 0xD2 || character == '"')
        ++sentence_state;
      else
        sentence_state = -(character == ',');
    } else {
      sentence_state = 0;
      output[index] = (char)(character - 'a' + 'A');
    }
  }
  return length;
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


void FormatCityNewspaperWeather(City *city, char *output) {
  sprintf(output, "%d", (int)city->weather.temperature - 100);
  strcat(output, "°F ");
  sprintf(output + strlen(output), "%u", (u32)city->weather.wind);
  strcat(output, "mph ");
  sprintf(output + strlen(output), "%u", (u32)city->weather.rainfall);
  strcat(output, "mm");
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
int FindNewspaperTextWrapPoint(const char *text) {
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
bool DrawNewspaperTextBlock(Game *game, NativeNewspaperDialog *capture, char *text, const CityRect *bounds, int font_size,
    int alignment) {
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
  Drawing *drawing = city->game->drawing;
  CityResourceState *resources = city->game->resources;
  const CityNewspaperEdition *edition = &city->newspaper_editions.edition[dialog->edition_index];
  const CityNewspaperLayout *layout = &drawing->map_state.newspaper_layouts.style[edition->layout_style];
  const CityNewspaperFonts *fonts = &drawing->map_state.newspaper_fonts.style[edition->layout_style];
  const CityNewspaperAlignments *alignments = &drawing->map_state.newspaper_alignments.style[edition->layout_style];
  int tick = (int)city->clock.tick;
  int month = tick / 25;
  city->clock.month = month % 12;
  city->clock.quarter = ((month % 12 + 1) % 12) / 3;
  city->clock.year = tick / 300;
  u32 seed = (u32)(int)city->newspaper.random_base_seed + (u32)month + 500 * (u32)dialog->edition_index;
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
    int picture = kNewspaperPictureByGroup[city->facility_rankings.record[0].id];
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
