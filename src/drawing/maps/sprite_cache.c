#include "drawing/maps/sprite_cache.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <SDL_atomic.h>
#include <SDL_cpuinfo.h>
#include <SDL_thread.h>

#include "assets/state.h"
#include "city/maps/buildings.h"
#include "drawing/maps/map_view.h"
#include "drawing/sprites/sources.h"
#include "drawing/state.h"

// Map source installation, prepared scale/preview caching and startup worker lifetime.
// Resampling uses the generic sprite preparation interface and the existing owner/worker row buffers.

enum {
  MAP_SPRITE_PREPARATION_WORKERS = 4,
  MAP_CIVIC_PREVIEW_COUNT = CITY_CIVIC_KIND_COUNT * (CITY_CIVIC_VIEWS + 1),
  MAP_BUILDING_PREVIEW_COUNT = MAP_CIVIC_PREVIEW_COUNT + CITY_CIVIC_VIEWS,
};

// Retain the replacement before releasing the previous source. Failure leaves that entry unchanged;
// display-scaled outputs are prepared separately after source installation finishes.
static bool InstallLandscapeSprite(MapSpriteCache *cache, int id, const RunSprite *sprite, int scale, int baseline,
    int offset_x, bool flipped) {
  RunSprite *copy = flipped ? ReferenceRunSprite(sprite, sprite->palette, true) : RetainRunSprite(sprite);
  if (!copy) return false;
  FreeRunSprite(cache->base[id]);
  cache->base[id] = copy;
  cache->base_scales[id] = scale;
  cache->base_baselines[id] = baseline;
  cache->base_offsets_x[id] = offset_x;
  return true;
}

// Sources are installed only after the cache has borrowed its validated Assets catalog.
static bool InstallResourceSprite(MapSpriteCache *cache, int id, int index, int scale) {
  const CityResourceState *assets = cache->assets;
  // Cache IDs identify the map level internally; resource IDs stay zoom independent.
  int level = id < CITY_MAP_SPRITE_COUNT ? id / CITY_MAP_SPRITES_PER_ZOOM :
      (id - MAP_LANDSCAPE_SPRITE_FIRST) / MAP_LANDSCAPE_SPRITES_PER_ZOOM;
  LandscapeSpriteSource source = GetResourceSpriteVariant(assets, index, level, true);
  const CityLandscapeSprite *art = source.artwork;
  return InstallLandscapeSprite(cache, id, art->sprite, art->scale * scale, art->baseline, art->offset_x,
      source.flipped);
}

