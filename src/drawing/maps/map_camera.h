#ifndef SC2K_MAP_CAMERA_H
#define SC2K_MAP_CAMERA_H

#include "city/types.h"

typedef struct Drawing Drawing;
typedef struct CityTileGeometry CityTileGeometry;
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
// Reuse this tile's decoded camera geometry for native dry land; visible water returns -1.
// Raw stock records retain their combined-code interpretation, with water codes normalized to -1.
int CityMapDryGroundShape(const City *city, TileIndex tile, const CityTileGeometry *geometry);
int CityMapViewUnderground(const City *city, TileIndex tile);
int CityMapViewOrientation(const City *city, TileIndex tile);
// The leftmost visible footprint corner, used to paint shared building artwork once.
bool IsCityMapViewFootprintAnchor(const City *city, TileIndex tile);
// Pick logical camera pixels or transfer the world point at its center. Neither operation binds or modifies a view.
// Terrain, layer visibility and underground mode come from the supplied City and its Drawing options.
TileIndex PickCityMapCameraPoint(const City *city, CityMapCamera camera, CityPoint point);
CityPoint TransferCityMapCameraCenter(const City *city, CityMapCamera source, CityMapCamera target);
// Inclusive origin bounds for the supplied viewport, zoom and rotation; no map scan.
CityRect CityMapOriginBounds(const City *city, CityMapCamera camera);
CityPoint ClampCityMapCameraOrigin(const City *city, CityMapCamera camera);

// Projection reads geometry without changing the camera or world tile data.
void CityMapTileCorners(const City *city, TileIndex tile, int zoom, int points[8]);

// Project a valid world tile into logical screen pixels. Drawing supplies the bound origin; view supplies rotation
// and artwork zoom. Magnification doubles the final integer coordinates. City and view remain unchanged.
CityPoint CityMapTileToScreen(const City *city, const CityMapViewConfig *view, CityPoint world);

// Invalid world coordinates return zero. Underground uses land height; surface mode uses the greater visible
// land/water height. Drawing supplies layer visibility and the bound camera; no state changes occur.
int CityMapTileElevation(const City *city, int x, int y);
// Pick a surface tile or closest terrain marker from this view's canvas coordinates; no City/view writes or allocation.
// Missing hits return INVALID_TILE. Display scale, fractional scroll and layer options come from Drawing.
TileIndex PickCityMapTile(const City *city, const CityMapViewConfig *view, int screen_x, int screen_y);
TileIndex PickCityMapTerrainPoint(const City *city, const CityMapViewConfig *view, int screen_x, int screen_y);
int ReadCityMapTerrainPointHeight(const City *city, TileIndex tile);

// Eight height pixels per 64-pixel tile, without changing City elevations.
enum { CITY_MAP_HEIGHT_UNIT = 1 };
static inline int CityMapHeightStep(int zoom) { return CITY_MAP_HEIGHT_UNIT << zoom; }

#endif
