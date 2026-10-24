#ifndef SC2K_CITY_EVENTS_NEWS_H
#define SC2K_CITY_EVENTS_NEWS_H

#include "city/types.h"
#include <stddef.h>

// Newspaper titles vary by city; these IDs preserve menu-slot order.
typedef enum CityNewspaperEditionId {
  CITY_NEWSPAPER_EDITION_FIRST = 0, CITY_NEWSPAPER_EDITION_SECOND = 1,
  CITY_NEWSPAPER_EDITION_THIRD = 2, CITY_NEWSPAPER_EDITION_FOURTH = 3,
  CITY_NEWSPAPER_EDITION_FIFTH = 4, CITY_NEWSPAPER_EDITION_SIXTH = 5,
  CITY_NEWSPAPER_EDITION_COUNT = 6,
} CityNewspaperEditionId;

// Edition values index the six stored configurations directly.
static inline bool IsCityNewspaperEditionValid(int edition) {
  return (unsigned)edition < CITY_NEWSPAPER_EDITION_COUNT;
}

typedef struct CityNewspaperEdition {
  u8 title_kind;
  u8 layout_style;
  u8 era_kind;
  u8 report_kind;
  u8 subtitle_kind;
} CityNewspaperEdition;
typedef struct CityNewspaperEditions {
  CityNewspaperEdition edition[CITY_NEWSPAPER_EDITION_COUNT];
} CityNewspaperEditions;

_Static_assert(sizeof(CityNewspaperEdition) == 5 && sizeof(CityNewspaperEditions) == 30,
    "stored newspaper edition layout");

// Initialize startup news flags, seed the CRT stream from the signed low 16 bits of logical time, initialize
// article records and shuffle edition choices. The shuffle consumes 122 CRT draws and selects the first edition.
void InitializeCityNews(City *city);

// News