bool InstallDrawingMapSpriteSources(Drawing *drawing, const CityAssets *assets) {
  MapSpriteCache *cache = &drawing->sprite_cache;
  FreeMapSpriteCache(cache);
  cache->assets = assets;
  for (int id = 0; id < CITY_MAP_SPRITE_COUNT; ++id) {
    int slot = id % CITY_MAP_SPRITES_PER_ZOOM;
    int resource = slot < assets->map_tile_sprite_count ? TileSpriteResourceIndex(assets, slot, 0) : slot;
    const CityLandscapeSprite *landscape = GetResourceSprite(assets, resource);
    if (landscape && landscape->sprite) {
      if (!InstallResourceSprite(cache, id, resource,
          1 << (2 - id / CITY_MAP_SPRITES_PER_ZOOM))) return false;
    }
  }
  for (int id = CITY_LANDSCAPE_SELECTION_FIRST; id < CITY_LANDSCAPE_SELECTION_END; ++id) {
    const CityLandscapeSprite *selection = &assets->landscape[id];
    if (!InstallLandscapeSprite(cache,
        MAP_SELECTION_SPRITE_FIRST + id - CITY_LANDSCAPE_SELECTION_FIRST, selection->sprite,
        selection->scale, selection->baseline, 0, false)) return false;
  }
  for (int zoom = 0; zoom < 3; ++zoom) {
    for (int slope = 0; slope < MAP_SELECTION_SPRITE_SHAPES; ++slope) {
      const CityLandscapeSprite *water = &assets->landscape[CITY_LANDSCAPE_SLOPED_WATER_FIRST + slope];
      int cached = MAP_SLOPED_WATER_SPRITE_FIRST + zoom * MAP_SELECTION_SPRITE_SHAPES + slope;
      if (!InstallLandscapeSprite(cache, cached, water->sprite,
          water->scale << (2 - zoom), water->baseline, water->offset_x, false)) return false;
    }
    const CityLandscapeSprite *point = &assets->landscape[CITY_LANDSCAPE_POINT + zoom];
    if (!InstallLandscapeSprite(cache, MAP_POINT_SPRITE_FIRST + zoom, point->sprite,
        point->scale, point->baseline, point->offset_x, false)) return false;
    for (int id = CITY_LANDSCAPE_GROUND_FIRST; id < CITY_LANDSCAPE_POINT; ++id) {
      int cached = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM +
          id - CITY_LANDSCAPE_GROUND_FIRST;
      if (!InstallResourceSprite(cache, cached, id, 1 << (2 - zoom))) return false;
    }
    for (int id = CITY_LANDSCAPE_RIVER_SIDE_FIRST; id < CITY_LANDSCAPE_SPRITE_COUNT; ++id) {
      int resource = LandscapeSpriteResourceIndex(assets, id);
      const CityLandscapeSprite *bank = GetResourceSprite(assets, resource);
      if (!bank || !bank->sprite) continue;
      int cached = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM +
          id - CITY_LANDSCAPE_GROUND_FIRST;
      if (!InstallResourceSprite(cache, cached, resource, 1 << (2 - zoom))) return false;
    }
  }
  for (int frame = 0; frame < CITY_DEMOLITION_FRAME_COUNT; ++frame) {
    const CityLandscapeSprite *effect = &assets->demolition[frame];
    if (!InstallLandscapeSprite(cache,
        MAP_DEMOLITION_SPRITE_FIRST + 2 * CITY_DEMOLITION_FRAME_COUNT + frame, effect->sprite,
        effect->scale, effect->baseline, effect->offset_x, false)) return false;
  }
  drawing->sprite_revision = assets->sprite_revision;
  InvalidateCityMapViews(drawing);
  return true;
}

MapSpriteRequirements DrawingMapSpriteRequirements(const Drawing *drawing) {
  int scale = drawing->map_pixel_scale * 100;
  int maximum = CITY_MAP_MAX_TILE_WIDTH * 100 / (8 << CITY_MAP_ZOOM_LARGE);
  MapSpriteRequirements requirements = {.scales = {scale},
      .preview_width = (CITY_BUILDING_PREVIEW_WIDTH * drawing->options.scale + 99) / 100,
      .preview_height = (CITY_BUILDING_PREVIEW_HEIGHT * drawing->options.scale + 99) / 100, .count = 1};
  if (scale * 2 > maximum) return requirements;
  requirements.scales[1] = scale * 2;
  requirements.count = 2;
  return requirements;
}

static CityRect SpriteContentBounds(const RunSprite *sprite) {
  CityRect content = {sprite->width, sprite->height, 0, 0};
  const u32 *rows = RunSpriteRows(sprite);
  const u8 *data = RunSpriteData(sprite);
  for (int y = 0; y < sprite->height; ++y) {
    const u8 *source = data + rows[y], *end = data + rows[y + 1];
    int x = source < end ? ReadSpriteInitialSkip(&source) : 0;
    while (source < end) {
      int kind, count = ReadSpriteRun(&source, &kind);
      int bytes = SpriteRunPayloadBytes(kind);
      int first = 0, last = count;
      if (kind == SPRITE_RUN_TRANSPARENT) last = 0;
      else if (SpriteRunHasAlpha(kind)) {
        int alpha = bytes - 1;
        while (first < last && !source[(size_t)first * bytes + alpha]) ++first;
        while (last > first && !source[(size_t)(last - 1) * bytes + alpha]) --last;
      }
      if (first < last) {
        content.left = IntMin(content.left, x + first);
        content.right = IntMax(content.right, x + last);
        content.top = IntMin(content.top, y);
        content.bottom = y + 1;
      }
      x += count;
      source += (size_t)count * bytes;
    }
  }
  return content;
}

int FindMapSpriteScale(const MapSpriteCache *cache, int scale) {
  for (int i = 0; i < MAP_SPRITE_SCALE_COUNT; ++i)
    if (cache->scales[i] == scale) return i;
  return -1;
}

typedef struct {
  const RunSprite *image;
  int divisor, baseline, offset_x;
  bool flipped;
} MapSpriteImage;

