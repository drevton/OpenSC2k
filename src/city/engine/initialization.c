#include "city/engine/city_name.h"
#include "city/engine/initialization.h"

#include <string.h>

#include "game/audio.h"
#include "city/disasters/types.h"
#include "city/economy/economy.h"
#include "city/economy/facilities.h"
#include "city/economy/tables.h"
#include "city/economy/types.h"
#include "city/engine/city_types.h"
#include "city/engine/commands.h"
#include "city/engine/random.h"
#include "city/engine/simulation.h"
#include "city/events/events.h"
#include "city/events/news.h"
#include "city/maps/labels.h"
#include "city/maps/layers.h"
#include "city/maps/map.h"
#include "city/maps/map_types.h"
#include "city/maps/tile_storage.h"
#include "city/military.h"
#include "city/presentation.h"
#include "city/saveload/map_format.h"
#include "city/state.h"
#include "city/terrain/terrain.h"
#include "game/application.h"
#include "game/errors.h"
#include "game/state.h"

// Reset overlay, annual-facility, and city-event state for a new city. Requires allocated map/event storage; discards
// old labels, facility instances and event records for a new City.
void ResetNewCityOverlayAndEventState(City *city) {
  ResetCityMapAssociations(city);
  ResetCityFacilityState(city);
  ClearCityMapEvents(city);
}

// Clear terrain-generation layers before a fresh new-city setup. Requires bound complete map layers; this resets
// generated data rather than preserving a loaded City.
static void ResetNewCityMapGenerationLayers(City *city) {
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
  ResetCityHelicopterSoundTiming(city);
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

// Initialize the pre-load disaster flag; city creation and saved MISC supply gameplay state. Call before load/setup
// fields are applied; this is a default write, not a request to toggle the disaster rule.
void InitializeCityDisasterDefaults(City *city) {
  city->indicators.event_flag = 1;
}

// Found the configured city on the edited map. Admission is independent of local tools and windows.
i64 CmdStartCity(City *city, u32 flags, TileIndex tile, int reserved1, int reserved2, int reserved3) {
  if (tile != INVALID_TILE || reserved1 || reserved2 || reserved3) return CITY_COMMAND_COST_INVALID;
  if (city->settings.timer_mode != CITY_TIMER_STOPPED || !city->name.length) return CITY_COMMAND_COST_BLOCKED;
  if (flags & CITY_COMMAND_TEST) return 0;
  RecordRankedCityEvent(city, CITY_NEWS_CITY_FOUNDED, 0);
  city->geography.settings_preserved = 0;
  SetCityTimerMode(city, CITY_TIMER_RUNNING);
  return 1;
}

// Install a prepared terrain map into the reset City's existing storage. Legacy conversion can fail after writes;
// success restores generator RNG state, statistics and climate before the caller resets the viewport.
bool InstallCityTerrainMap(City *city, const CityTerrainMap *map, const CityTerrainSettings *settings) {
  for (TileIndex tile = 0; tile < map->cell_count; ++tile) {
    TileInfo *tp = &city->tile[tile];
    tp->altitude = map->altitude[tile];
    tp->zone = 0;
    tp->terrain = 0;
    if (map->legacy) tp->terrain = map->terrain[tile];
    else WriteTerrainCode(tp, map->terrain[tile]);
    tp->kind = map->legacy && !IsTileKindTree(map->buildings[tile]) ? TK_CLEAR : map->buildings[tile];
    tp->flags = map->flags[tile];
  }
  if (map->legacy) {
    BeginLegacyCityMapImport(city);
    if (!ConvertCityMapToNative(city)) {
      SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
      return false;
    }
  }
  RecountCityTileStatistics(city);
  city->random.crt = map->process_state;
  city->random.event = map->game_state;
  city->geography.rotation = 0;
  GenerateCityLandscape(city, map);
  city->geography.ocean = map->ocean;
  city->geography.river = map->river;
  city->geography.edit_height = map->water_level;
  city->geography.hills = settings->hills;
  city->geography.water = settings->water;
  city->geography.trees = settings->trees;
  return true;
}

// Initialize the accepted year, difficulty, national economy, milestones and starting accounts. The caller has reset
// the City's accounts and supplied setup values; all milestone draws and presentation calls retain their order.
void InitializeCitySetupState(City *city, int start_year, int economy_year, int difficulty) {
  city->geography.start_year = start_year;
  city->indicators.difficulty = difficulty;
  switch (economy_year) {
  case CITY_START_YEAR_1900:
    city->indicators.national_population = 10000;
    break;
  case CITY_START_YEAR_1950:
    city->indicators.national_population = 25000;
    break;
  case CITY_START_YEAR_2000:
    city->indicators.national_population = 60000;
    break;
  case CITY_START_YEAR_2050:
    city->indicators.national_population = 150000;
    break;
  }
  city->indicators.federal_interest_rate = 3;
  city->indicators.national_value = (3 * city->indicators.national_population) / 10;
  city->indicators.national_economy_trend = city->indicators.difficulty - CITY_DIFFICULTY_EASY;
  WriteHistorySample(city, CITY_HISTORY_GNP, 0, 3);
  WriteHistorySample(city, CITY_HISTORY_NATIONAL_POPULATION, 0, city->indicators.national_population);
  for (int index = 0; index < CITY_MILESTONE_COUNT; ++index) {
    int due = Sc2kCrtRandRange(city, 20) + CityMilestoneDefaultYear(index);
    city->economic_tables.milestone_years.due_year[index] = due < city->geography.start_year ? 0 : due;
  }
  RebuildCityUnlockState(city);
  switch ((int)city->indicators.difficulty) {
  case CITY_DIFFICULTY_EASY:
    city->indicators.cash = 20000;
    break;
  case CITY_DIFFICULTY_MEDIUM:
    city->indicators.cash = 10000;
    break;
  case CITY_DIFFICULTY_HARD: {
    city->indicators.cash = 10000;
    city->economic_tables.bond_rates.rate[city->indicators.bond_count] = city->indicators.federal_interest_rate;
    city->indicators.bond_rate_total = city->indicators.federal_interest_rate;
    ++city->indicators.bond_count;
    EconomySector *bonds = &city->economy.sector[CITY_ECONOMY_BONDS];
    bonds->current_amount = city->indicators.bond_count;
    bonds->current_rate =
        (u32)(10000 * city->indicators.bond_rate_total / (int)city->indicators.bond_count);
    bonds->monthly_amount[0] = 1;
    bonds->monthly_rate[0] =
        (u32)(10000 * city->indicators.bond_rate_total / (int)city->indicators.bond_count);
    bonds->accumulated = bonds->current_rate * bonds->current_amount;
    bonds->projected_annual_total = 12 * bonds->accumulated;
    break;
  }
  }
  if (city->geography.ocean != 0) {
    city->account_names.value[0] = 0;
    memcpy(city->geography.neighbor_names.name[0], "Ocean", 6u);
    city->primary_accounts.value[0] = 0;
    city->secondary_accounts.value[0] = 0;
    city->fourth_accounts.value[0] = 0;
  }
  RefreshCityNeighborNames(city);
}
