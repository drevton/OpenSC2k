#ifndef SC2K_DOMAIN_SRC_CITY_INFRASTRUCTURE_INFRASTRUCTURE_H
#define SC2K_DOMAIN_SRC_CITY_INFRASTRUCTURE_INFRASTRUCTURE_H

#include "city/infrastructure/types.h"
#include "city/saveload/terrain_format.h"
#include "city/maps/tile_storage.h"
#include "city/terrain/terrain.h"
#include "city/engine/input.h"
#include "city/maps/map.h"
#include "city/types.h"
#include "city/engine/command_types.h"

// Read-only supply for a valid water-pump tile, in simulation units.
int CityWaterPumpSupply(const City *city, TileIndex tile);

// State

typedef struct {
  u32 power_total;
  u32 power_percent;
  u32 water_total;
  u32 water_percent;
  union {
    u32 capacity_word;
    struct {
      u16 capacity;
      u16 capacity_upper;
    };
  };
  u32 counter_a;
  u32 counter_b;
  u32 counter_c;
  u32 subway_tiles;
  u32 facility_total;
  i16 facility_ratio;
  i16 mayor_rating;
  u32 service_bonus_a;
  u32 service_bonus_b;
  u16 stadium_team_mask;
} CityInfrastructureState;

// Route Queue

enum { CITY_ROUTE_QUEUE_CAPACITY = 512, CITY_ROUTE_QUEUE_MASK = CITY_ROUTE_QUEUE_CAPACITY - 1 };

// Shared coordinate queue for power, water, traffic and terrain traversal.
// The original discards the oldest entry when its ring becomes full.
// Supported map dimensions are powers of two, so the scaled capacity is too.
static inline int CityRouteQueueCapacity(int width, int height) {
  return CITY_ROUTE_QUEUE_CAPACITY * IntMax(width, height) / CITY_MAP_MIN_SIZE;
}

typedef struct {
  u16 read, write, mask;
} RouteQueueCursor;

void PushRouteCoordinate(City *city, int x, int y);
void ResetRouteQueueAndPush(City *city, int x, int y);
PointU16 PowerDequeue(City *city);
PointU16 PopRouteCoordinate(City *city);
int RouteQueueLength(City *city);

// Tables

// Immutable simulation data, available without loading an executable image.
extern const u8 kLineTerrainOverrideBySlope[16];
extern const u8 kLineTileOffsetByConnectionMask[16];
extern const u16 kLineTileOffsetByTerrain[CITY_TERRAIN_BANK_48];
extern const u8 kLineHorizontalConnectionAllowed[256];
extern const u8 kLineVerticalConnectionAllowed[256];
enum {
  CITY_BRIDGE_TERRAIN_VALID_COUNT = CITY_TERRAIN_BANK_48 + CITY_REGRADE_SHAPE_21 + 1,
  CITY_BRIDGE_TERRAIN_TOO_STEEP = 256
};
extern const u16 kBridgeTunnelTerrainClass[CITY_BRIDGE_TERRAIN_VALID_COUNT];
extern const i8 kLineYStep[4];
extern const i8 kLineXStep[4];
extern const u16 kBridgeSelectionCostPerCell[CITY_SPAN_STYLE_COUNT];
extern const u16 kSpanCostPerCell[CITY_SPAN_STYLE_COUNT];
extern const char *const kCitySpanNames[CITY_SPAN_STYLE_COUNT];
extern const i8 kSpanYStep[4];
extern const i8 kSpanXStep[4];
extern const u8 kLineTerrainElevationDelta[16];
extern const u16 kInfrastructureTerrainByDirection[64];
extern const u8 kLineTerrainDirectionAllowed[64];
extern const u8 kTransportTerrainClass[256];
extern const u8 kTransportSpanNorthClass[18];
extern const u8 kTransportSpanSouthClass[18];
extern const u8 kTransportSpanDirectionByMask[16];
extern const u8 kTransportSpanDirectionByComplement[16];

// Lines

typedef enum CityInfrastructureKind {
  CITY_INFRASTRUCTURE_ROAD, CITY_INFRASTRUCTURE_RAIL, CITY_INFRASTRUCTURE_POWER,
  CITY_INFRASTRUCTURE_PIPE, CITY_INFRASTRUCTURE_SUBWAY, CITY_INFRASTRUCTURE_HIGHWAY,
  CITY_INFRASTRUCTURE_COUNT,
} CityInfrastructureKind;
typedef struct { u8 stage, unit_cost; } CityInfrastructureDefinition;
extern const CityInfrastructureDefinition kCityInfrastructure[CITY_INFRASTRUCTURE_COUNT];
CityCommandCost GetCityInfrastructureCost(const City *city, TileIndex start, TileIndex end,
    CityInfrastructureKind kind, int span_styles, CityInfrastructureSpan *span);

