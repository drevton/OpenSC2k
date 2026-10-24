#ifndef SC2K_DRAWING_MAPS_MAP_DEPTH_H
#define SC2K_DRAWING_MAPS_MAP_DEPTH_H

#include "city/types.h"

typedef struct Drawing Drawing;
typedef struct CityMapViewConfig CityMapViewConfig;

// Drawing owns one reusable workspace. A synchronous depth paint borrows it exclusively;
// each subdivision reuses the same records instead of allocating storage per region.
// Final blit coordinates are relative to the current subdivision's physical clip.
typedef struct {
  u16 sprite, depth;
  i16 x, y;
  u8 palette, flags, opacity, reserved;
} PreparedMapSprite;
enum { CITY_MAP_DEPTH_CAPACITY = 16384, CITY_MAP_DEPTH_PIXEL_LIMIT = 1000000 };
typedef enum MapDepthPhase {
  MAP_DEPTH_INACTIVE, MAP_DEPTH_COLLECT, MAP_DEPTH_GROUND, MAP_DEPTH_OBJECTS
} MapDepthPhase;
typedef struct {
  const u32 (*bridge_palettes)[256], (*underworld_palettes)[256], (*car_palettes)[256];
  CityRect clip;
  i32 depth;
  int count;
  MapDepthPhase phase;
  bool preparing, overflow;
} DrawingMapDepth;
typedef struct {
  PreparedMapSprite sprites[CITY_MAP_DEPTH_CAPACITY];
  u16 order[CITY_MAP_DEPTH_CAPACITY];
} MapSpriteWork;
_Static_assert(sizeof(PreparedMapSprite) == 12, "Prepared map sprites use twelve bytes");
_Static_assert(sizeof(MapSpriteWork) == 224 * 1024, "Shared map sprite workspace uses 224 KiB");

// Camera-space centre in 1/4096 tile units, and elevation in 1/4096 height units.
// Admission quantizes this value to quarter-tile precision; equal depths preserve submission order.
i32 CityMapObjectDepth(int x, int y, int height);
// Separate deck and near-side structure depths in the active camera leave room for bridge traffic.
i32 CityMapBridgeArtworkDepth(const City *city, TileIndex tile, bool foreground);

// Submit an already cached sprite at physical framebuffer coordinates to the current depth pass.
// Sprite and immutable palette IDs must be valid for the bound cache; commands never retain temporary colors.
// No image preparation or allocation occurs here.
// False means missing/clipped artwork. True means submitted, or overflow latched for the caller to retry;
// an overflowed queue must be discarded and its region subdivided before emission.
bool SubmitCityMapSprite(Drawing *drawing, int sprite, int x, int y, u32 flags, int palette);

// Emit the current commands in their order-array sequence, without sorting or clearing the queue.
// Cached images and palette sources must remain alive and unchanged through emission.
void DrawPreparedCityMapSprites(Drawing *drawing);

// Paint the bound target/camera using paint_rect and the target's physical clip. Reuses the fixed workspace;
// excessive area or queue overflow subdivides and retries, restoring the supplied City's RNG before a retry.
// Saves/restores the previous depth pass, paint rectangle and target clip, including on capacity failure.
// Native frames supply a paint copy of City for random artwork; this operation does not bind a different camera.
void DrawDepthSortedCityMap(City *city, const CityMapViewConfig *view, int zoom);

#endif
