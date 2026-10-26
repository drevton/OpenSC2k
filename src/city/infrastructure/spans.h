#ifndef SC2K_CITY_INFRASTRUCTURE_SPANS_H
#define SC2K_CITY_INFRASTRUCTURE_SPANS_H

#include "city/infrastructure/types.h"

// Span prices are shared by construction quotes, execution and the UI selector.
extern const u16 kSpanCostPerCell[CITY_SPAN_STYLE_COUNT];

// Read-only crossing eligibility at a valid tile; respects the native-landscape rule without reserving a route.
bool IsCityInfrastructureSpanWater(const City *city, TileIndex tile);
// Read-only shoreline search. A supplied direction uses CityInfrastructureDirection; choose_direction instead tries
// the map's default, opposite, next and previous directions. Invalid/unsupported origins return end == INVALID_TILE,
// with an optional notice code in error. Native spans include dry banks; legacy spans end at the last water code.
CityInfrastructureSpan FindCityInfrastructureSpan(const City *city, TileIndex origin,
    int direction, bool choose_direction);

// Use an explicit style mask when present, otherwise the legacy length-based choices. No style is selected here.
int CityInfrastructureSpanStyles(CityInfrastructureSpan span);
// Cheapest matching style charge, or -1 for an invalid end/no matching style. Does not check affordability or pay.
i64 CityInfrastructureSpanCost(CityInfrastructureSpan span, int styles);
i64 PlaceInfrastructureSpan(City *city, TileIndex origin_tile, int styles, int choose_direction);

// Family-specific style masks preserve the signed placement result and retained continuation.
i64 PlaceRoadSpan(City *city, TileIndex origin_tile, int choose_direction);
i64 PlaceRailSpan(City *city, TileIndex origin_tile, int choose_direction);
i64 PlacePowerLineSpan(City *city, TileIndex origin_tile, int choose_direction);

// Compatibility result: bits0..3 use line-direction order. The high
// sentinels are not enabled span-style bits.
#define CITY_SPAN_COMPATIBILITY_INVALID 0x8000u
#define CITY_SPAN_COMPATIBILITY_SPECIAL_TERRAIN 0x4000u
#define CITY_SPAN_COMPATIBILITY_ALL_DIRECTIONS 0x000Fu

#define CITY_SPAN_STYLE_HIGHWAY_MASK (1u << CITY_SPAN_HIGHWAY)
#define CITY_SPAN_STYLE_HIGHWAY_BOTH_MASK (CITY_SPAN_STYLE_HIGHWAY_MASK | (1u << CITY_SPAN_REINFORCED_HIGHWAY))

#endif
