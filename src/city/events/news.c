#include "city/events/news.h"
#include "city/economy/types.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "assets/resource_types.h"
#include "assets/state.h"
#include "audio/types.h"
#include "base/text.h"
#include "city/economy/facilities.h"
#include "city/engine/clock.h"
#include "city/engine/random.h"
#include "city/maps/labels.h"
#include "city/presentation.h"
#include "city/state.h"
#include "game/audio.h"
#include "game/state.h"

// Deterministic news ranking, insertion, problem ratings and article composition. Presentation may interrupt
// the caller; insertion happens first, and retained callers resume the existing tail after dismissal.

// Exchange two bytes in the newspaper edition table. Both pointers must address writable bytes; overlapping identical
// pointers are allowed and no ownership changes.
static void SwapCityNewspaperByte(u8 *first, u8 *second) {
  int saved = *first;
  *first = *second;
  *second = saved;
}

// Initialize and shuffle the six stored edition configurations, then reset article records and caches.
// The CRT stream is already seeded; retain the field-by-field swap order and all 122 draws.
static void InitializeCityNewspaperState(City *city) {
  CityNewspaperEditions *editions = &city->newspaper_editions;
  CityNewsRecords *rankings = &city->news_records;

  for (int i = 0; i < CITY_NEWSPAPER_EDITION_COUNT; ++i) {
    CityNewspaperEdition *edition = &editions->edition[i];
    edition->title_kind = i;
    edition->layout_style = i % 3;
    edition->era_kind = i % 3;
    edition->report_kind = i;
    edition->subtitle_kind = i;
  }

  SwapCityNewspaperByte(
      &editions->edition[0].report_kind, &editions->edition[3 + (Sc2kCrtRand(city) & 1)].report_kind);
  if ((Sc2kCrtRand(city) & 1) != 0)
    SwapCityNewspaperByte(&editions->edition[0].layout_style, &editions->edition[1].layout_style);

  for (int shuffle = 0; shuffle < 12; ++shuffle) {
    int first = Sc2kCrtRandRange(city, CITY_NEWSPAPER_EDITION_COUNT);
    int second = Sc2kCrtRandRange(city, CITY_NEWSPAPER_EDITION_COUNT);
    SwapCityNewspaperByte(&editions->edition[first].title_kind, &editions->edition[second].title_kind);

    first = Sc2kCrtRandRange(city, CITY_NEWSPAPER_EDITION_COUNT - 1);
    second = Sc2kCrtRandRange(city, CITY_NEWSPAPER_EDITION_COUNT - 1);
    SwapCityNewspaperByte(&editions->edition[first + 1].layout_style, &editions->edition[second + 1].layout_style);

    first = Sc2kCrtRandRange(city, CITY_NEWSPAPER_EDITION_COUNT);
    second = Sc2kCrtRandRange(city, CITY_NEWSPAPER_EDITION_COUNT);
    SwapCityNewspaperByte(&editions->edition[first].era_kind, &editions->edition[second].era_kind);

    first = Sc2kCrtRandRange(city, CITY_NEWSPAPER_EDITION_COUNT - 1);
    second = Sc2kCrtRandRange(city, CITY_NEWSPAPER_EDITION_COUNT - 1);
    SwapCityNewspaperByte(&editions->edition[first + 1].report_kind, &editions->edition[second + 1].report_kind);

    first = Sc2kCrtRandRange(city, CITY_NEWSPAPER_EDITION_COUNT);
    second = Sc2kCrtRandRange(city, CITY_NEWSPAPER_EDITION_COUNT);
    SwapCityNewspaperByte(&editions->edition[first].subtitle_kind, &editions->edition[second].subtitle_kind);
  }

  for (int i = 0; i < CITY_NEWS_RECORD_COUNT; ++i) {
    CityNewsRecord *record = &rankings->record[i];
    record->id = i + CITY_NEWS_CONGRESS;
    record->score = 0;
    record->event_data = 0;
    record->article_cache[0] = CITY_NEWS_CACHE_INVALID;
    record->article_cache[1] = CITY_NEWS_CACHE_INVALID;
    record->article_cache[2] = CITY_NEWS_CACHE_INVALID;
  }

  city->newspaper.edition_index = CITY_NEWSPAPER_EDITION_FIRST;
}

// Seed startup news from the signed low word of logical time before shuffling edition choices.
// These are existing City fields; initialization allocates no storage.
void InitializeCityNews(City *city) {
  city->indicators.ranked_disaster_gate = 1;
  city->newspaper.enabled = 0;
  int seed = (i16)SimulationFrameTime(city);
  city->newspaper.random_base_seed = seed;
  Sc2kSeedCrtRandom(city, seed);
  InitializeCityNewspaperState(city);
}

