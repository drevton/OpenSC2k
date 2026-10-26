#ifndef SC2K_SIMULATION_STATE_H
#define SC2K_SIMULATION_STATE_H

#include "city/disasters/types.h"
#include "city/economy/state.h"
#include "city/engine/city_types.h"
#include "city/engine/input.h"
#include "city/engine/simulation_state.h"
#include "city/events/types.h"
#include "city/events/news_types.h"
#include "city/fixes.h"
#include "city/infrastructure/types.h"
#include "city/maps/map_types.h"
#include "city/terrain/types.h"
#include <assert.h>

// Native simulation ownership, independent of the original executable's layout.
// Pending command contexts are not serialized by the SC2 save format.
typedef struct Game Game;
typedef struct Drawing Drawing;
typedef struct CityFileExtensions CityFileExtensions;

typedef struct City {
  // Map layers share map_storage; tile holds the full-resolution surface and underground fields.
  TileInfo *tile;
  CityHalfMapStatistics *half;
  CityQuarterMapStatistics *quarter;
  u16 *scratch;
  Game *game;
  // Borrowed presentation context, owned by Game. Not saved or part of deterministic state.
  Drawing *drawing;
  u32 map_cell_count;
  u16 map_width, map_height;
  u8 map_width_shift;
  // Keep hot scalars and pointers before array payloads. Natural alignment and
  // a sub-4KiB prefix permit ARM64 base+immediate loads, including byte fields.
  // Do not pack this native type or preserve the original global addresses.
  u64 scheduler_frame;
  u64 fixes;
  // Import/export marker; gameplay always uses native records.
  bool native_map_storage;
  // Optional restored-cheat state: 0 fresh, 1 warned, 2 church growth enabled.
  // debug_buildings is launch-only and is not saved.
  struct {
    u8 church, used;
    bool debug_buildings;
    TileIndex house; // tile + 1; zero means absent.
  } cheats;
  TimerCadenceCounter cadence_counter;
  SimulationClock clock;
  SimulationRandomState random;
  CityEconomicIndicators indicators;
  CityUnlockState unlocks;
  CityWeatherState weather;
  CityDisasterState disasters;
  CityEventRuntime event_runtime;
  CityGeographyState geography;
  SimulationMapChanges map_changes;
  u32 landscape_cursor, landscape_tick, landscape_random;
  bool tree_lifecycle;
  bool protect_terrain_structures;
  u8 tree_tick_counter;
  u8 terrain_climate;
  u16 desert_limit;
  CityCountryTag country;
  CityInfrastructureState infrastructure;
  CityMissionState mission;
  CitySavedSettings settings;
  CityNewspaperState newspaper;
  CityTileQueueCursor route_queue_cursor;
  u8 question_count;           // 0..CITY_PENDING_QUESTION_CAPACITY
  i8 unlock_bonus_answer;      // -1 pending, dialog choice otherwise
  u8 command_name_answer;      // CITY_DIALOG_OK/CANCEL
  u8 command_name_active;
  CityConstructionState construction;
  CityConstructionChoices command_choices; // Transient; nested commands restore the parent values.
  CityNameCommandInput command_name;
  CityMapEvents events;
  CityQuestion questions[CITY_PENDING_QUESTION_CAPACITY];
  CityStatistics statistics;
  CitySpecialTileCounts special_tile_counts;
  CityPopulationSectors population;
  CityBudgetState budget;
  CityOverlayNames overlay_names;
  CitySigns signs;
  bool individual_facilities;
  CityAnnualFacilities annual_facilities;
  CityEconomyMonths economy;
  CityAgeCohorts age_cohorts;
  CityAgeCohorts age_metric_a;
  CityAgeCohorts age_metric_b;
  CityHistoryMaxima history_maxima;
  CityIndustrySectors industry_sectors;
  CityAccountQuartet primary_accounts;
  CityAccountQuartet secondary_accounts;
  CityAccountQuartet fourth_accounts;
  CityAccountNameIndices account_names;
  CityIndustryAccounts industry_accounts;
  CityIndustryDemand industry_demand;
  CityIndustryDemand industry_baseline;
  CityNewspaperEditions newspaper_editions;
  CityEconomicTables economic_tables;
  CityNewsRecords news_records;
  // Defined compatibility observation of the stock one-past-sector clamp.
  // It is independent of the real sixteen-sector array and never indexes it.
  i32 budget_transit_clamp_rate;
  CityName name;
  // Owned SC2K metadata and unknown archive entries; no host file identity.
  CityFileExtensions *file_extensions;
  u32 load_sloped_zone_tiles; // Last stock import only; unsupported sloped zoning is removed.
  u32 load_submerged_occupant_tiles; // Last stock import only; not saved.
  u32 load_cleared_building_tiles; // Last load only; not saved or used by simulation.
  CityHistoryRow *history;
  PointU16 *route_points;
  u8 *route_queued;
  u32 *water_source_order;
  u8 *map_storage; // Contiguous backing; offsets depend on the map dimensions.
  CitySnowClimate snow;
  CitySimulationWork simulation_work;
  CityDisasterWork disaster_work;
  // Deterministic callers waiting at simulation dialog boundaries; never saved in SC2.
  CityTickContinuation *tick_continuations;
  int next_tick_continuation;
} City;

static SC2K_FORCE_INLINE bool IsMapCoordinateValid(const City *city, int x, int y) {
  return x >= 0 && x < city->map_width && y >= 0 && y < city->map_height;
}

