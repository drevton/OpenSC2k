#ifndef SC2K_DRAWING_MAPS_SPRITE_CACHE_H
#define SC2K_DRAWING_MAPS_SPRITE_CACHE_H

#include "drawing/maps/map_sprites.h"
#include "drawing/sprites/preparation.h"

// Drawing owns base/prepared references and previews; Assets remains borrowed until cache release.
// Install and synchronous preparation require idle startup workers. Do not read in-flight outputs before joining.

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
// Startup only, with no active preparation. Copy the complete requirements value and Drawing's current palette;
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
