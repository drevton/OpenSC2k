#ifndef SC2K_MAP_CAMERA_H
#define SC2K_MAP_CAMERA_H

#include "city/maps/map.h"
#include "city/types.h"

typedef struct City City;
typedef struct Drawing Drawing;
typedef struct CityMapViewConfig CityMapViewConfig;

typedef struct {
  int origin_x, origin_y, zoom, width, height, rotation;
} CityMapCamera;

// Quarter turns relative to the stored world. These transforms never write City.
SC2K_PRESERVE_MOST int CityMapViewRotation(const CityMapViewConfig *view);
SC2K_PRESERVE_MOST int CityMapCameraRotation(const Drawing *drawing);
SC2K_PRESERVE_MOST CityPoint CityMapCameraSize(const City *city, int rotation);
SC2K_PRESERVE_MOST CityPoint CityMapCameraPoint(const City *city, int rotation, int x, int y);
SC2K_PRESERVE_MOST CityPoint CityMapWorldPoint(const City *city, int rotation, int x, int y);
SC2K_PRESERVE_MOST CityPoint CityMapCameraVector(int rotation, int x, int y);
TileIndex PickCityMapCameraDiagonalTile(City *city, int rotation, int diagonal_x, int diagonal_y, int zoom);
TileIndex CityMapCameraTile(const City *city, int rotation, int x, int y);
CityPoint CityMapViewTilePoint(const City *city, TileIndex tile);
TileIndex CityMapViewNeighbor(const City *city, TileIndex tile, int dx, int dy);
int CityMapCameraShape(int shape, int rotation);
SC2K_PRESERVE_MOST int CityMapCameraMask(int mask, int rotation);
typedef struct CityResourceState CityResourceState;
int CityMapCameraSprite(const CityResourceState *assets, int layout, int rotation);
int CityMapViewTileFlip(const City *city, TileIndex tile);
// Resolve source cache slots in world space or the active view, independently of zoom.
int CityMapTileSpriteIndex(const City *city, TileIndex tile);
int CityMapViewTileSpriteIndex(const City *city, TileIndex tile);
CityTileGeometry ReadCityMapViewGeometry(const City *city, TileIndex tile);
// Underground drawing, picking and selection share the stored land/bed in the active camera.
// Elevation is in map levels; u/v span one tile from 0 to 1. Selection can construct fractional profiles from pixels,
// so these presentation coefficients retain fractional values even though stored terrain corners are integers.
typedef struct { int shape, elevation; } CityMapGround;
typedef struct { double coefficient[2][2]; int elevation, shape; } CityMapUnderworldPatch;
CityMapGround CityMapUnderworldGround(const City *city, TileIndex tile);
CityMapUnderworldPatch CityMapUnderworldSurface(const City *city, TileIndex tile);
double CityMapUnderworldHeight(const CityMapUnderworldPatch *patch, double u, double v);
int CityMapUnderworldShape(const CityMapUnderworldPatch *patch);
int CityMapViewTerrainCode(const City *city, TileIndex tile);
int CityMapViewUnderground(const City *city, TileIndex tile);
int CityMapViewOrientation(const City *city, TileIndex tile);
// The leftmost visible footprint corner, used to paint shared building artwork once.
bool IsCityMapViewFootprintAnchor(const City *city, TileIndex tile);
// Camera-only controls; preserve the world point under the view center.
TileIndex PickCityMapCameraPoint(City *city, CityMapCamera camera, CityPoint point);
CityPoint TransferCityMapCameraCenter(City *city, CityMapCamera source, CityMapCamera target);
CityPoint ClampCityMapCameraOrigin(const City *city, CityMapCamera camera);
void RotateCityMapViewport(City *city, u32 window_id, bool clockwise);
void RotateCityMapView(City *city, CityMapViewConfig *view, bool clockwise);

// Projection and local camera controls. These functions read geometry or change the view, never world tile data.
void CityMapTileCorners(const City *city, TileIndex tile, int zoom, int points[8]);

int *CityMapTileToScreen(City *city, const CityMapViewConfig *view, int x, int y, int *screen_x, int *screen_y);
i32 UpdateCityMapViewScrollPosition(City *city, CityMapViewConfig *view);
void MaskCityMapOriginForZoom(Drawing *drawing, CityMapViewConfig *view);
void OffsetCityMapOrigin(City *city, CityMapViewConfig *view, int delta_x, int delta_y);

// NULL selects the default map view.
i32 CenterCityMapViewAtTile(City *city, CityMapViewConfig *view, int x, int y);
void ZoomCityMapViewOut(City *city, CityMapViewConfig *view);
void ZoomCityMapViewIn(City *city, CityMapViewConfig *view);

i32 CenterCityMapViewAndResetOverlays(City *city, int x, int y, int zoom);

int CityMapTileElevation(City *city, int x, int y);
int CityMapDiagonalToTileSmall(City *city, int diagonal_x, int diagonal_y);
int CityMapDiagonalToTileMedium(City *city, int diagonal_x, int diagonal_y);
int CityMapDiagonalToTileLarge(City *city, int diagonal_x, int diagonal_y);
int CityMapScreenToTile(City *city, const CityMapViewConfig *view, int screen_x, int screen_y);
TileIndex PickCityMapTile(City *city, const CityMapViewConfig *view, int screen_x, int screen_y);
TileIndex PickCityMapTerrainPoint(City *city, const CityMapViewConfig *view, int screen_x, int screen_y);
int ReadCityMapTerrainPointHeight(const City *city, TileIndex tile);

// Rotate the focused camera without changing world tile data.
void RotateFocusedCityMapView(City *city, bool clockwise);

// Eight height pixels per 64-pixel tile, without changing City elevations.
enum { CITY_MAP_HEIGHT_UNIT = 1 };
static inline int CityMapHeightStep(int zoom) { return CITY_MAP_HEIGHT_UNIT << zoom; }

#endif
