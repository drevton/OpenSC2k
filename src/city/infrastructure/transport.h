#ifndef SC2K_CITY_INFRASTRUCTURE_TRANSPORT_H
#define SC2K_CITY_INFRASTRUCTURE_TRANSPORT_H

#include "city/types.h"

// Shared transport admission data; offset order and zone matching are fixed algorithm inputs.
typedef struct { i8 x, y; } TransportNeighborOffset;
typedef struct { TransportNeighborOffset offset[24]; } TransportNeighborOffsets;
extern const TransportNeighborOffsets kTransportNeighborOffsets;

extern const u16 kZoneCompatibilityMask[16];

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

// Routing borrows stable map storage and requires exclusive use of City's shared coordinate queue and scratch.
// It consumes City RNG, applies traffic and usage counters, and returns success; it does not create moving map events.
int FindTransportRouteAndApplyTraffic(City *city, int x, int y, int source_zone, int size, int maximum_cost);

#endif
