#ifndef SC2K_DOMAIN_SRC_DRAWING_MAPS_MAP_RENDER_H
#define SC2K_DOMAIN_SRC_DRAWING_MAPS_MAP_RENDER_H

#include "city/maps/map.h"
#include "city/types.h"
#include "drawing/canvas.h"
#include "drawing/maps/map_camera.h"
#include "drawing/maps/map_data.h"
#include "drawing/maps/map_terrain.h"
#include "drawing/maps/map_view.h"
#include "drawing/sprites/sprites.h"

enum { CITY_EXTRA_MAP_VIEW_COUNT = 8 };

typedef struct { u32 face, light, dark, ink; } CitySignColors;
CitySignColors CitySignColorStyle(int color);
void DrawCapturedCitySigns(City *city);
CityRect CitySignPixelBounds(const City *city, CityPoint anchor, const char *text);
CityRect CitySignMapBounds(const City *city, int zoom, int screen_x, int screen_y, const char *text);
// Road traffic

// Presentation seconds in wrapping Q8, converted once by the host caller.
bool UpdateCityRoadTraffic(City *city, u32 now);
void PopulateCityRoadTrafficView(City *city, int index, CityMapCamera previous, CityMapCamera view);
void CollectCityRoadTraffic(City *city, int zoom);
void DrawCityRoadCar(City *city, int slot, int zoom);
void RefreshCityRoadTrafficPoses(City *city);
struct RoadCar;
// Exact prepared sprite bounds in framebuffer pixels, relative to the viewport origin.
CityRect CityRoadCarPixelBounds(const City *city, const struct RoadCar *car, CityMapCamera view, int pixel_scale);
// Camera-space centre in 1/4096 tile units, and elevation in 1/4096 height units.
i32 CityMapObjectDepth(int x, int y, int height);
// Separate deck and near-side structure depths leave room for traffic on the bridge.
i32 CityMapBridgeArtworkDepth(const City *city, TileIndex tile, bool foreground);
// Prepared commands keep immutable palette IDs, never pointers to temporary colors.
bool PrepareCityMapSprite(Drawing *drawing, int sprite, int x, int y, u32 flags, int palette);
void DrawPreparedCityMapSprites(Drawing *drawing);
void ApplyCityMapSelectionPalette(SpriteSurface *surface, int style);
void DrawDepthSortedCityMap(City *city, const CityMapViewConfig *view, int zoom);


// Prepare event footprints and random-artwork replay for the bound target/camera before painting any damage.
// mark records map-pixel damage synchronously with paint as its context. Capture restores the supplied City's RNG;
// normal frames use a paint copy of City, and FinishCityMapAnimations ends replay after the camera is drawn.
void PrepareCityMapSpriteDamage(CityMapPaint *paint, CityCanvasVisiblePaint mark);

// Map Damage

// Last published map values, owned by drawing and advanced after all viewports
// receive damage. Simulation writes never capture or update these arrays.
typedef struct {
  u8 *tiles, *flags, *zones, *terrain, *underground, *overlays, *heights;
  u16 *altitude;
  u8 *pollution, *traffic_density, *crime, *land_value;
  u8 *density, *police_coverage, *fire_coverage, *density_history;
} CityMapDrawingValues;

typedef struct {
  const City *city;
  bool valid;
  int width, height, cell_count;
  void *storage;
  CityMapDrawingValues values;
  CityMapCamera regions[CITY_EXTRA_MAP_VIEW_COUNT + 1];
} CityMapDrawingSnapshot;

// An uninitialized snapshot or a different City requires full damage instead of layer comparison.
void FilterDrawingMapChanges(const City *city, SimulationMapChanges *changes,
    const CityMapDrawingSnapshot *snapshot);
// Reuse the backing allocation when dimensions match. Failure retains the previous allocation.
bool PrepareCityMapDrawingSnapshot(const City *city, CityMapDrawingSnapshot *snapshot);
bool PrepareCityMapDrawingStorage(const City *city);
void ReleaseCityMapDrawingStorage(Drawing *drawing);
// viewport is 0 for the main map or 1..CITY_EXTRA_MAP_VIEW_COUNT for an auxiliary camera.
// Advance snapshots after damage has been projected for every viewport; never changes City.
void UpdateDrawingMapSnapshot(const City *city, CityMapDrawingSnapshot *snapshot,
    int viewport, CityMapCamera region);

// Tile must be valid. Emit clipped rectangles in the supplied camera's logical map pixels.
void ProjectCityMapTileDamage(const City *city, TileIndex tile, CityMapCamera region,
    CityCanvasVisiblePaint mark, void *context);

