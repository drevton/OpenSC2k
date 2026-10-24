#ifndef SC2K_CITY_FIXES_H
#define SC2K_CITY_FIXES_H

#include "city/types.h"

// Stable bit positions in City.fixes: persisted settings and command recordings use these numbers.
// Construct masks with 1ull so shifts and complements retain all 64 bits.
typedef enum CityFix {
  CITY_FIX_ASSET_VALUES,
  CITY_FIX_TREATMENT_VALUE,
  CITY_FIX_DAILY_VALUATION,
  CITY_FIX_LOADED_CONNECTIONS,
  CITY_FIX_MILITARY_RUNWAYS,
  CITY_FIX_SILO_PLOTS,
  CITY_FIX_TUNNEL_RUBBLE,
  CITY_FIX_LABEL_CLEANUP,
  CITY_FIX_DEMOLITION_ELEVATION, // ID8 is presentation only; preserve saved preferences.
  CITY_FIX_BOND_RATING,
  CITY_FIX_MILITARY_PLACEMENT,
  CITY_FIX_MILITARY_GROWTH,
  CITY_FIX_POWER_CLEANUP,
  CITY_FIX_RESTORED_CHEATS,
  CITY_FIX_ALL_INVENTIONS,
  CITY_FIX_GRASS_CLEARING_COST, // Native gameplay option, not an original-game bug repair.
  CITY_FIX_BURIED_UTILITY_TERRAIN,
  CITY_FIX_REUSE_INFRASTRUCTURE,
  CITY_FIX_LANDSCAPE_DETAILS,
  CITY_FIX_ROUTE_ROOT_COST,
  CITY_FIX_DRY_BRIDGE_APPROACHES,
  CITY_FIX_COMPACT_HIGHWAYS,
  CITY_FIX_HIGHWAY_TERRAIN,
  CITY_FIX_NATIVE_LANDSCAPE,
  CITY_FIX_BUILDING_CONSTRUCTION,
  CITY_FIX_RAMP_CONNECTIONS,
  // ID26 was aircraft approach; the correction is now unconditional.
  CITY_FIX_BOUNDED_DISASTER_SEARCH = 27,
  // ID28 was inclusive event boundaries; byte fractions now always carry at 256.
  CITY_FIX_EXPANDED_EVENTS = 29,
  CITY_FIX_TRANSPORT_PAVING,
  CITY_FIX_ROUTE_QUEUE_DEDUPLICATION,
  CITY_FIX_WATER_SPREADING, // Allow adjacent water to reclaim flat land; shore slopes remain eligible.
  CITY_FIX_COUNT
} CityFix;

_Static_assert(CITY_FIX_COUNT <= 64, "city fixes fit the saved 64-bit mask");
#define CITY_FIX_SIMULATION_MASK ((UINT64_MAX >> (64 - CITY_FIX_COUNT)) & \
    ~(1ull << CITY_FIX_DEMOLITION_ELEVATION) & ~(1ull << 26) & ~(1ull << 28))
#define CITY_FIX_DEFAULT_MASK (CITY_FIX_SIMULATION_MASK & ~(1ull << CITY_FIX_GRASS_CLEARING_COST))
// Settings introduced after the two-byte compatibility mask.
#define CITY_FIX_NEW_MASK (CITY_FIX_SIMULATION_MASK & ~((1ull << CITY_FIX_BURIED_UTILITY_TERRAIN) - 1))

#endif
