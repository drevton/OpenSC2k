#ifndef SC2K_DOMAIN_SRC_DRAWING_MAPS_MAP_RENDER_H
#define SC2K_DOMAIN_SRC_DRAWING_MAPS_MAP_RENDER_H

#include "city/maps/map.h"
#include "drawing/canvas.h"
#include "drawing/maps/map_camera.h"
#include "drawing/maps/map_terrain.h"
#include "drawing/maps/map_view.h"
#include "drawing/sprites/sprites.h"
#include "city/types.h"

enum { CITY_EXTRA_MAP_VIEW_COUNT = 8 };

typedef struct Game Game;
typedef struct CityCanvasDesktop CityCanvasDesktop;

void ToggleCityMapLayer(Game *game, CityMapLayer layer);

typedef struct { u32 face, light, dark, ink; } CitySignColors;
CitySignColors CitySignColorStyle(int color);
void DrawCapturedCitySigns(City *city);
CityRect CitySignPixelBounds(const City *city, CityPoint anchor, const char *text);
CityRect CitySignMapBounds(const City *city, int zoom, int screen_x, int screen_y, const char *text);
// Events

void InvalidateCityMapEvent(City *city, int slot);
// Update timestamps use GAME_TIME_UNPAUSED; these effects freeze with the world.
void ClearCityVehicleVisualization(Drawing *drawing);
bool UpdateCityVehicleAnimation(const City *city, double now);
// Presentation seconds in wrapping Q8, converted once by the host caller.
bool UpdateCityRoadTraffic(City *city, u32 now);
void PopulateCityRoadTrafficView(City *city, int index, CityMapCamera previous, CityMapCamera view);
void CollectCityRoadTraffic(City *city, int zoom);
void DrawCityRoadCar(City *city, int slot, int zoom);
void RefreshCityRoadTrafficPoses(City *city);
struct RoadCar;
// Exact prepared sprite bounds in framebuffer pixels, relative to the viewport origin.
CityRect CityRoadCarPixelBounds(const City *city, const struct RoadCar *car, CityMapCamera view, int pixel_scale);
i32 CityMapOverlayDepth(const City *city, TileIndex tile);
// Camera-space centre in 1/4096 tile units, and elevation in 1/4096 height units.
i32 CityMapObjectDepth(int x, int y, int height);
// Separate deck and near-side structure depths leave room for traffic on the bridge.
i32 CityMapBridgeArtworkDepth(const City *city, TileIndex tile, bool foreground);
// Prepared commands keep immutable palette IDs, never pointers to temporary colors.
bool PrepareCityMapSprite(Drawing *drawing, int sprite, int x, int y, u32 flags, int palette);
void DrawPreparedCityMapSprites(Drawing *drawing);
void ApplyCityMapSelectionPalette(SpriteSurface *surface, int style);
void DrawDepthSortedCityMap(City *city, const CityMapViewConfig *view, int zoom);
void StartCityScreenShake(Drawing *drawing);
bool UpdateCityScreenShake(Drawing *drawing, double now);
bool UpdateCityPowerAnimation(const City *city, double now);
void ShowCityDemolitionEffect(City *city, TileIndex tile, bool large);
bool UpdateCityDemolitionEffects(Drawing *drawing, double now);


// Map Animation

// Presentation history only; reusable storage follows visible effects.
typedef struct {
  CityRect *bounds;
  int count, capacity;
  int origin_x, origin_y, zoom;
} CityMapAnimationFootprints;

bool IsCityMapOverlayAnimated(const City *city, int overlay);
CityRect CapturedCityMapAnimationBounds(const Drawing *drawing, TileIndex tile);
void BeginCityMapAnimationCapture(Drawing *drawing);
void EndCityMapAnimationCapture(const City *city, Drawing *drawing);
void ReplayCityMapAnimations(Drawing *drawing);
void FinishCityMapAnimations(City *city, Drawing *drawing);
void PublishCityMapAnimationDamage(City *city, const CityMapViewConfig *view,
    CityMapAnimationFootprints *previous,
    CityCanvasVisiblePaint mark, void *context);
