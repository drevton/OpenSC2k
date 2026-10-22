#include "city/maps/labels.h"
#include "city/engine/random.h"
#include "city/economy/facilities.h"

#include <string.h>

#include "assets/state.h"
#include "assets/resources.h"
#include "base/text.h"
#include "game/state.h"
#include "city/military.h"
#include "city/economy/types.h"
#include "city/engine/commands.h"
#include "city/events/events.h"
#include "city/events/news.h"
#include "city/maps/tile_storage.h"
#include "city/maps/buildings.h"
#include "city/tools/types.h"
#include "city/presentation.h"
#include "city/economy/economy.h"
#include "city/maps/layers.h"
#include "city/saveload/map_format.h"
#include "city/state.h"
#include "city/tools/tools.h"

// Unlisted kinds have no specialized facility information template.
const u8 kFacilityClassByTile[TK_COUNT] = {
  [TK_HYDRO_POWER] = CITY_FACILITY_INFO_HYDRO_POWER,
  [TK_WIND_POWER] = CITY_FACILITY_INFO_WIND_POWER,
  [TK_GAS_POWER_PLANT] = CITY_FACILITY_INFO_POWER_PLANT,
  [TK_OIL_POWER_PLANT] = CITY_FACILITY_INFO_POWER_PLANT,
  [TK_NUCLEAR_POWER_PLANT] = CITY_FACILITY_INFO_POWER_PLANT,
  [TK_SOLAR_POWER_PLANT] = CITY_FACILITY_INFO_POWER_PLANT,
  [TK_MICROWAVE_POWER_PLANT] = CITY_FACILITY_INFO_POWER_PLANT,
  [TK_FUSION_POWER_PLANT] = CITY_FACILITY_INFO_POWER_PLANT,
  [TK_COAL_POWER_PLANT] = CITY_FACILITY_INFO_POWER_PLANT,
  [TK_CITY_HALL] = CITY_FACILITY_INFO_CITY_HALL,
  [TK_HOSPITAL] = CITY_FACILITY_INFO_HOSPITAL,
  [TK_POLICE_STATION] = CITY_FACILITY_INFO_POLICE,
  [TK_FIRE_STATION] = CITY_FACILITY_INFO_FIRE,
  [TK_MUSEUM] = CITY_FACILITY_INFO_MUSEUM,
  [TK_LARGE_PARK] = CITY_FACILITY_INFO_LARGE_PARK,
  [TK_SCHOOL] = CITY_FACILITY_INFO_SCHOOL,
  [TK_STADIUM] = CITY_FACILITY_INFO_STADIUM,
  [TK_PRISON] = CITY_FACILITY_INFO_PRISON,
  [TK_COLLEGE] = CITY_FACILITY_INFO_COLLEGE,
  [TK_ZOO] = CITY_FACILITY_INFO_ZOO,
  [TK_STATUE] = CITY_FACILITY_INFO_STATUE,
  [TK_SUBWAY_STATION] = CITY_FACILITY_INFO_SUBWAY_STATION,
  [TK_BUS_STATION] = CITY_FACILITY_INFO_BUS_DEPOT,
  [TK_RAIL_STATION] = CITY_FACILITY_INFO_RAIL_DEPOT,
  [TK_MAYORS_HOUSE] = CITY_FACILITY_INFO_MAYORS_HOUSE,
  [TK_WATER_TREATMENT_PLANT] = CITY_FACILITY_INFO_WATER_TREATMENT,
  [TK_LIBRARY] = CITY_FACILITY_INFO_LIBRARY,
  [TK_MARINA] = CITY_FACILITY_INFO_MARINA,
  [TK_DESALINATION_PLANT] = CITY_FACILITY_INFO_DESALINATION,
  [TK_PLYMOUTH_ARCOLOGY] = CITY_FACILITY_INFO_ARCOLOGY,
  [TK_FOREST_ARCOLOGY] = CITY_FACILITY_INFO_ARCOLOGY,
  [TK_DARCO_ARCOLOGY] = CITY_FACILITY_INFO_ARCOLOGY,
  [TK_LAUNCH_ARCOLOGY] = CITY_FACILITY_INFO_ARCOLOGY,
  [TK_BRAUN_LLAMA_DOME] = CITY_FACILITY_INFO_LLAMA_DOME,
};

void ResetCityFacilityState(City *city) {
  for (int index = 0; index < CITY_TEAM_NAME_COUNT; ++index) {
    CityOverlayName *name = &city->overlay_names.entry[index + CITY_TEAM_NAME_FIRST];
    const char *text = kCityTeamNames[index];
    CopyText((char *)name->character, sizeof(name->character), text ? text : "");
  }

  CityAnnualFacilities *facilities = &city->annual_facilities;
  if (city->individual_facilities) {
    memset(facilities->record, 0, sizeof(facilities->record));
  } else {
    // Shared records retain unused payloads, including slot zero. Reserved slots 1..9 reset their metrics and names.
    static const u8 kReservedTypes[CITY_FACILITY_RECORD_RESERVED_COUNT] = {
      TK_BUS_STATION, TK_BUS_STATION, TK_RAIL_STATION, TK_SUBWAY_STATION, TK_WIND_POWER,
      TK_HYDRO_POWER, TK_LARGE_PARK, TK_MUSEUM, TK_LIBRARY, TK_MARINA
    };
    for (int index = 0; index < CITY_FACILITY_RECORD_COUNT; ++index)
      facilities->record[index].type = TK_CLEAR;
    facilities->record[0].type = kReservedTypes[0];
    for (int index = 1; index < CITY_FACILITY_RECORD_RESERVED_COUNT; ++index) {
      facilities->record[index] = (CityAnnualFacilityRecord){.type = kReservedTypes[index]};
      city->overlay_names.entry[index + CITY_OVERLAY_FACILITY_FIRST] = (CityOverlayName){0};
    }
  }
  city->infrastructure.counter_a = 0;
  city->infrastructure.counter_b = 0;
  city->infrastructure.counter_c = 0;
}

