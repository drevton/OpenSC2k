#include "assets/graphics.h"
#include "drawing/maps/map_view.h"
#include "drawing/sprites/sprites.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <SDL_atomic.h>
#include <SDL_cpuinfo.h>
#include <SDL_thread.h>

#if defined(__aarch64__) && defined(__ARM_NEON) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#include <arm_neon.h>
#endif

#include "assets/state.h"
#include "base/byte_order.h"
#include "drawing/raster/screen_damage.h"

// Map Sprite Cache

bool ValidateSpriteVariants(SpriteVariants variants, int sprite_count, int header_count) {
  int previous = -1;
  for (u32 at = 0; at < variants.size;) {
    if (variants.size - at < 4) return false;
    const u8 *record = variants.data + at;
    int sprite = ReadLE16(record), mask = record[2];
    if (sprite <= previous || sprite >= sprite_count || !mask || (mask & ~15) || record[3]) return false;
    previous = sprite;
    at += 4;
    for (int level = 0; level < 4; ++level) {
      if (!(mask & (1 << level))) continue;
      if (variants.size - at < 2) return false;
      int header = ReadLE16(variants.data + at) & CITY_SPRITE_HEADER_MASK;
      if (!header || header > header_count) return false;
      at += 2;
    }
  }
  return true;
}

u16 ResolveSpriteVariant(SpriteVariants variants, int sprite, int level, u16 fallback) {
  // Preparation only. Painting uses the selected, prescaled image directly.
  for (u32 at = 0; at < variants.size;) {
    const u8 *record = variants.data + at;
    int id = ReadLE16(record), mask = record[2];
    if (id > sprite) break;
    at += 4;
    for (int bit = 0; bit < 4; ++bit) {
      if (!(mask & (1 << bit))) continue;
      if (id == sprite && bit == level) return ReadLE16(variants.data + at);
      at += 2;
    }
    if (id == sprite) break;
  }
  return fallback;
}

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
  int level = id < CITY_MAP_SPRITE_COUNT ? id / 500 :
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

static int MapSpriteScaleSource(const MapSpriteCache *cache, int id, int *divisor) {
  int source = id, zoom = id / 500;
  if (id >= MAP_LANDSCAPE_SPRITE_FIRST) {
    *divisor = 100 * cache->base_scales[source];
  } else if (id >= MAP_SLOPED_WATER_SPRITE_FIRST) {
    *divisor = 100 * cache->base_scales[source];
  } else if (id >= MAP_DEMOLITION_SPRITE_FIRST) {
    zoom = (id - MAP_DEMOLITION_SPRITE_FIRST) / CITY_DEMOLITION_FRAME_COUNT;
    source = MAP_DEMOLITION_SPRITE_FIRST + 2 * CITY_DEMOLITION_FRAME_COUNT +
        (id - MAP_DEMOLITION_SPRITE_FIRST) % CITY_DEMOLITION_FRAME_COUNT;
    *divisor = 100 * (1 << (2 - zoom)) * cache->base_scales[source];
  } else if (id >= MAP_POINT_SPRITE_FIRST) {
    *divisor = 100 * cache->base_scales[source];
  } else if (id >= MAP_SELECTION_SPRITE_FIRST) {
    zoom = (id - MAP_SELECTION_SPRITE_FIRST) / MAP_SELECTION_SPRITE_SHAPES;
    // Enlarge the normal OpenTTD selector with the tile. Extra-zoom selectors
    // retain a narrow border instead of scaling its original pixel thickness.
    source = MAP_SELECTION_SPRITE_FIRST +
        (id - MAP_SELECTION_SPRITE_FIRST) % MAP_SELECTION_SPRITE_SHAPES;
    *divisor = 100 * (8 >> zoom);
  } else {
    // An authored variant at this level takes precedence over generated levels.
    for (int higher = 2; !cache->base[source] && higher > zoom; --higher)
      if (cache->base[higher * 500 + id % 500]) { source = higher * 500 + id % 500; break; }
    *divisor = 100 * (1 << (source / 500 - zoom)) * cache->base_scales[source];
  }
  return source;
}

typedef struct {
  const RunSprite *image;
  int divisor, baseline, offset_x;
  bool flipped;
} MapSpriteImage;

