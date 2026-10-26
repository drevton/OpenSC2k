#include "city/state.h"
#include "city/terrain/climate.h"

#include "city/maps/labels.h"
#include "city/engine/city_name.h"
#include "city/engine/simulation_state.h"
#include "city/disasters/types.h"
#include "city/infrastructure/types.h"
#include "city/maps/tile_queue.h"
#include "city/events/news.h"
#include "city/engine/random.h"
#include "city/economy/facilities.h"
#include "city/economy/types.h"
#include "city/events/events.h"
#include "city/maps/tile_storage.h"
#include "city/maps/map.h"

#include <stdlib.h>
#include <string.h>

#include "city/disasters/disasters.h"
#include "city/saveload/file.h"

// Compute the single backing allocation for the requested map, event pool, histories and working layers. Dimensions
// and capacity must be supported; the returned byte count includes required alignment but allocates nothing.
static size_t CityStorageSize(int width, int height, int event_capacity) {
  size_t cells = (size_t)width * height;
  size_t size = (sizeof(TileInfo) + sizeof(u16)) * cells + sizeof(CityHalfMapStatistics) * (cells / 4) +
      sizeof(CityQuarterMapStatistics) * (cells / 16) +
      sizeof(CityHistoryRow) * CITY_HISTORY_SERIES_COUNT;
  size += CityTileQueueCapacity(width, height) * sizeof(PointU16) + cells / 8;
  if (width != CITY_MAP_SIZE || height != CITY_MAP_SIZE) {
    int sources = IntMin(width * height / 2, CITY_WATER_SOURCE_SORT_LIMIT);
    size += sources * sizeof(u32);
  }
  size += event_capacity * sizeof(CityMapEvent) + CITY_EVENT_HASH_BUCKETS * sizeof(u16) +
      TerrainSnowNoiseSize(width, height);
  return (size + 3) & ~(size_t)3;
}

// Compute backing bytes for the map dimensions and their normal event-slot capacity. Requires supported map axes; use
// this size when supplying storage to simulation initialization.
size_t CityMapStorageSize(int width, int height) {
  return CityStorageSize(width, height, CityEventSlotCapacity(width, height));
}

// Rebind map, queue, event and climate pointers within City's existing backing allocation. Geometry and event capacity
// must match that allocation; no external aliases into a replaced allocation may be retained.
void BindOwnedSimulationMapLayers(City *city) {
  int width = city->map_width ? city->map_width : CITY_MAP_SIZE;
  int height = city->map_height ? city->map_height : CITY_MAP_SIZE;
  size_t cells = (size_t)width * height;
  u8 *storage = city->map_storage;
  int shift = 0;
  while ((1 << shift) < width) shift++;
  TileInfo *tiles = (TileInfo *)storage;
  u16 *scratch = (u16 *)(tiles + cells);
  CityHalfMapStatistics *half = (CityHalfMapStatistics *)(scratch + cells);
  CityQuarterMapStatistics *quarter = (CityQuarterMapStatistics *)(half + cells / 4);
  CityHistoryRow *history = (CityHistoryRow *)(quarter + cells / 16);
  u8 *route = (u8 *)(history + CITY_HISTORY_SERIES_COUNT);
  bool large = width != CITY_MAP_SIZE || height != CITY_MAP_SIZE;
  int route_capacity = CityTileQueueCapacity(width, height);
  city->route_queue_cursor.mask = route_capacity - 1;
  size_t route_bytes = route_capacity * sizeof(PointU16);
  city->tile = tiles;
  city->scratch = scratch;
  city->half = half;
  city->quarter = quarter;
  city->history = history;
  city->map_width = width;
  city->map_height = height;
  city->map_cell_count = cells;
  city->map_width_shift = shift;
  city->route_points = (PointU16 *)route;
  city->route_queued = route + route_bytes;
  city->water_source_order = large ? (u32 *)(route + route_bytes + cells / 8) : NULL;
  if (!city->events.capacity) city->events.capacity = CityEventSlotCapacity(width, height);
  size_t bytes = CityStorageSize(width, height, city->events.capacity);
  city->snow.points = storage + ((bytes - TerrainSnowNoiseSize(width, height)) & ~(size_t)3);
  city->events.buckets = (u16 *)(city->snow.points - CITY_EVENT_HASH_BUCKETS * sizeof(u16));
  city->events.event = (CityMapEvent *)((u8 *)city->events.buckets - city->events.capacity * sizeof(CityMapEvent));
  city->snow.moisture = city->snow.points + (TerrainSnowNoiseSize(width, height) >> 1);
}

