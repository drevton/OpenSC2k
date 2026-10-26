#ifndef SC2K_CITY_INFRASTRUCTURE_TRANSPORT_H
#define SC2K_CITY_INFRASTRUCTURE_TRANSPORT_H

#include "city/types.h"

// Shared transport access pattern. Offset order is fixed; both growth admission and route-start search use it.
enum { CITY_TRANSPORT_NEIGHBOR_COUNT = 24 };
typedef struct { i8 x, y; } TransportNeighborOffset;
typedef struct { TransportNeighborOffset offset[CITY_TRANSPORT_NEIGHBOR_COUNT]; } TransportNeighborOffsets;
extern const TransportNeighborOffsets kTransportNeighborOffsets;

// Commuter route states use byte arrays. Bus boarding switches
// from states 0..3 to 4..7; stations 8/10/11 tally separate facility traffic.
// Alighting at 9 returns to ordinary road travel. Rail 12 and subway 13 are
// distinct graph modes, not surface or underground tile IDs.
typedef enum CityTransportRouteState {
  CITY_ROUTE_ROAD = 0,
  CITY_ROUTE_HIGHWAY = 1,
  CITY_ROUTE_TUNNEL = 2,
  CITY_ROUTE_CONNECTOR = 3,
  CITY_ROUTE_BUS_ROAD = 4,
  CITY_ROUTE_BUS_HIGHWAY = 5,
  CITY_ROUTE_BUS_TUNNEL = 6,
  CITY_ROUTE_BUS_CONNECTOR = 7,
  CITY_ROUTE_BUS_STATION = 8,
  CITY_ROUTE_ALIGHTING_STATION = 9,
  CITY_ROUTE_RAIL_STATION = 10,
  CITY_ROUTE_SUBWAY_STATION = 11,
  CITY_ROUTE_RAIL = 12,
  CITY_ROUTE_SUBWAY = 13
} CityTransportRouteState;

// Recount the cached industry bonus from rail/highway border surface tiles. Does not change tiles or route state.
void RecountCityIndustryConnections(City *city);

// Decay one valid 128x128 map section's half-resolution traffic cells. Returns their sum; the scheduler owns
// invalidation and city-wide accumulation. Whole-map decay performs both in the same section order.
u32 DecayTrafficDensitySection(City *city, int section);
void DecayTrafficDensity(City *city);

// Read-only growth admission for a valid source tile: the fixed 24-offset pattern accepts road-class tiles or
// bus/rail/subway stations. It does not accept every rail/highway kind or resolve bridge geometry like route search.
// This does not search for a destination or guarantee a successful route; no RNG or presentation effects.
bool TileHasTransportAccess(const City *city, TileIndex tile);

// Routing requires a valid source tile/zone, nonnegative traffic size and maximum_cost in 0..127. The cost bound
// keeps the fixed path stack within capacity. It borrows stable maps and exclusive use of City's shared queue.
// Consumes City RNG, applies traffic and usage counters, and returns success; it does not create moving map events.
bool FindTransportRouteAndApplyTraffic(City *city, TileIndex source, int source_zone, int size, int maximum_cost);

#endif
