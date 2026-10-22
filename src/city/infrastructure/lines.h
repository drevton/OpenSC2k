#ifndef SC2K_CITY_INFRASTRUCTURE_LINES_H
#define SC2K_CITY_INFRASTRUCTURE_LINES_H

#include "city/engine/command_types.h"
#include "city/engine/input.h"
#include "city/infrastructure/types.h"

// Immutable simulation data, available without loading an executable image.
extern const u8 kLineTerrainOverrideBySlope[16];
extern const u8 kLineTileOffsetByConnectionMask[16];
// Retained reconnectors index stock terrain codes; the data definition owns their supported range.
extern const u16 kLineTileOffsetByTerrain[];
extern const u8 kLineHorizontalConnectionAllowed[256];
extern const u8 kLineVerticalConnectionAllowed[256];
// Lines and spans use CityInfrastructureDirection; the same vectors apply to both.
extern const i8 kLineYStep[CITY_LINE_DIRECTION_COUNT];
extern const i8 kLineXStep[CITY_LINE_DIRECTION_COUNT];
extern const u8 kLineTerrainElevationDelta[16];
extern const u16 kInfrastructureTerrainByDirection[64];
extern const u8 kLineTerrainDirectionAllowed[64];

typedef struct { u8 stage, unit_cost; } CityInfrastructureDefinition;
extern const CityInfrastructureDefinition kCityInfrastructure[CITY_INFRASTRUCTURE_COUNT];
// Quote and execution setup share this pure option decoder. Kind must be a valid infrastructure family.
CityConstructionChoices ReadCityLineCommandChoices(CityInfrastructureKind kind, TileIndex end, int options);
// Requires admitted endpoints/options. Family placement owns payment; diagnostic observers alone may collect input.
i64 PlaceCityInfrastructureLine(City *city, CityInfrastructureKind kind, TileIndex tile, TileIndex end);
// Quote a resolved line without changing City or RNG. span is an optional output for the adjoining crossing.
CityCommandCost GetCityInfrastructureCost(const City *city, TileIndex start, TileIndex end,
    CityInfrastructureKind kind, int span_styles, CityInfrastructureSpan *span);

TileIndex FindCityBorderConnection(const City *city, TileIndex start, TileIndex end, int direction, bool highway);
int GetCityLineConnectionCost(const City *city, TileIndex start, TileIndex end, CityInfrastructureKind kind);

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
// Pay the live construction charge once. Negative errors leave cash unchanged; zero-cost reuse has its own result.
i64 PayCityLineCost(City *city, bool allow_reuse);
// Resolved command choices use the planner directly; retained callers without endpoints collect their drag first.
// Placement performs the existing payment, cell writes and optional span/connection continuation.
i64 PlaceCityWaterPipe(City *city, TileIndex origin_tile);
i64 PlaceCitySubway(City *city, TileIndex origin_tile);
i64 PlaceCityRoad(City *city, TileIndex origin_tile);
i64 PlaceCityRail(City *city, TileIndex origin_tile);
i64 PlaceCityPowerLine(City *city, TileIndex origin_tile);

// Shared command admission is read-only in TEST mode; execution retains the family placement/payment order.
i64 CmdPlaceInfrastructureLine(City *city, u32 flags, TileIndex start,
    CityInfrastructureKind kind, TileIndex end, int options, int reserved);

#endif
