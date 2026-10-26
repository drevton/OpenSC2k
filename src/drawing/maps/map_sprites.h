#ifndef SC2K_DRAWING_MAPS_MAP_SPRITES_H
#define SC2K_DRAWING_MAPS_MAP_SPRITES_H

#include "assets/graphics.h"
#include "drawing/sprites/sprites.h"
#include "drawing/types.h"

typedef struct Drawing Drawing;

// Resolve unzoomed catalog-cache slots in world space or the active view, independently of image resolution.
// Requires installed graphics bindings; these queries do not prepare images or modify City or Drawing.
int CityMapTileSpriteIndex(const City *city, TileIndex tile);
int CityMapViewTileSpriteIndex(const City *city, TileIndex tile);
int CityMapViewTileFlip(const City *city, TileIndex tile);
// Rotate an unzoomed catalog-cache slot using the installed bindings. Slots outside the tile catalog pass through.
int CityMapCameraSprite(const CityResourceState *assets, int sprite, int rotation);

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

// Prepared river layers: one water surface and at most twelve bank pieces sharing a baseline.
// Values contain Drawing cache slots only, with no borrowed source pointers or owned storage.
enum { MAP_RIVER_SPRITE_CAPACITY = 13 };
typedef struct MapRiverSprites {
  u16 sprite[MAP_RIVER_SPRITE_CAPACITY];
  u8 count;
} MapRiverSprites;

// Retained inspector artwork contains cache-slot IDs and values, with no borrowed pointers or owned storage.
typedef struct MapTilePreview {
  u16 sprite;
  MapRiverSprites river;
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

// Prepared map-sprite identities. Cache state, preparation and worker lifetime are declared in sprite_cache.h.

// Select a prepared catalog-cache slot from its unzoomed entry and requested resolution.
// Drawing owns the resolution banks; resource image identities remain independent.
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

#endif
