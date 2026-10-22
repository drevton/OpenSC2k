#ifndef SC2K_CITY_INFRASTRUCTURE_SPANS_H
#define SC2K_CITY_INFRASTRUCTURE_SPANS_H

#include "city/infrastructure/types.h"

// Span prices are shared by construction quotes, execution and the UI selector.
extern const u16 kSpanCostPerCell[CITY_SPAN_STYLE_COUNT];
extern const char *const kCitySpanNames[CITY_SPAN_STYLE_COUNT];

bool IsCityInfrastructureSpanWater(const City *city, TileIndex tile);
CityInfrastructureSpan FindCityInfrastructureSpan(const City *city, TileIndex origin,
    int direction, bool choose_direction);

int CityInfrastructureSpanStyles(CityInfrastructureSpan span);
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