// Initialize a facility record or increment a special kind's counters. Index and kind must select an allocated
// facility record; some categories consume deterministic initialization draws or aggregate shared records.
static void InitializeFacilityRecord(City *city, int index, u8 tile) {
  CityAnnualFacilityRecord *record = CityFacilityRecordAt(city, index);
  if (kFacilityClassByTile[tile] >= CITY_FACILITY_INFO_SHARED_RECORD_FIRST) {
    switch (tile) {
    case TK_HYDRO_POWER:
      ++record->value_02;
      record->value_04 += 20;
      break;
    case TK_WIND_POWER:
      ++record->value_02;
      record->value_04 += 4;
      break;
    case TK_LARGE_PARK:
      record->value_04 += 9;
      break;
    case TK_SUBWAY_STATION:
    case TK_BUS_STATION:
    case TK_RAIL_STATION:
      ++record->value_02;
      break;
    default:
      break;
    }
    return;
  }
  record->age = 0;
  record->value_02 = 0;
  record->value_04 = 0;
  record->value_06 = 0;
  switch (tile) {
  case TK_GAS_POWER_PLANT:
  case TK_SOLAR_POWER_PLANT:
    record->value_02 = 50;
    break;
  case TK_OIL_POWER_PLANT:
    record->value_02 = 220;
    break;
  case TK_NUCLEAR_POWER_PLANT:
    record->value_02 = 500;
    break;
  case TK_MICROWAVE_POWER_PLANT:
    record->value_02 = 1600;
    break;
  case TK_FUSION_POWER_PLANT:
    record->value_02 = 2500;
    break;
  case TK_COAL_POWER_PLANT:
    record->value_02 = 200;
    break;
  case TK_CITY_HALL:
    record->value_02 = ClampAnnualFacilityMetric(city, 200, 900);
    record->value_04 = city->geography.start_year + city->clock.year;
    break;
  case TK_HOSPITAL:
  case TK_SCHOOL:
  case TK_COLLEGE:
    record->age = 6;
    break;
  case TK_POLICE_STATION:
    record->value_02 =
        ClampAnnualFacilityMetric(city, 2 * city->economy.sector[CITY_ECONOMY_POLICE].current_rate, 90);
    break;
  case TK_FIRE_STATION:
    record->value_02 =
        ClampAnnualFacilityMetric(city, city->economy.sector[CITY_ECONOMY_FIRE].current_rate >> 1, 70);
    record->value_04 = 4;
    break;
  case TK_MUSEUM:
    record->age = 100;
    break;
  case TK_STATUE:
    record->value_02 = city->geography.start_year + city->clock.year;
    break;
  case TK_MAYORS_HOUSE:
    record->value_02 = city->geography.start_year + city->clock.year;
    record->value_04 = Sc2kCrtRandRange(city, 30) + 10;
    record->value_06 = Sc2kCrtRandRange(city, 60);
    break;
  case TK_PLYMOUTH_ARCOLOGY:
  case TK_FOREST_ARCOLOGY:
  case TK_DARCO_ARCOLOGY:
  case TK_LAUNCH_ARCOLOGY: {
    static const u8 capacities[] = {55, 30, 45, 65};
    record->value_02 = capacities[tile - TK_PLYMOUTH_ARCOLOGY];
    record->age = 5;
    record->value_06 = city->geography.start_year + city->clock.year;
    break;
  }
  case TK_BRAUN_LLAMA_DOME:
    if (memcmp(city->country.character, "aus", 4) != 0)
      record->value_06 = Sc2kCrtRand(city) & 0x3F;
    else
      record->value_06 = city->geography.start_year + city->clock.year;
    break;
  default:
    break;
  }
}

// Allocate a facility label and record for an admitted building kind; returns its
// overlay association or zero when no facility record is available/needed, with City retaining label storage.
int CreateFacilityInstance(City *city, TileIndex anchor, u8 tile) {
  if (!IsTileKindFacility(tile))
    return 0;
  int facility_class = kFacilityClassByTile[tile];
  if (!city->individual_facilities && facility_class == CITY_FACILITY_INFO_DEFAULT)
    return 0;
  int index;
  if (city->individual_facilities || facility_class < CITY_FACILITY_INFO_SHARED_RECORD_FIRST) {
    int first = CITY_FACILITY_RECORD_FIRST_DYNAMIC;
    if (city->individual_facilities) first = IntMax(city->annual_facilities.free_slot, first);
    for (index = first; index < CityFacilityRecordLimit(city); ++index)
      if (CityFacilityRecordAt(city, index)->type == TK_CLEAR)
        break;
  } else {
    index = facility_class - (CITY_FACILITY_INFO_SHARED_RECORD_FIRST - 1);
  }
  if (city->individual_facilities) {
    if (!ReserveCityFacilities(city, index + 1)) return 0;
    city->annual_facilities.used = IntMax(city->annual_facilities.used, index + 1);
  } else if (index >= CITY_FACILITY_RECORD_COUNT) {
    if (!IsTileKindArcology(tile))
      return 0;
    for (index = CITY_FACILITY_RECORD_FIRST_DYNAMIC; index < CITY_FACILITY_RECORD_COUNT; ++index)
      if (!IsTileKindArcology(CityFacilityRecordAt(city, index)->type))
        break;
    if (index >= CITY_FACILITY_RECORD_COUNT)
      return 0;
    ClearOverlayAssociation(city, CityFacilityOverlay(index));
  }
  if (index >= CITY_FACILITY_RECORD_FIRST_DYNAMIC) city->annual_facilities.free_slot = index + 1;
  CityAnnualFacilityRecord *record = CityFacilityRecordAt(city, index);
  if (!ReserveCityFacilities(city, index + 1)) return 0;
  CityFacilityDetails *details = &city->annual_facilities.details[index];
  details->tile = anchor;
  details->years = 0;
  city->annual_facilities.used = IntMax(city->annual_facilities.used, index + 1);
  if (city->individual_facilities) *record = (CityAnnualFacilityRecord){0};
  record->type = tile;
  InitializeFacilityRecord(city, index, tile);
  CityOverlayName *label = CityOverlayNameAt(city, CityFacilityOverlay(index));
  if (facility_class < CITY_FACILITY_INFO_SHARED_RECORD_FIRST || label->character[0] == 0) {
    // Stock's fallback copies bytes from unrelated live global storage. A
    // nameless facility has a defined blank label instead of arbitrary bytes.
    memset(label->character, 0, sizeof(label->character));
  }
  LinkCityFacility(city, index);
  return CityFacilityOverlay(index);
}