// Coordinates must already be valid. Check external input and optional
// neighbors before conversion; this helper never returns INVALID_TILE.
static SC2K_FORCE_INLINE TileIndex MapTileXY(const City *city, int x, int y) {
  assert(IsMapCoordinateValid(city, x, y));
  return y * city->map_width + x;
}

// Optional coordinates, including neighbours outside the map.
static SC2K_FORCE_INLINE TileIndex MapTileXYOrInvalid(const City *city, int x, int y) {
  return IsMapCoordinateValid(city, x, y) ? MapTileXY(city, x, y) : INVALID_TILE;
}

// Bound maps use power-of-two row widths: the mask selects the column and the shift selects the row.
static SC2K_FORCE_INLINE int MapTileX(const City *city, TileIndex tile) { return tile & (city->map_width - 1); }
static SC2K_FORCE_INLINE int MapTileY(const City *city, TileIndex tile) { return tile >> city->map_width_shift; }

static SC2K_FORCE_INLINE CityPoint MapTilePt(const City *city, TileIndex tile) {
  return (CityPoint){MapTileX(city, tile), MapTileY(city, tile)};
}

// Offsets from a valid tile, without wrapping at map edges.
static SC2K_FORCE_INLINE TileIndex GetRelativeTileOrInvalid(const City *city, TileIndex tile, int x, int y) {
  CityPoint point = MapTilePt(city, tile);
  return MapTileXYOrInvalid(city, point.x + x, point.y + y);
}

// The source tile must be valid. Direct index offsets avoid decoding coordinates for each neighbor.
static SC2K_FORCE_INLINE TileNeighbors GetTileNeighbors(const City *city, TileIndex tile) {
  assert(tile < city->map_cell_count);
  u32 width = city->map_width, x = tile & (width - 1);
  TileNeighbors result;
  result.count = 0;
  if (tile >= width) result.tiles[result.count++] = tile - width;
  if (x != 0) result.tiles[result.count++] = tile - 1;
  if (tile < city->map_cell_count - width) result.tiles[result.count++] = tile + width;
  if (x + 1 < width) result.tiles[result.count++] = tile + 1;
  return result;
}

// Normalize valid endpoint tiles to half-open map-coordinate bounds.
static SC2K_FORCE_INLINE CityRect MapTileRectangle(const City *city, TileIndex start, TileIndex end) {
  CityPoint first = MapTilePt(city, start), last = MapTilePt(city, end);
  return (CityRect){IntMin(first.x, last.x), IntMin(first.y, last.y),
      IntMax(first.x, last.x) + 1, IntMax(first.y, last.y) + 1};
}
static SC2K_FORCE_INLINE bool IsMapTileValid(const City *city, TileIndex tile) { return tile < (u32)city->map_cell_count; }

_Static_assert(offsetof(City, tile) == 0, "map layer pointers start the simulation");
_Static_assert(offsetof(City, game) < 64, "hot subsystem link stays in the first cache line");
_Static_assert(offsetof(City, drawing) < 64, "borrowed drawing link stays in the first cache line");
_Static_assert(offsetof(City, statistics) + sizeof(CityStatistics) < 4096,
    "frequently updated tile totals precede cold tables");
_Static_assert(offsetof(City, questions) < 4096, "ARM64 hot state fits byte-load immediate range");
_Static_assert(
    offsetof(City, events) + sizeof(CityMapEvents) <= 4096, "event records fit ARM64 byte-load immediate range");
_Static_assert(offsetof(City, indicators) + sizeof(CityEconomicIndicators) <= 4096,
    "economic indicators fit ARM64 immediate addressing");
_Static_assert(
    offsetof(City, unlocks) + sizeof(CityUnlockState) <= 4096, "unlock state fits ARM64 immediate addressing");
_Static_assert(
    offsetof(City, weather) + sizeof(CityWeatherState) <= 4096, "weather state fits ARM64 byte-load immediate range");
_Static_assert(
    offsetof(City, disasters) + sizeof(CityDisasterState) <= 4096, "disaster state fits ARM64 immediate addressing");
_Static_assert(
    offsetof(City, event_runtime) + sizeof(CityEventRuntime) <= 4096, "event runtime fits ARM64 immediate addressing");
_Static_assert(
    offsetof(City, geography) + sizeof(CityGeographyState) <= 4096, "geography fits ARM64 immediate addressing");
_Static_assert(offsetof(City, infrastructure) + sizeof(CityInfrastructureState) <= 4096,
    "infrastructure state fits ARM64 immediate addressing");
_Static_assert(
    offsetof(City, mission) + sizeof(CityMissionState) <= 4096, "mission state fits ARM64 immediate addressing");
_Static_assert(offsetof(City, newspaper) + sizeof(CityNewspaperState) <= 4096,
    "settings and newspaper state fit ARM64 immediate addressing");
_Static_assert(offsetof(City, route_queue_cursor) + sizeof(CityTileQueueCursor) <= 4096,
    "route cursors fit ARM64 immediate addressing");

// Initial backing must be suitably aligned and hold CityMapStorageSize(CITY_MAP_SIZE, CITY_MAP_SIZE) bytes.
// Borrowed fixtures may bind live storage; replacing, growing or releasing backing requires heap ownership.
void InitializeSimulation(City *city, void *map_storage);
// Final release of owned name, archive, associations and map backing. Stop simulation, release tick
// continuations and drop external map aliases first. Reinitialize before reuse; City and Game stay caller-owned.
void ReleaseSimulation(City *city);
bool ResizeCityMap(City *city, int width, int height);
bool ReserveCityEventSlots(City *city, int capacity);
void InitializeCityDefaults(City *city);
void InitializeCityStorage(City *city);
void BindOwnedSimulationMapLayers(City *city);

#endif
