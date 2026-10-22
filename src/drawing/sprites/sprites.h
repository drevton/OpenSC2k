#ifndef SC2K_DOMAIN_SRC_DRAWING_SPRITES_SPRITES_H
#define SC2K_DOMAIN_SRC_DRAWING_SPRITES_SPRITES_H

#include <stddef.h>
#include <stdatomic.h>

#include "drawing/canvas.h"
#include "assets/graphics.h"

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

// Run Sprite

// A nonempty row starts with an initial skip (255 escapes to a full LE u16).
// Run headers have three mode bits and five length bits. Lengths 1..31 are
// inline; 31 escapes to 32+u8, with extension 255 escaping to a full LE u16.
// Empty rows have equal offsets; trailing transparent pixels are omitted.
enum { SPRITE_RUN_LIMIT = 65535 };
enum { SPRITE_WATER_COLORS = 5, SPRITE_WATER_GLITTER_COLORS = 5,
  SPRITE_WATER_PALETTE_COLORS = SPRITE_WATER_COLORS + SPRITE_WATER_GLITTER_COLORS };
enum { SPRITE_ANIMATION_FAST_FIRST = 1, SPRITE_ANIMATION_SLOW_FIRST = 50,
  SPRITE_ANIMATION_WATER_FIRST = 66, SPRITE_ANIMATION_COLORS = 76 };
typedef enum SpriteRunKind {
  SPRITE_RUN_TRANSPARENT, SPRITE_RUN_RGB, SPRITE_RUN_RGBA, SPRITE_RUN_PALETTE,
  SPRITE_RUN_PALETTE_ALPHA, SPRITE_RUN_ANIMATED, SPRITE_RUN_ANIMATED_ALPHA, SPRITE_RUN_RESERVED
} SpriteRunKind;

static inline int SpriteRunPayloadBytes(int kind) {
  const u8 bytes[] = {0, 3, 4, 1, 2, 1, 2, 0};
  return bytes[kind];
}
static inline bool SpriteRunIndexed(int kind) { return kind >= SPRITE_RUN_PALETTE && kind < SPRITE_RUN_RESERVED; }
static inline bool SpriteRunAnimated(int kind) {
  return kind == SPRITE_RUN_ANIMATED || kind == SPRITE_RUN_ANIMATED_ALPHA;
}
static inline bool SpriteRunHasAlpha(int kind) {
  return kind == SPRITE_RUN_RGBA || kind == SPRITE_RUN_PALETTE_ALPHA || kind == SPRITE_RUN_ANIMATED_ALPHA;
}
// Source palette indices and retained animation IDs are different namespaces.
static inline int SpriteAnimationId(int index) {
  if (index >= 171 && index <= 219) return index - 170;
  if (index >= 224 && index <= 239) return index - 174;
  return 0;
}
static inline int SpriteAnimationSource(int id) {
  if (id >= 1 && id < 50) return id + 170;
  if (id >= 50 && id < 66) return id + 174;
  return 0;
}
// Trusted readers: external streams are checked by ValidateRunSprite first.
static inline int ReadSpriteInitialSkip(const u8 **source) {
  int skip = *(*source)++;
  if (skip == 255) { skip = (*source)[0] | (*source)[1] << 8; *source += 2; }
  return skip;
}
static inline int ReadSpriteRun(const u8 **source, int *kind) {
  int header = *(*source)++, length = (header & 31) + 1;
  *kind = header >> 5;
  if (length == 32) {
    int extension = *(*source)++;
    length = extension + 32;
    if (extension == 255) { length = (*source)[0] | (*source)[1] << 8; *source += 2; }
  }
  return length;
}
typedef enum SpriteDrawMode {
  SPRITE_COPY, SPRITE_FIXED_PALETTE, SPRITE_OPAQUE_PALETTE
} SpriteDrawMode;
// Mode occupies the low three bits; zero transparency preserves opaque fast paths.
enum { SPRITE_MODE_MASK = 7, SPRITE_FLIPPED = 8, SPRITE_DISABLED = 16, SPRITE_TRANSPARENCY_SHIFT = 24 };

typedef struct {
  u32 rgba; // Numeric 0xAARRGGBB, independent of host byte order.
  u8 index, kind; // Explicit indexed mode permits opaque static source index zero.
} SpritePixel;

typedef struct {
  atomic_uint references;
  u32 colors[256]; // Loaded image colors are framebuffer-ready 0x00RRGGBB; alpha belongs to runs.
} SpritePalette;

typedef struct RunSprite {
  int width, height;
  u32 stream_size;
  atomic_uint references;
  const struct RunSprite *image; /* References share immutable rows; never chains. */
  SpritePalette *palette; /* Shared image palette; alpha remains in the pixel stream. */
  bool mirrored;
  /* Owned image: this record, height+1 row offsets, then packed runs. */
} RunSprite;

typedef struct {
  Canvas canvas;
  u8 *indices; // Optional animation identity layer, same pixel dimensions as canvas; zero is fixed RGB.
  int index_stride;
  const u32 *palette; /* 256 colors; fixed-palette drawing uses full 0xAARRGGBB including alpha. */
  u32 *row_flags; /* Bit n hints that this row contains a nonzero palette index with (index & 31) == n. */
  const u32 *animation_palette; /* Compact animation IDs; entry zero is unused. */
  const u8 *remap; /* Optional source-palette translation for fixed-palette overlays; water has its own namespace. */
  struct ScreenDamage *upload_damage; /* Optional final-framebuffer changes, retained until presentation. */
  bool image_palette; /* Static image colours must not retain global palette/selection identity. */
} SpriteSurface;

