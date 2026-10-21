#include "city/economy/facilities.h"
#include "city/military.h"
#include "city/disasters/types.h"
#include "city/economy/types.h"
#include "city/engine/city_types.h"
#include "city/events/events.h"
#include "city/events/news.h"
#include "city/maps/map_types.h"
#include "city/maps/tile_storage.h"
#include "city/tools/types.h"
#include "game/application.h"
#include "city/presentation.h"
#include "city/engine/initialization.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio/state.h"
#include "city/economy/tables.h"
#include "game/state.h"
#include "city/engine/simulation.h"
#include "city/maps/layers.h"
#include "city/state.h"
#include "city/saveload/file.h"
#include "assets/catalog.h"
#include "assets/resources.h"
#include "base/text.h"

// Initialization

// The nine reserved facility types start with empty 25-byte names rather than localized defaults.
static const CityOverlayName kReservedFacilityDefaultName = {0};

// Change industry tax rates and refresh the sector's mean rate. Index must select an industry tax entry; updates its
// baseline and derived mean without opening a budget interaction.
void AdjustIndustryTaxRates(City *city, int direction) {
  CityIndustryDemand *baseline = &city->industry_baseline;
  int total = 0;
  for (int index = 0; index < CITY_INDUSTRY_COUNT; ++index) {
    // Stock adds in AX before its signed clamp; narrowing here is intentional.
    int value = (i16)(baseline->value[index] + direction);
    value = IntClamp(value, 0, 20);
    baseline->value[index] = value;
    total += value;
  }
  city->economy.sector[CITY_ECONOMY_INDUSTRIAL].current_rate = (total + 6) / CITY_INDUSTRY_COUNT;
}

// Reset overlay, annual-facility, and city-event state for a new city. Requires allocated map/event storage; discards
// old labels, facility instances and event records for a new City.
void ResetNewCityOverlayAndEventState(City *city) {
  static const u8 facility_types[CITY_FACILITY_RECORD_RESERVED_COUNT] = {TK_BUS_STATION, TK_BUS_STATION,
      TK_RAIL_STATION, TK_SUBWAY_STATION, TK_WIND_POWER, TK_HYDRO_POWER,
      TK_LARGE_PARK, TK_MUSEUM, TK_LIBRARY, TK_MARINA};

  CityAnnualFacilityRecord stock_prefix[CITY_FACILITY_RECORD_COUNT];
  if (!city->individual_facilities) memcpy(stock_prefix, city->annual_facilities.record, sizeof(stock_prefix));
  ReleaseCityAssociations(city);
  // Stock resets only the type of unused records; preserve their other payload fields.
  if (!city->individual_facilities) memcpy(city->annual_facilities.record, stock_prefix, sizeof(stock_prefix));
  for (int y = 0; y < city->map_height; ++y)
    for (int x = 0; x < city->map_width; ++x)
      city->tile[MapTileXY(city, x, y)].overlay = 0;

  for (int overlay = CITY_OVERLAY_SIGN_FIRST; overlay < CITY_OVERLAY_NAME_COUNT; ++overlay)
    city->overlay_names.entry[overlay].character[0] = 0;

  for (int index = 0; index < CITY_TEAM_NAME_COUNT; ++index) {
    const char *text = kCityTeamNames[index];
    CityOverlayName *name = &city->overlay_names.entry[index + CITY_TEAM_NAME_FIRST];
    snprintf((char *)name->character, sizeof(name->character), "%s", text != NULL ? text : "");
  }
  const char *mayor = "Mayor McSim";
  CityOverlayName *name = &city->overlay_names.entry[0];
  snprintf((char *)name->character, sizeof(name->character), "%s", mayor != NULL ? mayor : "");

  CityAnnualFacilities *facilities = &city->annual_facilities;
  for (int index = 0; index < CITY_FACILITY_RECORD_COUNT; ++index)
    facilities->record[index].type = TK_CLEAR;

  for (int index = 0; !city->individual_facilities && index < CITY_FACILITY_RECORD_RESERVED_COUNT; ++index) {
    CityAnnualFacilityRecord *record = &facilities->record[index];
    record->type = facility_types[index];
    if (index == 0)
      continue;
    record->age = 0;
    record->value_02 = 0;
    record->value_04 = 0;
    record->value_06 = 0;
    memcpy(city->overlay_names.entry[index + CITY_OVERLAY_FACILITY_FIRST].character,
        kReservedFacilityDefaultName.character, sizeof(CityOverlayName));
  }

  city->infrastructure.counter_a = 0;
  city->infrastructure.counter_b = 0;
  city->infrastructure.counter_c = 0;

  memset(city->events.event, 0, city->events.capacity * sizeof(CityMapEvent));
  memset(city->events.buckets, 0, CITY_EVENT_HASH_BUCKETS * sizeof(u16));
}