static MapSpriteImage SelectMapSpriteImage(const MapSpriteCache *cache, int id, int scale) {
  int divisor, source = id;
  if (id >= MAP_SLOPED_WATER_SPRITE_FIRST) {
    divisor = 100 * cache->base_scales[source];
  } else if (id >= MAP_DEMOLITION_SPRITE_FIRST) {
    int zoom = (id - MAP_DEMOLITION_SPRITE_FIRST) / CITY_DEMOLITION_FRAME_COUNT;
    source = MAP_DEMOLITION_SPRITE_FIRST + 2 * CITY_DEMOLITION_FRAME_COUNT +
        (id - MAP_DEMOLITION_SPRITE_FIRST) % CITY_DEMOLITION_FRAME_COUNT;
    divisor = 100 * (1 << (2 - zoom)) * cache->base_scales[source];
  } else if (id >= MAP_POINT_SPRITE_FIRST) {
    divisor = 100 * cache->base_scales[source];
  } else if (id >= MAP_SELECTION_SPRITE_FIRST) {
    int zoom = (id - MAP_SELECTION_SPRITE_FIRST) / MAP_SELECTION_SPRITE_SHAPES;
    // Enlarge the normal OpenTTD selector with the tile. Extra-zoom selectors
    // retain a narrow border instead of scaling its original pixel thickness.
    source = MAP_SELECTION_SPRITE_FIRST +
        (id - MAP_SELECTION_SPRITE_FIRST) % MAP_SELECTION_SPRITE_SHAPES;
    divisor = 100 * (8 >> zoom);
  } else {
    int zoom = id / CITY_MAP_SPRITES_PER_ZOOM, sprite = id % CITY_MAP_SPRITES_PER_ZOOM;
    // An authored variant at this level takes precedence over generated levels.
    for (int higher = CITY_MAP_ZOOM_LARGE; !cache->base[source] && higher > zoom; --higher) {
      int candidate = MapSpriteZoomIndex(sprite, higher);
      if (cache->base[candidate]) { source = candidate; break; }
    }
    divisor = 100 * (1 << (source / CITY_MAP_SPRITES_PER_ZOOM - zoom)) * cache->base_scales[source];
  }
  MapSpriteImage image = {cache->base[source], divisor, cache->base_baselines[source],
      cache->base_offsets_x[source], false};
  const CityResourceState *assets = cache->assets;
  if (!assets || scale != cache->pixel_scale * 2) return image;
  int resource;
  if (id >= CITY_SPRITE_CATALOG_LARGE_BASE && id < CITY_MAP_SPRITE_COUNT) {
    int sprite = id - CITY_SPRITE_CATALOG_LARGE_BASE;
    resource = sprite < assets->map_tile_sprite_count ? TileSpriteResourceIndex(assets, sprite, 0) : sprite;
  } else if (id >= MAP_LANDSCAPE_SPRITE_FIRST + 2 * MAP_LANDSCAPE_SPRITES_PER_ZOOM) {
    resource = LandscapeSpriteResourceIndex(assets,
        id - MAP_LANDSCAPE_SPRITE_FIRST - 2 * MAP_LANDSCAPE_SPRITES_PER_ZOOM + CITY_LANDSCAPE_GROUND_FIRST);
  } else return image;
  LandscapeSpriteSource variant = GetResourceSpriteVariant(assets, resource, 3, false);
  if (!variant.artwork) return image;
  const CityLandscapeSprite *art = variant.artwork;
  return (MapSpriteImage){art->sprite, 100 * art->scale, art->baseline, art->offset_x, variant.flipped};
}