// Project bulk simulation notifications into each viewport before acknowledging them.
// Partial updates require a valid snapshot for this City and its current dimensions.
// Both projection functions call mark synchronously; it records damage without modifying City or the camera.
void ProjectSimulationMapDamage(const City *city, const SimulationMapChanges *changes,
    const CityMapDrawingSnapshot *snapshot,
    CityMapCamera region, bool data_overlay, CityCanvasVisiblePaint mark, void *context);


// Map traversal

// Conservative world-cell bounds for a screen clip, including sprites and labels.
CityRect VisibleCityMapTileBounds(const City *city, int origin_x, int origin_y, int zoom, int rotation, CityRect area);
// Prepare sprite bounds and event anchors, then paint clip-visible tiles in isometric order for the active map mode.
// Scratch storage follows the event pool, never map area. Animation capture uses scan_clip instead of the paint rect.
void PaintVisibleCityMapTiles(City *city, const CityMapViewConfig *view, int zoom);


// Map presentation entry points.

CityRect Sc2kCityOverlayLabelBounds(const Drawing *drawing, int zoom, int screen_x, int screen_y, const char *text);
void SetCityMapLabelStyle(Drawing *drawing, bool enabled);

void InvalidateCityMapViews(Drawing *drawing);

// Data layers and their compact preview images.
void BuildCityMapDefaultOverlay(const City *city, u8 *pixels, int row_stride);
void BuildCityMapZoneOverlay(const City *city, u8 *pixels, int row_stride);
void BuildCityMapResidentialOverlay(const City *city, u8 *pixels, int row_stride);
void BuildCityMapCommercialOverlay(const City *city, u8 *pixels, int row_stride);
void BuildCityMapTrafficDensityOverlay(const City *city, u8 *pixels, int row_stride);
void BuildCityMapIndustrialOverlay(const City *city, u8 *pixels, int row_stride);
void BuildCityMapUndergroundOverlay(const City *city, u8 *pixels, int row_stride);
void BuildCityMapDensityOverlay(const City *city, u8 *pixels, int row_stride);
void BuildCityMapDensityHistoryOverlay(const City *city, u8 *pixels, int row_stride);
void BuildCityMapPollutionOverlay(const City *city, u8 *pixels, int row_stride);
void BuildCityMapPoliceCoverageOverlay(const City *city, u8 *pixels, int row_stride);
void BuildCityMapLandValueOverlay(const City *city, u8 *pixels, int row_stride);
void BuildCityMapCrimeOverlay(const City *city, u8 *pixels, int row_stride);
void BuildCityMapFireCoverageOverlay(const City *city, u8 *pixels, int row_stride);
void BuildCityMapTileTypeHighlight(const City *city, u8 *pixels, int row_stride, int tile_id);

// Map-view pixel rectangles schedule damage in the existing screen grid.
void InvalidateCityMapRect(Drawing *drawing, const CityMapViewConfig *view, CityRect rect);

void Sc2kSetCityMapClientRect(Drawing *drawing, CityRect rect);
void Sc2kGetCityMapClientRect(Drawing *drawing, CityRect *rect);
// Intersect logical map pixels with the client rectangle; an empty request selects the whole client.
// A nonempty request outside the client clears paint_clip and returns false. Successful preparation preserves the
// one-pixel top overlap used by immediate previews and selects paint_rect for traversal; the sprite target is separate.
bool PrepareCityMapPaintClip(Drawing *drawing, CityRect request);
void ResetCityMapPaintClip(Drawing *drawing);

// Overlay and tile painting at the prepared target.
void DrawCityMapOverlay(City *city, int zoom, int x, int y, int screen_x, int screen_y);
// Paint the surface in the active map target. Zoom selects artwork; view supplies the camera configuration.
// Traversal derives tile ranges from the logical paint rectangle and preserves the caller's scan clip.
void DrawCityMapSurface(City *city, const CityMapViewConfig *view, int zoom);
void DrawCityMapData(City *city, int zoom);
// Paint the active underground view, binding its palette and preserving the caller's scan clip.
void DrawCityMapUnderground(City *city, int zoom);

// Decode an installed map-cache source into caller-owned ARGB pixels (size by size; size must exceed four).
// Missing/out-of-range artwork or sprite preparation failure returns false; temporary sprites are released.
bool DecodeCityMapIcon(const Drawing *drawing, int sprite_id, const u32 palette[256], u32 *icon, int size);

#endif