// Release reward/team bookkeeping before the footprint cells and their overlays are replaced. Reward notifications
// may change local presentation; the stadium overlay is read live at the captured northeast footprint corner.
void PrepareCityFacilityRemoval(City *city, TileIndex anchor, int kind) {
  if (kind == TK_MAYORS_HOUSE)
    SetSpecialFacilityAvailability(city, CITY_REWARD_MAYORS_HOUSE, 1);
  if (kind == TK_CITY_HALL)
    SetSpecialFacilityAvailability(city, CITY_REWARD_CITY_HALL, 1);
  if (kind == TK_STATUE)
    SetSpecialFacilityAvailability(city, CITY_REWARD_STATUE, 1);
  if (kind == TK_BRAUN_LLAMA_DOME)
    SetSpecialFacilityAvailability(city, CITY_REWARD_LLAMA_DOME, 1);
  if (kind == TK_STADIUM) {
    int overlay = ReadCityFacilityOverlay(city, anchor);
    if (IsCityFacilityOverlay(overlay)) {
      CityAnnualFacilityRecord *record = CityFacilityRecordAt(city, CityFacilityOverlaySlot(overlay));
      if (record->type == TK_STADIUM)
        city->infrastructure.stadium_team_mask -= (1u << ((u8)record->stadium_team & 31));
    }
  }
}

// Switch live facility record policy only during execution; TEST does not allocate or convert records.
i64 CmdSetIndividualFacilities(City *city, u32 flags, int enabled, int reserved1, int reserved2) {
  if ((enabled != 0 && enabled != 1) || reserved1 || reserved2)
    return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) return 0;
  return SetCityIndividualFacilities(city, enabled != 0) ? 1 : CITY_COMMAND_COST_BLOCKED;
}

// Convert live facility records and overlays to the selected sharing policy. Allocation failure may retain earlier
// conversion writes; success rebuilds association hashes and invalidates the map. Does not charge or advance time.
bool SetCityIndividualFacilities(City *city, bool individual) {
  if (city->individual_facilities == individual) return true;
  if (individual) {
    int additional = 0;
    for (TileIndex tile = 0; tile < (u32)city->map_cell_count; ++tile) {
      int kind = ReadTile(city, tile);
      if (!IsTileKindFacility(kind) || !kCityBuildings[kind].size ||
          ResolveBulldozeFootprint(city, tile).start != tile) continue;
      int category = kFacilityClassByTile[kind];
      if (category >= CITY_FACILITY_INFO_SHARED_RECORD_FIRST || !ReadCityFacilityOverlay(city, tile)) ++additional;
    }
    int needed = IntMax(city->annual_facilities.used, CITY_FACILITY_RECORD_COUNT) + additional;
    if (additional && !ReserveCityFacilities(city, IntMin(needed,
        city->map_cell_count + CITY_FACILITY_RECORD_RESERVED_COUNT))) return false;
    city->individual_facilities = true;
    for (TileIndex tile = 0; tile < (u32)city->map_cell_count; ++tile) {
      int kind = ReadTile(city, tile);
      if (!IsTileKindFacility(kind) || !kCityBuildings[kind].size ||
          ResolveBulldozeFootprint(city, tile).start != tile || ReadCityFacilityOverlay(city, tile)) continue;
      int category = kFacilityClassByTile[kind];
      CityOverlayName shared_name = {0};
      if (category >= CITY_FACILITY_INFO_SHARED_RECORD_FIRST) {
        int shared = category - (CITY_FACILITY_INFO_SHARED_RECORD_FIRST - 1);
        shared_name = *ReadCityOverlayName(city, CityFacilityOverlay(shared));
      }
      int overlay = CreateFacilityInstance(city, tile, kind);
      if (!overlay) return false;
      if (shared_name.character[0]) *CityOverlayNameAt(city, overlay) = shared_name;
    }
    for (int slot = 0; slot < CITY_FACILITY_RECORD_RESERVED_COUNT; ++slot)
      city->annual_facilities.record[slot] = (CityAnnualFacilityRecord){0};
  } else {
    static const u8 types[CITY_FACILITY_RECORD_RESERVED_COUNT] = {TK_BUS_STATION, TK_BUS_STATION,
        TK_RAIL_STATION, TK_SUBWAY_STATION, TK_WIND_POWER, TK_HYDRO_POWER,
        TK_LARGE_PARK, TK_MUSEUM, TK_LIBRARY, TK_MARINA};
    for (int slot = 0; slot < CITY_FACILITY_RECORD_RESERVED_COUNT; ++slot)
      city->annual_facilities.record[slot] = (CityAnnualFacilityRecord){.type = types[slot]};
    for (TileIndex tile = 0; tile < (u32)city->map_cell_count; ++tile) {
      int kind = ReadTile(city, tile), category = kFacilityClassByTile[kind];
      if (!IsTileKindFacility(kind) || !kCityBuildings[kind].size ||
          ResolveBulldozeFootprint(city, tile).start != tile) continue;
      int overlay = ReadCityFacilityOverlay(city, tile);
      if (category != CITY_FACILITY_INFO_DEFAULT && category < CITY_FACILITY_INFO_SHARED_RECORD_FIRST) continue;
      if (overlay) ClearOverlayAssociation(city, overlay);
      if (category != CITY_FACILITY_INFO_DEFAULT) {
        city->individual_facilities = false;
        CreateFacilityInstance(city, tile, kind);
        city->individual_facilities = true;
      }
    }
    for (int slot = CITY_FACILITY_RECORD_COUNT; slot < city->annual_facilities.used; ++slot) {
      CityAnnualFacilityRecord *record = CityFacilityRecordAt(city, slot);
      if (!record->type) continue;
      int free_slot = CITY_FACILITY_RECORD_FIRST_DYNAMIC;
      while (free_slot < CITY_FACILITY_RECORD_COUNT && city->annual_facilities.record[free_slot].type) ++free_slot;
      if (free_slot < CITY_FACILITY_RECORD_COUNT) {
        city->annual_facilities.record[free_slot] = *record;
        city->annual_facilities.details[free_slot] = city->annual_facilities.details[slot];
        *CityOverlayNameAt(city, CityFacilityOverlay(free_slot)) = *CityOverlayNameAt(city, CityFacilityOverlay(slot));
      }
      record->type = TK_CLEAR;
      city->annual_facilities.details[slot].tile = INVALID_TILE;
    }
    city->individual_facilities = false;
  }
  city->annual_facilities.free_slot = CITY_FACILITY_RECORD_FIRST_DYNAMIC;
  RebuildCityAssociationHashes(city);
  InvalidateMap(city);
  return true;
}