// Map Cache

typedef struct {
  CityRect fragments[1024];
  i32 count;
  i32 valid;
  i32 origin_x;
  i32 origin_y;
  i32 magnification;
  int target_x, target_y, target_scale;
  u64 painted_pixels;
  u64 reused_pixels;
  u64 copied_pixels;
  u64 full_refreshes;
} NativeMapCache;

typedef struct {
  SpriteSurface *surface;
  u32 *scratch_colors;
  u8 *scratch_indices;
  int scale, x, y;
  bool physical_coordinates;
  bool preserved_source; // Shared frame-start snapshot, before any camera repaints its old screen area.
} MapCachePixels;
void SaveMapCacheScrollSource(MapCachePixels *pixels);
void DrawMapCachePixels(Drawing *drawing, NativeMapCache *cache, const CityCanvasDesktop *desktop, u32 window_id,
    u8 *diagnostic_indices, int stride, int width, int height, int magnification, int origin_x, int origin_y,
    MapCachePixels *pixels, void (*paint)(CityRect, void *), void *context);

// Visible display rectangles, rounded outwards and unioned in source pixels.
void NativeMapCacheVisitView(const CityCanvasDesktop *desktop, u32 window_id, CityRect display_area,
    i32 magnification, void (*paint)(CityRect, void *), void *context);


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
  City *city;
  bool valid;
  int width, height, cell_count;
  void *storage;
  CityMapDrawingValues values;
  CityMapCamera regions[CITY_EXTRA_MAP_VIEW_COUNT + 1];
} CityMapDrawingSnapshot;

void FilterDrawingMapChanges(City *city, SimulationMapChanges *changes, CityMapDrawingSnapshot *snapshot);
bool PrepareCityMapDrawingSnapshot(City *city, CityMapDrawingSnapshot *snapshot);
bool PrepareCityMapDrawingStorage(City *city);
bool PrepareCityEventDrawingStorage(const City *city);
void ReleaseCityMapDrawingStorage(Drawing *drawing);
void UpdateDrawingMapSnapshot(City *city, CityMapDrawingSnapshot *snapshot,
    int viewport, CityMapCamera region);

void ProjectCityMapTileDamage(City *city, TileIndex tile, CityMapCamera region,
    CityCanvasVisiblePaint mark, void *context);

// Project bulk simulation notifications into each viewport before acknowledging them.
// Damage is scheduled in one screen-level grid, not in per-viewport grids.
void ProjectSimulationMapDamage(City *city, const SimulationMapChanges *changes,
    const CityMapDrawingSnapshot *snapshot,
    CityMapCamera region, bool data_overlay, CityCanvasVisiblePaint mark, void *context);


// Map traversal

// Conservative world-cell bounds for a screen clip, including sprites and labels.
CityRect VisibleCityMapTileBounds(City *city, int origin_x, int origin_y, int zoom, int rotation, CityRect area);
// Prepare sprite bounds and event anchors, then paint clip-visible tiles in isometric order for the active map mode.
// Scratch storage follows the event pool, never map area. Animation capture uses scan_clip instead of the paint rect.
void PaintVisibleCityMapTiles(City *city, const CityMapViewConfig *view, int zoom);


// Refresh

void FlushCityMapSelectionRedraw(City *city);

void CenterCityMapOnEvent(City *city, int x, int y);

// Map presentation entry points.
typedef struct CityZoneCostPresentation CityZoneCostPresentation;

CityRect Sc2kCityOverlayLabelBounds(const Drawing *drawing, int zoom, int screen_x, int screen_y, const char *text);
void SetCityMapLabelStyle(Drawing *drawing, bool enabled);
CityRect Sc2kCityMapEventBounds(City *city, int zoom, int x, int y, int slot);

// Temporary preview target.
void Sc2kSetCityLinePreviewSurface(Drawing *drawing, CityPaintBuffer pixels, u32 stride, CityRect clip);
void Sc2kClearCityLinePreviewSurface(Drawing *drawing);