RunSprite *EncodeRunSprite(const SpritePixel *pixels, int width, int height, int stride, int scale);
RunSprite *EncodeRunSpriteRatio(const SpritePixel *pixels, int width, int height, int stride,
    int numerator, int denominator);
// Fixed UI images use the same resampler as map sprites; returned image is owned.
RunSprite *ResizeRunSprite(const RunSprite *source, int width, int height);
// Crop coordinates refer to the source canvas, preserving shared preview alignment.
RunSprite *ResizeRunSpriteRegion(const RunSprite *source, CityRect content, int width, int height);
// Host cursor and generated previews share the normal run encoder/decoder.
RunSprite *EncodeRgbaRunSprite(const u32 *pixels, int width, int height, int stride);
bool CopyRunSpriteRgba(const RunSprite *source, u32 *pixels, int stride,
    const u32 *palette, const u32 *animation_palette);
void FreeRunSprite(RunSprite *sprite);
SpritePalette *RetainSpritePalette(SpritePalette *palette);
void FreeSpritePalette(SpritePalette *palette);
RunSprite *RetainRunSprite(const RunSprite *sprite);
RunSprite *ReferenceRunSprite(const RunSprite *sprite, SpritePalette *palette, bool mirrored);
const u32 *RunSpriteRows(const RunSprite *sprite);
const u8 *RunSpriteData(const RunSprite *sprite);
bool ValidateRunSprite(const RunSprite *sprite);
// A null palette uses the image palette for normal copies, otherwise the surface palette.
void DrawRunSprite(SpriteSurface *surface, const RunSprite *sprite, int x, int y, u32 flags, const u32 *palette);
// Stream full-screen images through row scratch and the same sprite drawer;
// no full-size scaled backdrop or duplicate screen buffer is retained.
bool DrawScaledRunSprite(SpriteSurface *surface, const RunSprite *sprite, CityRect bounds, u32 flags);
void FillSpriteSurface(SpriteSurface *surface, CityRect rect, u8 index);
bool AnimateSpriteSurface(SpriteSurface *surface, int first, int count);
bool AnimateSpriteSurfaceChanges(SpriteSurface *surface, u32 changed_buckets);
bool AnimateSpriteSurfaceWater(SpriteSurface *surface, u32 changed_buckets);


// Select interface or prepared map sources; tile selection never interprets packed image references.
int InterfaceSpriteLevel(int scale);
const RunSprite *GetInterfaceSprite(const CityInterfaceIcons *icons, int sprite, int level, bool *flipped);
const CityLandscapeSprite *GetResourceSprite(const CityResourceState *assets, int index);
// Cache preparation translates its public slots to opaque resource indexes.
int TileSpriteResourceIndex(const CityResourceState *assets, int sprite, int direction);
int LandscapeSpriteResourceIndex(const CityResourceState *assets, int source);
const CityLandscapeSprite *GetTileSpriteSource(const CityResourceState *assets, int sprite, int direction);

// Map Sprite Cache

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
  MAP_SPRITE_PREPARATION_WORKERS = 4,
  MAP_BUILDING_PREVIEW_COUNT = CITY_CIVIC_KIND_COUNT * (CITY_CIVIC_VIEWS + 1) + CITY_CIVIC_VIEWS,
};
typedef struct {
  SpritePixel *pixels;
  size_t capacity;
} SpritePreparationWorkspace;

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
bool InstallResourceSprite(MapSpriteCache *cache, const CityResourceState *assets, int id, int index, int scale);
bool InstallLandscapeSprite(MapSpriteCache *cache, int id, const RunSprite *sprite, int scale, int baseline,
    int offset_x, bool flipped);
int FindMapSpriteScale(const MapSpriteCache *cache, int scale);
int PrepareMapSpriteScale(MapSpriteCache *cache, int scale, const u32 *palette);
// Ranges are consecutive; publish the scale only when its final range completes.
int PrepareMapSpriteScaleRange(MapSpriteCache *cache, int scale, const u32 *palette, int first, int count);
bool PrepareMapBuildingPreviews(MapSpriteCache *cache, int width, int height, const u32 *palette);
bool PrepareMapBuildingPreviewRange(MapSpriteCache *cache, int width, int height, const u32 *palette,
    int first, int count);
// Startup only, with no active preparation. Copies scales/palette and installs the base pixel density before launch.
// Until Finish joins every worker, cache sources, Assets and pixel_scale must remain unchanged and alive;
// no other code may read or replace output images/previews. Workers use disjoint outputs and private row scratch.
bool StartMapSpritePreparation(MapSpriteCache *cache, const int *scales, int count,
    int preview_width, int preview_height, const u32 *palette);
// Polls weighted work without reading in-flight images. Returns -1 for no active run or worker failure;
// total is written only on success. No worker is joined here.
int MapSpritePreparationProgress(const MapSpriteCache *cache, int *total);
// Join every worker before freeing its shared preparation record. Active cancellation returns false;
// success (or an already idle cache) returns true. Only a completed, uncancelled run publishes scale/preview readiness.
bool FinishMapSpritePreparation(MapSpriteCache *cache, bool cancel);
void FreeMapSpriteScales(MapSpriteCache *cache);
void FreeMapSpriteCache(MapSpriteCache *cache);

#endif