// Bounds annual facility metrics by population, funding, and city statistics. Preserves stock signed-word inputs and
// unsigned-word output; zero divisor selects its defined fallback and no record is written.
int ClampAnnualFacilityMetric(City *city, int requested, int divisor) {
  // Stock's signed word arguments and unsigned word result affect arcology growth.
  requested = ((requested & 0xFFFF) ^ 0x8000) - 0x8000;
  divisor = ((divisor & 0xFFFF) ^ 0x8000) - 0x8000;
  CityTileCounts *statistics = &city->statistics;
  int total = statistics->count[TK_DARCO_ARCOLOGY] + statistics->count[TK_FOREST_ARCOLOGY] +
      statistics->count[TK_PLYMOUTH_ARCOLOGY] + statistics->count[TK_LAUNCH_ARCOLOGY];
  u32 average = (u32)(total / 16);
  u32 bonus = average > 140 ? 20000 * average - 2800000 : 0;
  if (divisor == 0)
    divisor = 100;
  int maximum = ((city->indicators.population + city->indicators.arcology_population + bonus) /
      (u32)divisor) & 0xFFFF;
  return IntMin(requested, maximum) & 0xFFFF;
}

// Locate the overlay tile associated with an annual facility record. Record index must be valid; returns the first
// matching tile or INVALID_TILE without changing labels or facility ownership.
static TileIndex FindAnnualFacilityOverlayTile(City *city, int record_index) {
  if (city->individual_facilities && city->annual_facilities.details)
    return city->annual_facilities.details[record_index].tile;
  for (int y = 0; y < city->map_height; ++y)
    for (int x = 0; x < city->map_width; ++x) {
      TileIndex tile = MapTileXY(city, x, y);
      if (ReadOverlay(city, tile) == (u8)(record_index + CITY_OVERLAY_FACILITY_FIRST)) return tile;
    }
  return INVALID_TILE;
}

// Annual facility updates and large-city award. Requires the annual phase boundary; exodus notices retain their caller
// and the captured count is consumed once between them.
void AdvanceAnnualFacilityState(City *city) {
  int arcology_count = UpdateCityAnnualFacilities(city);
  if (arcology_count < 0) return;
  ShowCityMessage(city, CITY_NOTICE_ARCOLOGY_EXODUS_BEGUN);
  CompleteCityArcologyExodus(city, arcology_count);
  ShowCityMessage(city, CITY_NOTICE_ARCOLOGY_EXODUS_COMPENSATION);
}

// Resolve an aged plant after its warning by renewing it when admitted or removing its footprint. Index must identify
// the retained facility record; uses live cash/location after callbacks and preserves the stock tile-zero
// compatibility case.
static void FinishAgingCityPowerPlant(City *city, int index) {
  CityAnnualFacilityRecord *facility = CityFacilityRecordAt(city, index);
  if (facility->type < TK_GAS_POWER_PLANT || facility->type > TK_COAL_POWER_PLANT) return;
  TileIndex location = facility->age > 50 ? FindAnnualFacilityOverlayTile(city, index) : INVALID_TILE;
  // Stock's packed return used zero for both failure and tile (0, 0).
  if (location != INVALID_TILE &&
      (city->individual_facilities || location != 0 || city->map_cell_count != CITY_MAP_CELL_COUNT)) {
    if (city->indicators.event_flag != 0) {
      int cost_index = kPowerPlantToolVariants[facility->type - TK_GAS_POWER_PLANT];
      u32 cost = kCityToolCosts[CITY_TOOL_GROUP_POWER * CITY_TOOL_VARIANT_CAPACITY + cost_index];
      if (city->indicators.cash >= cost) {
        city->indicators.cash = AddInt64Clamped(city->indicators.cash, -(i64)cost);
        facility->age = 0;
        if (city->annual_facilities.details) city->annual_facilities.details[index].years = 0;
        return;
      }
    }
    CityPoint location_point = MapTilePt(city, location);
    if (city->settings.auto_goto != 0)
      CenterCityMapOnEvent(city, location_point.x, location_point.y);
    BulldozeCityTile(city, location, true);
    ClearOverlayAssociation(city, CityFacilityOverlay(index));
  }
}

// Returns the captured compensation count when an exodus begins, or -1 otherwise. Completes the same annual scan
// across aging notices; callers must propagate its captured exodus count to the real compensation path.
int UpdateCityAnnualFacilities(City *city) {
  CityAnnualFacilityWork work = BeginCityAnnualFacilities(city);
  while (!AdvanceCityAnnualFacilities(city, &work))
    ShowRankedCityEventModal(city, CITY_NEWS_POWER_PLANT_AGING, work.notice_data);
  return FinishCityAnnualFacilities(city, &work);
}