// Original ranked newspaper event/article-group IDs. Names come from the
// DATA_USA 1000/1001 group tables and 1002/1003 article templates, corroborated
// by callers of RecordRankedCityEvent. The seven ranked records retain their u16 ID, signed i16
// score, u8 event data and three cache bytes. Weather/report groups share
// this domain but are composed in the two extra records rather than ranked.
typedef enum CityNewsEvent {
  CITY_NEWS_WEATHER = 0,
  CITY_NEWS_LOCAL_NEWS = 1,
  CITY_NEWS_CITY_FOUNDED = 2,
  CITY_NEWS_POPULATION_REWARD = 3,
  CITY_NEWS_INVENTION = 4,
  CITY_NEWS_BUILDING_AVAILABLE = 5,
  CITY_NEWS_INTERNATIONAL_NEWS = 6,
  CITY_NEWS_MARKET = 7,
  CITY_NEWS_SPORTS = 8,
  CITY_NEWS_INTEREST_RATE_INCREASE = 9,
  CITY_NEWS_INTEREST_RATE_DECREASE = 10,
  CITY_NEWS_CONGRESS = 11,
  CITY_NEWS_DIPLOMACY = 12,
  CITY_NEWS_OTHER_CITY_FIRE = 13,
  CITY_NEWS_MEDICAL_RESEARCH = 14,
  CITY_NEWS_COMMUNITY_NEWS = 15,
  CITY_NEWS_HIGH_CRIME = 16,
  CITY_NEWS_HIGH_TRAFFIC = 17,
  CITY_NEWS_HIGH_POLLUTION = 18,
  CITY_NEWS_LOW_EDUCATION = 19,
  CITY_NEWS_POOR_HEALTH = 20,
  CITY_NEWS_HIGH_UNEMPLOYMENT = 21,
  CITY_NEWS_FIRE = 22,
  CITY_NEWS_FLOOD = 23,
  CITY_NEWS_PLANE_CRASH = 24,
  CITY_NEWS_HELICOPTER_CRASH = 25,
  CITY_NEWS_TORNADO = 26,
  CITY_NEWS_EARTHQUAKE = 27,
  CITY_NEWS_MONSTER = 28,
  CITY_NEWS_MELTDOWN = 29,
  CITY_NEWS_MICROWAVE = 30,
  CITY_NEWS_VOLCANO = 31,
  CITY_NEWS_POLLUTION_DISASTER = 32,
  CITY_NEWS_CHEMICAL_SPILL = 33,
  CITY_NEWS_HURRICANE = 34,
  CITY_NEWS_RIOTS = 35,
  CITY_NEWS_POWER_PLANT_AGING = 36,
  CITY_NEWS_PRISON_OVERCROWDING = 37,
  CITY_NEWS_SCHOOL_FUNDING = 38,
  CITY_NEWS_BRIDGE_COLLAPSE = 39,
  CITY_NEWS_TREE_REMOVAL_PROTEST = 40,
  CITY_NEWS_ORDINANCE_PASSED = 41,
  CITY_NEWS_FREDS_OPINION = 42,
  CITY_NEWS_EDITOR_COMMENTARY = 43,
  CITY_NEWS_OPINION_SURVEY = 44,
  CITY_NEWS_ADVICE = 45,
  CITY_NEWS_POWER_SHORTAGE = 46,
  CITY_NEWS_TRANSPORT_SHORTAGE = 47,
  CITY_NEWS_POLICE_SHORTAGE = 48,
  CITY_NEWS_FIRE_STATION_SHORTAGE = 49,
  CITY_NEWS_WATER_SHORTAGE = 50,
  CITY_NEWS_HOSPITAL_SHORTAGE = 51,
  CITY_NEWS_SCHOOL_SHORTAGE = 52,
  CITY_NEWS_SEAPORT_SHORTAGE = 53,
  CITY_NEWS_AIRPORT_SHORTAGE = 54,
  CITY_NEWS_ZOO_SHORTAGE = 55,
  CITY_NEWS_STADIUM_SHORTAGE = 56,
  CITY_NEWS_MARINA_SHORTAGE = 57,
  CITY_NEWS_PARK_SHORTAGE = 58,
  CITY_NEWS_INDUSTRIAL_CONNECTION_SHORTAGE = 59,
  CITY_NEWS_COMMERCIAL_CONNECTION_SHORTAGE = 60,
  CITY_NEWS_LOW_CRIME = 61,
  CITY_NEWS_LOW_TRAFFIC = 62,
  CITY_NEWS_LOW_POLLUTION = 63,
  CITY_NEWS_HIGH_EDUCATION = 64,
  CITY_NEWS_GOOD_HEALTH = 65,
  CITY_NEWS_LOW_UNEMPLOYMENT = 66,
  CITY_NEWS_EVENT_COUNT = 67,
} CityNewsEvent;

// Original random selection 1..5 is added to this base, producing 11..15.
// Keep the offset distinct from the unrelated rate-decrease event at 10.
enum { CITY_NEWS_RANDOM_ARTICLE_OFFSET = 10, CITY_NEWS_CACHE_INVALID = 255 };

// Problem-description table indices, distinct from article IDs.
typedef enum CityProblem {
  CITY_PROBLEM_WEATHER = -2, CITY_PROBLEM_NONE = -1,
  CITY_PROBLEM_POWER = 0, CITY_PROBLEM_TRANSPORT = 1,
  CITY_PROBLEM_POLICE = 2, CITY_PROBLEM_FIRE_STATION = 3,
  CITY_PROBLEM_WATER = 4, CITY_PROBLEM_HOSPITAL = 5,
  CITY_PROBLEM_SCHOOL = 6, CITY_PROBLEM_SEAPORT = 7,
  CITY_PROBLEM_AIRPORT = 8, CITY_PROBLEM_ZOO = 9,
  CITY_PROBLEM_STADIUM = 10, CITY_PROBLEM_MARINA = 11,
  CITY_PROBLEM_PARK = 12, CITY_PROBLEM_INDUSTRIAL_CONNECTION = 13,
  CITY_PROBLEM_COMMERCIAL_CONNECTION = 14,
  CITY_PROBLEM_COUNT = 15,
} CityProblem;

