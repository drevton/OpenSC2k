#ifndef SC2K_CITY_MAPS_GROUND_H
#define SC2K_CITY_MAPS_GROUND_H

#include "city/types.h"
#include "city/maps/terrain.h"
#include "city/maps/tile_storage.h"
#include "city/terrain/climate.h"

// Snow coverage is 0..4 quarter steps. Wet banks retain ground cover; other wet occupants
// use this nibble for an underlying water kind and have no stored snow coverage.
static SC2K_FORCE_INLINE int ReadTileSnowCoverage(const TileInfo *tp) {
  int state = tp->terrain & 15;
  return (!HasTileWater(tp) || IsTileKindShoreBank(tp->kind)) && state >= 1 && state <= 4 ? state : 0;
}

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

// Encode a valid climate's permanent region for the supplied landscape preset. Snow maps to its base climate;
// uniform presets write region zero. Preserve all other flags and leave visible ground cover unchanged.
// The caller owns invalidation; this packed write consumes no RNG and does not change City settings.
static SC2K_FORCE_INLINE void WriteTileClimateRegion(TileInfo *tp, CityTerrainClimate preset, int climate) {
  int base = TerrainBaseClimate(climate), region = 0;
  if (preset == CITY_TERRAIN_MIXED)
    region = base >= CLIMATE_DESERT_50 && base <= CLIMATE_DESERT_100 ? 3 :
        base == CLIMATE_ARCTIC || base == CLIMATE_TEMPERATE_ARCTIC || base == CLIMATE_ARCTIC_TROPICAL ? 1 :
        base == CLIMATE_TROPICAL || base == CLIMATE_TEMPERATE_TROPICAL ? 2 : 0;
  else if (preset == CITY_TERRAIN_DESERT)
    region = base >= CLIMATE_DESERT_50 && base <= CLIMATE_DESERT_100 ? 1 : base == CLIMATE_RAINFOREST ? 2 : 0;
  tp->flags = (tp->flags & ~TF_REGION_MASK) | region << 1;
}

// Ground reads treat wet non-bank tiles as grass. Writes replace only terrain's low nibble;
// callers choose kinds appropriate to the occupant and own old/new invalidation.
int ReadGroundKind(const TileInfo *tp);
void WriteGroundKind(TileInfo *tp, int kind);

// Clearing queries include empty dry bridge underpasses. Bare recovery is a no-op; grass includes snow,
// desert and the later recovery phases. Read only the supplied record, without starting recovery.
bool IsCityGroundClearingNoop(const TileInfo *tp);
bool IsClearCityGrassTile(const TileInfo *tp);
// Rough, rocks or grass-like cover on clear ground or an empty dry underpass. Bare recovery is a separate no-op.
bool CanClearCityGroundCover(const TileInfo *tp);
// Replace an admitted unbuilt tile with its underlying dry/wet ground. Caller owns payment and old/new damage.
i64 ClearCityGroundTile(City *city, TileIndex tile);

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
bool DrawCityGroundSurface(const City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry);
// Handles native clear/tree ground; returns false for other kinds and stock artwork.
bool DrawCityGroundTile(const City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry);
// Fallback for source layouts below the road artwork range (clear, water, trees, remains and the small park).
// Caller owns layer visibility and clipping. Geometry/layout are camera-relative; this reads City without mutation.
// Terrain, placement/park selection, optional details and the power warning retain their drawing order.
void DrawCityGroundTileFallback(const City *city, TileIndex tile, int layout, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry);

#endif