// Capture annual facility averages and initialize its resumable scan state. Clears the current facility-ratio flag
// once; callers must keep the returned work through notices and finish the same scan.
CityAnnualFacilityWork BeginCityAnnualFacilities(City *city) {
  CityTileCounts *statistics = &city->statistics;
  CityAnnualFacilityWork work = {
      .index = 1,
      .average = (statistics->count[TK_DARCO_ARCOLOGY] + statistics->count[TK_FOREST_ARCOLOGY] +
          statistics->count[TK_PLYMOUTH_ARCOLOGY] + statistics->count[TK_LAUNCH_ARCOLOGY]) / 16,
      .ratio_divisor = IntMax(statistics->count[TK_PRISON] / 16, 1),
      .prior_ratio_flag = city->infrastructure.facility_ratio};
  city->infrastructure.facility_ratio = 0;
  return work;
}

// False retains the selected power-plant branch until its newspaper closes. Work must come from
// BeginCityAnnualFacilities and survive each notice; true means the scan reached its end, not that its final totals
// were committed.
bool AdvanceCityAnnualFacilities(City *city, CityAnnualFacilityWork *work) {
  CityTileCounts *statistics = &city->statistics;
  CityEconomyMonths *economy = &city->economy;
  CityAgeCohorts *cohorts = &city->age_cohorts;
  for (; work->index < CityFacilityRecordLimit(city); ++work->index) {
    int index = work->index;
    CityAnnualFacilityRecord *facility = CityFacilityRecordAt(city, index);
    if (work->resume_power) {
      work->resume_power = false;
      FinishAgingCityPowerPlant(city, index);
      continue;
    }
    if (index >= CITY_FACILITY_RECORD_FIRST_DYNAMIC && facility->type && city->annual_facilities.details &&
        city->annual_facilities.details[index].years < UINT32_MAX)
      ++city->annual_facilities.details[index].years;
    if (facility->type == TK_HYDRO_POWER) {
      facility->value_02 = city->individual_facilities ? 1 : statistics->count[TK_HYDRO_POWER];
      facility->value_04 = 20 * facility->value_02;
    } else if (facility->type == TK_WIND_POWER) {
      facility->value_02 = city->individual_facilities ? 1 : statistics->count[TK_WIND_POWER];
      facility->value_04 = 4 * facility->value_02;
    } else if (facility->type >= TK_GAS_POWER_PLANT && facility->type <= TK_COAL_POWER_PLANT) {
      ++facility->age;
      facility->value_04 = city->infrastructure.power_percent + (Sc2kCrtRand(city) & 7);
      if (facility->age > 48 &&
          PrepareRankedCityEvent(city, CITY_NEWS_POWER_PLANT_AGING, facility->type - TK_GAS_POWER_PLANT)) {
        work->resume_power = true;
        work->notice_data = facility->type - TK_GAS_POWER_PLANT;
        return false;
      }
      FinishAgingCityPowerPlant(city, index);
    } else if (facility->type == TK_CITY_HALL) {
      facility->value_02 = ClampAnnualFacilityMetric(city, 200, 900);
    } else if (facility->type == TK_HOSPITAL) {
      u32 rate = economy->sector[CITY_ECONOMY_HEALTH].current_rate;
      facility->value_06 = (int)rate / 2;
      int divisor = 25 * (statistics->count[TK_HOSPITAL] / 9);
      divisor = IntMax(divisor, 1);
      int requested = (int)((Sc2kCrtRand(city) & 15) + city->indicators.population / (u32)divisor);
      if (requested > 1000)
        requested = (Sc2kCrtRand(city) & 127) + 1000;
      facility->value_02 = ClampAnnualFacilityMetric(city, requested, 30);
      int workload = 2 * facility->age - 24 + (int)(Sc2kCrtRand(city) & 7) + (int)rate;
      workload = IntMax(workload, 0);
      facility->value_04 = ClampAnnualFacilityMetric(city, workload, 120);
      int score = 10 * facility->value_02 / (facility->value_04 == 0 ? 1 : facility->value_04);
      facility->age = score < 50 ? 12 : score <= 110 ? (110 - score) / 5 : 0;
    } else if (facility->type == TK_POLICE_STATION) {
      facility->age = economy->sector[CITY_ECONOMY_POLICE].current_rate;
      facility->value_02 =
          ClampAnnualFacilityMetric(city, 2 * economy->sector[CITY_ECONOMY_POLICE].current_rate, 90);
      int count = statistics->count[TK_POLICE_STATION];
      count = IntMax(count, 1);
      u32 value = city->indicators.city_crime / (u32)count;
      facility->value_04 = value;
      int divisor = 5 - work->prior_ratio_flag;
      divisor = IntMax(divisor, 1);
      int capacity = (int)value / divisor + (int)(Sc2kCrtRand(city) & 15);
      facility->value_06 = capacity;
      int total = capacity + work->value_sum;
      // Stock stores the running sum as a word between facility records.
      work->value_sum = IntMin(total, 0xFFFF) & 0xFFFF;
    } else if (facility->type == TK_FIRE_STATION) {
      facility->age = economy->sector[CITY_ECONOMY_FIRE].current_rate;
      facility->value_02 = ClampAnnualFacilityMetric(city, economy->sector[CITY_ECONOMY_FIRE].current_rate >> 1, 70);
      facility->value_04 = (facility->value_02 >> 4) + 1;
      facility->value_06 = Sc2kCrtRandRange(city, 20) + 2;
    } else if (facility->type == TK_MUSEUM) {
      u32 rate = economy->sector[CITY_ECONOMY_COLLEGES].current_rate;
      u32 requested = 4 * rate * (city->individual_facilities ? 9 : statistics->count[TK_MUSEUM]);
      facility->value_02 = ClampAnnualFacilityMetric(city, requested, 20);
      facility->value_04 = (city->individual_facilities ? 9 : statistics->count[TK_MUSEUM]) * ((int)rate / 10);
    } else if (facility->type == TK_LARGE_PARK) {
      u32 value = 412 * facility->value_04;
      if (value > 65000)
        value = 65000;
      u32 population = city->indicators.population / 6;
      if (population > 65000)
        population = 65000;
      if (population < value)
        value = population;
      facility->value_02 = value;
      facility->value_04 = city->individual_facilities ? 9 :
          statistics->count[TK_LARGE_PARK] + statistics->count[TKL_PARK];
      facility->value_06 = ClampAnnualFacilityMetric(city, facility->value_04 / 9, 120);
    } else if (facility->type == TK_SCHOOL) {
      u32 rate = economy->sector[CITY_ECONOMY_SCHOOLS].current_rate;
      facility->value_06 = (int)rate / 4;
      if (facility->value_06 < 20) {
        InsertRankedCityEvent(city, CITY_NEWS_SCHOOL_FUNDING, 0);
        if (index >= CityFacilityRecordLimit(city)) continue;
        facility = CityFacilityRecordAt(city, index);
      }
      int divisor = statistics->count[TK_SCHOOL] / 9;
      divisor = IntMax(divisor, 1);
      int requested = (int)((cohorts->value[2] + cohorts->value[1]) / (u32)divisor + (Sc2kCrtRand(city) & 15));
      if (requested > 1500)
        requested = (Sc2kCrtRand(city) & 255) + 1500;
      facility->value_02 = ClampAnnualFacilityMetric(city, requested, 20);
      int workload = facility->age - 12 + (6 * (int)rate) / 10 + (Sc2kCrtRand(city) & 7);
      workload = IntMax(workload, 0);
      facility->value_04 = ClampAnnualFacilityMetric(city, workload, 100);
      int score = facility->value_02 / (facility->value_04 == 0 ? 1 : facility->value_04);
      facility->age = score < 15 ? 12 : score <= 51 ? (51 - score) / 3 : 0;
      if (facility->age < 4)
        work->low_school_quality = true;
    } else if (facility->type == TK_STADIUM) {
      int divisor = statistics->count[TK_STADIUM];
      divisor = IntMax(divisor, 1);
      int total = statistics->count[TK_DARCO_ARCOLOGY] + statistics->count[TK_FOREST_ARCOLOGY] +
          statistics->count[TK_PLYMOUTH_ARCOLOGY] + statistics->count[TK_LAUNCH_ARCOLOGY];
      int bonus = total / 16 > 140 ? 4000 * (5 * (total / 16) - 700) : 0;
      u32 requested = (city->indicators.population + (u32)bonus + city->indicators.arcology_population) / (u32)divisor;
      if ((int)requested > 25000)
        requested = 25000 - (u8)Sc2kCrtRand(city);
      facility->value_02 = ClampAnnualFacilityMetric(city, requested, 5) + (u8)Sc2kCrtRand(city);
      facility->age = (Sc2kCrtRand(city) & 31) + 9;
    } else if (facility->type == TK_PRISON) {
      int requested =
          city->infrastructure.facility_total / work->ratio_divisor + facility->value_02 - facility->value_02 / 4u;
      if (requested > 10000)
        requested = (Sc2kCrtRand(city) & 1023) + 10000;
      facility->value_02 = ClampAnnualFacilityMetric(city, requested, 20);
      facility->value_04 =
          ClampAnnualFacilityMetric(city, 3 * economy->sector[CITY_ECONOMY_POLICE].current_rate, 120);
      facility->value_06 = requested / 100;
      city->infrastructure.facility_ratio += requested / 100;
      if (requested / 100 > 105)
        work->high_prison_load = true;
      if (requested / 100 <= 90) {
        facility->age = 0;
      } else {
        int divisor = (int)(100 - economy->sector[CITY_ECONOMY_POLICE].current_rate) / 10 + requested / 100 - 90;
        facility->age = Sc2kCrtRandRange(city, divisor);
      }
    } else if (facility->type == TK_COLLEGE) {
      u32 rate = economy->sector[CITY_ECONOMY_COLLEGES].current_rate;
      facility->value_06 = rate;
      int divisor = statistics->count[TK_COLLEGE] / 16;
      divisor = IntMax(divisor, 1);
      int requested = (int)((Sc2kCrtRand(city) & 31) + cohorts->value[3] / (u32)divisor);
      if (requested > 5000)
        requested = (Sc2kCrtRand(city) & 511) + 5000;
      facility->value_02 = ClampAnnualFacilityMetric(city, requested, 30);
      int workload = (int)(4u * facility->age - 48 + (Sc2kCrtRand(city) & 15) + 2 * rate);
      workload = IntMax(workload, 0);
      facility->value_04 = ClampAnnualFacilityMetric(city, workload, 100);
      int score = 4 * facility->value_02 / (facility->value_04 == 0 ? 1 : facility->value_04);
      facility->age = score < 50 ? 12 : score <= 110 ? (110 - score) / 5 : 0;
    } else if (facility->type == TK_ZOO) {
      facility->age = NextEventOrientationRandom(city, 100);
      facility->value_02 = NextEventOrientationRandom(city, 100);
      facility->value_04 = NextEventOrientationRandom(city, 100);
      facility->value_06 = NextEventOrientationRandom(city, 100);
    } else if (facility->type == TK_STATUE) {
      facility->value_04 = Sc2kCrtRandRange(city, 42);
    } else if (facility->type == TK_SUBWAY_STATION) {
      facility->value_02 = city->individual_facilities ? 1 : statistics->count[TK_SUBWAY_STATION];
      facility->value_06 = city->infrastructure.counter_c;
    } else if (facility->type == TK_BUS_STATION) {
      facility->value_02 = (city->individual_facilities ? 4 : statistics->count[TK_BUS_STATION]) / 4;
      facility->value_04 = city->individual_facilities ? 4 : statistics->count[TK_BUS_STATION];
      facility->value_06 = city->infrastructure.counter_a;
    } else if (facility->type == TK_RAIL_STATION) {
      facility->value_02 = (city->individual_facilities ? 4 : statistics->count[TK_RAIL_STATION]) / 4;
      facility->value_06 = city->infrastructure.counter_b;
    } else if (facility->type == TK_MAYORS_HOUSE) {
      facility->value_04 = city->infrastructure.mayor_rating;
      if (facility->value_06 != 0) {
        --facility->value_06;
        ++facility->age;
      }
    } else if (facility->type == TK_WATER_TREATMENT_PLANT || facility->type == TK_DESALINATION_PLANT) {
      facility->age = city->infrastructure.water_percent + (Sc2kCrtRand(city) & 7);
      facility->value_02 = Sc2kCrtRandRange(city, 100);
      u32 value = (Sc2kCrtRand(city) & 31) + 135;
      u32 population = city->indicators.population / 50;
      facility->value_04 = population < value ? population : value;
    } else if (facility->type == TK_LIBRARY) {
      u32 rate = economy->sector[CITY_ECONOMY_SCHOOLS].current_rate;
      u32 requested = 4 * rate * (city->individual_facilities ? 4 : statistics->count[TK_LIBRARY]);
      facility->value_02 = ClampAnnualFacilityMetric(city, requested, 18);
      int count = (city->individual_facilities ? 4 : statistics->count[TK_LIBRARY]);
      int value = (int)((u32)count * (rate - 50) + facility->value_04);
      if (value > 0 && value < 32000)
        facility->value_04 = value;
      int population = (int)city->indicators.population;
      population = IntMax(population, 1);
      int age = (int)(300 * rate * (u32)count) / population;
      facility->age = IntMin(age, 12);
    } else if (facility->type == TK_MARINA) {
      int count = city->individual_facilities ? 9 : statistics->count[TK_MARINA];
      int requested = NextCityMapRandomModulo(city, 20) + 8 * count;
      facility->value_02 = ClampAnnualFacilityMetric(city, requested, 150);
    } else if (IsTileKindArcology(facility->type) && facility->type != TK_BRAUN_LLAMA_DOME) {
      if (facility->type == TK_LAUNCH_ARCOLOGY)
        ++work->arcology_count;
      int divisor = IntMax(work->average, 1);
      int requested = 1000 * facility->value_02 / 10;
      int change = ClampAnnualFacilityMetric(city, requested, 20 * divisor);
      int rate_delta = (int)(60 - economy->sector[CITY_ECONOMY_COMMERCIAL].current_rate -
                           economy->sector[CITY_ECONOMY_RESIDENTIAL].current_rate -
                           economy->sector[CITY_ECONOMY_INDUSTRIAL].current_rate) /
          6;
      int limit = 5 * (facility->age + rate_delta) - 50;
      int limit_change = 40 * limit;
      change = IntMin(change, limit_change);
      int value = facility->value_04 + facility->value_04 / 50 + change;
      value = IntMin(value, 1000 * facility->value_02);
      facility->value_04 = value + NextCityMapRandomModulo(city, 64);
      work->arcology_population += facility->value_04;
    } else if (facility->type == TK_BRAUN_LLAMA_DOME) {
      CityCountryTag *locale = &city->country;
      for (int index = 0; index < (int)sizeof(locale->character) && locale->character[index] != 0; ++index)
        if (locale->character[index] >= 'A' && locale->character[index] <= 'Z')
          locale->character[index] += 'a' - 'A';
      bool australian_locale = locale->character[0] == 'a' && locale->character[1] == 'u' &&
          locale->character[2] == 's' && locale->character[3] == 0;
      if (australian_locale) {
        facility->value_02 = (city->indicators.population >> 3) + (Sc2kCrtRand(city) & 1023);
        facility->age = Sc2kCrtRand(city) & 127;
        facility->value_04 = (Sc2kCrtRand(city) & 127) + 10;
      } else {
        facility->age = Sc2kCrtRand(city);
        u32 population = (city->indicators.population >> 3) + (Sc2kCrtRand(city) & 1023);
        facility->value_02 = population;
        facility->value_04 = population / 8 + (Sc2kCrtRand(city) & 127);
        facility->value_06 = population / 16 + (Sc2kCrtRand(city) & 63);
      }
    }
  }
  return true;
}