// Complete the captured edition after any opening notice. Read live City values, then consume the original
// single event-RNG draw for its music request. This tail must not repeat when the window is repainted.
void FinishCityNewspaperInitialization(City *city, int edition_index) {
  static const u8 kSpecialReportGroup[6] = {
      CITY_NEWS_FREDS_OPINION, CITY_NEWS_EDITOR_COMMENTARY, CITY_NEWS_EDITOR_COMMENTARY,
      CITY_NEWS_OPINION_SURVEY, CITY_NEWS_OPINION_SURVEY, CITY_NEWS_ADVICE};
  static const u8 kMusicTrack[5] = {9, 15, 14, 6, 2};
  const CityNewspaperEdition *edition = &city->newspaper_editions.edition[edition_index];
  CityNewsRecords *rankings = &city->news_records;
  rankings->record[7].id = CITY_NEWS_WEATHER;
  rankings->record[7].event_data = city->weather.state;
  rankings->record[8].id = kSpecialReportGroup[edition->report_kind];
  rankings->record[8].event_data = (u8)city->newspaper.problems.category_order[0];
  int music = NextCityMapRandomModulo(city, 5);
  MaybePlayCityEventMusic(city->game, CITY_MUSIC_TRACK_00 + kMusicTrack[music]);
}

// Rank the seven city problems for the newspaper. Applies ranking once and returns whether its parade notice is due;
// the caller must retain the matching tail across dismissal.
bool BeginCityNewspaperProblemRankings(City *city) {
  CityNewspaperProblemRankings *rankings = &city->newspaper.problems;
  const CityEconomyMonths *economy = &city->economy;
  // Stored weights use signed low words. Multiply the unsigned saved rate before narrowing.
  int weights[7] = {
      (i16)city->history[CITY_HISTORY_TRAFFIC].sample[0],
      (i16)city->history[CITY_HISTORY_POLLUTION].sample[0],
      (i16)city->history[CITY_HISTORY_CRIME].sample[0],
      (i16)(3 * economy->sector[CITY_ECONOMY_RESIDENTIAL].current_rate),
      (i16)city->indicators.unemployment,
      city->indicators.workforce_education <= 100 ? 100 - city->indicators.workforce_education : 0,
      city->indicators.workforce_life_expectancy <= 70 ? 70 - city->indicators.workforce_life_expectancy : 0,
  };
  int total = (i16)((u16)city->history[CITY_HISTORY_LAND_VALUE].sample[0] + 50);
  int counts[7] = {0};
  for (int index = 0; index < 7; ++index) {
    total = (i16)(total + weights[index]);
    rankings->scores[index] = 0;
    rankings->category_order[index] = index;
  }
  if (total == 0 || city->indicators.population < 100)
    return false;
  int prior_value = city->infrastructure.mayor_rating;
  city->infrastructure.mayor_rating = 0;
  for (int draw = 0; draw < 100; ++draw) {
    int choice = (int)Sc2kCrtRandRange(city, total);
    int category = 0;
    for (; category < 7; ++category) {
      if (choice < weights[category])
        break;
      choice = (i16)(choice - weights[category]);
    }
    if (category == 7)
      ++city->infrastructure.mayor_rating;
    else
      ++counts[category];
  }
  for (int upper = 6; upper > 0; --upper)
    for (int index = 0; index < upper; ++index) {
      int current = rankings->category_order[index];
      int next = rankings->category_order[index + 1];
      if (counts[next] > counts[current]) {
        rankings->category_order[index] = next;
        rankings->category_order[index + 1] = current;
      }
    }
  for (int index = 0; index < 7; ++index)
    rankings->scores[index] = counts[rankings->category_order[index]];
  if (prior_value < 80 && city->infrastructure.mayor_rating >= 80) {
    PlayCitySound(city, INVALID_TILE, CITY_SOUND_CIVIC_PLACEMENT);
    return true;
  }
  return false;
}

// Rank current City problems and show the mayor-parade notice when the ranking step requests it. The ranking effects
// happen before presentation, so callers must not repeat this after dismissal.
static void UpdateCityNewspaperProblemRankings(City *city) {
  if (BeginCityNewspaperProblemRankings(city))
    ShowCityResourceAlertDialog(city, CITY_BITMAP_NOTICE_407, CITY_NOTICE_MAYOR_PARADE);
}

// Refresh newspaper rankings and the corresponding type-0xF3 records. Executes ranking and the facility tail in the
// original order; nested notice callbacks may change City before the tail resumes.
void UpdateCityNewspaperRanksAndF3Records(City *city) {
  UpdateCityNewspaperProblemRankings(city);
  FinishCityNewspaperRanksAndF3Records(city);
}

// Complete the facility-record updates that follow newspaper problem ranking. Uses live facility state after any
// intervening notice; the caller must preserve the original phase ordering.
void FinishCityNewspaperRanksAndF3Records(City *city) {
  for (int index = 1; index < CityFacilityRecordLimit(city); ++index) {
    CityAnnualFacilityRecord *facility = CityFacilityRecordAt(city, index);
    if (facility->type != TK_MAYORS_HOUSE)
      continue;
    facility->mayors_house_approval = city->infrastructure.mayor_rating;
    if (facility->mayors_house_staff != 0) {
      --facility->mayors_house_staff;
      ++facility->age;
    }
  }
}