static bool PrepareMapSpriteRange(MapSpriteCache *cache, SpritePreparationWorkspace *workspace,
    int slot, int scale, const u32 *palette, int first, int count) {
  int first_car = MAP_LANDSCAPE_SPRITE_FIRST + CITY_MAP_ZOOM_LARGE * MAP_LANDSCAPE_SPRITES_PER_ZOOM +
      CITY_LANDSCAPE_ROAD_VEHICLE_FIRST - CITY_LANDSCAPE_GROUND_FIRST;
  int car_count = CITY_LANDSCAPE_HIGHWAY_OVERPASS_FIRST - CITY_LANDSCAPE_ROAD_VEHICLE_FIRST;
  for (int id = first; id < first + count; ++id) {
    MapSpriteImage image = SelectMapSpriteImage(cache, id, scale);
    int divisor = image.divisor;
    const RunSprite *base = image.image;
    if (!base || cache->sprites[slot][id]) continue;
    // Original catalog heights are baseline offsets, excluding the final encoded row.
    cache->baselines[slot][id] = image.baseline * scale / divisor;
    cache->offsets_x[slot][id] = image.offset_x * scale / divisor;
    // Prepared surfaces share an exact footprint. Searching neighboring source
    // pixels here expands its boundary and makes adjacent tiles overlap.
    RunSprite *flipped = image.flipped ? ReferenceRunSprite(base, base->palette, true) : NULL;
    if (image.flipped && !flipped) return false;
    int catalog_sprite = id < CITY_MAP_SPRITE_COUNT ? id % CITY_MAP_SPRITES_PER_ZOOM : -1;
    cache->sprites[slot][id] = ResampleRunSprite(workspace, flipped ? flipped : base,
        (CityRect){0, 0, base->width, base->height}, (CityPoint){scale, scale}, (CityPoint){divisor, divisor}, palette,
        catalog_sprite >= CITY_SPRITE_CATALOG_GROUND_OVERLAY_FIRST &&
        catalog_sprite <= CITY_SPRITE_CATALOG_GROUND_OVERLAY_LAST &&
        catalog_sprite != CITY_SPRITE_CATALOG_SERVICE_MISSING ?
        SPRITE_FILTER_COLORS : SPRITE_FILTER_NEAREST);
    FreeRunSprite(flipped);
    if (!cache->sprites[slot][id]) return false;
    if (id >= first_car && id < first_car + car_count) {
      RunSprite *sprite = cache->sprites[slot][id];
      CityRect content = SpriteContentBounds(sprite);
      if (content.left < content.right && content.top < content.bottom &&
          (content.left || content.top || content.right != sprite->width || content.bottom != sprite->height)) {
        // Trim after scaling so the original sampling grid and every visible pixel stay fixed.
        RunSprite *trimmed = ResampleRunSprite(workspace, sprite, content,
            (CityPoint){1, 1}, (CityPoint){1, 1}, palette, SPRITE_FILTER_NEAREST);
        if (!trimmed) return false;
        cache->sprites[slot][id] = trimmed;
        cache->offsets_x[slot][id] += content.left;
        cache->baselines[slot][id] -= content.top;
        FreeRunSprite(sprite);
      }
    }
  }
  return true;
}

int PrepareMapSpriteScale(MapSpriteCache *cache, int scale, const u32 *palette) {
  if (scale < 100 || scale > CITY_MAP_MAX_TILE_WIDTH * 100 / (8 << CITY_MAP_ZOOM_LARGE)) return -1;
  int cached = FindMapSpriteScale(cache, scale);
  if (cached >= 0) return cached;
  int slot = 0;
  while (slot < MAP_SPRITE_SCALE_COUNT && cache->scales[slot]) ++slot;
  if (slot == MAP_SPRITE_SCALE_COUNT) slot = 0;
  cache->scales[slot] = 0;
  for (int id = 0; id < MAP_SPRITE_COUNT; ++id) {
    FreeRunSprite(cache->sprites[slot][id]);
    cache->sprites[slot][id] = NULL;
  }
  if (!PrepareMapSpriteRange(cache, &cache->preparation_workspace, slot, scale, palette, 0, MAP_SPRITE_COUNT))
    return -1;
  cache->scales[slot] = scale;
  return slot;
}

// Internal callers have already joined workers, or require an idle cache before beginning preparation.
static void ReleaseMapSpriteScales(MapSpriteCache *cache) {
  for (int scale = 0; scale < MAP_SPRITE_SCALE_COUNT; ++scale)
    for (int id = 0; id < MAP_SPRITE_COUNT; ++id) {
      FreeRunSprite(cache->sprites[scale][id]);
      cache->sprites[scale][id] = NULL;
    }
  memset(cache->scales, 0, sizeof(cache->scales));
}

void FreeMapSpriteScales(MapSpriteCache *cache) {
  FinishMapSpritePreparation(cache, true);
  ReleaseMapSpriteScales(cache);
}