// Clear terrain-generation layers before a fresh new-city setup. Requires bound complete map layers; this resets
// generated data rather than preserving a loaded City.
void ResetNewCityMapGenerationLayers(City *city) {
  for (int y = 0; y < city->map_height; ++y) {
    for (int x = 0; x < city->map_width; ++x) {
      const TileIndex cell_index = MapTileXY(city, x, y);
      TileInfo *tp = &city->tile[cell_index];
      tp->altitude = 66;
      tp->zone = 0;
      tp->terrain = 0;
      tp->kind = TK_CLEAR;
      tp->flags = 0;
    }
  }
}

// Prepare a new-city document, then initialize its simulation state. Requires an initialized City and attached owners;
// generation/setup state supplies the new geography flags.
void InitializeNewCityDocument(City *city) {
  if (city->geography.settings_preserved == 0) {
    ResetNewCityMapGenerationLayers(city);
    city->geography.ocean = 0;
    city->geography.river = 1;
    city->geography.hills = 12;
    city->geography.water = 5;
    city->geography.trees = 15;
  }
  InitializeNewCitySimulationState(city);
}

// Reset simulation state after new-city map generation. Call only after map generation with initialized owners; clears
// old simulation accounts and installs the selected gameplay defaults.
void InitializeNewCitySimulationState(City *city) {
  city->fixes = city->game->default_city_fixes;
  memset(&city->cheats, 0, sizeof(city->cheats));
  city->unlocks.disaster_level = 0;
  city->clock.tick = 0;
  ResetNewCityOverlayAndEventState(city);

  for (int y = 0; y < city->map_height; ++y) {
    for (int x = 0; x < city->map_width; ++x) {
      const TileIndex cell_index = MapTileXY(city, x, y);
      TileInfo *tp = &city->tile[cell_index];
      int tile = tp->kind;
      if (!IsTileKindTree(tile)) {
        if (CityTileCanStoreBuildingStage(tp))
          tp->altitude &= CITY_ALTITUDE_CLEAR_WATER;
        tp->kind = ReadCityWaterGroundKind(tp);
        if (IsTileKindExposedWater(tp->kind)) tp->terrain &= CITY_TERRAIN_SHAPE_MASK;
      }
      if (!IsTileKindTree(tile)) {
        tp->terrain &= 15;
        tp->zone &= 31;
      }
      WriteCityZoneKind(tp, ZONE_NONE);
      WriteUnderground(city, cell_index, TUK_CLEAR);
      tp->flags &= TF_SALTWATER | TF_REGION_MASK;
    }
  }

  CityStatistics *statistics = &city->statistics;
  memset(statistics->value, 0, sizeof(statistics->value));
  city->special_tile_counts.count[CITY_SPECIAL_TILE_OTHER] = 0;
  memset(city->special_tile_counts.count + 1, 0,
      sizeof(city->special_tile_counts.count) - sizeof(city->special_tile_counts.count[CITY_SPECIAL_TILE_OTHER]));
  city->infrastructure.service_bonus_b = 0;
  city->infrastructure.service_bonus_a = 0;
  city->infrastructure.subway_tiles = 0;
  statistics->value[TK_CLEAR] = city->map_cell_count;

  for (int y = 0; y < city->map_height / 2; ++y) {
    for (int x = 0; x < city->map_width / 2; ++x) {
      const TileIndex cell_index = MapTileXY(city, x << 1, y << 1);
      WritePollution(city, cell_index, 0);
      WriteTrafficDensity(city, cell_index, 0);
      WriteLandValue(city, cell_index, 0);
      WriteCrime(city, cell_index, 0);
    }
  }
  for (int y = 0; y < city->map_height / 4; ++y) {
    for (int x = 0; x < city->map_width / 4; ++x) {
      const TileIndex cell_index = MapTileXY(city, x << 2, y << 2);
      WritePoliceCoverage(city, cell_index, 0);
      WriteFireCoverage(city, cell_index, 0);
      WriteDensity(city, cell_index, 0);
      WriteDensityHistory(city, cell_index, 127);
    }
  }

  ResetCityNewTools(city);

  if (city->game->idle_state == CITY_IDLE_TERRAIN)
    return;

  city->indicators.asset_value = 0;
  city->indicators.city_land_value = 0;
  city->indicators.city_crime = 0;
  city->indicators.city_traffic = 0;
  city->indicators.city_pollution = 0;
  city->infrastructure.water_percent = 0;
  city->infrastructure.power_percent = 0;
  city->settings.misc_14 = 0;
  city->settings.misc_15 = 0;
  city->indicators.population_cumulative = 0;
  city->indicators.birth_count = 0;
  city->indicators.attrition_count = 0;
  city->indicators.population = 0;
  city->indicators.workforce_education = 0;
  city->indicators.workforce_life_expectancy = 0;
  city->indicators.workforce_percent = 0;
  memset(city->age_cohorts.value, 0, sizeof(city->age_cohorts.value));
  memset(city->age_metric_a.value, 0, sizeof(city->age_metric_a.value));
  memset(city->age_metric_b.value, 0, sizeof(city->age_metric_b.value));
  memset(city->population.value, 0, sizeof(city->population.value));
  memset(city->industry_sectors.value, 0, sizeof(city->industry_sectors.value));
  city->weather.temperature = 150;
  city->weather.wind = 10;
  city->weather.rainfall = 15;
  city->weather.state = CITY_WEATHER_CHILLY;

  for (int series = 0; series < CITY_HISTORY_SERIES_COUNT; ++series) {
    memset(GetCityHistoryRow(city, series), 0, sizeof(CityHistoryRow));
    city->history_maxima.value[series] = kCityHistoryMaximumDefaults.value[series];
  }
  city->history_maxima.value[CITY_HISTORY_POWER_PERCENT] = 100;
  city->history_maxima.value[CITY_HISTORY_WATER_PERCENT] = 100;
  city->history_maxima.value[CITY_HISTORY_HEALTH] = 90;
  city->history_maxima.value[CITY_HISTORY_EDUCATION] = 150;

  CityAccountNameIndices *account_name_indices = &city->account_names;
  CityAccountQuartet *primary_accounts = &city->primary_accounts;
  CityAccountQuartet *secondary_accounts = &city->secondary_accounts;
  CityAccountQuartet *fourth_accounts = &city->fourth_accounts;
  for (int account = 0; account < 4; ++account) {
    int name_index;
    bool unique;
    do {
      name_index = Sc2kCrtRandRange(city, 36) + 1;
      unique = true;
      for (int previous = 0; previous < account; ++previous)
        if (account_name_indices->value[previous] == name_index)
          unique = false;
    } while (!unique);
    account_name_indices->value[account] = name_index;
    GenerateCityNeighborName(city, account);

    int amount = Sc2kCrtRandRange(city, 7400) + 100;
    int sample = Sc2kCrtRandRange(city, 7400) + 100;
    amount = IntMin(amount, sample);
    sample = Sc2kCrtRandRange(city, 7400) + 100;
    amount = IntMin(amount, sample);
    primary_accounts->value[account] = amount;
    secondary_accounts->value[account] = amount / (Sc2kCrtRandRange(city, 3) + 1);
    fourth_accounts->value[account] = 0;
  }

  memset(city->economic_tables.sector_rates.offsets.value, 0, sizeof(city->economic_tables.sector_rates.offsets.value));
  for (int sector = 0; sector < CITY_INDUSTRY_COUNT; ++sector) {
    city->industry_demand.value[sector] = 0;
    city->industry_baseline.value[sector] = 7;
    city->industry_accounts.value[sector] = 0;
  }
  memset(city->economic_tables.bond_rates.rate, 0, sizeof(city->economic_tables.bond_rates.rate));
  city->indicators.bond_rate_total = 0;
  city->indicators.bond_count = 0;
  for (int sector = 0; sector < CITY_ECONOMY_SECTOR_COUNT; ++sector) {
    EconomySector *economy = &city->economy.sector[sector];
    u32 default_rate = kCityDefaultEconomyRates.value[sector];
    economy->monthly_amount[0] = 0;
    economy->monthly_rate[0] = default_rate;
    economy->current_amount = 0;
    economy->current_rate = default_rate;
    economy->accumulated = 0;
    economy->projected_annual_total = 0;
  }

  city->indicators.year_flag = 0;
  city->unlocks.ordinance_flags = 0;
  city->indicators.industrial_pollution_bonus = 0;
  city->indicators.sector_population_total = 0;
  city->indicators.industrial_mix_bonus = 0;
  city->indicators.unemployment = 0;
  city->infrastructure.stadium_team_mask = 0;
  city->indicators.arcology_population = 0;
  city->infrastructure.facility_total = 0;
  city->infrastructure.facility_ratio = 0;
  city->disasters.type1_count = 0;
  city->disasters.type2_count = 0;
  city->disasters.border_water_count = 0;
  city->disasters.monster_count = 0;
  city->game->audio->helicopter_sound_deadline = SimulationFrameTime(city);
  city->disasters.tile237_count = 0;
  city->infrastructure.counter_a = 0;
  city->disasters.type16_count = 0;
  city->infrastructure.counter_b = 0;
  city->disasters.tile248_count = 0;
  city->infrastructure.counter_c = 0;
  city->disasters.tornado_count = 0;
  city->disasters.map_update_pending = 0;
  city->disasters.last_type = CITY_DISASTER_NONE;
  city->unlocks.tools.shared.special_facility_mask = 0;
  city->infrastructure.capacity = 0;
  city->disasters.last_event_slot = -1;
  city->mission.active = 0;
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
}

