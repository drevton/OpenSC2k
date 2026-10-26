#ifndef SC2K_DRAWING_MAPS_ROAD_CARS_H
#define SC2K_DRAWING_MAPS_ROAD_CARS_H

#include "city/maps/tile_storage.h"
#include "city/types.h"
#include "drawing/maps/map_camera.h"

// Decorative traffic belongs to Drawing. City supplies immutable roads, geometry and density;
// this pool, path progress and random stream are presentation state, not saved City events.
enum { CITY_ROAD_CAR_COUNT = 1024, CITY_ROAD_CAR_BUCKETS = 1024, CITY_ROAD_CAR_AGE_PHASES = 16 };
typedef struct RoadCar {
  TileIndex tile : 24; // Owns the front-axle navigation point.
  u32 path : 5, rotation : 2; // Path zero is inactive; real paths are one-based.
  u32 progress : 11, next : 11, palette : 5, model : 3, reserved : 1, fading : 1;
  u32 age : 15, base_height : 6, surface : 5, ahead : 2, behind : 2, junction_ahead : 1, straight : 1;
  u16 heading; // Body yaw in 1/65536 turns, retained across tile boundaries.
  u8 speed; // Cached base speed in 1/128 tile lengths per second.
  u8 steering; // Straight, left or right front-wheel artwork; reuses record padding.
} RoadCar;
_Static_assert(sizeof(RoadCar) == 16, "Compact car records use sixteen bytes");
_Static_assert(CITY_ROAD_CAR_COUNT < (1 << 11), "Car links include the zero terminator");
typedef struct {
  RoadCar cars[CITY_ROAD_CAR_COUNT];
  u16 buckets[CITY_ROAD_CAR_BUCKETS];
  u32 random;
  u16 age_clock, age_updated[CITY_ROAD_CAR_AGE_PHASES];
  u8 age_phase;
  u32 time, next_spawn; // Presentation seconds in 1/256 units, wrapping together.
  const TileInfo *map;
  bool enabled;
} DrawingRoadTraffic;

// Operations use the City's existing Drawing owner and keep City state unchanged.
// Presentation seconds in wrapping Q8, converted once by the host caller. Returns whether car visuals changed.
bool UpdateCityRoadTraffic(const City *city, u32 now);
// Seed newly exposed camera strips from traffic density. Index 0 is the main view; positive indexes are auxiliary
// views. The caller supplies the previous published camera, or an empty camera for an initial population.
void PopulateCityRoadTrafficView(const City *city, int index, CityMapCamera previous, CityMapCamera view);
// Use the bound map target/camera and its current sprite-preparation pass. Collection selects the object phase;
// drawing one slot requires a valid active car. Display rules suppress collection when vehicles are hidden.
void CollectCityRoadTraffic(const City *city, int zoom);
void DrawCityRoadCar(const City *city, int slot, int zoom);
// Refresh cached paths/surface poses after road edits, including while paused, and remove invalid routes.
void RefreshCityRoadTrafficPoses(const City *city);
// Exact prepared sprite bounds in framebuffer pixels, relative to the viewport origin.
// pixel_scale is physical pixels per logical map pixel. This allocates/prepares nothing; unavailable art,
// inactive/invalid cars and unsupported zooms return empty bounds.
CityRect CityRoadCarPixelBounds(const City *city, const RoadCar *car, CityMapCamera view, int pixel_scale);

// The map-window frontend implements these synchronous traffic presentation services.
// It resolves live window occlusion, magnification and viewport geometry from the supplied car/camera data.

// A decorative car's tile vicinity must reach an unobscured map window at large zoom.
// Height uses 1/256 map levels. view_index zero selects the main map; other indices select Drawing's extra views.
// Read-only. Requires attached Drawing/UI, a valid tile and view_index in 0..CITY_EXTRA_MAP_VIEW_COUNT.
bool IsCityRoadCarInView(const City *city, TileIndex tile, int height, CityMapCamera view, int view_index);

// Project both borrowed footprints into each visible map window and update screen damage synchronously.
// Either car may be NULL or inactive, contributing no bounds. Requires attached Drawing; disabled damage or a
// missing UI is a no-op. Viewport geometry may be synchronized, but the cars and City simulation remain unchanged.
void PublishCityRoadCarDamage(const City *city, const RoadCar *previous, const RoadCar *current);

#endif