// Seven retained ranked articles plus the weather and report composition slots.
enum { CITY_NEWS_RANKED_COUNT = 7, CITY_NEWS_RECORD_COUNT = 9 };

typedef struct {
  u16 id;
  i16 score;
  u8 event_data;
  u8 article_cache[3];
} CityNewsRecord;

typedef struct {
  CityNewsRecord record[CITY_NEWS_RECORD_COUNT];
} CityNewsRecords;

_Static_assert(sizeof(CityNewsRecord) == 8 && offsetof(CityNewsRecord, score) == 2 &&
    offsetof(CityNewsRecord, event_data) == 4 && offsetof(CityNewsRecord, article_cache) == 5,
    "news record layout");
_Static_assert(sizeof(CityNewsRecords) == 72, "nine retained news records");

typedef struct {
  u16 scores[8];
  u16 category_order[8];
} CityNewspaperProblemRankings;

// Shared token choices for the current article; the body reuses the headline's choices.
// This transient composition state is not saved. Preview City copies own their own choices.
typedef struct {
  i8 choice[250];
} CityNewspaperSharedChoices;

_Static_assert(sizeof(CityNewspaperSharedChoices) == 250, "newspaper shared choice span");

typedef struct {
  u32 enabled;
  i16 random_base_seed;
  u16 edition_index;
  CityNewspaperProblemRankings problems;
  CityNewspaperSharedChoices choices;
} CityNewspaperState;

// Populate weather/editorial records and choose music using City RNG. Edition must be valid; run once after any
// opening notice is dismissed, before the first paint. Window state belongs to the caller.
void FinishCityNewspaperInitialization(City *city, int edition);

// Rankings

// Rank live City problem metrics using signed 16-bit weights/totals. Returns whether a parade notice is due.
// This performs the ranking once; callers retain the result through any resulting presentation.
bool BeginCityNewspaperProblemRankings(City *city);
void UpdateCityNewspaperRanksAndF3Records(City *city);
void FinishCityNewspaperRanksAndF3Records(City *city);
// Apply immutable Assets decay inputs and reorder existing records; no RNG or presentation.
void DecayCityNewsRankings(City *city);
// Insert a supported event before presentation, subject to Game/resource gates. Returns whether insertion occurred.
// Reads immutable severity, updates City records and consumes no RNG.
bool RecordRankedCityEvent(City *city, int event_code, u8 event_data);
// Records the event and returns whether its caller must present a newspaper.
bool PrepareRankedCityEvent(City *city, int event_code, u8 event_data);
void InsertRankedCityEvent(City *city, int event_code, u8 event_data);

// Composition consumes CRT RNG and may update article caches and shared choices. It needs only City and immutable
// resources; callers choose the seed. Compose the headline before its body to establish shared token choices.
typedef struct CityNewspaperResources CityNewspaperResources;

// Output must fit the expanded text. Returns its last occupied byte index; callers add the terminator.
int ExpandCityNewspaperTemplate(City *city, const CityNewspaperResources *resources, char *output, int template_id,
    int article_index, int half);
// Compose and terminate a headline; returns true. Output must fit the expanded text and terminator.
bool ComposeCityNewspaperArticle(City *city, const CityNewspaperResources *resources, int article_index, char *output);
// Compose and terminate its body; returns the last occupied byte index. Output has the same capacity contract.
int ComposeCityNewspaperArticleBody(City *city, const CityNewspaperResources *resources, int article_index,
    char *output);
// Output must fit the formatted temperature, wind and rainfall string, including the terminator.
void FormatCityNewspaperWeather(City *city, char *output);

#endif