// Commit accumulated annual facility totals and determine any arcology-exodus compensation count. Work must be the
// completed matching scan; returns that captured count or -1, leaving presentation and deferred exodus to its caller.
int FinishCityAnnualFacilities(City *city, const CityAnnualFacilityWork *work) {
  CityTileCounts *statistics = &city->statistics;
  city->infrastructure.facility_total = work->value_sum;
  city->indicators.arcology_population = work->arcology_population;
  city->infrastructure.counter_a = 0;
  city->infrastructure.counter_b = 0;
  city->infrastructure.counter_c = 0;
  int original_ratio_divisor = statistics->count[TK_PRISON] / 16;
  city->infrastructure.facility_ratio =
      original_ratio_divisor > 0 && city->infrastructure.facility_ratio / original_ratio_divisor < 80;
  if (work->low_school_quality)
    InsertRankedCityEvent(city, CITY_NEWS_SCHOOL_FUNDING, 0);
  if (work->high_prison_load)
    InsertRankedCityEvent(city, CITY_NEWS_PRISON_OVERCROWDING, 0);
  int annual_metric = statistics->count[TK_LAUNCH_ARCOLOGY] / 16;
  return annual_metric > 300 && work->arcology_population > 6000000 ? work->arcology_count : -1;
}

// Runs after the first notice, against live tiles/cash with the caller's captured count. Count is captured before the
// first notice; applies compensation/removal once against live map and treasury after dismissal.
void CompleteCityArcologyExodus(City *city, int arcology_count) {
  for (int y = 0; y < city->map_height; ++y)
    for (int x = 0; x < city->map_width; ++x) {
      TileIndex tile = MapTileXY(city, x, y);
      if (ReadTile(city, tile) == TK_LAUNCH_ARCOLOGY) BulldozeCityTile(city, tile, true);
    }
  city->indicators.cash = AddInt64Clamped(city->indicators.cash, 100000 * (i64)arcology_count);
}

