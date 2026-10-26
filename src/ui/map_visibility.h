#ifndef SC2K_UI_MAP_VISIBILITY_H
#define SC2K_UI_MAP_VISIBILITY_H

#include "drawing/maps/map_cache.h"

typedef struct CityMapPaint CityMapPaint;
typedef struct CityCanvasDesktop CityCanvasDesktop;

// Main-map input admission in logical map pixels. Requires an active city, no blocking dialog,
// and a point outside menus and visible windows, including their existing shadow margins.
// This does not determine sound audibility or the visibility of an extra map window.
bool IsCityMainMapInteractiveAt(const City *city, int x, int y);

// Resolve the main map (window_id zero) or an extra map window's visible pixels before invoking Drawing's cache.
// Logical target rectangles are rounded outwards and unioned after magnification; physical targets stay in pixels.
// Borrow pixels and paint context synchronously. Fragment overflow invalidates the cache and paints visible pieces.
void DrawCityMapCache(NativeMapCache *cache, const CityCanvasDesktop *desktop, u32 window_id,
    int width, int height, int magnification, int origin_x, int origin_y,
    const MapCachePixels *pixels, CityCanvasVisiblePaint paint, void *context);
// Clip framebuffer pixels to the map client area; the main map keeps the supplied rectangle.
CityRect ClipCityMapViewportPixels(CityRect rect, const CityMapPaint *paint);
// Convert view-local logical pixels to framebuffer pixels, including pending scroll, then clip to that window.
CityRect CityMapScreenRect(CityRect rect, const CityMapPaint *paint);
// Aircraft audio must have a visible plane in an unobscured map window.
bool IsCityAirplaneVisible(City *city, int slot);

// Tile-local audio is audible only through an unobscured map view containing its source.
bool IsCityMapSoundVisible(City *city, TileIndex tile);

// Horizontal balance in a visible map window; city-wide/off-screen effects remain centered.
u8 CityMapSoundPan(City *city, TileIndex tile);

#endif