void FreeMapSpriteCache(MapSpriteCache *cache) {
  FinishMapSpritePreparation(cache, true);
  for (int id = 0; id < MAP_SPRITE_COUNT; ++id) FreeRunSprite(cache->base[id]);
  ReleaseMapSpriteScales(cache);
  for (int kind = 0; kind < CITY_CIVIC_KIND_COUNT; ++kind)
    for (int view = 0; view <= CITY_CIVIC_VIEWS; ++view) FreeRunSprite(cache->building_previews[kind][view]);
  for (int view = 0; view < CITY_CIVIC_VIEWS; ++view) FreeRunSprite(cache->modern_wind_previews[view]);
  FreeSpritePreparationWorkspace(&cache->preparation_workspace);
  memset(cache, 0, sizeof(*cache));
}

static bool PrepareBuildingPreview(SpritePreparationWorkspace *workspace, RunSprite **prepared, const RunSprite *source,
    int width, int height, const u32 *palette) {
  FreeRunSprite(*prepared);
  *prepared = NULL;
  if (!source) return true;
  CityRect content = SpriteContentBounds(source);
  int w = content.right - content.left, h = content.bottom - content.top;
  if (w > 0 && h > 0) {
    int numerator = width, denominator = w;
    if ((i64)width * h > (i64)height * w) { numerator = height; denominator = h; }
    *prepared = ResampleRunSprite(workspace, source, content, (CityPoint){numerator, numerator},
        (CityPoint){denominator, denominator}, palette, SPRITE_FILTER_NEAREST);
  }
  if (w > 0 && h > 0 && !*prepared) return false;
  return true;
}

typedef struct {
  const RunSprite *source;
  RunSprite **prepared;
} MapBuildingPreview;

// Pair the source image with its cache-owned output slot for a valid preview index. Neither is changed or retained;
// preparation and progress weighting share this selection while the cache remains alive and its sources stay fixed.
static MapBuildingPreview SelectMapBuildingPreview(MapSpriteCache *cache, int index) {
  int source;
  RunSprite **prepared;
  if (index < MAP_CIVIC_PREVIEW_COUNT) {
    int kind = index / (CITY_CIVIC_VIEWS + 1), view = index % (CITY_CIVIC_VIEWS + 1);
    prepared = &cache->building_previews[kind][view];
    if (kind + CITY_CIVIC_KIND_FIRST == CITY_MAP_TILE_SPRITE_WIND_POWER && view < CITY_CIVIC_VIEWS)
      source = MAP_LANDSCAPE_SPRITE_FIRST + 2 * MAP_LANDSCAPE_SPRITES_PER_ZOOM +
          CITY_LANDSCAPE_WIND_PREVIEW_FIRST + view - CITY_LANDSCAPE_GROUND_FIRST;
    else
      source = view == CITY_CIVIC_VIEWS ? CITY_SPRITE_CATALOG_LARGE_BASE + CITY_CIVIC_KIND_FIRST + kind :
          MAP_LANDSCAPE_SPRITE_FIRST + 2 * MAP_LANDSCAPE_SPRITES_PER_ZOOM +
          CITY_LANDSCAPE_CIVIC_FIRST + kind * CITY_CIVIC_VIEWS + view - CITY_LANDSCAPE_GROUND_FIRST;
  } else {
    int view = index - MAP_CIVIC_PREVIEW_COUNT;
    prepared = &cache->modern_wind_previews[view];
    source = MAP_LANDSCAPE_SPRITE_FIRST + 2 * MAP_LANDSCAPE_SPRITES_PER_ZOOM +
        CITY_LANDSCAPE_WIND_PREVIEW_FIRST + CITY_CIVIC_VIEWS + view - CITY_LANDSCAPE_GROUND_FIRST;
  }
  return (MapBuildingPreview){cache->base[source], prepared};
}

static bool PrepareMapBuildingPreview(MapSpriteCache *cache, SpritePreparationWorkspace *workspace,
    int width, int height, const u32 *palette, int index) {
  MapBuildingPreview preview = SelectMapBuildingPreview(cache, index);
  return PrepareBuildingPreview(workspace, preview.prepared, preview.source, width, height, palette);
}

