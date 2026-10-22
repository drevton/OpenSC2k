#include "drawing/sprites/sprites.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <SDL_atomic.h>
#include <SDL_cpuinfo.h>
#include <SDL_thread.h>

#include "assets/state.h"
#include "drawing/maps/map_view.h"

// Image encoding, resampling and cache preparation share bounded row workspace.
// Direct run rasterization and shared reference lifetime belong to sprites.c.

// Map Sprite Cache

int InterfaceSpriteLevel(int scale) {
  return scale <= 100 ? 0 : scale <= 200 ? 1 : 2;
}

const RunSprite *GetInterfaceSprite(const CityInterfaceIcons *icons, int sprite, int level, bool *flipped) {
  u16 reference = ResolveSpriteVariant(icons->variants, sprite, level, icons->lookup[sprite]);
  *flipped ^= (reference & CITY_SPRITE_HEADER_FLIPPED) != 0;
  return icons->images[(reference & CITY_SPRITE_HEADER_MASK) - 1];
}

static SpritePixel *MapSpritePreparationPixels(SpritePreparationWorkspace *workspace, size_t count) {
  size_t limit = SIZE_MAX / sizeof(SpritePixel);
  if (count > limit) return NULL;
  if (count > workspace->capacity) {
    size_t capacity = count;
    if (workspace->capacity <= limit / 2 && capacity < workspace->capacity * 2)
      capacity = workspace->capacity * 2;
    SpritePixel *pixels = realloc(workspace->pixels, capacity * sizeof(*pixels));
    if (!pixels) return NULL;
    workspace->pixels = pixels;
    workspace->capacity = capacity;
  }
  return workspace->pixels;
}

static void FreeMapSpritePreparationPixels(SpritePreparationWorkspace *workspace) {
  free(workspace->pixels);
  workspace->pixels = NULL;
  workspace->capacity = 0;
}