// Unlocks

static const CityUnlockTable kCityRebuiltToolAvailability = {
    .slot = {31, 3, 3, 3, 7, 0, 5, 5, 1, 3, 3, 3, 15, 15, 31, 0, 0, 0},
};

// Change the availability of a special facility. Facility must index the supported reward mask; updates availability
// and its toolbar presentation without placing a building.
void SetSpecialFacilityAvailability(City *city, int facility, i32 enabled) {
  if (city->cheats.debug_buildings) enabled = 1;
  if (facility == CITY_REWARD_ARCOLOGY_PICKER &&
      !city->cheats.debug_buildings && (city->unlocks.disaster_level < 6 || city->unlocks.arcology_count == 0))
    return;
  u32 bit = 1u << ((u32)facility & 31);
  u32 previous = city->unlocks.tools.shared.special_facility_mask;
  u32 next = enabled != 0 ? previous | bit : previous & ~bit;
  if (enabled == 0) {
    ResetCityRewardSelection(city);
    RebuildCityToolbar(city);
  }
  if ((previous == 0) != (next == 0))
    DisableCityToolbarItem(city, CITY_TOOL_GROUP_REWARDS);
  city->unlocks.tools.shared.special_facility_mask = next;
  RebuildCityToolbar(city);
}

// Rebuild unlock state and toolbar selection; callers discard the menu-refresh result. Requires current
// calendar/population/unlock inputs and attached UI; replaces derived tool availability without selecting a new player
// action.
void RebuildCityUnlockState(City *city) {
  CityUnlockTable *state = &city->unlocks.tools;
  const CityUnlockTable *defaults = &kCityRebuiltToolAvailability;
  CityMilestoneYears *milestones = &city->economic_tables.milestone_years;
  for (int group = 0; group < CITY_TOOL_GROUP_COUNT; ++group)
    if (group != CITY_TOOL_GROUP_REWARDS)
      state->slot[group] = defaults->slot[group];
  int mask = CITY_POWER_UNLOCK_INITIAL;
  int unlocked_groups = 0;
  for (int milestone = 0; milestone < CITY_MILESTONE_COUNT; ++milestone) {
    if (milestones->due_year[milestone] == 0) {
      switch (milestone) {
      case CITY_MILESTONE_HYDRO_POWER:
        mask |= CITY_POWER_UNLOCK_HYDRO;
        break;
      case CITY_MILESTONE_NUCLEAR_POWER:
        if ((city->unlocks.ordinance_flags & CITY_POLICY_FLAG_NUCLEAR_FREE_ZONE) == 0)
          mask |= CITY_POWER_UNLOCK_NUCLEAR;
        break;
      case CITY_MILESTONE_WIND_POWER:
        mask |= CITY_POWER_UNLOCK_WIND;
        break;
      case CITY_MILESTONE_SOLAR_POWER:
        mask |= CITY_POWER_UNLOCK_SOLAR;
        break;
      case CITY_MILESTONE_MICROWAVE_POWER:
        mask |= CITY_POWER_UNLOCK_MICROWAVE;
        break;
      case CITY_MILESTONE_FUSION_POWER:
        mask |= CITY_POWER_UNLOCK_FUSION;
        break;
      case CITY_MILESTONE_DISASTER_CHOICE:
        city->unlocks.tools.shared.policy_availability_flags |= CITY_AVAILABILITY_DISASTER_CHOICE;
        break;
      case CITY_MILESTONE_HIGHWAY_AND_TUNNEL:
        state->slot[CITY_TOOL_GROUP_ROAD] |= CITY_TOOL_UNLOCK_HIGHWAY_TUNNEL;
        break;
      case CITY_MILESTONE_BUS_DEPOT:
        state->slot[CITY_TOOL_GROUP_ROAD] |= CITY_TOOL_UNLOCK_BUS_DEPOT;
        break;
      case CITY_MILESTONE_SUBWAY_NETWORK:
        state->slot[CITY_TOOL_GROUP_RAIL] |= CITY_TOOL_UNLOCK_SUBWAY_NETWORK;
        break;
      case CITY_MILESTONE_WATER_TREATMENT:
        state->slot[CITY_TOOL_GROUP_WATER] |= CITY_TOOL_UNLOCK_WATER_TREATMENT;
        break;
      case CITY_MILESTONE_DESALINATION:
        state->slot[CITY_TOOL_GROUP_WATER] |= CITY_TOOL_UNLOCK_DESALINATION;
        break;
      case CITY_MILESTONE_PLYMOUTH_ARCOLOGY:
      case CITY_MILESTONE_FOREST_ARCOLOGY:
      case CITY_MILESTONE_DARCO_ARCOLOGY:
      case CITY_MILESTONE_LAUNCH_ARCOLOGY:
        ++unlocked_groups;
        break;
      default:
        break;
      }
    }
    city->unlocks.power_mask = mask;
    city->unlocks.arcology_count = unlocked_groups;
  }
  if (city->unlocks.military_base == CITY_MILITARY_BASE_ARMY ||
      city->unlocks.military_base == CITY_MILITARY_BASE_AIR_FORCE ||
      city->unlocks.military_base == CITY_MILITARY_BASE_NAVY)
    state->slot[CITY_TOOL_GROUP_DISPATCH] |= CITY_TOOL_UNLOCK_MILITARY_DISPATCH;
  if (city->cheats.debug_buildings) {
    city->unlocks.power_mask = CITY_POWER_UNLOCK_ALL;
    city->unlocks.arcology_count = 4;
    state->slot[CITY_TOOL_GROUP_ROAD] |= CITY_TOOL_UNLOCK_HIGHWAY_TUNNEL | CITY_TOOL_UNLOCK_BUS_DEPOT;
    state->slot[CITY_TOOL_GROUP_RAIL] |= CITY_TOOL_UNLOCK_SUBWAY_NETWORK;
    state->slot[CITY_TOOL_GROUP_WATER] |= CITY_TOOL_UNLOCK_WATER_TREATMENT | CITY_TOOL_UNLOCK_DESALINATION;
    state->shared.special_facility_mask = (1 << (CITY_REWARD_LAUNCH_ARCOLOGY + 1)) - 1;
  }
  SetSpecialFacilityAvailability(city, CITY_REWARD_ARCOLOGY_PICKER, 1);
  RebuildCityToolbar(city);
}