// Exchange two bytes in the newspaper edition table. Both pointers must address writable bytes; overlapping identical
// pointers are allowed and no ownership changes.
void SwapCityNewspaperByte(u8 *first, u8 *second) {
  int saved = *first;
  *first = *second;
  *second = saved;
}

// Shuffle newspaper editions and initialize facility article caches. Requires initialized resource and City buffers;
// consumes the deterministic shuffle draws and replaces cached article state.
void InitializeCityNewspaperAndFacilityState(City *city) {
  CityNewspaperEditions *editions = &city->newspaper_editions;
  FacilityRankings *rankings = &city->facility_rankings;

  for (int i = 0; i < 6; ++i) {
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
    int first = Sc2kCrtRandRange(city, 6);
    int second = Sc2kCrtRandRange(city, 6);
    SwapCityNewspaperByte(&editions->edition[first].title_kind, &editions->edition[second].title_kind);

    first = Sc2kCrtRandRange(city, 5);
    second = Sc2kCrtRandRange(city, 5);
    SwapCityNewspaperByte(&editions->edition[first + 1].layout_style, &editions->edition[second + 1].layout_style);

    first = Sc2kCrtRandRange(city, 6);
    second = Sc2kCrtRandRange(city, 6);
    SwapCityNewspaperByte(&editions->edition[first].era_kind, &editions->edition[second].era_kind);

    first = Sc2kCrtRandRange(city, 5);
    second = Sc2kCrtRandRange(city, 5);
    SwapCityNewspaperByte(&editions->edition[first + 1].report_kind, &editions->edition[second + 1].report_kind);

    first = Sc2kCrtRandRange(city, 6);
    second = Sc2kCrtRandRange(city, 6);
    SwapCityNewspaperByte(&editions->edition[first].subtitle_kind, &editions->edition[second].subtitle_kind);
  }

  for (int i = 0; i < 9; ++i) {
    FacilityRankingRecord *record = &rankings->record[i];
    record->id = i + CITY_NEWS_CONGRESS;
    record->score = 0;
    record->event_data = 0;
    record->article_cache[0] = 0xFF;
    record->article_cache[1] = 0xFF;
    record->article_cache[2] = 0xFF;
  }

  city->newspaper.edition_index = CITY_NEWSPAPER_EDITION_FIRST;
}

// Initialize City-owned newspaper and facility state. Requires City-owned buffers; resets their runtime defaults
// without adding a backing allocation.
void InitializeCityRuntimeBuffers(City *city) {
  city->indicators.ranked_disaster_gate = 1;
  city->newspaper.enabled = 0;
  int seed = (i16)SimulationFrameTime(city);
  city->newspaper.random_base_seed = seed;
  Sc2kSeedCrtRandom(city, seed);
  InitializeCityNewspaperAndFacilityState(city);
}

// Startup
// Original startup dialog lifetimes; platform creation/destruction uses canvas services.

// Initialize the pre-load disaster flag; city creation and saved MISC supply gameplay state. Call before load/setup
// fields are applied; this is a default write, not a request to toggle the disaster rule.
void InitializeCityDisasterDefaults(City *city) {
  city->indicators.event_flag = 1;
}

// Runtime

// Replace the City's CRT random seed with the supplied deterministic value. This seeds only that generator and does
// not draw a random number.
void Sc2kSeedCrtRandom(City *city, u32 seed) { city->random.crt = seed; }