bool InstallLandscapeSprite(MapSpriteCache *cache, int id, const RunSprite *sprite, int scale, int baseline,
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

int TileSpriteResourceIndex(const CityResourceState *assets, int sprite, int direction) {
  int index = assets->map_sprite_sources[sprite];
  return (index & ~3) | ((index + direction) & 3);
}

const CityLandscapeSprite *GetTileSpriteSource(const CityResourceState *assets, int sprite, int direction) {
  int index = sprite < assets->map_tile_sprite_count ? TileSpriteResourceIndex(assets, sprite, direction) : sprite;
  return GetResourceSprite(assets, index);
}

// Internal zoom-cache slots stay stable. Their images come from ordinary sprite references.
int LandscapeSpriteResourceIndex(const CityResourceState *assets, int source) {
  int sprite = -1, direction = 0;
  if (source >= CITY_LANDSCAPE_CIVIC_FIRST && source < CITY_LANDSCAPE_POWER_SMOKE_FIRST) {
    int slot = (source - CITY_LANDSCAPE_CIVIC_FIRST) / 4;
    sprite = slot == CITY_CIVIC_KIND_COUNT ? CITY_MAP_TILE_SPRITE_PARK : CITY_CIVIC_KIND_FIRST + slot;
    direction = (source - CITY_LANDSCAPE_CIVIC_FIRST) & 3;
  } else if (source >= CITY_LANDSCAPE_BUILDING_FIRST && source < CITY_LANDSCAPE_CONSTRUCTION_FIRST) {
    sprite = CITY_BUILDING_ART_FIRST + (source - CITY_LANDSCAPE_BUILDING_FIRST) / 4;
    direction = (source - CITY_LANDSCAPE_BUILDING_FIRST) & 3;
  }
  return sprite < 0 ? source : TileSpriteResourceIndex(assets, sprite, direction);
}

// Map code passes an opaque resource index. Header aliases and artwork flips are
// interpreted only here and by the sprite-pack loader, never by tile selection.
const CityLandscapeSprite *GetResourceSprite(const CityResourceState *assets, int index) {
  int header = assets->sprite_index_lookup[index] & CITY_SPRITE_HEADER_MASK;
  return header ? &assets->landscape[header - 1] : NULL;
}

bool InstallResourceSprite(MapSpriteCache *cache, const CityResourceState *assets, int id, int index, int scale) {
  // Cache IDs identify the map level internally; resource IDs stay zoom independent.
  int level = id < CITY_MAP_SPRITE_COUNT ? id / CITY_MAP_SPRITES_PER_ZOOM :
      (id - MAP_LANDSCAPE_SPRITE_FIRST) / MAP_LANDSCAPE_SPRITES_PER_ZOOM;
  u16 reference = ResolveSpriteVariant(assets->sprite_variants, index, level, assets->sprite_index_lookup[index]);
  int source = (reference & CITY_SPRITE_HEADER_MASK) - 1;
  const CityLandscapeSprite *art = &assets->landscape[source];
  bool flipped = ((reference ^ assets->sprite_index_lookup[source]) & CITY_SPRITE_HEADER_FLIPPED) != 0;
  return InstallLandscapeSprite(cache, id, art->sprite, art->scale * scale, art->baseline, art->offset_x, flipped);
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

typedef enum SpriteFilter { SPRITE_FILTER_NEAREST, SPRITE_FILTER_COLORS, SPRITE_FILTER_IMAGE } SpriteFilter;

static RunSprite *ScaleRunSprite(SpritePreparationWorkspace *workspace, const RunSprite *source,
    CityRect content, CityPoint numerator, CityPoint denominator, const u32 *palette, SpriteFilter filter);

int FindMapSpriteScale(const MapSpriteCache *cache, int scale) {
  for (int i = 0; i < MAP_SPRITE_SCALE_COUNT; ++i)
    if (cache->scales[i] == scale) return i;
  return -1;
}

int PrepareMapSpriteScale(MapSpriteCache *cache, int scale, const u32 *palette) {
  return PrepareMapSpriteScaleRange(cache, scale, palette, 0, MAP_SPRITE_COUNT);
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
  u16 reference = ResolveSpriteVariant(assets->sprite_variants, resource, 3, 0);
  if (!reference) return image;
  int header = (reference & CITY_SPRITE_HEADER_MASK) - 1;
  const CityLandscapeSprite *art = &assets->landscape[header];
  return (MapSpriteImage){art->sprite, 100 * art->scale, art->baseline, art->offset_x,
      ((reference ^ assets->sprite_index_lookup[header]) & CITY_SPRITE_HEADER_FLIPPED) != 0};
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
    cache->sprites[slot][id] = ScaleRunSprite(workspace, flipped ? flipped : base,
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
        RunSprite *trimmed = ScaleRunSprite(workspace, sprite, content,
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

int PrepareMapSpriteScaleRange(MapSpriteCache *cache, int scale, const u32 *palette, int first, int count) {
  if (scale < 100 || scale > CITY_MAP_MAX_TILE_WIDTH * 100 / (8 << CITY_MAP_ZOOM_LARGE)) return -1;
  if (first < 0 || first > MAP_SPRITE_COUNT || count < 0 || count > MAP_SPRITE_COUNT - first) return -1;
  int cached = FindMapSpriteScale(cache, scale);
  if (cached >= 0) return cached;
  int slot = 0;
  while (slot < MAP_SPRITE_SCALE_COUNT && cache->scales[slot]) ++slot;
  if (slot == MAP_SPRITE_SCALE_COUNT) slot = 0;
  if (!first) cache->scales[slot] = 0;
  if (!first) for (int id = 0; id < MAP_SPRITE_COUNT; ++id) {
    FreeRunSprite(cache->sprites[slot][id]);
    cache->sprites[slot][id] = NULL;
  }
  if (!PrepareMapSpriteRange(cache, &cache->preparation_workspace, slot, scale, palette, first, count)) return -1;
  if (first + count == MAP_SPRITE_COUNT) cache->scales[slot] = scale;
  return slot;
}

void FreeMapSpriteScales(MapSpriteCache *cache) {
  for (int scale = 0; scale < MAP_SPRITE_SCALE_COUNT; ++scale)
    for (int id = 0; id < MAP_SPRITE_COUNT; ++id) {
      FreeRunSprite(cache->sprites[scale][id]);
      cache->sprites[scale][id] = NULL;
    }
  memset(cache->scales, 0, sizeof(cache->scales));
}

void FreeMapSpriteCache(MapSpriteCache *cache) {
  FinishMapSpritePreparation(cache, true);
  for (int id = 0; id < MAP_SPRITE_COUNT; ++id) FreeRunSprite(cache->base[id]);
  FreeMapSpriteScales(cache);
  for (int kind = 0; kind < CITY_CIVIC_KIND_COUNT; ++kind)
    for (int view = 0; view <= CITY_CIVIC_VIEWS; ++view) FreeRunSprite(cache->building_previews[kind][view]);
  for (int view = 0; view < CITY_CIVIC_VIEWS; ++view) FreeRunSprite(cache->modern_wind_previews[view]);
  FreeMapSpritePreparationPixels(&cache->preparation_workspace);
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
    *prepared = ScaleRunSprite(workspace, source, content, (CityPoint){numerator, numerator},
        (CityPoint){denominator, denominator}, palette, SPRITE_FILTER_NEAREST);
  }
  if (w > 0 && h > 0 && !*prepared) return false;
  return true;
}

bool PrepareMapBuildingPreviews(MapSpriteCache *cache, int width, int height, const u32 *palette) {
  return PrepareMapBuildingPreviewRange(cache, width, height, palette, 0, MAP_BUILDING_PREVIEW_COUNT);
}

static int MapBuildingPreviewSource(int index) {
  if (index < CITY_CIVIC_KIND_COUNT * (CITY_CIVIC_VIEWS + 1)) {
    int kind = index / (CITY_CIVIC_VIEWS + 1), view = index % (CITY_CIVIC_VIEWS + 1);
    if (kind + CITY_CIVIC_KIND_FIRST == CITY_MAP_TILE_SPRITE_WIND_POWER && view < CITY_CIVIC_VIEWS)
      return MAP_LANDSCAPE_SPRITE_FIRST + 2 * MAP_LANDSCAPE_SPRITES_PER_ZOOM +
          CITY_LANDSCAPE_WIND_PREVIEW_FIRST + view - CITY_LANDSCAPE_GROUND_FIRST;
    return view == CITY_CIVIC_VIEWS ? CITY_SPRITE_CATALOG_LARGE_BASE + CITY_CIVIC_KIND_FIRST + kind :
        MAP_LANDSCAPE_SPRITE_FIRST + 2 * MAP_LANDSCAPE_SPRITES_PER_ZOOM +
        CITY_LANDSCAPE_CIVIC_FIRST + kind * CITY_CIVIC_VIEWS + view - CITY_LANDSCAPE_GROUND_FIRST;
  }
  int view = index - CITY_CIVIC_KIND_COUNT * (CITY_CIVIC_VIEWS + 1);
  return MAP_LANDSCAPE_SPRITE_FIRST + 2 * MAP_LANDSCAPE_SPRITES_PER_ZOOM +
      CITY_LANDSCAPE_WIND_PREVIEW_FIRST + CITY_CIVIC_VIEWS + view - CITY_LANDSCAPE_GROUND_FIRST;
}

static bool PrepareMapBuildingPreview(MapSpriteCache *cache, SpritePreparationWorkspace *workspace,
    int width, int height, const u32 *palette, int index) {
  RunSprite **prepared;
  if (index < CITY_CIVIC_KIND_COUNT * (CITY_CIVIC_VIEWS + 1)) {
    int kind = index / (CITY_CIVIC_VIEWS + 1), view = index % (CITY_CIVIC_VIEWS + 1);
    prepared = &cache->building_previews[kind][view];
  } else {
    int view = index - CITY_CIVIC_KIND_COUNT * (CITY_CIVIC_VIEWS + 1);
    prepared = &cache->modern_wind_previews[view];
  }
  return PrepareBuildingPreview(workspace, prepared, cache->base[MapBuildingPreviewSource(index)],
      width, height, palette);
}

bool PrepareMapBuildingPreviewRange(MapSpriteCache *cache, int width, int height, const u32 *palette,
    int first, int count) {
  if (cache->preview_width == width && cache->preview_height == height) {
    FreeMapSpritePreparationPixels(&cache->preparation_workspace);
    return true;
  }
  if (first < 0 || first > MAP_BUILDING_PREVIEW_COUNT || count < 0 || count > MAP_BUILDING_PREVIEW_COUNT - first)
    return false;
  for (int index = first; index < first + count; ++index)
    if (!PrepareMapBuildingPreview(cache, &cache->preparation_workspace, width, height, palette, index)) return false;
  if (first + count == MAP_BUILDING_PREVIEW_COUNT) {
    cache->preview_width = width;
    cache->preview_height = height;
    FreeMapSpritePreparationPixels(&cache->preparation_workspace);
  }
  return true;
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
  int scales[MAP_SPRITE_SCALE_COUNT], scale_count, worker_count;
  int preview_width, preview_height, total, total_work;
  u32 palette[256];
  SDL_atomic_t next, completed, cancelled, failed;
};

static int MapSpritePreparationJobWork(const MapSpritePreparation *preparation, int job) {
  const RunSprite *source;
  u64 work;
  if (job < MAP_BUILDING_PREVIEW_COUNT) {
    source = preparation->cache->base[MapBuildingPreviewSource(job)];
    if (!source) return 1;
    // Preview fitting traverses source runs before sampling the small output.
    work = 1 + source->stream_size / 1024 + source->height +
        (u64)preparation->preview_width * preparation->preview_height / 128;
  } else {
    int map_job = job - MAP_BUILDING_PREVIEW_COUNT;
    int slot = map_job % preparation->scale_count, id = map_job / preparation->scale_count;
    int scale = preparation->scales[slot];
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
  int maximum = INT_MAX / preparation->total;
  return work < maximum ? (int)work : maximum;
}

static int PrepareMapSpritesWorker(void *context) {
  MapSpritePreparationWorker *worker = context;
  MapSpritePreparation *preparation = worker->preparation;
  while (!SDL_AtomicGet(&preparation->cancelled) && !SDL_AtomicGet(&preparation->failed)) {
    int job = SDL_AtomicAdd(&preparation->next, 1);
    if (job >= preparation->total) break;
    bool ok;
    if (job < MAP_BUILDING_PREVIEW_COUNT) {
      ok = PrepareMapBuildingPreview(preparation->cache, &worker->workspace,
          preparation->preview_width, preparation->preview_height, preparation->palette, job);
    } else {
      // Start previews first, then interleave scales instead of leaving the
      // most expensive scale and previews at the end of the queue.
      int map_job = job - MAP_BUILDING_PREVIEW_COUNT;
      int slot = map_job % preparation->scale_count, id = map_job / preparation->scale_count;
      ok = PrepareMapSpriteRange(preparation->cache, &worker->workspace, slot, preparation->scales[slot],
          preparation->palette, id, 1);
    }
    if (!ok) { SDL_AtomicSet(&preparation->failed, 1); break; }
    SDL_AtomicAdd(&preparation->completed, MapSpritePreparationJobWork(preparation, job));
  }
  FreeMapSpritePreparationPixels(&worker->workspace);
  return 0;
}

bool StartMapSpritePreparation(MapSpriteCache *cache, const int *scales, int count,
    int preview_width, int preview_height, const u32 *palette) {
  if (cache->preparation || count < 1 || count > MAP_SPRITE_SCALE_COUNT ||
      preview_width <= 0 || preview_height <= 0) return false;
  int maximum = CITY_MAP_MAX_TILE_WIDTH * 100 / (8 << CITY_MAP_ZOOM_LARGE);
  for (int i = 0; i < count; ++i) {
    if (scales[i] < 100 || scales[i] > maximum) return false;
    for (int j = 0; j < i; ++j) if (scales[i] == scales[j]) return false;
  }
  MapSpritePreparation *preparation = calloc(1, sizeof(*preparation));
  if (!preparation) return false;
  FreeMapSpriteScales(cache);
  cache->pixel_scale = scales[0];
  FreeMapSpritePreparationPixels(&cache->preparation_workspace);
  cache->preview_width = cache->preview_height = 0;
  preparation->cache = cache;
  preparation->scale_count = count;
  memcpy(preparation->scales, scales, count * sizeof(*scales));
  memcpy(preparation->palette, palette, sizeof(preparation->palette));
  preparation->preview_width = preview_width;
  preparation->preview_height = preview_height;
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

int MapSpritePreparationProgress(const MapSpriteCache *cache, int *total) {
  MapSpritePreparation *preparation = cache->preparation;
  if (!preparation || SDL_AtomicGet(&preparation->failed)) return -1;
  *total = preparation->total_work;
  return SDL_AtomicGet(&preparation->completed);
}

bool FinishMapSpritePreparation(MapSpriteCache *cache, bool cancel) {
  MapSpritePreparation *preparation = cache->preparation;
  if (!preparation) return true;
  if (cancel) SDL_AtomicSet(&preparation->cancelled, 1);
  for (int i = 0; i < preparation->worker_count; ++i) SDL_WaitThread(preparation->workers[i].thread, NULL);
  bool ok = !cancel && !SDL_AtomicGet(&preparation->failed) &&
      SDL_AtomicGet(&preparation->completed) == preparation->total_work;
  if (ok) {
    memcpy(cache->scales, preparation->scales, preparation->scale_count * sizeof(int));
    cache->preview_width = preparation->preview_width;
    cache->preview_height = preparation->preview_height;
  }
  cache->preparation = NULL;
  free(preparation);
  return ok;
}

static SpriteRunKind PixelKind(SpritePixel pixel) {
  int alpha = pixel.rgba >> 24;
  if (!alpha) return SPRITE_RUN_TRANSPARENT;
  if (SpriteRunIndexed(pixel.kind)) {
    if (SpriteRunAnimated(pixel.kind)) return alpha == 255 ? SPRITE_RUN_ANIMATED : SPRITE_RUN_ANIMATED_ALPHA;
    return alpha == 255 ? SPRITE_RUN_PALETTE : SPRITE_RUN_PALETTE_ALPHA;
  }
  return alpha == 255 ? SPRITE_RUN_RGB : SPRITE_RUN_RGBA;
}

static int SpriteRunHeaderSize(int count) { return count <= 31 ? 1 : count <= 286 ? 2 : 4; }

static u8 *WriteSpriteRun(u8 *destination, int kind, int count) {
  *destination++ = kind << 5 | IntMin(count - 1, 31);
  if (count >= 32) {
    *destination++ = count <= 286 ? count - 32 : 255;
    if (count > 286) { *destination++ = count; *destination++ = count >> 8; }
  }
  return destination;
}

// Nearest-neighbor expansion happens once per cached scale. The same row
// encoder sizes and writes the stream, avoiding divergent length decisions.
static size_t EncodeSpriteRowRange(const SpritePixel *row, int first, int last,
    int numerator, int denominator, u8 *destination) {
  while (first < last && PixelKind(row[(i64)first * denominator / numerator]) == SPRITE_RUN_TRANSPARENT) ++first;
  while (last > first && PixelKind(row[(i64)(last - 1) * denominator / numerator]) == SPRITE_RUN_TRANSPARENT) --last;
  if (first == last) return 0;
  size_t size = first < 255 ? 1 : 3;
  if (destination) {
    *destination++ = IntMin(first, 255);
    if (first >= 255) { *destination++ = first; *destination++ = first >> 8; }
  }
  for (int x = first; x < last;) {
    SpriteRunKind kind = PixelKind(row[(i64)x * denominator / numerator]);
    int next = x + 1, limit = IntMin(last, x + SPRITE_RUN_LIMIT);
    while (next < limit && PixelKind(row[(i64)next * denominator / numerator]) == kind) ++next;
    size += SpriteRunHeaderSize(next - x) + (size_t)(next - x) * SpriteRunPayloadBytes(kind);
    if (destination) {
      destination = WriteSpriteRun(destination, kind, next - x);
      if (kind != SPRITE_RUN_TRANSPARENT) for (int i = x; i < next; ++i) {
        SpritePixel pixel = row[(i64)i * denominator / numerator];
        if (SpriteRunIndexed(kind)) *destination++ = pixel.index;
        else {
          *destination++ = pixel.rgba >> 16;
          *destination++ = pixel.rgba >> 8;
          *destination++ = pixel.rgba;
        }
        if (SpriteRunHasAlpha(kind)) *destination++ = pixel.rgba >> 24;
      }
    }
    x = next;
  }
  return size;
}

static size_t EncodeSpriteRow(const SpritePixel *row, int width, int numerator, int denominator, u8 *destination) {
  return EncodeSpriteRowRange(row, 0, width, numerator, denominator, destination);
}

// Sample directly from the packed source. Downscaling skips unused source rows
// and pixels, and needs only one output row rather than a full decoded image.
static void SampleSpriteRow(const RunSprite *sprite, int y, int left, SpritePixel *row, int width,
    int numerator, int denominator, const u32 *palette, int *first_pixel, int *last_pixel) {
  if (sprite->palette) palette = sprite->palette->colors;
  *first_pixel = *last_pixel = 0;
  if (y < 0 || y >= sprite->height) return;
  const u32 *rows = RunSpriteRows(sprite);
  const u8 *data = RunSpriteData(sprite), *at = data + rows[y], *end = data + rows[y + 1];
  if (at == end) return;
  int source_x = ReadSpriteInitialSkip(&at) - left, x = 0;
  int step = denominator / numerator, fraction = denominator % numerator;
  *first_pixel = IntMax(0, IntMin(width,
      (int)(((i64)source_x * numerator + denominator - 1) / denominator)));
  while (at < end) {
    int kind, count = ReadSpriteRun(&at, &kind), bytes = SpriteRunPayloadBytes(kind);
    int first = IntMax(0, IntMin(width,
        (int)(((i64)source_x * numerator + denominator - 1) / denominator)));
    int last = IntMax(0, IntMin(width,
        (int)(((i64)(source_x + count) * numerator + denominator - 1) / denominator)));
    x = IntMax(x, first);
    if (kind == SPRITE_RUN_TRANSPARENT) {
      memset(row + x, 0, (size_t)(last - x) * sizeof(*row));
      x = last;
    } else {
      int sample = (int)((i64)x * denominator / numerator) - source_x;
      i64 phase = (i64)x * denominator % numerator;
      for (; x < last; ++x) {
        const u8 *pixel = at + (size_t)sample * bytes;
        row[x].kind = kind;
        if (SpriteRunIndexed(kind)) {
          row[x].index = pixel[0];
          row[x].rgba = (SpriteRunAnimated(kind) ? 0 : palette[pixel[0]]) & 0xFFFFFF;
          row[x].rgba |= (u32)(SpriteRunHasAlpha(kind) ? pixel[1] : 255) << 24;
        } else {
          row[x].rgba = (pixel[0] << 16) | (pixel[1] << 8) | pixel[2];
          row[x].rgba |= kind == SPRITE_RUN_RGBA ? (u32)pixel[3] << 24 : 0xFF000000;
        }
        sample += step;
        phase += fraction;
        if (phase >= numerator) { ++sample; phase -= numerator; }
      }
    }
    if (x == width) break;
    source_x += count;
    at += (size_t)count * bytes;
  }
  *last_pixel = x;
}

// Interpolate fixed image colors or zoning color/opacity within the nearest
// sample's footprint. Transparent neighbors never expand the sprite boundary.
static void SmoothSpriteRowColors(const RunSprite *source, SpritePixel *row, SpritePixel *samples,
    CityRect content, int y, CityPoint numerator, CityPoint denominator, const u32 *palette,
    int first, int last, bool image) {
  int width = content.right - content.left, height = content.bottom - content.top;
  int sy = IntClamp((int)((i64)(2 * y + 1) * denominator.y * 128 / numerator.y - 128), 0, (height - 1) * 256);
  int y0 = sy >> 8, fy = sy & 255;
  for (int side = 0; side < 2; ++side) {
    SpritePixel *sample = samples + side * width;
    memset(sample, 0, (size_t)width * sizeof(*sample));
    int begin, end;
    SampleSpriteRow(source, content.top + IntMin(y0 + side, height - 1), content.left,
        sample, width, 1, 1, palette, &begin, &end);
  }
  for (int x = first; x < last; ++x) {
    if (image && SpriteRunIndexed(row[x].kind) && !SpriteRunAnimated(row[x].kind))
      row[x].kind = SpriteRunHasAlpha(row[x].kind) ? SPRITE_RUN_RGBA : SPRITE_RUN_RGB;
    if (row[x].kind != SPRITE_RUN_RGB && row[x].kind != SPRITE_RUN_RGBA) continue;
    int sx = IntClamp((int)((i64)(2 * x + 1) * denominator.x * 128 / numerator.x - 128), 0, (width - 1) * 256);
    int x0 = sx >> 8, fx = sx & 255;
    u32 red = 0, green = 0, blue = 0, alpha = 0, total = 0;
    for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
      SpritePixel pixel = samples[dy * width + IntMin(x0 + dx, width - 1)];
      int weight = (dx ? fx : 256 - fx) * (dy ? fy : 256 - fy);
      if (image) total += weight;
      if (pixel.kind != SPRITE_RUN_RGB && pixel.kind != SPRITE_RUN_RGBA &&
          !(image && SpriteRunIndexed(pixel.kind) && !SpriteRunAnimated(pixel.kind))) continue;
      int color_weight = image ? weight * (pixel.rgba >> 24) : weight;
      red += ((pixel.rgba >> 16) & 255) * color_weight;
      green += ((pixel.rgba >> 8) & 255) * color_weight;
      blue += (pixel.rgba & 255) * color_weight;
      alpha += (pixel.rgba >> 24) * weight;
      if (!image) total += weight;
    }
    // Interpolate image colors with premultiplied weights, then encode straight
    // alpha again. Invisible RGB must not darken antialiased icon and text edges.
    u32 colors = image ? alpha : total;
    if (total && colors) row[x].rgba = ((alpha + total / 2) / total) << 24 |
        ((red + colors / 2) / colors) << 16 | ((green + colors / 2) / colors) << 8 | (blue + colors / 2) / colors;
    else if (image) row[x].rgba = 0;
  }
}

static RunSprite *ScaleRunSprite(SpritePreparationWorkspace *workspace, const RunSprite *source,
    CityRect content, CityPoint numerator, CityPoint denominator, const u32 *palette, SpriteFilter filter) {
  bool smooth_colors = filter != SPRITE_FILTER_NEAREST;
  if (numerator.x == denominator.x && numerator.y == denominator.y && !content.left && !content.top &&
      content.right == source->width && content.bottom == source->height) {
    return RetainRunSprite(source);
  }
  i64 width = ((i64)(content.right - content.left) * numerator.x + denominator.x - 1) / denominator.x;
  i64 height = ((i64)(content.bottom - content.top) * numerator.y + denominator.y - 1) / denominator.y;
  if (width > 65535 || height > INT_MAX) return NULL;
  size_t sample_count = smooth_colors ? 2 * (size_t)(content.right - content.left) : 0;
  SpritePixel *row = MapSpritePreparationPixels(workspace, (size_t)width + sample_count);
  if (!row) return NULL;
  SpritePixel *samples = row + width;
  RunSprite *sprite = NULL;
  const u32 *source_rows = RunSpriteRows(source);
  const u8 *source_data = RunSpriteData(source);
  u64 stream_size = 0;
  for (int pass = 0; pass < 2; ++pass) {
    u32 *rows = sprite ? (u32 *)(sprite + 1) : NULL;
    u8 *data = rows ? (u8 *)(rows + height + 1) : NULL;
    size_t row_size = 0;
    u64 offset = 0;
    int previous_y = -1;
    for (int y = 0; y < height; ++y) {
      int source_y = content.top + (int)((i64)y * denominator.y / numerator.y);
      if (rows) rows[y] = (u32)offset;
      size_t source_size = source_y >= 0 && source_y < source->height ?
          source_rows[source_y + 1] - source_rows[source_y] : 0;
      bool repeated = !smooth_colors && source_size && previous_y >= 0 && previous_y < source->height &&
          (source_y == previous_y ||
          (source_size == source_rows[previous_y + 1] - source_rows[previous_y] &&
          !memcmp(source_data + source_rows[source_y], source_data + source_rows[previous_y], source_size)));
      if (!source_size) {
        row_size = 0;
      } else if (!repeated) {
        int first, last;
        SampleSpriteRow(source, source_y, content.left, row, (int)width,
            numerator.x, denominator.x, palette, &first, &last);
        if (smooth_colors && first < last)
          SmoothSpriteRowColors(source, row, samples, content, y, numerator, denominator, palette, first, last,
              filter == SPRITE_FILTER_IMAGE);
        // Initial skips and omitted trailing pixels already describe the transparent
        // margins. Neither clear nor scan those pixels in the temporary row.
        row_size = EncodeSpriteRowRange(row, first, last, 1, 1, data ? data + offset : NULL);
      } else if (data) {
        memcpy(data + offset, data + rows[y - 1], row_size);
      }
      previous_y = source_y;
      offset += row_size;
      if (offset > UINT32_MAX) { FreeRunSprite(sprite); return NULL; }
    }
    if (!pass) {
      stream_size = offset;
      u64 allocation = sizeof(*sprite) + (height + 1) * sizeof(u32) + stream_size;
      if (allocation > SIZE_MAX) return NULL;
      sprite = malloc((size_t)allocation);
      if (!sprite) return NULL;
      *sprite = (RunSprite){.width = (int)width, .height = (int)height, .stream_size = (u32)stream_size,
          .references = 1, .palette = source->palette, .mirrored = source->mirrored};
      RetainSpritePalette(sprite->palette);
    } else rows[height] = (u32)offset;
  }
  return sprite;
}

// Prepare a fixed image at its final pixel dimensions through the shared resampler.
// Only row workspace is temporary; the returned normal sprite owns one reference.
RunSprite *ResizeRunSprite(const RunSprite *source, int width, int height) {
  return ResizeRunSpriteRegion(source, (CityRect){0, 0, source->width, source->height}, width, height);
}

RunSprite *ResizeRunSpriteRegion(const RunSprite *source, CityRect content, int width, int height) {
  assert(source && width > 0 && height > 0 && content.left >= 0 && content.top >= 0 &&
      content.left < content.right && content.top < content.bottom);
  SpritePreparationWorkspace workspace = {0};
  RunSprite *sprite = ScaleRunSprite(&workspace, source, content,
      (CityPoint){width, height}, (CityPoint){content.right - content.left, content.bottom - content.top},
      NULL, SPRITE_FILTER_IMAGE);
  FreeMapSpritePreparationPixels(&workspace);
  return sprite;
}

RunSprite *EncodeRunSpriteRatio(const SpritePixel *pixels, int width, int height, int stride,
    int numerator, int denominator) {
  if (!pixels || width <= 0 || height <= 0 || stride < width || numerator <= 0 || denominator <= 0) return NULL;
  i64 w = ((i64)width * numerator + denominator - 1) / denominator;
  i64 h = ((i64)height * numerator + denominator - 1) / denominator;
  // The initial skip and extended run lengths are u16.
  if (w > 65535 || h > INT_MAX) return NULL;
  int output_width = (int)w, output_height = (int)h;
  u64 stream_size = 0;
  size_t row_size = 0;
  for (int y = 0; y < output_height; ++y) {
    if (!y || (i64)y * denominator / numerator != (i64)(y - 1) * denominator / numerator)
      row_size = EncodeSpriteRow(pixels + (size_t)((i64)y * denominator / numerator) * stride,
          output_width, numerator, denominator, NULL);
    stream_size += row_size;
    if (stream_size > UINT32_MAX) return NULL;
  }
  u64 allocation = sizeof(RunSprite) + ((u64)output_height + 1) * sizeof(u32) + stream_size;
  if (allocation > SIZE_MAX) return NULL;
  RunSprite *sprite = malloc((size_t)allocation);
  if (!sprite) return NULL;
  *sprite = (RunSprite){.width = output_width, .height = output_height,
      .stream_size = (u32)stream_size, .references = 1};
  u32 *rows = (u32 *)(sprite + 1);
  u8 *data = (u8 *)(rows + output_height + 1), *destination = data;
  for (int y = 0; y < output_height; ++y) {
    rows[y] = (u32)(destination - data);
    if (y && (i64)y * denominator / numerator == (i64)(y - 1) * denominator / numerator) {
      size_t size = rows[y] - rows[y - 1];
      memcpy(destination, data + rows[y - 1], size);
      destination += size;
    } else destination += EncodeSpriteRow(pixels + (size_t)((i64)y * denominator / numerator) * stride,
        output_width, numerator, denominator, destination);
  }
  rows[output_height] = (u32)(destination - data);
  return sprite;
}

RunSprite *EncodeRunSprite(const SpritePixel *pixels, int width, int height, int stride, int scale) {
  return EncodeRunSpriteRatio(pixels, width, height, stride, scale, 1);
}
// Encode generated straight-alpha pixels without keeping a second decoded image.
RunSprite *EncodeRgbaRunSprite(const u32 *pixels, int width, int height, int stride) {
  assert(pixels && width > 0 && width <= SPRITE_RUN_LIMIT && height > 0 && stride >= width);
  SpritePixel *row = calloc(width, sizeof(*row));
  if (!row) return NULL;
  RunSprite *sprite = NULL;
  for (int pass = 0; pass < 2; ++pass) {
    u32 *rows = sprite ? (u32 *)(sprite + 1) : NULL;
    u8 *data = rows ? (u8 *)(rows + height + 1) : NULL;
    u64 offset = 0;
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) row[x].rgba = pixels[(size_t)y * stride + x];
      if (rows) rows[y] = offset;
      offset += EncodeSpriteRow(row, width, 1, 1, data ? data + offset : NULL);
      if (offset > UINT32_MAX) { FreeRunSprite(sprite); free(row); return NULL; }
    }
    if (!pass) {
      u64 allocation = sizeof(*sprite) + ((u64)height + 1) * sizeof(u32) + offset;
      sprite = allocation <= SIZE_MAX ? malloc(allocation) : NULL;
      if (!sprite) { free(row); return NULL; }
      *sprite = (RunSprite){.width = width, .height = height, .stream_size = offset, .references = 1};
    } else rows[height] = offset;
  }
  free(row);
  return sprite;
}

// SDL needs straight-alpha pixels rather than framebuffer palette identities.
// Scaling stays in ResizeRunSprite; this boundary only exports already prepared rows.
bool CopyRunSpriteRgba(const RunSprite *source, u32 *pixels, int stride,
    const u32 *palette, const u32 *animation_palette) {
  assert(source && pixels && stride >= source->width);
  SpritePixel *row = calloc(source->width, sizeof(*row));
  if (!row) return false;
  for (int y = 0; y < source->height; ++y) {
    memset(row, 0, source->width * sizeof(*row));
    int first, last;
    SampleSpriteRow(source, y, 0, row, source->width, 1, 1, palette, &first, &last);
    for (int x = 0; x < source->width; ++x) {
      u32 color = row[x].rgba;
      if (SpriteRunAnimated(row[x].kind))
        color = (color & 0xFF000000) | (animation_palette[row[x].index] & 0xFFFFFF);
      pixels[(size_t)y * stride + (source->mirrored ? source->width - 1 - x : x)] = color;
    }
  }
  free(row);
  return true;
}

// Stream resized full-screen images through the same row sampler and ordinary run drawer.
bool DrawScaledRunSprite(SpriteSurface *surface, const RunSprite *source, CityRect bounds, u32 flags) {
  int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
  CityRect clip = IntersectCityRects(surface->canvas.clip, bounds);
  clip = IntersectCityRects(clip, (CityRect){0, 0, surface->canvas.width, surface->canvas.height});
  if (clip.left >= clip.right || clip.top >= clip.bottom) return true;
  if (width > SPRITE_RUN_LIMIT) return false;
  if (width == source->width && height == source->height) {
    DrawRunSprite(surface, source, bounds.left, bounds.top, flags, NULL);
    return true;
  }
  SpritePreparationWorkspace workspace = {0};
  SpritePixel *row = MapSpritePreparationPixels(&workspace, (size_t)width + 2 * source->width);
  RunSprite *line = malloc(sizeof(*line) + 2 * sizeof(u32) + (size_t)width * 6 + 3);
  if (!row || !line) { free(line); FreeMapSpritePreparationPixels(&workspace); return false; }
  *line = (RunSprite){.width = width, .height = 1, .references = 1,
      .palette = RetainSpritePalette(source->palette), .mirrored = source->mirrored};
  u32 *rows = (u32 *)(line + 1);
  u8 *data = (u8 *)(rows + 2);
  rows[0] = 0;
  CityRect content = {0, 0, source->width, source->height};
  CityPoint numerator = {width, height}, denominator = {source->width, source->height};
  for (int y = clip.top; y < clip.bottom; ++y) {
    int first, last, relative_y = y - bounds.top;
    SampleSpriteRow(source, (i64)relative_y * source->height / height, 0, row, width,
        width, source->width, source->palette ? source->palette->colors : surface->palette, &first, &last);
    if (first == last) continue;
    SmoothSpriteRowColors(source, row, row + width, content, relative_y, numerator, denominator,
        surface->palette, first, last, true);
    line->stream_size = rows[1] = EncodeSpriteRowRange(row, first, last, 1, 1, data);
    DrawRunSprite(surface, line, bounds.left, y, flags, NULL);
  }
  FreeRunSprite(line);
  FreeMapSpritePreparationPixels(&workspace);
  return true;
}