// Fit construction previews synchronously. Publish dimensions only after the complete set succeeds;
// a failed fit leaves partial images owned by the cache for retry or release.
static bool PrepareMapBuildingPreviews(MapSpriteCache *cache, int width, int height, const u32 *palette) {
  if (cache->preview_width == width && cache->preview_height == height) {
    FreeSpritePreparationWorkspace(&cache->preparation_workspace);
    return true;
  }
  for (int index = 0; index < MAP_BUILDING_PREVIEW_COUNT; ++index)
    if (!PrepareMapBuildingPreview(cache, &cache->preparation_workspace, width, height, palette, index)) return false;
  cache->preview_width = width;
  cache->preview_height = height;
  FreeSpritePreparationWorkspace(&cache->preparation_workspace);
  return true;
}

bool PrepareDrawingMapSprites(Drawing *drawing) {
  MapSpriteCache *cache = &drawing->sprite_cache;
  // Every ordinary zoom, magnified zoom and auxiliary viewport is ready
  // before drawing. Only a display pixel-density change replaces this set.
  cache->pixel_scale = drawing->map_pixel_scale * 100;
  MapSpriteRequirements requirements = DrawingMapSpriteRequirements(drawing);
  bool missing = false;
  for (int i = 0; i < requirements.count; ++i)
    if (FindMapSpriteScale(cache, requirements.scales[i]) < 0) missing = true;
  if (missing) {
    ReleaseMapSpriteScales(cache);
    for (int i = 0; i < requirements.count; ++i)
      if (PrepareMapSpriteScale(cache, requirements.scales[i], drawing->colors) < 0)
        return false;
  }
  return PrepareMapBuildingPreviews(cache,
      requirements.preview_width, requirements.preview_height, drawing->colors);
}

typedef struct MapSpritePreparation MapSpritePreparation;
typedef struct {
  SDL_Thread *thread;
  MapSpritePreparation *preparation;
  SpritePreparationWorkspace workspace;
} MapSpritePreparationWorker;

struct MapSpritePreparation {
  MapSpriteCache *cache;
  MapSpritePreparationWorker workers[MAP_SPRITE_PREPARATION_WORKERS];
  MapSpriteRequirements requirements; // Immutable sizes shared by workers until they all join.
  int worker_count, total, total_work;
  u32 palette[256];
  SDL_atomic_t next, completed, cancelled, failed;
};

static int MapSpritePreparationJobWork(const MapSpritePreparation *preparation, int job) {
  const MapSpriteRequirements *requirements = &preparation->requirements;
  const RunSprite *source;
  u64 work;
  if (job < MAP_BUILDING_PREVIEW_COUNT) {
    MapBuildingPreview preview = SelectMapBuildingPreview(preparation->cache, job);
    source = preview.source;
    if (!source) return 1;
    // Preview fitting traverses source runs before sampling the small output.
    work = 1 + source->stream_size / 1024 + source->height +
        (u64)requirements->preview_width * requirements->preview_height / 128;
  } else {
    int map_job = job - MAP_BUILDING_PREVIEW_COUNT;
    int slot = map_job % requirements->count, id = map_job / requirements->count;
    int scale = requirements->scales[slot];
    MapSpriteImage image = SelectMapSpriteImage(preparation->cache, id, scale);
    int divisor = image.divisor;
    source = image.image;
    if (!source) return 1;
    if (scale == divisor) work = 1 + source->stream_size / 1024;
    else {
      u64 width = ((u64)source->width * scale + divisor - 1) / divisor;
      u64 height = ((u64)source->height * scale + divisor - 1) / divisor;
      work = 1 + width * height / 128 + height;
    }
  }
  // Bound the sum for SDL's integer atomic counter, including very large artwork.
  u32 maximum = INT_MAX / preparation->total;
  return (int)(work < maximum ? work : maximum);
}

