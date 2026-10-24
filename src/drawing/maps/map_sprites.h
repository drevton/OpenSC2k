#ifndef SC2K_DRAWING_MAPS_MAP_SPRITES_H
#define SC2K_DRAWING_MAPS_MAP_SPRITES_H

#include "assets/graphics.h"
#include "city/maps/water.h"
#include "drawing/sprites/preparation.h"

typedef struct Drawing Drawing;

// Immutable palette IDs accepted by sprite drawing. Each family adds its style/color offset to its first ID.
enum {
  MAP_PALETTE_BRIDGE_FIRST = 1, MAP_PALETTE_UNDERWORLD_FIRST = 16,
  MAP_PALETTE_CAR_FIRST = 32, MAP_PALETTE_SELECTION_FIRST = 64
};
_Static_assert(MAP_PALETTE_BRIDGE_FIRST + CITY_LANDSCAPE_BRIDGE_STYLES <= MAP_PALETTE_UNDERWORLD_FIRST &&
    MAP_PALETTE_UNDERWORLD_FIRST + CITY_UNDERWORLD_GROUND_STYLES <= MAP_PALETTE_CAR_FIRST &&
    MAP_PALETTE_CAR_FIRST + CITY_ROAD_VEHICLE_PALETTES <= MAP_PALETTE_SELECTION_FIRST,
    "Prepared palette ranges must not overlap");

// Logical width and top-to-ground-anchor distance of an already zoomed cache slot, before target/display scaling.
CityPoint CityMapSpriteExtent(const Drawing *drawing, int sprite);

// Retained inspector artwork contains cache-slot IDs and values, with no borrowed pointers or owned storage.
typedef struct MapTilePreview {
  u16 sprite;
  CityRiverSprites river;
  bool flipped; // Tile mirror metadata retained for optional inspector detail text.
} MapTilePreview;
// Capture the existing large-zoom artwork selection for a valid tile and resolved sailboat-overlay choice.
// Requires attached Drawing and loaded graphics. Reads the current water-camera orientation without binding a target,
// preparing images, painting, allocating or changing City. MapSpriteCacheZoomIndex selects another prepared zoom.
MapTilePreview ReadCityMapTilePreview(const City *city, TileIndex tile, bool sailboat);
// Fit the retained artwork as one group using prepared cache sizes, then paint inside logical canvas bounds.
// If none fits, clip the smallest available size; missing groups draw nothing. Preserve canvas clip/raster state,
// shared river-layer anchors and cached-source mirroring. No City reads, cache preparation or allocation.
void DrawCityMapTilePreview(Drawing *drawing, Canvas *canvas, const MapTilePreview *preview, CityRect bounds);

// Cached map-sprite drawing uses the active Drawing clip and depth pass.
bool DrawCityMapSprite(Drawing *drawing, int sprite_id, int x, int y, int flipped);
bool DrawCityMapSpriteAtBaseline(Drawing *drawing, int sprite_id, int x, int baseline, int flipped);
// Tree roots use camera-relative sixteenths of a tile, centred at (8, 8).
// Preserve their positions when interleaving silhouettes with adjacent structures.
void DrawCityMapTreeAtBaseline(Drawing *drawing, int sprite_id, int x, int baseline, CityPoint root);
// Fractional map coordinates share the same cache, clipping and prepared-sprite path. One pixel is 256 units.
bool DrawCityMapSpriteAtBaselineQ8(Drawing *drawing, int sprite_id, i64 x_q8, i64 baseline_q8, int flipped);
// Palette is an immutable drawing palette ID; flags carry mode, flip and transparency.
bool DrawCityMapPaletteSpriteAtBaseline(Drawing *drawing, int sprite_id, int x, int baseline, u32 flags, int palette);

// Map Sprite Cache
// Drawing owns base/prepared references and previews; Assets remains borrowed until cache release.
// Install and synchronous preparation require idle startup workers. Do not read in-flight outputs before joining.

// Legacy cache banks are private prepared resolutions, not public sprite identities.
static inline int MapSpriteZoomIndex(int sprite, int zoom) { return sprite + CITY_MAP_SPRITES_PER_ZOOM * zoom; }

enum {
  MAP_SELECTION_SPRITE_FIRST = 1500,
  MAP_SELECTION_SPRITE_SHAPES = 14,
  MAP_POINT_SPRITE_FIRST = MAP_SELECTION_SPRITE_FIRST + 3 * MAP_SELECTION_SPRITE_SHAPES,
  MAP_DEMOLITION_SPRITE_FIRST = MAP_POINT_SPRITE_FIRST + 3,
  MAP_SLOPED_WATER_SPRITE_FIRST = MAP_DEMOLITION_SPRITE_FIRST + 3 * CITY_DEMOLITION_FRAME_COUNT,
  MAP_LANDSCAPE_SPRITE_FIRST = MAP_SLOPED_WATER_SPRITE_FIRST + 3 * MAP_SELECTION_SPRITE_SHAPES,
  MAP_LANDSCAPE_SPRITES_PER_ZOOM = CITY_LANDSCAPE_SPRITE_COUNT - CITY_LANDSCAPE_GROUND_FIRST,
  MAP_SPRITE_COUNT = MAP_LANDSCAPE_SPRITE_FIRST + 3 * MAP_LANDSCAPE_SPRITES_PER_ZOOM,
  MAP_SPRITE_SCALE_COUNT = 2,
};

