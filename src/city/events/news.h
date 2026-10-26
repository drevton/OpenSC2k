#ifndef SC2K_CITY_EVENTS_NEWS_H
#define SC2K_CITY_EVENTS_NEWS_H

#include "city/events/news_types.h"

// Initialize startup news flags, seed the CRT stream from the signed low 16 bits of logical time, initialize
// article records and shuffle edition choices. The shuffle consumes 122 CRT draws and selects the first edition.
void InitializeCityNews(City *city);

// Populate weather/editorial records and choose music using City RNG. Edition must be valid; run once after any
// opening notice is dismissed, before the first paint. Window state belongs to the caller.
void FinishCityNewspaperInitialization(City *city, int edition);

// Read the current news-resource gate. Game's event-suppression policy remains separate.
// This query changes no owner or RNG state; callers retain their own result when presentation may run callbacks.
bool AreCityNewsResourcesAvailable(const City *city);

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
