#ifndef SC2K_CITY_MAPS_TREES_H
#define SC2K_CITY_MAPS_TREES_H

#include "city/engine/command_types.h"
#include "city/maps/tile_storage.h"

enum { CITY_TREE_RANDOM = 0, CITY_TREE_SPECIES_MASK = 15, CITY_TREE_REMOVE = 16,
    CITY_TREE_CLIMATE_SHIFT = 8, CITY_TREE_CLIMATE_MASK = 15 << CITY_TREE_CLIMATE_SHIFT };

// The random neighboring tree probe in GrowCityStockTrees uses S/N/E/W.
typedef enum CityTreeProbeDirection {
  CITY_TREE_PROBE_SOUTH = 0, CITY_TREE_PROBE_NORTH = 1,
  CITY_TREE_PROBE_EAST = 2, CITY_TREE_PROBE_WEST = 3,
} CityTreeProbeDirection;

enum { CITY_TREE_PLANT_COST = 3 };

// Enable or disable aging, death and reseeding. Validate Boolean/zero reserved values; TEST has no effects.
i64 CmdSetTreeLifecycle(City *city, u32 flags, int enabled, int reserved1, int reserved2);

// Species are stored relative to the tile's base climate; only RandomCityTreeSpecies advances the landscape RNG.
int CityTreeSpeciesCount(int climate);
int CityTreeGlobalSpecies(int climate, int species);
int RandomCityTreeSpecies(City *city, int climate);

// Replacement keeps the previous kind installed until metadata, counters and damage have been handled.
// The destination kind must be a tree; this prepares metadata but does not write kind or invalidate.
void InitializeCityTreeMetadata(City *city, TileInfo *tp, int kind);

// Queries require decoded options and a valid tile. Rectangle quotes validate endpoints/options and return raw prices.
// Neither query mutates City or consumes RNG; the command applies shared charging policy.
bool CanPlaceCityTree(const City *city, TileIndex tile, int options);
CityCommandCost GetCityTreeRectangleCost(const City *city, TileIndex start, TileIndex end, int options);
i64 CmdPlantTreesRect(City *city, u32 flags, TileIndex start, TileIndex end, int options);

// Original tree tools consume CRT draws. They mutate admitted tiles without charging; brush attempts keep their order.
i64 ApplyCityTreeToolAtTile(City *city, TileIndex tile, bool remove);
i64 ApplyCityTreeToolBrush(City *city, int x, int y, bool remove);

// Landscape traversal owns timing and visit order. Generation initializes an existing forest tile; GrowCityTrees
// runs after ground recovery on every sixteenth tree visit, and SeedCityTrees follows each landscape tick.
void GenerateCityTree(City *city, TileIndex tile);
void GrowCityTrees(City *city, TileIndex tile);
void SeedCityTrees(City *city);
// The stock economy phase calls this after its waste-decay attempt, preserving CRT draw order.
void GrowCityStockTrees(City *city, TileIndex tile);

// Tree layout banks are shared with asset bounds and species previews; per-tile arrangements stay private.
extern const u8 kCityTreeArtLayouts[196][4];
typedef struct Drawing Drawing;
int CityTreeGroundOffset(int land_shape, int zoom);
// Called only for visible tree details, after shared ground drawing; never changes City.
void DrawCityTrees(const City *city, Drawing *drawing, TileIndex tile, int zoom, int screen_x, int ground_y,
    int land_shape);

#endif