// Reset landscape generation metadata, independent RNG/cursors and incremental simulation work. Requires initialized
// City storage; does not allocate or preserve an in-progress map-processing pass.
static void ResetCityLandscapeState(City *city) {
  city->native_map_storage = true;
  city->snow.line = CITY_SNOW_LINE_DISABLED;
  city->terrain_climate = CITY_TERRAIN_SIMCITY;
  city->desert_limit = 0;
  city->snow.seed = city->snow.variation = city->snow.stride = 0;
  city->landscape_cursor = city->landscape_random = 1;
  city->landscape_tick = 0;
  city->tree_lifecycle = false;
  city->protect_terrain_structures = false;
  city->tree_tick_counter = 0;
  city->simulation_work = (CitySimulationWork){0};
}

// Replace the City's map backing for supported dimensions, retaining/reindexing its event records. Returns false
// before replacement on invalid dimensions/allocation failure; success resets map layers and invalidates all prior
// backing pointers.
bool ResizeCityMap(City *city, int width, int height) {
  if (!IsSupportedMapAxis(width) || !IsSupportedMapAxis(height)) return false;
  if (city->map_width == width && city->map_height == height) return true;
  int capacity = IntMax(city->events.capacity, CityEventSlotCapacity(width, height));
  u8 *storage = calloc(1, CityStorageSize(width, height, capacity));
  if (storage == NULL) return false;
  ReleaseCityAssociations(city);
  ReindexCityMapEvents(city, width, height);
  CityMapEvent *old_events = city->events.event;
  u8 *old_storage = city->map_storage;
  int old_capacity = city->events.capacity;
  city->events.capacity = capacity;
  city->map_storage = storage;
  city->map_width = width;
  city->map_height = height;
  BindOwnedSimulationMapLayers(city);
  memcpy(city->events.event, old_events, old_capacity * sizeof(CityMapEvent));
  free(old_storage);
  RebuildCityEventHash(city);
  city->route_queue_cursor.read = city->route_queue_cursor.write = 0;
  ResetCityLandscapeState(city);
  ClearSimulationMapChanges(&city->map_changes);
  InvalidateMap(city);
  return true;
}

// Grow the event pool inside City's shared backing allocation when required. Returns false for excess capacity or
// allocation failure; success preserves existing events/layers but can move every pointer into that backing.
bool ReserveCityEventSlots(City *city, int capacity) {
  if (capacity <= city->events.capacity) return true;
  if (capacity > CITY_EVENT_MAX_SLOTS) return false;
  int width = city->map_width, height = city->map_height;
  size_t snow_offset = city->snow.points - city->map_storage;
  size_t snow_bytes = TerrainSnowNoiseSize(width, height);
  size_t event_offset = (u8 *)city->events.event - city->map_storage;
  size_t event_bytes = city->events.capacity * sizeof(CityMapEvent);
  u8 *storage = realloc(city->map_storage, CityStorageSize(width, height, capacity));
  if (!storage) return false;
  size_t new_snow_offset = snow_offset + (capacity - city->events.capacity) * sizeof(CityMapEvent);
  memmove(storage + new_snow_offset, storage + snow_offset, snow_bytes);
  memset(storage + event_offset + event_bytes, 0,
      (capacity - city->events.capacity) * sizeof(CityMapEvent) + CITY_EVENT_HASH_BUCKETS * sizeof(u16));
  city->map_storage = storage;
  city->events.capacity = capacity;
  BindOwnedSimulationMapLayers(city);
  RebuildCityEventHash(city);
  return true;
}

// Raw executable defaults differ from the availability rebuilt after loading a city.
const CityUnlockTable kCityInitialToolAvailability = {
  .slot = {31, 3, 3, 3, 31, 0, 31, 31, 3, 3, 3, 3, 15, 15, 31, 0, 0, 0},
};

// Reset deterministic City fields and initial unlocks while retaining map storage bindings. Intended for
// initialization, not a running City's rollback; owners and buffer lifetimes remain the caller's responsibility.
void InitializeCityDefaults(City *city) {
  ResetCityLandscapeState(city);
  city->scheduler_frame = 0;
  city->clock = (SimulationClock){0};
  city->random = (SimulationRandomState){0};
  city->cadence_counter = (TimerCadenceCounter){0};
  city->indicators = (CityEconomicIndicators){0};
  city->economic_tables = (CityEconomicTables){0};
  city->unlocks = (CityUnlockState){.tools = kCityInitialToolAvailability, .power_mask = CITY_POWER_UNLOCK_ALL};
  city->weather = (CityWeatherState){0};
  city->disasters = (CityDisasterState){.periods = kCityDefaultDisasterPeriods, .event_data = -1};
  city->event_runtime = (CityEventRuntime){0};
  city->geography = (CityGeographyState){0};
  city->infrastructure = (CityInfrastructureState){0};
  city->mission = (CityMissionState){0};
  city->settings = (CitySavedSettings){.speed = CITY_SPEED_NORMAL};
  city->newspaper = (CityNewspaperState){0};
  city->route_queue_cursor = (CityTileQueueCursor){.mask =
      CityTileQueueCapacity(city->map_width ? city->map_width : CITY_MAP_SIZE,
          city->map_height ? city->map_height : CITY_MAP_SIZE) - 1};
}