void InvalidateCityMapViews(Drawing *drawing);

// Data layers and their compact preview images.
void BuildCityMapDefaultOverlay(const City *city, u8 *pixels, int row_stride);
void ApplyCityMapDataModeSelection(Drawing *drawing, int selection);
void BuildCityMapZoneOverlay(const City *city, u8 *pixels, int row_stride);
bool BuildCityMapDataPreview(const City *city, int selection, u32 *pixels);
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

// Target geometry and framebuffer composition.
i32 GetCityMapClientRect(Drawing *drawing, CityRect *rect);
bool RefreshCityMapDataPreview(City *city);

void Sc2kCanvasBlitCitySurface(Canvas *canvas, CityMapViewConfig *view,
    CityRect destination, CityRect source, const u32 *palette);

// Map-view pixel rectangles schedule damage in the existing screen grid.
void InvalidateCityMapRect(Drawing *drawing, const CityMapViewConfig *view, CityRect rect);
void InvalidateCityMapSelectionTile(City *city, int x, int y);
void InvalidateCityMapSelectionRectangle(City *city, int first_x, int first_y,
    int last_x, int last_y);

void Sc2kSetCityMapClientRect(Drawing *drawing, CityRect rect);
void Sc2kGetCityMapClientRect(Drawing *drawing, CityRect *rect);
bool PrepareCityMapClippedPaint(Drawing *drawing, CityPaintBuffer pixels,
    u32 pixel_stride, int mode_word, CityRect *requested);
i32 StoreCityMapPaintParameters(Drawing *drawing, CityPaintBuffer pixels, CitySpriteCatalog catalog, u32 pixel_stride,
    int mode_word, CityRect *rect);
void ResetCityMapPaintClip(Drawing *drawing);

bool EnsureCityMapViewSurface(Drawing *drawing, CityMapViewConfig *view);
CityPaintBuffer BorrowCityMapViewPixels(Drawing *drawing, CityMapViewConfig *view);
CityMapImmediatePaintState ReadCityMapImmediatePaint(const CityMapViewConfig *view);
static inline CityPaintBuffer CityImmediatePaintPixels(const CityMapImmediatePaintState *paint) {
  return paint->pixels;
}

// Overlay and tile painting at the prepared target.
void DrawCityMapAnimatedOverlay(City *city, int zoom, int x, int y, int overlay);
void DrawCityMapEventOverlay(City *city, int zoom, int x, int y, int slot);
void DrawCityMapEventStaticSprite(City *city, int zoom, int x, int y, int slot);
void DrawCityMapEventTileDetail(City *city, int zoom, int x, int y, int slot);
void DrawCityMapEventRandomSprite(City *city, int zoom, int x, int y, int slot);
void DrawCityMapEventStructure(City *city, int zoom, int x, int y, int slot);
void DrawCityMapEventStructureSprite(City *city, int zoom, int sprite_id, int x, int y, int flipped);
void DrawCityMapOverlay(City *city, int zoom, int x, int y, int screen_x, int screen_y);
// Paint the surface in the active map target. Zoom selects artwork; view supplies the camera configuration.
// Traversal derives tile ranges from the logical paint rectangle and preserves the caller's scan clip.
void DrawCityMapSurface(City *city, const CityMapViewConfig *view, int zoom);
void DrawCityMapData(City *city, int zoom);
// Paint the active underground view, binding its palette and preserving the caller's scan clip.
void DrawCityMapUnderground(City *city, int zoom);

// Transient placement cost labels.
void ClearCityZoneCostLabel(Drawing *drawing, const CityMapViewConfig *view);
void DrawCityZoneCostLabel(City *city, const CityZoneCostPresentation *presentation, int cost,
    int screen_x, int screen_y);
void DrawCityCanvasZoneCostLabel(City *city, Canvas *canvas, CityMapViewConfig *view, int cost, const char *text,
    int screen_x, int screen_y);
void Sc2kShowZoneDragCost(City *city, CityMapViewConfig *view, u32 cost, int screen_x, int screen_y);

#endif