bool AreCityNewsResourcesAvailable(const City *city) {
  return city->game->resources->news_resources_unavailable == 0;
}

// Subtract each ranked article's decay and reorder those records by descending score.
// Requires initialized article records and ranking resources.
void DecayCityNewsRankings(City *city) {
  const CityResourceState *assets = city->game->resources;
  if (!AreCityNewsResourcesAvailable(city))
    return;
  CityNewsRecords *rankings = &city->news_records;
  for (int index = 0; index < CITY_NEWS_RANKED_COUNT; ++index) {
    CityNewsRecord *record = &rankings->record[index];
    int decay = assets->rankings.event_decay[record->id];
    record->score = record->score > decay ? record->score - decay : 0;
  }
  for (int index = 0; index < CITY_NEWS_RANKED_COUNT - 1; ++index)
    for (int candidate = index + 1; candidate < CITY_NEWS_RANKED_COUNT; ++candidate) {
      CityNewsRecord *first = &rankings->record[index];
      CityNewsRecord *second = &rankings->record[candidate];
      if (second->score > first->score) {
        CityNewsRecord saved = *first;
        *first = *second;
        *second = saved;
      }
    }
}

// Insert one ranked city event before any UI callback. Requires supported event code/data; returns whether insertion
// was admitted and performs it before any newspaper callback.
bool RecordRankedCityEvent(City *city, int event_code, u8 event_data) {
  if (city->game->ranked_events_suppressed != 0) return false;
  const CityResourceState *assets = city->game->resources;
  if (!AreCityNewsResourcesAvailable(city)) return false;
  CityNewsRecords *rankings = &city->news_records;
  int severity = assets->rankings.event_severity[event_code];
  int insert = 0;
  for (int index = CITY_NEWS_RANKED_COUNT - 2; index >= 0; --index) {
    CityNewsRecord *source = &rankings->record[index];
    if (source->score > severity) {
      insert = index + 1;
      break;
    }
    rankings->record[index + 1] = *source;
  }
  CityNewsRecord *target = &rankings->record[insert];
  target->id = event_code;
  target->score = severity;
  target->event_data = event_data;
  target->article_cache[0] = CITY_NEWS_CACHE_INVALID;
  target->article_cache[1] = CITY_NEWS_CACHE_INVALID;
  target->article_cache[2] = CITY_NEWS_CACHE_INVALID;
  return true;
}

// Insert an event into the ranked history and determine whether its newspaper should open. Returns false when
// insertion or presentation gating declines it; a true result does not mean the newspaper has been dismissed.
bool PrepareRankedCityEvent(City *city, int event_code, u8 event_data) {
  if (!RecordRankedCityEvent(city, event_code, event_data)) return false;
  return event_code == CITY_NEWS_CITY_FOUNDED ||
      (city->indicators.ranked_disaster_gate != 0 &&
          ((event_code >= CITY_NEWS_POPULATION_REWARD && event_code <= CITY_NEWS_BUILDING_AVAILABLE) ||
              event_code == CITY_NEWS_POWER_PLANT_AGING));
}

// Record a ranked City event and present its newspaper when admitted. Callers supply a valid event code and data;
// nested presentation must retain this caller's lifetime.
void InsertRankedCityEvent(City *city, int event_code, u8 event_data) {
  if (PrepareRankedCityEvent(city, event_code, event_data))
    ShowRankedCityEventModal(city, event_code, event_data);
}

// Article composition

static inline const u8 *NewspaperTemplate(const CityNewspaperResources *resources, int index) {
  return resources->text + resources->offsets[index];
}

// Newspaper data is token bytecode. Only literal bytes carry the stock codepage.
static int WriteNewspaperLiteral(char *output, u8 byte) {
  if (byte == 0xD4 || byte == 0xD5) byte = '\'';
  if (byte == 0xD2 || byte == 0xD3) byte = '"';
  return WriteUtf8Character(output, DecodeOem437Byte(byte));
}

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
  const u8 *source = NewspaperTemplate(resources, template_id);
  CityNewsRecord *article = &city->news_records.record[article_index];
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
        i8 *cached = &city->newspaper.choices.choice[group];
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
  int group = city->news_records.record[article_index].id;
  int count = resources->group_counts.value[group];
  int choice = Sc2kCrtRandRange(city, count);
  return resources->group_starts.value[group] + choice;
}

// Choose and compose a ranked newspaper article.
bool ComposeCityNewspaperArticle(City *city, const CityNewspaperResources *resources, int article_index, char *output) {
  memset(city->newspaper.choices.choice, -1, sizeof(city->newspaper.choices.choice));
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

void FormatCityNewspaperWeather(City *city, char *output) {
  sprintf(output, "%d", (int)city->weather.temperature - 100);
  strcat(output, "°F ");
  sprintf(output + strlen(output), "%u", (u32)city->weather.wind);
  strcat(output, "mph ");
  sprintf(output + strlen(output), "%u", (u32)city->weather.rainfall);
  strcat(output, "mm");
}