// Zero and initialize a City using caller-supplied stock-size backing storage. Backing must have CityMapStorageSize
// bytes and suitable alignment; City takes its storage binding and enables initial full-map damage.
void InitializeSimulation(City *city, void *map_storage) {
  memset(city, 0, sizeof(*city));
  memset(map_storage, 0, CityMapStorageSize(CITY_MAP_SIZE, CITY_MAP_SIZE));
  city->map_storage = map_storage;
  city->individual_facilities = true;
  InitializeCityDefaults(city);
  BindOwnedSimulationMapLayers(city);
  city->unlock_bonus_answer = -1;
  city->map_changes.enabled = true;
  city->map_changes.full = true;
}

// Final owned storage release. Callers have already stopped work and released external map aliases.
void ReleaseSimulation(City *city) {
  FreeCityNameStorage(&city->name);
  ReleaseCityFileExtensions(city);
  ReleaseCityAssociations(city);
  free(city->map_storage);
  city->map_storage = NULL;
  city->tile = NULL;
  city->scratch = NULL;
  city->half = NULL;
  city->quarter = NULL;
  city->history = NULL;
  city->route_points = NULL;
  city->route_queued = NULL;
  city->water_source_order = NULL;
  city->events = (CityMapEvents){0};
  city->snow.points = city->snow.moisture = NULL;
  city->route_queue_cursor = (CityTileQueueCursor){0};
  city->map_width = city->map_height = city->map_width_shift = city->map_cell_count = 0;
}

// Initialization effects, using the City's existing backing. Requires bound history storage; replaces initial
// samples/maxima without allocating or retaining transport pointers.
static void InitializeCityHistoryStorage(City *city) {
  memset(city->history, 0, sizeof(CityHistoryRow) * CITY_HISTORY_SERIES_COUNT);
  for (int series = 0; series < CITY_HISTORY_SERIES_COUNT; ++series)
    city->history_maxima.value[series] = 1;
  city->history_maxima.value[CITY_HISTORY_POWER_PERCENT] = 100;
  city->history_maxima.value[CITY_HISTORY_WATER_PERCENT] = 100;
  city->history_maxima.value[CITY_HISTORY_EDUCATION] = 150;
  city->history_maxima.value[CITY_HISTORY_HEALTH] = 90;
  city->indicators.history_initialization_state = 0;
}

// Initialization effects. All buffers already belong to City; no heap or transport needed. Requires existing City
// backing and initialized owner bindings; clears per-City records without replacing their allocation.
void InitializeCityStorage(City *city) {
  memset(&city->cheats, 0, sizeof(city->cheats));
  ResetCityLandscapeState(city);
  ReleaseCityFileExtensions(city);
  memset(city->map_storage, 0, CityStorageSize(city->map_width, city->map_height, city->events.capacity));
  BindOwnedSimulationMapLayers(city);
  InitializeCityHistoryStorage(city);
  city->overlay_names = (CityOverlayNames){0};
  ReleaseCityAssociations(city);
  city->economy = (CityEconomyMonths){0};
  city->budget_transit_clamp_rate = 0;
  city->population = (CityPopulationSectors){0};
  city->industry_sectors = (CityIndustrySectors){0};
  city->account_names = (CityAccountNameIndices){0};
  city->secondary_accounts = (CityAccountQuartet){0};
  city->primary_accounts = (CityAccountQuartet){0};
  city->fourth_accounts = (CityAccountQuartet){0};
  city->age_cohorts = (CityAgeCohorts){0};
  city->age_metric_a = (CityAgeCohorts){0};
  city->age_metric_b = (CityAgeCohorts){0};
  city->industry_demand = (CityIndustryDemand){0};
  city->industry_baseline = (CityIndustryDemand){0};
  city->industry_accounts = (CityIndustryAccounts){0};
  city->statistics = (CityStatistics){0};
  city->special_tile_counts = (CitySpecialTileCounts){0};
}
