#ifndef SC2K_CITY_MAPS_GROUND_H
#define SC2K_CITY_MAPS_GROUND_H

#include "city/types.h"
#include "city/maps/tile_storage.h"

typedef struct CityTileGeometry CityTileGeometry;

// Clear-ground recovery counters and derived climate queries. Counter writes do not change kind or coverage;
// callers own recovery policy and invalidation. These helpers do not consume RNG.
static SC2K_FORCE_INLINE int ReadGroundRecoveryCounter(const TileInfo *tp) {
  return tp->zone >> CITY_TILE_GROWTH_SHIFT;
}

static SC2K_FORCE_INLINE void WriteGroundRecoveryCounter(TileInfo *tp, int counter) {
  tp->zone = (tp->zone & 31) | counter << CITY_TILE_GROWTH_SHIFT;
}

static SC2K_FORCE_INLINE bool CityClimateHasGroundCover(int climate) {
  return (climate >= CLIMATE_SNOW_25 && climate <= CLIMATE_SNOW_100) ||
      (climate >= CLIMATE_DESERT_50 && climate <= CLIMATE_DESERT_100) ||
      climate == CLIMATE_SNOW_TEMPERATE_ARCTIC_50 || climate == CLIMATE_SNOW_ARCTIC_TROPICAL_50;
}

static SC2K_FORCE_INLINE int CityClimateSnowCoverage(int climate) {
  if (climate == CLIMATE_SNOW_TEMPERATE_ARCTIC_50 || climate == CLIMATE_SNOW_ARCTIC_TROPICAL_50) return 2;
  return climate >= CLIMATE_SNOW_25 && climate <= CLIMATE_SNOW_100 ? climate - CLIMATE_ARCTIC : 0;
}

// Ground reads treat wet non-bank tiles as grass. Writes replace only terrain's low nibble;
// callers choose kinds appropriate to the occupant and own old/new invalidation.
int ReadGroundKind(const TileInfo *tp);
void WriteGroundKind(TileInfo *tp, int kind);

// Resolve permanent climate and stored coverage without RNG or mutation. The tile query also blends
// neighboring permanent regions; tile indices must be valid.
int ReadCityTileBaseClimate(const City *city, const TileInfo *tp);
int ReadTileClimate(const City *city, const TileInfo *tp);
int ReadCityTileClimate(const City *city, TileIndex tile);

// For a valid tile, move coverage toward its height/region target, immediately or by one snow step.
// Bare recovery phases and paved transport ground are skipped. Consumes no RNG; changes publish old/new damage.
void RefreshCityTileGroundCover(City *city, TileIndex tile, bool immediate);
// Refresh the bounded three-by-three neighborhood with old/new damage; invalid input is ignored.
// The terrain editor settles coverage immediately; ordinary simulation uses gradual transitions.
void RefreshCityTileClimates(City *city, TileIndex tile);

// Start recovery and reset its counter without invalidation. Ordinary recovery settles immediately in
// the terrain editor; explicit clearing and exposed beds retain their bare phase there too.
void BeginCityGroundRecovery(City *city, TileInfo *tp);
void BeginCityBareGroundRecovery(City *city, TileInfo *tp);

// Generation follows tree/climate initialization and borrows its landscape RNG and prior full-map invalidation.
void GenerateCityGround(City *city);
// Import settles paving without RNG. Scheduled visits preserve traversal order; recovery precedes tree growth.
void InitializeCityTransportPaving(City *city);
void GrowCityTransportGround(City *city, TileIndex tile);
// One landscape visit of a valid tile. Kind selects occupant-safe timing; City supplies the current scan phase.
// Consumes no RNG. Updates recovery metadata in place and invalidates visible transitions.
void AdvanceCityGroundRecovery(City *city, TileIndex tile);

// Sprite-cache indexes include the requested zoom. Geometry and shape are camera-relative; queries change no state.
int CityGroundSpriteForShape(const City *city, TileIndex tile, int zoom, int shape);
// Paint native clear/tree ground and zoning in the ground pass without selection. Returns false for other kinds
// and stock artwork; callers draw any tree silhouettes and placement marker in their existing object phase.
bool DrawCityGroundSurface(City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry);
// Handles native clear/tree ground; returns false for other kinds and stock artwork.
bool DrawCityGroundTile(City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry);
// Fallback for source layouts below the road artwork range (clear, water, trees, remains and the small park).
// Caller owns layer visibility and clipping. Geometry/layout are camera-relative; this reads City without mutation.
// Terrain, placement/park selection, optional details and the power warning retain their drawing order.
void DrawCityGroundTileFallback(City *city, TileIndex tile, int layout, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry);

#endif