// Geometry is read-only. Traversal writes only the supplied state/cursors; execution may pass City.construction.
// Quotes supply a separate state so they leave City working state unchanged. Neither path consumes RNG.
int CityLineDirection(int x, int y, int end_x, int end_y);
int CityLineTerrainDirection(int x, int y, int end_x, int end_y);
bool PrepareCityLine(const City *city, CityConstructionState *state, int x, int y, int end_x, int end_y);
bool AdvanceCityLine(const City *city, CityConstructionState *state, int *x, int *y);

bool CityLineCellAlreadyBuilt(const City *city, TileIndex tile, int stage);
// Retained command preview between valid endpoints. Prepare the live construction line, count new cells/surcharges
// and invalidate visited tiles. On failure retain PrepareCityLine's writes and leave prior counts intact; no RNG.
bool PrepareCityZonePreview(City *city, TileIndex start, TileIndex end);

i64 CalculateCityLineCost(const City *city, TileIndex start, TileIndex end, CityConstructionState *state, int unit_cost,
    bool highway);
// Resolve explicit endpoints into City.construction using its prepared stage. No pointer input, payment or RNG.
// Failure returns INVALID_TILE and retains planner writes; success installs the calculated cost and reached endpoint.
TileIndex PrepareCityLineEndpoint(City *city, TileIndex start, TileIndex end, int unit_cost, bool highway);
typedef enum CityLinePayment {
  CITY_LINE_PAYMENT_REUSED = 1, CITY_LINE_PAYMENT_PAID,
} CityLinePayment;
i64 PayCityLineCost(City *city, bool allow_reuse);
// Resolved command choices use the planner directly; retained callers without endpoints collect their drag first.
// Placement performs the existing payment, cell writes and optional span/connection continuation.
i64 PlaceCityWaterPipe(City *city, TileIndex origin_tile);
i64 PlaceCitySubway(City *city, TileIndex origin_tile);
i64 PlaceCityRoad(City *city, TileIndex origin_tile);
i64 PlaceCityRail(City *city, TileIndex origin_tile);
i64 PlaceCityPowerLine(City *city, TileIndex origin_tile);

// Networks

// Requires a valid tile. Invalidates the old artwork, updates subway statistics outside military zones, then writes.
// The caller reconnects neighbors and invalidates the final artwork after all related fields are installed.
void SetUndergroundTile(City *city, TileIndex tile, int kind);
void PlaceCityWaterPipeCell(City *city, TileIndex origin_tile);
void PlaceCitySubwayCell(City *city, TileIndex origin_tile);

void RefreshAdjacentSurfaceConnections(City *city, TileIndex tile);
void RefreshAdjacentUndergroundConnections(City *city, TileIndex tile);
void PrepareInfrastructureTerrainCell(City *city, TileIndex tile_index);

// Power

void ScanPowerGridPhase(City *city);

// Spans


bool IsCityInfrastructureSpanWater(const City *city, TileIndex tile);
CityInfrastructureSpan FindCityInfrastructureSpan(const City *city, TileIndex origin, int direction, bool choose_direction);

TileIndex FindCityBorderConnection(const City *city, TileIndex start, TileIndex end, int direction, bool highway);
int GetCityLineConnectionCost(const City *city, TileIndex start, TileIndex end, CityInfrastructureKind kind);
int CityInfrastructureSpanStyles(CityInfrastructureSpan span);
i64 CityInfrastructureSpanCost(CityInfrastructureSpan span, int styles);
i64 PlaceInfrastructureSpan(City *city, TileIndex origin_tile, int styles, int choose_direction);

// Inspect immediately after successful highway placement.
i64 PlaceRoadSpan(City *city, TileIndex origin_tile, int choose_direction);
i64 PlaceRailSpan(City *city, TileIndex origin_tile, int choose_direction);
i64 PlacePowerLineSpan(City *city, TileIndex origin_tile, int choose_direction);

// Transport

// Decay one valid 128x128 map section's half-resolution traffic cells. Returns their sum; the scheduler owns
// invalidation and city-wide accumulation. Whole-map decay performs both in the same section order.
u32 DecayTrafficDensitySection(City *city, int section);
void DecayTrafficDensity(City *city);

int FindTransportRouteAndApplyTraffic(City *city, int x, int y, int source_zone, int size, int maximum_cost);
void ScanCityWaterComponent(City *city, int source_x, int source_y);

// Compatibility result: bits0..3 use line-direction order. The high
// sentinels are not enabled span-style bits.
#define CITY_SPAN_COMPATIBILITY_INVALID 0x8000u
#define CITY_SPAN_COMPATIBILITY_SPECIAL_TERRAIN 0x4000u
#define CITY_SPAN_COMPATIBILITY_ALL_DIRECTIONS 0x000Fu

enum {
  CITY_BORDER_CONNECTION_ROAD = 0,
  CITY_BORDER_CONNECTION_RAIL = 1,
  CITY_BORDER_CONNECTION_HIGHWAY_OR_TUNNEL = 2,
};
#define CITY_SPAN_STYLE_HIGHWAY_MASK 0x20u
#define CITY_SPAN_STYLE_HIGHWAY_BOTH_MASK 0x60u

#endif