// Select a prepared slot (zoom 0..2) for catalog, native landscape or sloped-water sprites.
// Input may already include zoom. Native families keep their own strides; catalog slots use the legacy banks.
static inline int MapSpriteCacheZoomIndex(int sprite, int zoom) {
  if (sprite >= MAP_LANDSCAPE_SPRITE_FIRST)
    return MAP_LANDSCAPE_SPRITE_FIRST + (sprite - MAP_LANDSCAPE_SPRITE_FIRST) % MAP_LANDSCAPE_SPRITES_PER_ZOOM +
        zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM;
  if (sprite >= MAP_SLOPED_WATER_SPRITE_FIRST)
    return MAP_SLOPED_WATER_SPRITE_FIRST + (sprite - MAP_SLOPED_WATER_SPRITE_FIRST) % MAP_SELECTION_SPRITE_SHAPES +
        zoom * MAP_SELECTION_SPRITE_SHAPES;
  return MapSpriteZoomIndex(sprite % CITY_MAP_SPRITES_PER_ZOOM, zoom);
}

// Resolved preparation sizes travel together as a caller-owned value; startup copies them before launching workers.
// Scales use framebuffer percentages; preview dimensions use UI scale independently of map zoom.
// Entries beyond count are zero, not prepared cache slots.
typedef struct {
  int scales[MAP_SPRITE_SCALE_COUNT];
  int preview_width, preview_height;
  u8 count;
} MapSpriteRequirements;
// Requires positive map/UI scales. Return the required sizes without changing or preparing the cache.
MapSpriteRequirements DrawingMapSpriteRequirements(const Drawing *drawing);

// Replace source bindings from immutable Assets, retaining partial results on failure.
// The cache borrows Assets until replaced or released. Game invalidates dependent UI previews first.
// The new revision and map invalidation are published only on success.
bool InstallDrawingMapSpriteSources(Drawing *drawing, const CityAssets *assets);
// Prepare the required zoom scales and construction previews from already installed sources.
bool PrepareDrawingMapSprites(Drawing *drawing);

typedef struct {
  const CityResourceState *assets;
  int pixel_scale; // Framebuffer pixel density in percent; immutable until preparation workers join.
  RunSprite *base[MAP_SPRITE_COUNT];
  RunSprite *sprites[MAP_SPRITE_SCALE_COUNT][MAP_SPRITE_COUNT];
  int baselines[MAP_SPRITE_SCALE_COUNT][MAP_SPRITE_COUNT];
  int offsets_x[MAP_SPRITE_SCALE_COUNT][MAP_SPRITE_COUNT];
  int scales[MAP_SPRITE_SCALE_COUNT];
  int base_scales[MAP_SPRITE_COUNT], base_baselines[MAP_SPRITE_COUNT], base_offsets_x[MAP_SPRITE_COUNT];
  // Four native views and one legacy source, fitted once for construction choosers.
  RunSprite *building_previews[CITY_CIVIC_KIND_COUNT][CITY_CIVIC_VIEWS + 1];
  RunSprite *modern_wind_previews[CITY_CIVIC_VIEWS];
  int preview_width, preview_height;
  // Shared preparation workspace, released after the final preview is ready.
  SpritePreparationWorkspace preparation_workspace;
  struct MapSpritePreparation *preparation;
} MapSpriteCache;
// Query an already published scale; -1 means absent. This does not start preparation.
int FindMapSpriteScale(const MapSpriteCache *cache, int scale);
// Prepare a complete display scale synchronously, or return its already published slot. -1 means failure.
// A failed replacement retains partial images under an unpublished slot; cache release or retry reclaims them.
int PrepareMapSpriteScale(MapSpriteCache *cache, int scale, const u32 *palette);
// Startup only, with no active preparation. Copy the caller's resolved sizes and Drawing's current palette;
// install the base pixel density before launch. Keep the caller's requirements snapshot across loading events.
// Until Finish joins every worker, cache sources, Assets and pixel_scale must remain unchanged and alive;
// no other code may read or replace output images/previews. Workers use disjoint outputs and private row scratch.
bool StartMapSpritePreparation(Drawing *drawing, MapSpriteRequirements requirements);
typedef struct {
  int completed, total; // Weighted preparation work, not sprite counts.
} MapSpriteProgress;
// Poll without reading in-flight images or joining workers. A completed value of -1 and total of zero mean
// no active run or worker failure; otherwise total is fixed for this run and completed is an atomic sample.
MapSpriteProgress MapSpritePreparationProgress(const MapSpriteCache *cache);
// Join every worker before freeing its shared preparation record. Active cancellation returns false;
// success (or an already idle cache) returns true. Only a completed, uncancelled run publishes scale/preview readiness.
bool FinishMapSpritePreparation(MapSpriteCache *cache, bool cancel);
// Between paints, cancel/join active workers before releasing all scaled images and clearing scale readiness.
// Sources, preview images and synchronous workspace stay owned by the cache for reuse or full release.
void FreeMapSpriteScales(MapSpriteCache *cache);
// Cancel/join active workers, release all references/workspace and reset the cache.
void FreeMapSpriteCache(MapSpriteCache *cache);

#endif