static MapSpriteImage SelectMapSpriteImage(const MapSpriteCache *cache, int id, int scale) {
  int divisor, source = MapSpriteScaleSource(cache, id, &divisor);
  MapSpriteImage image = {cache->base[source], divisor, cache->base_baselines[source],
      cache->base_offsets_x[source], false};
  const CityResourceState *assets = cache->assets;
  if (!assets || scale != cache->pixel_scale * 2) return image;
  int resource;
  if (id >= 1000 && id < CITY_MAP_SPRITE_COUNT) {
    int sprite = id - 1000;
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
    cache->sprites[slot][id] = ScaleRunSprite(workspace, flipped ? flipped : base,
        (CityRect){0, 0, base->width, base->height}, (CityPoint){scale, scale}, (CityPoint){divisor, divisor}, palette,
        id < CITY_MAP_SPRITE_COUNT && id % 500 >= 291 && id % 500 <= 304 && id % 500 != 303 ?
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
    return view == CITY_CIVIC_VIEWS ? 1000 + CITY_CIVIC_KIND_FIRST + kind :
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

// Run Sprite

const u32 *RunSpriteRows(const RunSprite *sprite) {
  return (const u32 *)((sprite->image ? sprite->image : sprite) + 1);
}
const u8 *RunSpriteData(const RunSprite *sprite) {
  return (const u8 *)(RunSpriteRows(sprite) + sprite->height + 1);
}

bool ValidateRunSprite(const RunSprite *sprite) {
  if (sprite->width <= 0 || sprite->height <= 0) return false;
  const u32 *rows = RunSpriteRows(sprite);
  const u8 *data = RunSpriteData(sprite);
  if (rows[0] || rows[sprite->height] != sprite->stream_size) return false;
  for (int y = 0; y < sprite->height; ++y) {
    if (rows[y] > rows[y + 1] || rows[y + 1] > sprite->stream_size) return false;
    const u8 *at = data + rows[y], *end = data + rows[y + 1];
    if (at == end) continue;
    if (*at == 255 && end - at < 3) return false;
    int x = ReadSpriteInitialSkip(&at);
    if (x >= sprite->width || at == end) return false;
    bool first = true;
    while (at < end) {
      int header = *at++, kind = header >> 5, length = (header & 31) + 1;
      if (kind == SPRITE_RUN_RESERVED || (first && kind == SPRITE_RUN_TRANSPARENT)) return false;
      first = false;
      if (length == 32) {
        if (at == end) return false;
        int extension = *at++;
        length = 32 + extension;
        if (extension == 255) {
          if (end - at < 2) return false;
          length = at[0] | at[1] << 8;
          at += 2;
          if (length < 287) return false;
        }
      }
      int bytes = SpriteRunPayloadBytes(kind);
      if (length > sprite->width - x || (size_t)(end - at) < (size_t)length * bytes) return false;
      if (SpriteRunAnimated(kind))
        for (int i = 0; i < length; ++i)
          if (!at[i * bytes] || at[i * bytes] >= SPRITE_ANIMATION_COLORS) return false;
      at += length * bytes;
      x += length;
      if (at == end && kind == SPRITE_RUN_TRANSPARENT) return false;
    }
  }
  return true;
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
  *sprite = (RunSprite){.width = output_width, .height = output_height, .stream_size = (u32)stream_size, .references = 1};
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

SpritePalette *RetainSpritePalette(SpritePalette *palette) {
  if (palette) atomic_fetch_add_explicit(&palette->references, 1, memory_order_relaxed);
  return palette;
}

void FreeSpritePalette(SpritePalette *palette) {
  if (palette && atomic_fetch_sub_explicit(&palette->references, 1, memory_order_acq_rel) == 1) free(palette);
}

RunSprite *RetainRunSprite(const RunSprite *sprite) {
  RunSprite *owned = (RunSprite *)sprite;
  if (owned) atomic_fetch_add_explicit(&owned->references, 1, memory_order_relaxed);
  return owned;
}

RunSprite *ReferenceRunSprite(const RunSprite *sprite, SpritePalette *palette, bool mirrored) {
  if (!mirrored && palette == sprite->palette) return RetainRunSprite(sprite);
  RunSprite *reference = malloc(sizeof(*reference));
  if (!reference) return NULL;
  *reference = (RunSprite){.width = sprite->width, .height = sprite->height,
      .stream_size = sprite->stream_size, .references = 1,
      .image = RetainRunSprite(sprite->image ? sprite->image : sprite),
      .palette = palette, .mirrored = sprite->mirrored ^ mirrored};
  RetainSpritePalette(reference->palette);
  return reference;
}

void FreeRunSprite(RunSprite *sprite) {
  if (!sprite || atomic_fetch_sub_explicit(&sprite->references, 1, memory_order_acq_rel) != 1) return;
  FreeRunSprite((RunSprite *)sprite->image);
  FreeSpritePalette(sprite->palette);
  free(sprite);
}

static u32 SurfaceIndexedColor(const SpriteSurface *surface, int kind, int index) {
  if (SpriteRunAnimated(kind))
    return ((u32)SpriteAnimationSource(index) << 24) | (surface->animation_palette[index] & 0xFFFFFF);
  /* The unused display byte retains source identity for selection/water masks.
   * Only compact animation IDs are stored in the separate animation plane. */
  return (surface->image_palette ? 0 : (u32)index << 24) | (surface->palette[index] & 0xFFFFFF);
}

#if defined(__aarch64__) && defined(__ARM_NEON) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
static uint32x4_t PaletteIndexBuckets(uint32x4_t indices) {
  uint32x4_t shifts = vandq_u32(indices, vdupq_n_u32(31));
  return vandq_u32(vshlq_u32(vdupq_n_u32(1), vreinterpretq_s32_u32(shifts)),
      vcgtq_u32(indices, vdupq_n_u32(0)));
}

static void AccumulatePaletteBuckets(uint8x16x4_t *buckets, uint8x16_t indices) {
  uint8x16_t bits = vandq_u8(vshlq_u8(vdupq_n_u8(1),
      vreinterpretq_s8_u8(vandq_u8(indices, vdupq_n_u8(7)))), vcgtq_u8(indices, vdupq_n_u8(0)));
  uint8x16_t groups = vandq_u8(indices, vdupq_n_u8(24));
  for (int group = 0; group < 4; ++group)
    buckets->val[group] = vorrq_u8(buckets->val[group],
        vandq_u8(bits, vceqq_u8(groups, vdupq_n_u8(group * 8))));
}

static u32 CombinePaletteBuckets(uint8x16x4_t buckets) {
  u32 result = 0;
  for (int group = 0; group < 4; ++group) {
    uint8x8_t bytes = vorr_u8(vget_low_u8(buckets.val[group]), vget_high_u8(buckets.val[group]));
    u64 combined = vget_lane_u64(vreinterpret_u64_u8(bytes), 0);
    combined |= combined >> 32;
    combined |= combined >> 16;
    combined |= combined >> 8;
    result |= (combined & 255) << (group * 8);
  }
  return result;
}

static u32 CombineWidePaletteBuckets(uint32x4_t buckets) {
  uint32x2_t halves = vorr_u32(vget_low_u32(buckets), vget_high_u32(buckets));
  return vget_lane_u32(halves, 0) | vget_lane_u32(halves, 1);
}

static uint8x16_t ReverseSpriteBytes(uint8x16_t bytes) {
  uint8x16_t reversed = vrev64q_u8(bytes);
  return vextq_u8(reversed, reversed, 8);
}

static uint32x4_t ReverseSpriteWords(uint32x4_t words) {
  uint32x4_t reversed = vrev64q_u32(words);
  return vextq_u32(reversed, reversed, 2);
}

static uint8x8_t BlendSpriteChannel8(uint8x8_t source, uint8x8_t destination,
    uint8x8_t alpha, uint8x8_t inverse) {
  uint16x8_t sum = vmlal_u8(vmull_u8(source, alpha), destination, inverse);
  // For sums through 65025, this exactly equals (sum + 127) / 255.
  sum = vaddq_u16(sum, vdupq_n_u16(128));
  return vshrn_n_u16(vaddq_u16(sum, vshrq_n_u16(sum, 8)), 8);
}

static uint8x16_t BlendSpriteChannel(uint8x16_t source, uint8x16_t destination,
    uint8x16_t alpha, uint8x16_t inverse) {
  return vcombine_u8(BlendSpriteChannel8(vget_low_u8(source), vget_low_u8(destination),
      vget_low_u8(alpha), vget_low_u8(inverse)), BlendSpriteChannel8(vget_high_u8(source),
      vget_high_u8(destination), vget_high_u8(alpha), vget_high_u8(inverse)));
}

#endif

static u32 BlendRGB(u32 source, u32 destination, int alpha) {
  int inverse = 255 - alpha;
  // Red and blue occupy separate 16-bit lanes. Each weighted sum fits u16,
  // so their multiplication and exact rounded division can share u32 work.
  u32 rb = (source & 0xFF00FF) * alpha + (destination & 0xFF00FF) * inverse + 0x800080;
  rb = (rb + ((rb >> 8) & 0xFF00FF)) >> 8 & 0xFF00FF;
  u32 green = (source >> 8 & 255) * alpha + (destination >> 8 & 255) * inverse + 128;
  green = (green + (green >> 8)) >> 8;
  return rb | green << 8;
}

static inline void CopyOpaquePaletteRun(u32 *destination, const u8 *payload, const u32 *palette,
    int length, bool flipped, u32 mask) {
  int i = 0;
  if (flipped) {
    for (; i + 4 <= length; i += 4) {
      u32 first = palette[payload[i]] & mask, second = palette[payload[i + 1]] & mask;
      u32 third = palette[payload[i + 2]] & mask, fourth = palette[payload[i + 3]] & mask;
      destination[-i] = first;
      destination[-i - 1] = second;
      destination[-i - 2] = third;
      destination[-i - 3] = fourth;
    }
    for (; i < length; ++i) destination[-i] = palette[payload[i]] & mask;
  } else {
    for (; i + 4 <= length; i += 4) {
      u32 first = palette[payload[i]] & mask, second = palette[payload[i + 1]] & mask;
      u32 third = palette[payload[i + 2]] & mask, fourth = palette[payload[i + 3]] & mask;
      destination[i] = first;
      destination[i + 1] = second;
      destination[i + 2] = third;
      destination[i + 3] = fourth;
    }
    for (; i < length; ++i) destination[i] = palette[payload[i]] & mask;
  }
}

#if defined(__aarch64__) && defined(__ARM_NEON) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
typedef struct {
  u32 *pixels;
  u8 *indices;
  int length;
  SpriteRunKind kind;
  bool flipped, clear_mapping, opaque_palette;
  u32 buckets;
} SpriteCopyRun;

// Keep SIMD register setup out of tiny runs and conditional drawing modes.
static int DrawSpriteCopyRun(const SpriteSurface *surface, const u8 *payload, SpriteCopyRun *run) {
  u32 *destination = run->pixels;
  u8 *mapping = run->indices;
  int length = run->length;
  SpriteRunKind kind = run->kind;
  bool flipped = run->flipped, clear_mapping = run->clear_mapping;
  int copied = 0;
  if (kind == SPRITE_RUN_RGB || kind == SPRITE_RUN_RGBA) {
    for (; copied + 16 <= length; copied += 16) {
      int offset = flipped ? -copied - 15 : copied;
      u32 *target = destination + offset;
      uint8x16x4_t color;
      if (kind == SPRITE_RUN_RGB) {
        uint8x16x3_t rgb = vld3q_u8(payload + copied * 3);
        if (flipped)
          for (int channel = 0; channel < 3; ++channel) rgb.val[channel] = ReverseSpriteBytes(rgb.val[channel]);
        color = (uint8x16x4_t){{rgb.val[2], rgb.val[1], rgb.val[0], vdupq_n_u8(0)}};
        if (clear_mapping) vst1q_u8(mapping + offset, vdupq_n_u8(0));
      } else {
        uint8x16x4_t rgba = vld4q_u8(payload + copied * 4);
        if (flipped)
          for (int channel = 0; channel < 4; ++channel) rgba.val[channel] = ReverseSpriteBytes(rgba.val[channel]);
        uint8x16x4_t before = vld4q_u8((const u8 *)target);
        uint8x16_t alpha = rgba.val[3], inverse = vsubq_u8(vdupq_n_u8(255), alpha);
        color = (uint8x16x4_t){{BlendSpriteChannel(rgba.val[2], before.val[0], alpha, inverse),
            BlendSpriteChannel(rgba.val[1], before.val[1], alpha, inverse),
            BlendSpriteChannel(rgba.val[0], before.val[2], alpha, inverse), vdupq_n_u8(0)}};
        uint8x16_t keep = vceqq_u8(alpha, vdupq_n_u8(0));
        color.val[3] = vandq_u8(before.val[3], keep);
        if (clear_mapping) vst1q_u8(mapping + offset, vandq_u8(vld1q_u8(mapping + offset), keep));
      }
      vst4q_u8((u8 *)target, color);
    }
    if (copied + 8 <= length) {
      int offset = flipped ? -copied - 7 : copied;
      u32 *target = destination + offset;
      uint8x8x4_t color;
      if (kind == SPRITE_RUN_RGB) {
        uint8x8x3_t rgb = vld3_u8(payload + copied * 3);
        if (flipped)
          for (int channel = 0; channel < 3; ++channel) rgb.val[channel] = vrev64_u8(rgb.val[channel]);
        color = (uint8x8x4_t){{rgb.val[2], rgb.val[1], rgb.val[0], vdup_n_u8(0)}};
        if (clear_mapping) vst1_u8(mapping + offset, vdup_n_u8(0));
      } else {
        uint8x8x4_t rgba = vld4_u8(payload + copied * 4);
        if (flipped)
          for (int channel = 0; channel < 4; ++channel) rgba.val[channel] = vrev64_u8(rgba.val[channel]);
        uint8x8x4_t before = vld4_u8((const u8 *)target);
        uint8x8_t alpha = rgba.val[3], inverse = vsub_u8(vdup_n_u8(255), alpha);
        color = (uint8x8x4_t){{BlendSpriteChannel8(rgba.val[2], before.val[0], alpha, inverse),
            BlendSpriteChannel8(rgba.val[1], before.val[1], alpha, inverse),
            BlendSpriteChannel8(rgba.val[0], before.val[2], alpha, inverse), vdup_n_u8(0)}};
        uint8x8_t keep = vceq_u8(alpha, vdup_n_u8(0));
        color.val[3] = vand_u8(before.val[3], keep);
        if (clear_mapping) vst1_u8(mapping + offset, vand_u8(vld1_u8(mapping + offset), keep));
      }
      vst4_u8((u8 *)target, color);
      copied += 8;
    }
    if (kind == SPRITE_RUN_RGB && copied + 4 <= length) {
      // Exactly twelve source bytes: never read past a clipped RGB run.
      const u8 order[16] = {2, 1, 0, 255, 5, 4, 3, 255, 8, 7, 6, 255, 11, 10, 9, 255};
      uint8x16_t rgb = vcombine_u8(vld1_u8(payload + copied * 3), vdup_n_u8(0));
      u32 tail;
      memcpy(&tail, payload + copied * 3 + 8, sizeof(tail));
      rgb = vreinterpretq_u8_u32(vsetq_lane_u32(tail, vreinterpretq_u32_u8(rgb), 2));
      uint32x4_t color = vreinterpretq_u32_u8(vqtbl1q_u8(rgb, vld1q_u8(order)));
      int offset = flipped ? -copied - 3 : copied;
      vst1q_u32(destination + offset, flipped ? ReverseSpriteWords(color) : color);
      if (clear_mapping) memset(mapping + offset, 0, 4);
      copied += 4;
    }
  } else if (kind == SPRITE_RUN_PALETTE_ALPHA) {
    for (; copied + 8 <= length; copied += 8) {
      const u8 *source = payload + copied * 2;
      const u32 *palette = surface->palette;
      u32 colors[8] = {palette[source[0]], palette[source[2]], palette[source[4]], palette[source[6]],
          palette[source[8]], palette[source[10]], palette[source[12]], palette[source[14]]};
      uint8x8x4_t color = vld4_u8((const u8 *)colors);
      uint8x8x2_t indexed = vld2_u8(source);
      uint8x8_t alpha = indexed.val[1];
      uint8x8_t identity = surface->image_palette || run->opaque_palette ? vdup_n_u8(0) : indexed.val[0];
      if (flipped) {
        for (int channel = 0; channel < 3; ++channel) color.val[channel] = vrev64_u8(color.val[channel]);
        alpha = vrev64_u8(alpha);
        identity = vrev64_u8(identity);
      }
      int offset = flipped ? -copied - 7 : copied;
      u32 *target = destination + offset;
      uint8x8x4_t before = vld4_u8((const u8 *)target);
      uint8x8_t inverse = vsub_u8(vdup_n_u8(255), alpha);
      for (int channel = 0; channel < 3; ++channel)
        color.val[channel] = BlendSpriteChannel8(color.val[channel], before.val[channel], alpha, inverse);
      uint8x8_t keep = vceq_u8(alpha, vdup_n_u8(0));
      color.val[3] = vbsl_u8(vceq_u8(alpha, vdup_n_u8(255)), identity, vand_u8(before.val[3], keep));
      if (clear_mapping) vst1_u8(mapping + offset, vand_u8(vld1_u8(mapping + offset), keep));
      vst4_u8((u8 *)target, color);
    }
  } else if ((kind == SPRITE_RUN_PALETTE || kind == SPRITE_RUN_ANIMATED) && length >= 8) {
    uint32x4_t buckets = vdupq_n_u32(0);
    bool animated = kind == SPRITE_RUN_ANIMATED;
    const u32 *palette = animated ? surface->animation_palette : surface->palette;
    uint8x16x3_t water = {{vdupq_n_u8(0), vdupq_n_u8(0), vdupq_n_u8(0)}};
    if (animated) {
      const u32 *colors = palette + SPRITE_ANIMATION_WATER_FIRST;
      water.val[0] = vreinterpretq_u8_u32(vld1q_u32(colors));
      water.val[1] = vreinterpretq_u8_u32(vld1q_u32(colors + 4));
      water.val[2] = vcombine_u8(vreinterpret_u8_u32(vld1_u32(colors + 8)), vdup_n_u8(0));
    }
    for (; copied + 8 <= length; copied += 8) {
      uint8x8_t indices = vld1_u8(payload + copied);
      uint8x8_t animation = animated ? indices : vdup_n_u8(0);
      if (animated || clear_mapping)
        vst1_u8(flipped ? mapping - copied - 7 : mapping + copied,
            flipped ? vrev64_u8(animation) : animation);
      uint16x8_t wide = vmovl_u8(indices);
      bool all_water = animated && vminv_u8(indices) >= SPRITE_ANIMATION_WATER_FIRST &&
          vmaxv_u8(indices) < SPRITE_ANIMATION_COLORS;
      for (int half = 0; half < 2; ++half) {
        int at = copied + half * 4;
        uint32x4_t index = vmovl_u16(half ? vget_high_u16(wide) : vget_low_u16(wide));
        if (animated) buckets = vorrq_u32(buckets, PaletteIndexBuckets(index));
        uint32x4_t color;
        if (all_water) {
          uint32x4_t offsets = vaddq_u32(vmulq_n_u32(
              vsubq_u32(index, vdupq_n_u32(SPRITE_ANIMATION_WATER_FIRST)), 0x04040404), vdupq_n_u32(0x03020100));
          color = vandq_u32(vreinterpretq_u32_u8(vqtbl3q_u8(water, vreinterpretq_u8_u32(offsets))),
              vdupq_n_u32(0xFFFFFF));
        } else {
          u32 colors[4] = {palette[payload[at]], palette[payload[at + 1]], palette[payload[at + 2]],
              palette[payload[at + 3]]};
          uint32x4_t source_index = surface->image_palette || run->opaque_palette ? vdupq_n_u32(0) : index;
          if (animated) {
            source_index = vaddq_u32(index, vbslq_u32(vcltq_u32(index, vdupq_n_u32(50)),
                vdupq_n_u32(170), vdupq_n_u32(174)));
            source_index = vandq_u32(source_index, vcltq_u32(index, vdupq_n_u32(SPRITE_ANIMATION_WATER_FIRST)));
          }
          color = vorrq_u32(vandq_u32(vld1q_u32(colors), vdupq_n_u32(0xFFFFFF)),
              vshlq_n_u32(source_index, 24));
        }
        vst1q_u32(flipped ? destination - at - 3 : destination + at,
            flipped ? ReverseSpriteWords(color) : color);
      }
    }
    run->buckets = CombineWidePaletteBuckets(buckets);
  }
  return copied;
}

#endif

// Payload kind and blend mode are selected outside the pixel loop. Runs never
// cross rows; clipping computes one interval per run, including flipped runs.
void DrawRunSprite(SpriteSurface *surface, const RunSprite *sprite, int x, int y, u32 flags, const u32 *palette) {
  int mode = flags & SPRITE_MODE_MASK, opacity = 255 - (flags >> SPRITE_TRANSPARENCY_SHIFT);
  bool flipped = (flags & SPRITE_FLIPPED) != 0, disabled = (flags & SPRITE_DISABLED) != 0;
  if (!opacity) return;
  SpriteSurface local;
  if (palette || (sprite->palette && mode == SPRITE_COPY)) {
    local = *surface;
    local.palette = palette ? palette : sprite->palette->colors;
    local.remap = NULL;
    local.image_palette = mode == SPRITE_COPY;
    surface = &local;
  }
  flipped ^= sprite->mirrored;
  Canvas *canvas = &surface->canvas;
  CityRect clip = canvas->clip;
  clip.left = IntMax(0, clip.left); clip.top = IntMax(0, clip.top);
  clip.right = IntMin(canvas->width, clip.right); clip.bottom = IntMin(canvas->height, clip.bottom);
  i64 right = (i64)x + sprite->width, bottom = (i64)y + sprite->height;
  if (right <= clip.left || x >= clip.right || bottom <= clip.top || y >= clip.bottom) return;
  if (canvas->draw_stats && canvas->draw_stats->active) ++canvas->draw_stats->sprite_calls;
  int first_row = (int)IntMax(0, (i64)clip.top - y);
  int end_row = (int)IntMin(sprite->height, (i64)clip.bottom - y);
  int first_pixel = (int)IntMax(0, flipped ? right - clip.right : (i64)clip.left - x);
  int end_pixel = (int)IntMin(sprite->width, flipped ? right - clip.left : (i64)clip.right - x);
  const u32 *rows = RunSpriteRows(sprite);
  const u8 *data = RunSpriteData(sprite);
#if defined(__aarch64__) && defined(__ARM_NEON) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  bool vector_copy = mode == SPRITE_COPY && surface->indices;
#endif
  for (int row = first_row; row < end_row; ++row) {
    u32 row_flag = 0;
    bool clear_mapping = surface->indices && (!surface->row_flags || surface->row_flags[y + row] != 0);
    const u8 *source = data + rows[row], *end = data + rows[row + 1];
    int position = source < end ? ReadSpriteInitialSkip(&source) : 0;
    while (source < end) {
      int kind, count = ReadSpriteRun(&source, &kind), bytes = SpriteRunPayloadBytes(kind);
      const u8 *payload = source;
      source = payload + count * bytes;
      int run_first = position;
      position += count;
      if (run_first >= end_pixel) break;
      if (kind == SPRITE_RUN_TRANSPARENT || position <= first_pixel) continue;
      int first = IntMax(run_first, first_pixel), finish = IntMin(position, end_pixel);
      int length = finish - first;
      int left = flipped ? (int)(right - finish) : x + first, last = left + length;
      payload += (first - run_first) * bytes;
      int step = flipped ? -1 : 1;
      int start = flipped ? last - 1 : left;
      u32 *destination = canvas->pixels + (size_t)(y + row) * canvas->stride + start;
      u8 *mapping = surface->indices ? surface->indices + (size_t)(y + row) * surface->index_stride + start : NULL;
      if (opacity != 255 || disabled) {
        // Apply whole-sprite opacity only to covered pixels, without rebuilding palettes.
        for (int i = 0; i < length; ++i, payload += bytes) {
          int alpha = SpriteRunHasAlpha(kind) ? payload[bytes - 1] : 255;
          u32 color;
          int animation = 0;
          if (SpriteRunIndexed(kind)) {
            int index = payload[0], color_kind = kind;
            bool fixed = mode != SPRITE_COPY;
            if (fixed && surface->remap) {
              int source_index = SpriteRunAnimated(kind) ? SpriteAnimationSource(index) : index;
              if (!SpriteRunAnimated(kind) || source_index) {
                index = surface->remap[source_index];
                color_kind = SPRITE_RUN_PALETTE;
              }
            }
            if (mode == SPRITE_FIXED_PALETTE) alpha = alpha * (surface->palette[index] >> 24) / 255;
            color = SurfaceIndexedColor(surface, color_kind, index);
            if (!fixed && SpriteRunAnimated(kind)) animation = index;
          } else color = payload[0] << 16 | payload[1] << 8 | payload[2];
          if (disabled) {
            int gray = (((color >> 16 & 255) + (color >> 8 & 255) + (color & 255)) / 3 + 131) / 2;
            color = gray * 0x010101;
            animation = 0;
          }
          alpha = alpha * opacity / 255;
          if (alpha) {
            if (canvas->palette_composite_rows && mapping) {
              int pixel = start + i * step;
              RecordCanvasPaletteComposite(canvas, y + row, pixel, pixel + 1,
                  CanvasPaletteIndexMask(animation) | CanvasPaletteIndexMask(*mapping));
            }
            *destination = BlendRGB(color, *destination, alpha);
            if (mapping) *mapping = 0;
          }
          if (i + 1 < length) {
            if (mapping) mapping += step;
            destination += step;
          }
        }
        continue;
      }
      bool opaque_palette = mode == SPRITE_OPAQUE_PALETTE && !surface->remap;
      if (kind == SPRITE_RUN_PALETTE && ((mode == SPRITE_COPY && surface->image_palette) || opaque_palette)) {
        // Four independent lookups and stores, with no blend or vector packing.
        if (clear_mapping) memset(flipped ? mapping - length + 1 : mapping, 0, length);
        if (opaque_palette) CopyOpaquePaletteRun(destination, payload, surface->palette, length, flipped, 0xFFFFFF);
        else CopyOpaquePaletteRun(destination, payload, surface->palette, length, flipped, 0xFFFFFFFF);
        continue;
      }
#if defined(__aarch64__) && defined(__ARM_NEON) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      // Never load across a clipped run.
      if (surface->indices && (vector_copy || (opaque_palette && kind == SPRITE_RUN_PALETTE_ALPHA)) &&
          (length >= 8 || (kind == SPRITE_RUN_RGB && length >= 4))) {
        if ((kind == SPRITE_RUN_RGBA || kind == SPRITE_RUN_PALETTE_ALPHA) && canvas->palette_composite_rows &&
            (!surface->row_flags || surface->row_flags[y + row] || row_flag)) {
          // Only background pixels actually covered by a translucent run
          // contribute palette dependencies. A water pixel elsewhere on the
          // same scanline must not make this entire sprite need repainting.
          u32 buckets = 0;
          int dependent_left = last, dependent_right = left;
          for (int i = 0; i < length; ++i) {
            int index = mapping[i * step];
            if (!index) continue;
            int alpha = payload[i * bytes + bytes - 1];
            if (!alpha || alpha == 255) continue;
            buckets |= CanvasPaletteIndexMask(index);
            int pixel = start + i * step;
            dependent_left = IntMin(dependent_left, pixel);
            dependent_right = IntMax(dependent_right, pixel + 1);
          }
          RecordCanvasPaletteComposite(canvas, y + row, dependent_left, dependent_right, buckets);
        }
        SpriteCopyRun run = {destination, mapping, length, kind, flipped, clear_mapping, opaque_palette, 0};
        int copied = DrawSpriteCopyRun(surface, payload, &run);
        row_flag |= run.buckets;
        if (copied == length) continue;
        payload += copied * bytes;
        destination += copied * step;
        mapping += copied * step;
        start += copied * step;
        length -= copied;
      }
#endif
      if (((mode == SPRITE_COPY && surface->image_palette) || opaque_palette) &&
          !canvas->palette_composite_rows && kind == SPRITE_RUN_PALETTE_ALPHA) {
        // Image and opaque replacement palettes retain no animation identity.
        // Keep their small runs and SIMD tails out of the general mode and
        // remapping branches for every individual pixel.
        const u32 *palette = surface->palette;
        for (int i = 0; i < length; ++i) {
          int alpha = payload[i * 2 + 1];
          if (!alpha) continue;
          u32 color = palette[payload[i * 2]];
          u32 *pixel = destination + i * step;
          *pixel = alpha == 255 ? color & 0xFFFFFF : BlendRGB(color, *pixel, alpha);
          if (clear_mapping) mapping[i * step] = 0;
        }
        continue;
      }
      if (kind == SPRITE_RUN_PALETTE && mode == SPRITE_OPAQUE_PALETTE) {
        // Local palettes contain opaque RGB. No background read, palette-alpha
        // composition, or registration with the global animation palette.
        for (int i = 0; i < length; ++i, ++payload) {
          *destination = surface->palette[*payload] & 0xFFFFFF;
          if (mapping) *mapping = 0;
          if (i + 1 < length) {
            if (mapping) mapping += step;
            destination += step;
          }
        }
      } else if (SpriteRunIndexed(kind)) {
        for (int i = 0; i < length; ++i, payload += bytes) {
          int index = payload[0], alpha = SpriteRunHasAlpha(kind) ? payload[1] : 255;
          int color_kind = kind;
          bool fixed = mode == SPRITE_FIXED_PALETTE || mode == SPRITE_OPAQUE_PALETTE;
          if (fixed && surface->remap) {
            int source_index = SpriteRunAnimated(kind) ? SpriteAnimationSource(index) : index;
            if (!SpriteRunAnimated(kind) || source_index) {
              index = surface->remap[source_index];
              color_kind = SPRITE_RUN_PALETTE;
            }
          }
          if (fixed && mode != SPRITE_OPAQUE_PALETTE) alpha = alpha * (surface->palette[index] >> 24) / 255;
          if (alpha) {
            u32 color = SurfaceIndexedColor(surface, color_kind, index);
            int animation = !fixed && SpriteRunAnimated(kind) ? index : 0;
            if (alpha == 255) {
              *destination = fixed ? color & 0xFFFFFF : color;
              if (mapping) *mapping = animation;
              row_flag |= CanvasPaletteIndexMask(animation);
            } else {
              if (canvas->palette_composite_rows && mapping) {
                u32 buckets = CanvasPaletteIndexMask(animation) | CanvasPaletteIndexMask(*mapping);
                int pixel = start + i * step;
                RecordCanvasPaletteComposite(canvas, y + row, pixel, pixel + 1, buckets);
              }
              *destination = BlendRGB(color, *destination, alpha);
              if (mapping) *mapping = 0;
            }
          }
          if (i + 1 < length) {
            if (mapping) mapping += step;
            destination += step;
          }
        }
      } else if (kind == SPRITE_RUN_RGB) {
        for (int i = 0; i < length; ++i, payload += 3) {
          *destination = (payload[0] << 16) | (payload[1] << 8) | payload[2];
          if (clear_mapping) *mapping = 0;
          if (i + 1 < length) {
            if (mapping) mapping += step;
            destination += step;
          }
        }
      } else {
        for (int i = 0; i < length; ++i, payload += 4) {
          if (payload[3]) {
            if (canvas->palette_composite_rows && mapping && payload[3] != 255 && *mapping) {
              int pixel = start + i * step;
              RecordCanvasPaletteComposite(canvas, y + row, pixel, pixel + 1, CanvasPaletteIndexMask(*mapping));
            }
            u32 rgb = (payload[0] << 16) | (payload[1] << 8) | payload[2];
            *destination = BlendRGB(rgb, *destination, payload[3]);
            if (clear_mapping) *mapping = 0;
          }
          if (i + 1 < length) {
            if (mapping) mapping += step;
            destination += step;
          }
        }
      }
    }
    if (surface->row_flags) surface->row_flags[y + row] |= row_flag;
  }
}

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

void FillSpriteSurface(SpriteSurface *surface, CityRect rect, u8 index) {
  Canvas *canvas = &surface->canvas;
  rect.left = IntMax(0, IntMax(rect.left, canvas->clip.left));
  rect.top = IntMax(0, IntMax(rect.top, canvas->clip.top));
  rect.right = IntMin(canvas->width, IntMin(rect.right, canvas->clip.right));
  rect.bottom = IntMin(canvas->height, IntMin(rect.bottom, canvas->clip.bottom));
  if (rect.left >= rect.right || rect.top >= rect.bottom) return;
  u32 color = SurfaceIndexedColor(surface, SPRITE_RUN_PALETTE, index);
  int animation = SpriteAnimationId(index);
  for (int y = rect.top; y < rect.bottom; ++y) {
    u32 *row = canvas->pixels + (size_t)y * canvas->stride;
    for (int x = rect.left; x < rect.right; ++x) row[x] = color;
    if (animation || !surface->row_flags || surface->row_flags[y])
      memset(surface->indices + (size_t)y * surface->index_stride + rect.left, animation, rect.right - rect.left);
    if (surface->row_flags) surface->row_flags[y] |= CanvasPaletteIndexMask(animation);
  }
}


static bool RecolorSpriteSurface(SpriteSurface *surface, int first, int end, u32 changed_buckets) {
  bool changed = false;
  Canvas *canvas = &surface->canvas;
  if (!changed_buckets) return false;
  for (int y = 0; y < canvas->height; ++y) {
    if (surface->row_flags && !(surface->row_flags[y] & changed_buckets)) continue;
    u32 row_flag = 0;
    int dirty_left = canvas->width, dirty_right = 0;
    u32 *pixels = canvas->pixels + (size_t)y * canvas->stride;
    const u8 *indices = surface->indices + (size_t)y * surface->index_stride;
    int x = 0;
#if defined(__aarch64__) && defined(__ARM_NEON) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    // Eight index bytes feed two four-pixel vectors without reading past a row.
    {
      uint32x4_t row_buckets = vdupq_n_u32(0);
      for (; x + 8 <= canvas->width; x += 8) {
        uint8x8_t packed = vld1_u8(indices + x);
        uint16x8_t wide = vmovl_u8(packed);
        for (int half = 0; half < 2; ++half) {
          uint32x4_t index = vmovl_u16(half ? vget_high_u16(wide) : vget_low_u16(wide));
          uint32x4_t buckets = PaletteIndexBuckets(index);
          row_buckets = vorrq_u32(row_buckets, buckets);
          uint32x4_t selected = vandq_u32(vcgeq_u32(index, vdupq_n_u32(first)),
              vcltq_u32(index, vdupq_n_u32(end)));
          selected = vandq_u32(selected, vtstq_u32(buckets, vdupq_n_u32(changed_buckets)));
          if (!vmaxvq_u32(selected)) continue;
          int at = x + half * 4;
          uint32x4_t before = vld1q_u32(pixels + at);
          u32 palette[4] = {surface->animation_palette[indices[at]], surface->animation_palette[indices[at + 1]],
              surface->animation_palette[indices[at + 2]], surface->animation_palette[indices[at + 3]]};
          uint32x4_t color = vorrq_u32(vandq_u32(vld1q_u32(palette), vdupq_n_u32(0xFFFFFF)),
              vandq_u32(before, vdupq_n_u32(0xFF000000)));
          color = vbslq_u32(selected, color, before);
          if (vmaxvq_u32(veorq_u32(before, color))) {
            vst1q_u32(pixels + at, color);
            changed = true;
            dirty_left = IntMin(dirty_left, at); dirty_right = at + 4;
          }
        }
      }
      row_flag = CombineWidePaletteBuckets(row_buckets);
    }
#endif
    for (; x < canvas->width; ++x) {
      int index = indices[x];
      row_flag |= CanvasPaletteIndexMask(index);
      if (index < first || index >= end || !(CanvasPaletteIndexMask(index) & changed_buckets)) continue;
      u32 color = (pixels[x] & 0xFF000000) | (surface->animation_palette[index] & 0xFFFFFF);
      if (pixels[x] != color) {
        pixels[x] = color; changed = true;
        dirty_left = IntMin(dirty_left, x); dirty_right = x + 1;
      }
    }
    if (surface->row_flags) surface->row_flags[y] = row_flag;
    if (surface->upload_damage && dirty_left < dirty_right)
      MarkScreenDirty(surface->upload_damage, (CityRect){dirty_left, y, dirty_right, y + 1});
  }
  return changed;
}

bool AnimateSpriteSurface(SpriteSurface *surface, int first, int count) {
  if (count <= 0) return false;
  i64 limit = (i64)first + count;
  int end = limit < SPRITE_ANIMATION_COLORS ? (int)limit : SPRITE_ANIMATION_COLORS;
  first = IntMax(first, 1);
  u32 changed_buckets = 0;
  for (int index = first; index < end; ++index) changed_buckets |= CanvasPaletteIndexMask(index);
  return RecolorSpriteSurface(surface, first, end, changed_buckets);
}

bool AnimateSpriteSurfaceChanges(SpriteSurface *surface, u32 changed_buckets) {
  return RecolorSpriteSurface(surface, 1, SPRITE_ANIMATION_COLORS, changed_buckets);
}

// Ten consecutive water animation IDs fit a sixteen-byte table per channel.
// Static indexed pixels never enter this animation scan.
bool AnimateSpriteSurfaceWater(SpriteSurface *surface, u32 changed_buckets) {
  if (!surface->animation_palette || !changed_buckets) return false;
  Canvas *canvas = &surface->canvas;
  bool changed = false;
#if defined(__aarch64__) && defined(__ARM_NEON) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  // Deinterleaved channels let each NEON table lookup recolor sixteen pixels.
  // Index zero is fixed RGB; ten water colors fit in one table per channel.
  u32 water[16] = {0};
  u8 selected_indices[16] = {0};
  for (int index = 0; index < SPRITE_WATER_PALETTE_COLORS; ++index)
    water[index] = surface->animation_palette[SPRITE_ANIMATION_WATER_FIRST + index] & 0xFFFFFF;
  for (int index = 0; index < SPRITE_WATER_PALETTE_COLORS; ++index)
    if (CanvasPaletteIndexMask(SPRITE_ANIMATION_WATER_FIRST + index) & changed_buckets) selected_indices[index] = 255;
  uint8x16x4_t table = vld4q_u8((const u8 *)water);
  uint8x16_t selection = vld1q_u8(selected_indices);
#endif
  for (int y = 0; y < canvas->height; ++y) {
    if (surface->row_flags && !(surface->row_flags[y] & changed_buckets)) continue;
    u32 *pixels = canvas->pixels + (size_t)y * canvas->stride;
    const u8 *indices = surface->indices + (size_t)y * surface->index_stride;
    u32 row_flag = 0;
    int dirty_left = canvas->width, dirty_right = 0;
    int x = 0;
#if defined(__aarch64__) && defined(__ARM_NEON) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    {
      uint8x16x4_t row_buckets = {{vdupq_n_u8(0), vdupq_n_u8(0), vdupq_n_u8(0), vdupq_n_u8(0)}};
      for (; x + 16 <= canvas->width; x += 16) {
        uint8x16_t index = vld1q_u8(indices + x);
        if (!vmaxvq_u8(index)) continue;
        // Recompute in bytes: each vector accumulates eight adjacent buckets.
        // This shares the index load with recoloring and avoids widening to words.
        AccumulatePaletteBuckets(&row_buckets, index);
        uint8x16_t water_index = vsubq_u8(index, vdupq_n_u8(SPRITE_ANIMATION_WATER_FIRST));
        uint8x16_t selected = vqtbl1q_u8(selection, water_index);
        if (!vmaxvq_u8(selected)) continue;
        uint8x16x4_t before = vld4q_u8((const u8 *)(pixels + x));
        uint8x16x4_t color = before;
        uint8x16_t difference = vdupq_n_u8(0);
        for (int channel = 0; channel < 3; ++channel) {
          color.val[channel] = vbslq_u8(selected, vqtbl1q_u8(table.val[channel], water_index), before.val[channel]);
          difference = vorrq_u8(difference, veorq_u8(color.val[channel], before.val[channel]));
        }
        if (vmaxvq_u8(difference)) {
          vst4q_u8((u8 *)(pixels + x), color);
          changed = true;
          dirty_left = IntMin(dirty_left, x); dirty_right = x + 16;
        }
      }
      row_flag = CombinePaletteBuckets(row_buckets);
    }
#endif
    for (; x < canvas->width; ++x) {
      int index = indices[x];
      row_flag |= CanvasPaletteIndexMask(index);
      if (index < SPRITE_ANIMATION_WATER_FIRST || index >= SPRITE_ANIMATION_COLORS ||
          !(CanvasPaletteIndexMask(index) & changed_buckets)) continue;
      u32 color = (pixels[x] & 0xFF000000) | (surface->animation_palette[index] & 0xFFFFFF);
      if (pixels[x] != color) {
        pixels[x] = color;
        changed = true;
        dirty_left = IntMin(dirty_left, x); dirty_right = x + 1;
      }
    }
    if (surface->row_flags) surface->row_flags[y] = row_flag;
    if (surface->upload_damage && dirty_left < dirty_right)
      MarkScreenDirty(surface->upload_damage, (CityRect){dirty_left, y, dirty_right, y + 1});
  }
  return changed;
}