static int PrepareMapSpritesWorker(void *context) {
  MapSpritePreparationWorker *worker = context;
  MapSpritePreparation *preparation = worker->preparation;
  const MapSpriteRequirements *requirements = &preparation->requirements;
  while (!SDL_AtomicGet(&preparation->cancelled) && !SDL_AtomicGet(&preparation->failed)) {
    int job = SDL_AtomicAdd(&preparation->next, 1);
    if (job >= preparation->total) break;
    bool ok;
    if (job < MAP_BUILDING_PREVIEW_COUNT) {
      ok = PrepareMapBuildingPreview(preparation->cache, &worker->workspace,
          requirements->preview_width, requirements->preview_height, preparation->palette, job);
    } else {
      // Start previews first, then interleave scales instead of leaving the
      // most expensive scale and previews at the end of the queue.
      int map_job = job - MAP_BUILDING_PREVIEW_COUNT;
      int slot = map_job % requirements->count, id = map_job / requirements->count;
      ok = PrepareMapSpriteRange(preparation->cache, &worker->workspace, slot, requirements->scales[slot],
          preparation->palette, id, 1);
    }
    if (!ok) { SDL_AtomicSet(&preparation->failed, 1); break; }
    SDL_AtomicAdd(&preparation->completed, MapSpritePreparationJobWork(preparation, job));
  }
  FreeSpritePreparationWorkspace(&worker->workspace);
  return 0;
}

bool StartMapSpritePreparation(Drawing *drawing, MapSpriteRequirements requirements) {
  MapSpriteCache *cache = &drawing->sprite_cache;
  const int *scales = requirements.scales;
  int count = requirements.count;
  if (cache->preparation || count < 1 || count > MAP_SPRITE_SCALE_COUNT ||
      requirements.preview_width <= 0 || requirements.preview_height <= 0) return false;
  int maximum = CITY_MAP_MAX_TILE_WIDTH * 100 / (8 << CITY_MAP_ZOOM_LARGE);
  for (int i = 0; i < count; ++i) {
    if (scales[i] < 100 || scales[i] > maximum) return false;
    for (int j = 0; j < i; ++j) if (scales[i] == scales[j]) return false;
  }
  MapSpritePreparation *preparation = calloc(1, sizeof(*preparation));
  if (!preparation) return false;
  ReleaseMapSpriteScales(cache);
  cache->pixel_scale = scales[0];
  FreeSpritePreparationWorkspace(&cache->preparation_workspace);
  cache->preview_width = cache->preview_height = 0;
  preparation->cache = cache;
  preparation->requirements = requirements;
  memcpy(preparation->palette, drawing->colors, sizeof(preparation->palette));
  preparation->total = count * MAP_SPRITE_COUNT + MAP_BUILDING_PREVIEW_COUNT;
  for (int job = 0; job < preparation->total; ++job)
    preparation->total_work += MapSpritePreparationJobWork(preparation, job);
  cache->preparation = preparation;
  int workers = IntMin(MAP_SPRITE_PREPARATION_WORKERS, IntMax(1, SDL_GetCPUCount() - 1));
  for (int i = 0; i < workers; ++i) {
    MapSpritePreparationWorker *worker = &preparation->workers[i];
    worker->preparation = preparation;
    worker->thread = SDL_CreateThread(PrepareMapSpritesWorker, "Sprite preparation", worker);
    if (!worker->thread) break;
    ++preparation->worker_count;
  }
  if (preparation->worker_count) return true;
  FinishMapSpritePreparation(cache, true);
  return false;
}

MapSpriteProgress MapSpritePreparationProgress(const MapSpriteCache *cache) {
  MapSpritePreparation *preparation = cache->preparation;
  if (!preparation || SDL_AtomicGet(&preparation->failed)) return (MapSpriteProgress){.completed = -1};
  MapSpriteProgress progress = {.total = preparation->total_work};
  progress.completed = SDL_AtomicGet(&preparation->completed);
  return progress;
}

bool FinishMapSpritePreparation(MapSpriteCache *cache, bool cancel) {
  MapSpritePreparation *preparation = cache->preparation;
  if (!preparation) return true;
  if (cancel) SDL_AtomicSet(&preparation->cancelled, 1);
  for (int i = 0; i < preparation->worker_count; ++i) SDL_WaitThread(preparation->workers[i].thread, NULL);
  bool ok = !cancel && !SDL_AtomicGet(&preparation->failed) &&
      SDL_AtomicGet(&preparation->completed) == preparation->total_work;
  if (ok) {
    const MapSpriteRequirements *requirements = &preparation->requirements;
    memcpy(cache->scales, requirements->scales, requirements->count * sizeof(int));
    cache->preview_width = requirements->preview_width;
    cache->preview_height = requirements->preview_height;
  }
  cache->preparation = NULL;
  free(preparation);
  return ok;
}
