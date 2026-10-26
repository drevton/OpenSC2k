#ifndef SC2K_CITY_MAPS_TREES_H
#define SC2K_CITY_MAPS_TREES_H

#include "city/types.h"
#include "city/maps/tile_storage.h"

typedef struct Canvas Canvas;
typedef struct SpriteImage SpriteImage;

enum { CITY_TREE_RANDOM = 0, CITY_TREE_SPECIES_MASK = 15, CITY_TREE_REMOVE = 16,
    CITY_TREE_CLIMATE_SHIFT = 8, CITY_TREE_CLIMATE_MASK = 15 << CITY_TREE_CLIMATE_SHIFT };

// The random neighboring tree probe in GrowCityStockTrees uses S/N/E/W.
typedef enum CityTreeProbeDirection {
  CITY_TREE_PROBE_SOUTH = 0, CITY_TREE_PROBE_NORTH = 1,
  CITY_TREE_PROBE_EAST = 2, CITY_TREE_PROBE_WEST = 3,
} CityTreeProbeDirection;

enum { CITY_TREE_PLANT_COST = 3 };

// Local tree payload access does not select a kind or invalidate the tile.
// Callers choose the species/climate interpretation and publish the completed edit.
// Tree-local species, 0..15, interpreted in the tile climate.
static SC2K_FORCE_INLINE int ReadTreeSpecies(const TileInfo *tp) {
  return tp->terrain >> CITY_ZONE_METADATA_SHIFT;
}

static SC2K_FORCE_INLINE void WriteTreeSpecies(TileInfo *tp, int species) {
  tp->terrain = (tp->terrain & CITY_ZONE_KIND_MASK) |
      ((species & CITY_ZONE_METADATA_MAX) << CITY_ZONE_METADATA_SHIFT);
}

static SC2K_FORCE_INLINE int ReadTreeGrowth(const TileInfo *tp) {
  return tp->zone >> CITY_TILE_GROWTH_SHIFT;
}

static SC2K_FORCE_INLINE void WriteTreeGrowth(TileInfo *tp, int growth) {
  tp->zone = (tp->zone & 31) | growth << CITY_TILE_GROWTH_SHIFT;
}

// Enable or disable aging, death and reseeding. Validate Boolean/zero reserved values; TEST has no effects.
i64 CmdSetTreeLifecycle(City *city, u32 flags, int enabled, int reserved1, int reserved2);

// Species are stored relative to the tile's base climate; only RandomCityTreeSpecies advances the landscape RNG.
int CityTreeSpeciesCount(int climate);
int CityTreeGlobalSpecies(int climate, int species);
int RandomCityTreeSpecies(City *city, int climate);

// Replacement keeps the previous kind installed until metadata, counters and damage have been handled.
// The destination kind must be a tree; this prepares metadata but does not write kind or invalidate.
void InitializeCityTreeMetadata(City *city, TileInfo *tp, int kind);

// Rectangle TEST validates endpoints/options and quotes without changing City or consuming RNG.
// Execution uses the same tile admission and applies shared command charging policy.
i64 CmdPlantTreesRect(City *city, u32 flags, TileIndex start, TileIndex end, int options);

// Original tree tools consume CRT draws. They mutate admitted tiles without charging; brush attempts keep their order.
i64 ApplyCityTreeToolAtTile(City *city, TileIndex tile, bool remove);
i64 ApplyCityTreeToolBrush(City *city, int x, int y, bool remove);

// Landscape traversal owns timing and visit order. Generation initializes an existing forest tile.
// The tree module owns per-tile recovery/growth; SeedCityTrees follows each landscape tick.
void GenerateCityTree(City *city, TileIndex tile);
void SeedCityTrees(City *city);
// The stock economy phase calls this after its waste-decay attempt, preserving CRT draw order.
void GrowCityStockTrees(City *city, TileIndex tile);

// Tree layout banks are shared with asset bounds and species previews; per-tile arrangements stay private.
extern const u8 kCityTreeArtLayouts[196][4];
// Draw a global species' common climate crop using immutable Assets and a caller-supplied Drawing cache.
// CityTreeGlobalSpecies resolves the species index. Missing artwork/empty bounds are no-ops; false reports
// preparation failure without replacing the prior cache. The caller owns application error handling.
bool DrawCityTreePreview(const CityAssets *assets, Canvas *canvas, SpriteImage *cache, CityRect bounds, int species);
// Align fallback silhouettes to their camera-relative ground slope.
int CityTreeGroundOffset(int land_shape, int zoom);

#endif
