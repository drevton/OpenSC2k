#include "city/maps/layers.h"
#include "city/infrastructure/surface_lines.h"
#include "city/infrastructure/types.h"
#include "city/maps/tile_storage.h"
#include "drawing/maps/map_view.h"
#include "city/maps/tables.h"
#include "game/errors.h"
#include "ui/map.h"
#include "ui/tools/tools.h"
#include "drawing/drawing_internal.h"
#include "drawing/maps/map_render.h"

// Drawing subsystem: map coordinates.

// World coordinates stay fixed; camera coordinates use the existing one-direction
// isometric renderer. Rectangular map axes swap only in this projection.
SC2K_PRESERVE_MOST int CityMapViewRotation(const CityMapViewConfig *view) {
  return view->rotation;
}

SC2K_PRESERVE_MOST int CityMapCameraRotation(const Drawing *drawing) {
  return CityMapViewRotation(drawing->default_map_view);
}

SC2K_PRESERVE_MOST CityPoint CityMapCameraSize(const City *city, int rotation) {
  return rotation & 1 ? (CityPoint){city->map_height, city->map_width} :
      (CityPoint){city->map_width, city->map_height};
}

SC2K_PRESERVE_MOST CityPoint CityMapCameraPoint(const City *city, int rotation, int x, int y) {
  switch (rotation) {
  case 1: return (CityPoint){city->map_height - 1 - y, x};
  case 2: return (CityPoint){city->map_width - 1 - x, city->map_height - 1 - y};
  case 3: return (CityPoint){y, city->map_width - 1 - x};
  default: return (CityPoint){x, y};
  }
}

SC2K_PRESERVE_MOST CityPoint CityMapWorldPoint(const City *city, int rotation, int x, int y) {
  switch (rotation) {
  case 1: return (CityPoint){y, city->map_height - 1 - x};
  case 2: return (CityPoint){city->map_width - 1 - x, city->map_height - 1 - y};
  case 3: return (CityPoint){city->map_width - 1 - y, x};
  default: return (CityPoint){x, y};
  }
}

SC2K_PRESERVE_MOST CityPoint CityMapCameraVector(int rotation, int x, int y) {
  switch (rotation & 3) {
  case 1: return (CityPoint){-y, x};
  case 2: return (CityPoint){-x, -y};
  case 3: return (CityPoint){y, -x};
  default: return (CityPoint){x, y};
  }
}

TileIndex CityMapCameraTile(const City *city, int rotation, int x, int y) {
  CityPoint point = CityMapWorldPoint(city, rotation, x, y);
  return MapTileXYOrInvalid(city, point.x, point.y);
}

CityPoint CityMapViewTilePoint(const City *city, TileIndex tile) {
  return CityMapCameraPoint(city, CityMapCameraRotation(city->game->drawing), MapTileX(city, tile), MapTileY(city, tile));
}

TileIndex CityMapViewNeighbor(const City *city, TileIndex tile, int dx, int dy) {
  CityPoint offset = CityMapCameraVector(-CityMapCameraRotation(city->game->drawing), dx, dy);
  return GetRelativeTileOrInvalid(city, tile, offset.x, offset.y);
}

int CityMapCameraShape(int shape, int rotation) {
  if (!rotation || (unsigned)shape >= CITY_TERRAIN_ROTATION_COUNT) return shape;
  const CityMapRotationTables *tables = ReadCityMapRotationTables();
  for (int turn = 0; turn < rotation; ++turn) shape = tables->terrain_clockwise[shape];
  return shape;
}

SC2K_PRESERVE_MOST int CityMapCameraMask(int mask, int rotation) {
  return ((mask >> rotation) | (mask << (4 - rotation))) & 15;
}

CityTileGeometry ReadCityMapViewGeometry(const City *city, TileIndex tile) {
  CityTileGeometry geometry = ReadCityTileGeometry(city, tile);
  if (!city->game->drawing->map_state.water_enabled) {
    geometry.water = CITY_WATER_NONE;
    geometry.water_height = 0;
    geometry.water_surface = CITY_WATER_SURFACE_NONE;
    geometry.surface_height = geometry.land_height;
    geometry.surface_shape = geometry.land_shape;
  }
  int rotation = CityMapCameraRotation(city->game->drawing);
  geometry.land_shape = CityMapCameraShape(geometry.land_shape, rotation);
  geometry.surface_shape = CityMapCameraShape(geometry.surface_shape, rotation);
  return geometry;
}

int CityMapViewTerrainCode(const City *city, TileIndex tile) {
  if (!city->game->drawing->map_state.water_enabled) return ReadCityMapViewGeometry(city, tile).land_shape;
  return CityMapCameraShape(ReadLegacyTerrainCode(city, tile), CityMapCameraRotation(city->game->drawing));
}

int CityMapViewUnderground(const City *city, TileIndex tile) {
  int kind = ReadUnderground(city, tile);
  if (kind >= CITY_UNDERGROUND_ROTATION_COUNT) return kind;
  const CityMapRotationTables *tables = ReadCityMapRotationTables();
  for (int turn = 0; turn < CityMapCameraRotation(city->game->drawing); ++turn) kind = tables->underground_clockwise[kind];
  return kind;
}

int CityMapViewOrientation(const City *city, TileIndex tile) {
  return (ReadBuildingOrientation(city, tile) + CityMapCameraRotation(city->game->drawing)) & 3;
}

bool IsCityMapViewFootprintAnchor(const City *city, TileIndex tile) {
  return IsCityFootprintCorner(city, tile, (CITY_BUILDING_CORNER_TOP_RIGHT + CityMapCameraRotation(city->game->drawing)) & 3);
}

static const u8 kGroundCorners[] = {0, 9, 3, 6, 12, 11, 7, 14, 13, 1, 2, 4, 8, 15, 15, 0};

CityMapGround CityMapUnderworldGround(const City *city, TileIndex tile) {
  CityTileGeometry geometry = ReadCityMapViewGeometry(city, tile);
  return (CityMapGround){geometry.land_shape, geometry.land_height};
}

static const u8 kUnderworldCorners[CITY_UNDERWORLD_SHAPES][4] = {
  {0, 0, 0, 0}, {1, 0, 0, 1}, {1, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 1, 1},
  {1, 1, 0, 1}, {1, 1, 1, 0}, {0, 1, 1, 1}, {1, 0, 1, 1},
  {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1, 1},
  {1, 0, 1, 0}, {0, 1, 0, 1},
  {0, 1, 2, 1}, {1, 0, 1, 2}, {2, 1, 0, 1}, {1, 2, 1, 0},
};

double CityMapUnderworldHeight(const CityMapUnderworldPatch *patch, double u, double v) {
  return patch->coefficient[0][0] + patch->coefficient[1][0] * u + patch->coefficient[0][1] * v +
      patch->coefficient[1][1] * u * v;
}

int CityMapUnderworldShape(const CityMapUnderworldPatch *patch) {
  double heights[] = {CityMapUnderworldHeight(patch, 0, 0), CityMapUnderworldHeight(patch, 0, 1),
      CityMapUnderworldHeight(patch, 1, 1), CityMapUnderworldHeight(patch, 1, 0)};
  for (int shape = 0; shape < CITY_UNDERWORLD_SHAPES; ++shape) {
    bool match = true;
    for (int corner = 0; corner < 4; ++corner)
      if (heights[corner] != kUnderworldCorners[shape][corner]) match = false;
    if (match) return shape;
  }
  return CITY_TERRAIN_FLAT;
}

CityMapUnderworldPatch CityMapUnderworldSurface(const City *city, TileIndex tile) {
  CityMapGround ground = CityMapUnderworldGround(city, tile);
  CityMapUnderworldPatch patch = {.elevation = ground.elevation, .shape = ground.shape};
  // Drawing, picking and selection describe this tile's stored corners.
  int heights[4];
  for (int corner = 0; corner < 4; ++corner) heights[corner] = (kGroundCorners[ground.shape] >> corner) & 1;
  patch.coefficient[0][0] = heights[0];
  patch.coefficient[1][0] = heights[3] - heights[0];
  patch.coefficient[0][1] = heights[1] - heights[0];
  patch.coefficient[1][1] = heights[0] - heights[1] - heights[3] + heights[2];
  return patch;
}

static bool UnderworldContainsPoint(const City *city, TileIndex tile, int zoom, int screen_x, int screen_y) {
  const Drawing *drawing = city->game->drawing;
  CityMapUnderworldPatch patch = CityMapUnderworldSurface(city, tile);
  CityPoint point = CityMapViewTilePoint(city, tile);
  int scale = 1 << zoom, x = point.x, y = point.y;
  double px = (screen_x - drawing->map_state.origin_x - 4 * scale * (y - x)) / (8.0 * scale);
  double py = (screen_y - drawing->map_state.origin_y - 2 * scale * (y + x) +
      CityMapHeightStep(zoom) * patch.elevation) / (8.0 * scale);
  if (px < 0 || px >= 1 || py < -0.625 || py >= 0) return false;
  double d = 2 * px - 1;
  double first = d < 0 ? -d : 0, last = d > 0 ? 1 - d : 1;
  double entry = (2 * first + d) * 0.25 - 0.5 - CityMapUnderworldHeight(&patch, first, first + d) * 0.125;
  double exit = (2 * last + d) * 0.25 - 0.5 - CityMapUnderworldHeight(&patch, last, last + d) * 0.125;
  return py >= entry && py < exit;
}

void CityMapTileCorners(const City *city, TileIndex tile, int zoom, int points[8]) {
  const Drawing *drawing = city->game->drawing;
  CityPoint point = CityMapViewTilePoint(city, tile);
  int x = point.x, y = point.y, scale = 1 << zoom;
  CityTileGeometry geometry = ReadCityMapViewGeometry(city, tile);
  bool water = !drawing->map_state.events_suppressed && geometry.water != CITY_WATER_NONE;
  int elevation = water ? geometry.surface_height : geometry.land_height;
  // Raised shores and stream waterfalls retain the hillside.
  int shape = geometry.surface_shape;
  if (drawing->map_state.events_suppressed) {
    CityMapUnderworldPatch patch = CityMapUnderworldSurface(city, tile);
    elevation = patch.elevation;
    int left = drawing->map_state.origin_x + 4 * scale * (y - x);
    int bottom = drawing->map_state.origin_y + 2 * scale * (y + x) - CityMapHeightStep(zoom) * elevation;
    const int u[] = {0, 0, 1, 1}, v[] = {0, 1, 1, 0};
    for (int corner = 0; corner < 4; ++corner) {
      points[2 * corner] = left + 4 * scale * (1 + v[corner] - u[corner]);
      points[2 * corner + 1] = bottom + 2 * scale * (u[corner] + v[corner] - 2) -
          CityMapHeightStep(zoom) * CityMapUnderworldHeight(&patch, u[corner], v[corner]);
    }
    return;
  }
  if (!drawing->map_state.events_suppressed && !water &&
      drawing->map_state.tile_details_enabled) {
    CityMapSurface surface = ResolveCitySurfaceLineViewGround(city, tile, CityMapCameraRotation(drawing));
    shape = surface.shape;
    elevation += surface.raised;
  }
  int raised = kGroundCorners[shape];
  int left = drawing->map_state.origin_x + 4 * scale * (y - x);
  int bottom = drawing->map_state.origin_y + 2 * scale * (y + x) - CityMapHeightStep(zoom) * elevation;
  const int corners[8] = {left + 4 * scale, bottom - 4 * scale,
      left + 8 * scale, bottom - 2 * scale, left + 4 * scale, bottom, left, bottom - 2 * scale};
  for (int point = 0; point < 4; ++point) {
    points[2 * point] = corners[2 * point];
    points[2 * point + 1] = corners[2 * point + 1] - ((raised >> point) & 1) * CityMapHeightStep(zoom);
  }
}

// Elevation used when converting screen diagonals to a map tile.
int CityMapTileElevation(City *city, int x, int y) {
  const Drawing *drawing = city->game->drawing;
  TileIndex tile = MapTileXYOrInvalid(city, x, y);
  if (tile == INVALID_TILE) return 0;
  int cell = ReadAltitudeWord(city, tile);
  int upper = ReadCityWaterHeight(&city->tile[tile]);
  int lower = cell & CITY_ALTITUDE_LAND_MASK;
  if (drawing->map_state.events_suppressed)
    return CityMapUnderworldSurface(city, tile).elevation;
  {
    CityTileGeometry geometry = ReadCityMapViewGeometry(city, tile);
    return IntMax(geometry.land_height, geometry.water_height);
  }
  if (drawing->map_state.events_suppressed != 0 || upper <= lower)
    return lower;
  return upper;
}

static CityPoint PickCityMapDiagonalPoint(City *city, int diagonal_x, int diagonal_y, int zoom,
    bool legacy, int rotation) {
  int step = 4 << zoom, elevation_step = CityMapHeightStep(zoom);
  int diagonal[] = {diagonal_x, diagonal_y};
  int coordinate[] = {diagonal_x / step, diagonal_y / step};
  int edge[] = {step * coordinate[0], step * coordinate[1]};
  CityPoint camera_size = CityMapCameraSize(city, rotation);
  int size[] = {legacy ? CITY_MAP_SIZE : camera_size.x, legacy ? CITY_MAP_SIZE : camera_size.y};
  int limit = legacy ? CITY_MAP_SIZE : CITY_ALTITUDE_LAND_MASK * elevation_step / step + 2;
  int first_axis = diagonal_x >= diagonal_y ? 0 : 1;
  CityPoint chosen = {coordinate[0], coordinate[1]};
  for (int count = 0; count < limit; ++count) {
    for (int turn = 0; turn < 2; ++turn) {
      int axis = first_axis ^ turn;
      edge[axis] += step;
      if (++coordinate[axis] >= size[axis]) return chosen;
      CityPoint world = CityMapWorldPoint(city, rotation, coordinate[0], coordinate[1]);
      if (edge[axis] - elevation_step * CityMapTileElevation(city, world.x, world.y) < diagonal[axis])
        chosen = (CityPoint){coordinate[0], coordinate[1]};
    }
  }
  return chosen;
}

static int CityMapDiagonalToLegacyTile(City *city, int diagonal_x, int diagonal_y, int zoom) {
  CityPoint point = PickCityMapDiagonalPoint(city, diagonal_x, diagonal_y, zoom, true, 0);
  return point.y + 256 * point.x;
}

// Inverse projection for small map tiles.
int CityMapDiagonalToTileSmall(City *city, int diagonal_x, int diagonal_y) {
  return CityMapDiagonalToLegacyTile(city, diagonal_x, diagonal_y, 0);
}

// Inverse projection for medium map tiles.
int CityMapDiagonalToTileMedium(City *city, int diagonal_x, int diagonal_y) {
  return CityMapDiagonalToLegacyTile(city, diagonal_x, diagonal_y, 1);
}

// Inverse projection for large map tiles.
int CityMapDiagonalToTileLarge(City *city, int diagonal_x, int diagonal_y) {
  return CityMapDiagonalToLegacyTile(city, diagonal_x, diagonal_y, 2);
}

TileIndex PickCityMapCameraDiagonalTile(City *city, int rotation, int diagonal_x, int diagonal_y, int zoom) {
  CityPoint point = PickCityMapDiagonalPoint(city, diagonal_x, diagonal_y, zoom, false, rotation);
  return CityMapCameraTile(city, rotation, point.x, point.y);
}

static TileIndex PickCityMapSurface(City *city, const CityMapViewConfig *view,
    int screen_x, int screen_y, bool point) {
  const Drawing *drawing = city->game->drawing;
  int scaled_x = screen_x, scaled_y = screen_y;
  int scale = drawing->map_pixel_scale * (view->magnified_large_zoom ? 2 : 1);
  int x = screen_x * drawing->map_pixel_scale - drawing->scroll_pixels.x;
  int y = screen_y * drawing->map_pixel_scale - drawing->scroll_pixels.y;
  scaled_x = x / scale - (x % scale < 0);
  scaled_y = y / scale - (y % scale < 0);
  // A ray can cross at most the supported elevation range. Test projected
  // surfaces in painter order, independent of total map dimensions.
  int unit = 1 << view->zoom;
  int px = scaled_x - drawing->map_state.origin_x - 4 * unit;
  int py = scaled_y - drawing->map_state.origin_y + 4 * unit;
  int best_diagonal = -1, best_y = -1;
  i64 best_distance = 128 * unit * unit;
  TileIndex best = INVALID_TILE;
  for (int elevation = 0; elevation <= CITY_ALTITUDE_LAND_MASK + 1; ++elevation) {
    int sum = (py + CityMapHeightStep(view->zoom) * elevation) / unit;
    int center_x = (2 * sum - px / unit) / 8;
    int center_y = (2 * sum + px / unit) / 8;
    for (int ty = center_y - 1; ty <= center_y + 1; ++ty)
      for (int tx = center_x - 1; tx <= center_x + 1; ++tx) {
        TileIndex tile = CityMapCameraTile(city, CityMapViewRotation(view), tx, ty);
        if (tile == INVALID_TILE || (!point && (tx + ty < best_diagonal ||
            (tx + ty == best_diagonal && ty <= best_y)))) continue;
        if (drawing->map_state.events_suppressed) {
          if (UnderworldContainsPoint(city, tile, view->zoom, scaled_x, scaled_y)) {
            best = tile; best_diagonal = tx + ty; best_y = ty;
          }
          continue;
        }
        int points[8], positive = 0, negative = 0;
        CityMapTileCorners(city, tile, view->zoom, points);
        if (point) {
          i64 dx = scaled_x - points[0], dy = scaled_y - points[1];
          i64 distance = dx * dx + dy * dy;
          if (distance < best_distance || (distance == best_distance &&
              (tx + ty > best_diagonal || (tx + ty == best_diagonal && ty > best_y)))) {
            best = tile; best_diagonal = tx + ty; best_y = ty; best_distance = distance;
          }
          continue;
        }
        for (int corner = 0; corner < 4; ++corner) {
          int next = (corner + 1) & 3;
          i64 cross = (i64)(points[2 * next] - points[2 * corner]) * (scaled_y - points[2 * corner + 1]) -
              (i64)(points[2 * next + 1] - points[2 * corner + 1]) * (scaled_x - points[2 * corner]);
          positive |= cross > 0; negative |= cross < 0;
        }
        if (!(positive && negative)) { best = tile; best_diagonal = tx + ty; best_y = ty; }
      }
  }
  return best;
}

TileIndex PickCityMapTile(City *city, const CityMapViewConfig *view, int screen_x, int screen_y) {
  return PickCityMapSurface(city, view, screen_x, screen_y, false);
}

// Point tools snap to the closest projected marker anchor, including elevation and camera rotation.
// The bounded ray search is independent of map dimensions and shares projection with the marker renderer.
TileIndex PickCityMapTerrainPoint(City *city, const CityMapViewConfig *view, int screen_x, int screen_y) {
  return PickCityMapSurface(city, view, screen_x, screen_y, true);
}

int ReadCityMapTerrainPointHeight(const City *city, TileIndex tile) {
  CityTileGeometry geometry = ReadCityMapViewGeometry(city, tile);
  return geometry.land_height + (kGroundCorners[geometry.land_shape] & 1);
}

// Stock byte-packed result for original callers that have not migrated.
int CityMapScreenToTile(City *city, const CityMapViewConfig *view, int screen_x, int screen_y) {
  TileIndex tile = PickCityMapTile(city, view, screen_x, screen_y);
  return IsMapTileValid(city, tile) ? (MapTileX(city, tile) << 8) | MapTileY(city, tile) : -1;
}

int *CityMapTileToScreen(City *city, const CityMapViewConfig *view, int x, int y, int *screen_x, int *screen_y) {
  const Drawing *drawing = city->game->drawing;
  TileIndex tile = MapTileXY(city, x, y);
  CityPoint point = CityMapCameraPoint(city, CityMapViewRotation(view), x, y);
  int delta = point.y - point.x;
  int sum = point.y + point.x;
  int elevation = ReadCityLandHeight(&city->tile[tile]);
  int offset_y = sum;

  if (view->zoom == CITY_MAP_ZOOM_SMALL) {
    delta = delta * 4;
    offset_y = 2 * sum - CityMapHeightStep(0) * elevation;
  } else if (view->zoom == CITY_MAP_ZOOM_MEDIUM) {
    delta = delta * 8;
    offset_y = 4 * sum - CityMapHeightStep(1) * elevation;
  } else if (view->zoom == CITY_MAP_ZOOM_LARGE) {
    delta = delta * 16;
    offset_y = 8 * sum - CityMapHeightStep(2) * elevation;
  }

  int result_x = drawing->map_state.origin_x + delta;
  int result_y = drawing->map_state.origin_y + offset_y;
  if (view->magnified_large_zoom != 0) {
    result_x = result_x * 2;
    result_y = result_y * 2;
  }
  *screen_x = result_x;
  *screen_y = result_y;
  return screen_x;
}

int CenterCityMapViewAtTile(City *city, CityMapViewConfig *view, int x, int y) {
  Drawing *drawing = city->game->drawing;
  if (view == NULL) view = drawing->default_map_view;
  const CityInterfaceState *ui = city->game->ui;
  if (ui->main_window_iconic)
    return 0;

  bool magnified = false;

  if (view->magnified_large_zoom == 1) {
    view->magnified_large_zoom = 0;
    int saved_x = drawing->map_state.save_x;
    int saved_y = drawing->map_state.save_y;
    drawing->map_state.save_x = saved_x * 2;
    drawing->map_state.save_y = saved_y * 2;
    drawing->map_state.origin_x += saved_x;
    drawing->map_state.origin_y += saved_y;
    magnified = true;
  }

  int screen_x;
  int screen_y;
  CityMapTileToScreen(city, view, x, y, &screen_x, &screen_y);
  drawing->map_state.origin_x = drawing->map_state.origin_x + drawing->map_state.save_x - screen_x;
  drawing->map_state.origin_y = drawing->map_state.origin_y + drawing->map_state.save_y - screen_y;

  if (magnified) {
    view->magnified_large_zoom = 1;
    int saved_x = drawing->map_state.save_x;
    int saved_y = drawing->map_state.save_y;
    drawing->map_state.save_x = saved_x >> 1;
    drawing->map_state.save_y = saved_y >> 1;
    drawing->map_state.origin_x -= drawing->map_state.save_x;
    drawing->map_state.origin_y -= drawing->map_state.save_y;
  }

  return UpdateCityMapViewScrollPosition(city, view);
}

// Enlarge the map while retaining the original 16-bit origin arithmetic.
void ZoomCityMapViewIn(City *city, CityMapViewConfig *view) {
  Drawing *drawing = city->game->drawing;
  if (view->zoom + view->magnified_large_zoom >= CityMapZoomLimit(drawing->map_pixel_scale * 100))
    return;

  if (view->zoom == CITY_MAP_ZOOM_LARGE) {
    view->magnified_large_zoom = 1;
    drawing->map_state.save_x >>= 1;
    drawing->map_state.save_y >>= 1;
    drawing->map_state.origin_x = drawing->map_state.origin_x - drawing->map_state.save_x;
    drawing->map_state.origin_y = drawing->map_state.origin_y - drawing->map_state.save_y;
  } else {
    ++view->zoom;
    drawing->map_state.origin_x = 2 * drawing->map_state.origin_x - drawing->map_state.save_x;
    drawing->map_state.origin_y = 2 * drawing->map_state.origin_y - drawing->map_state.save_y;
  }

  GetCityMapClientRect(drawing, &view->view_rect);
  if (view->magnified_large_zoom == 1) {
    view->view_rect.left >>= 1;
    view->view_rect.top >>= 1;
    view->view_rect.right >>= 1;
    view->view_rect.bottom >>= 1;
  }
  UpdateCityMapViewScrollPosition(city, view);
  RefreshCityMapViews(city->game);
  RefreshCityMapDataPreview(city);
}

// Step out of magnified-large mode or reduce the map zoom by one.
void ZoomCityMapViewOut(City *city, CityMapViewConfig *view) {
  Drawing *drawing = city->game->drawing;
  if (view->zoom == CITY_MAP_ZOOM_SMALL)
    return;

  GetCityMapClientRect(drawing, &view->view_rect);
  if (view->magnified_large_zoom == 1) {
    int saved_x = drawing->map_state.save_x;
    int saved_y = drawing->map_state.save_y;
    drawing->map_state.save_x = saved_x * 2;
    drawing->map_state.save_y = saved_y * 2;
    drawing->map_state.origin_x += saved_x;
    drawing->map_state.origin_y += saved_y;
    view->magnified_large_zoom = 0;
  } else {
    --view->zoom;
    drawing->map_state.origin_x =
        ((drawing->map_state.origin_x - drawing->map_state.save_x) / 2 + drawing->map_state.save_x);
    drawing->map_state.origin_y =
        ((drawing->map_state.origin_y - drawing->map_state.save_y) / 2 + drawing->map_state.save_y);
  }

  UpdateCityMapViewScrollPosition(city, view);
  RefreshCityMapViews(city->game);
  RefreshCityMapDataPreview(city);
}

// Restore the stock map overlays, center the saved tile, and redraw.
int CenterCityMapViewAndResetOverlays(City *city, int x, int y, int zoom) {
  Drawing *drawing = city->game->drawing;
  CityMapViewConfig *view = drawing->default_map_view;
  // A stock save without a camera position supplies (128, 64), outside a
  // 128-wide map. Resolve that fallback before looking up tile elevation.
  if (!IsMapCoordinateValid(city, x, y)) {
    x = city->map_width / 2;
    y = city->map_height / 2;
  }
  static const u8 kEventDisplayFlags[5] = {0, 1, 1, 1, 1};
  static const CityMapOverlaySettings kOverlayDefaults = {
    .surface = {0, 1, 1, 1, 1},
    .underground = {0, 0, 1, 0, 0}
  };

  GetCityMapClientRect(drawing, &view->view_rect);
  if (view->magnified_large_zoom != 0)
    ZoomCityMapViewOut(city, view);
  view->magnified_large_zoom = 0;
  view->rotation = 0;
  view->zoom = zoom;

  for (int index = 0; index < 5; ++index)
    drawing->map_state.display_flags[index] = kEventDisplayFlags[index];
  RebuildCityToolbar(city);
  drawing->map_state.overlay_settings = kOverlayDefaults;

  CityCanvasDesktop *desktop = drawing->map_navigation_enabled ? &city->game->ui->desktop : NULL;
  if (desktop != NULL) {
    for (int index = desktop->count - 1; index >= 0; --index) {
      Window *window = &desktop->windows[index];
      if (Sc2kCanvasChartCode(window->kind) != 0)
        Sc2kCanvasDesktopDismissWindow(desktop, window->window_id);
    }
  }

  InvalidateCityMapViews(drawing);
  return CenterCityMapViewAtTile(city, view, x, y);
}


// Picking another viewport borrows the same bounded surface picker. Restore
// the bound Drawing state before returning; no City state or RNG is touched.
TileIndex PickCityMapCameraPoint(City *city, CityMapCamera camera, CityPoint point) {
  Drawing *drawing = city->game->drawing;
  CityMapViewConfig saved_view = *drawing->default_map_view;
  CityPoint origin = {drawing->map_state.origin_x, drawing->map_state.origin_y};
  CityPoint scroll = drawing->scroll_pixels;
  int scale = drawing->map_pixel_scale;
  drawing->default_map_view->zoom = camera.zoom;
  drawing->default_map_view->rotation = camera.rotation;
  drawing->default_map_view->magnified_large_zoom = 0;
  drawing->map_state.origin_x = camera.origin_x;
  drawing->map_state.origin_y = camera.origin_y;
  drawing->scroll_pixels = (CityPoint){0};
  drawing->map_pixel_scale = 1;
  TileIndex tile = PickCityMapTile(city, drawing->default_map_view, point.x, point.y);
  *drawing->default_map_view = saved_view;
  drawing->map_state.origin_x = origin.x;
  drawing->map_state.origin_y = origin.y;
  drawing->scroll_pixels = scroll;
  drawing->map_pixel_scale = scale;
  return tile;
}

// Transfer the same world point, including the offset inside its terrain tile,
// between cameras. Quarter turns require only integer coordinate arithmetic.
CityPoint TransferCityMapCameraCenter(City *city, CityMapCamera source, CityMapCamera target) {
  CityPoint source_center = {source.width / 2, source.height / 2};
  CityPoint target_center = {target.width / 2, target.height / 2};
  if (source.rotation == target.rotation) {
    int scale = 1 << source.zoom, next_scale = 1 << target.zoom;
    return (CityPoint){target_center.x - (source_center.x - source.origin_x) * next_scale / scale,
        target_center.y - (source_center.y - source.origin_y) * next_scale / scale};
  }
  TileIndex tile = PickCityMapCameraPoint(city, source, source_center);
  if (tile == INVALID_TILE) tile = MapTileXY(city, city->map_width / 2, city->map_height / 2);
  int x = MapTileX(city, tile), y = MapTileY(city, tile);
  CityPoint before = CityMapCameraPoint(city, source.rotation, x, y);
  CityPoint after = CityMapCameraPoint(city, target.rotation, x, y);
  int elevation = CityMapTileElevation(city, x, y);
  int scale = 1 << source.zoom;
  int dx = source_center.x - source.origin_x - (before.y - before.x) * 4 * scale;
  int dy = source_center.y - source.origin_y - (before.x + before.y) * 2 * scale +
      elevation * CityMapHeightStep(source.zoom);
  CityPoint offset = CityMapCameraVector(target.rotation - source.rotation,
      (2 * dy - dx) * 32 / scale, (2 * dy + dx) * 32 / scale);
  scale = 1 << target.zoom;
  return (CityPoint){target_center.x - (after.y - after.x) * 4 * scale - (offset.y - offset.x) * scale / 64,
      target_center.y - (after.x + after.y) * 2 * scale + elevation * CityMapHeightStep(target.zoom) -
          (offset.x + offset.y) * scale / 128};
}

void RotateCityMapView(City *city, CityMapViewConfig *view, bool clockwise) {
  Drawing *drawing = city->game->drawing;
  int rotation = (view->rotation + (clockwise ? 1 : 3)) & 3;
  CityPoint center = {drawing->map_state.save_x, drawing->map_state.save_y};
  CityPoint origin = TransferCityMapCameraCenter(city,
      (CityMapCamera){drawing->map_state.origin_x, drawing->map_state.origin_y, view->zoom,
          center.x * 2, center.y * 2, view->rotation},
      (CityMapCamera){0, 0, view->zoom, center.x * 2, center.y * 2, rotation});
  view->rotation = rotation;
  drawing->map_state.origin_x = origin.x;
  drawing->map_state.origin_y = origin.y;
  drawing->main_cache.valid = false;
  drawing->main_sprites.initialized = false;
  drawing->main_animations.count = 0;
  memset(drawing->cost_text_bounds[0], 0, sizeof(drawing->cost_text_bounds[0]));
  memset(drawing->demolition_bounds[0], 0, sizeof(drawing->demolition_bounds[0]));
  UpdateCityMapViewScrollPosition(city, view);
  InvalidateCityMapRect(drawing, view, view->view_rect);
}

void RotateCityMapViewport(City *city, u32 window_id, bool clockwise) {
  Drawing *drawing = city->game->drawing;
  for (int index = 0; index < CITY_EXTRA_MAP_VIEW_COUNT; ++index) {
    CityExtraMapView *view = &drawing->views[index];
    if (!window_id || view->window_id != window_id) continue;
    Window *window = FindCityCanvasWindow(&city->game->ui->desktop, window_id);
    if (!window) break;
    if (window->shaded) return;
    int rotation = (view->rotation + (clockwise ? 1 : 3)) & 3;
    int zoom = IntMin(view->zoom, CITY_MAP_ZOOM_LARGE);
    CityPoint center = {view->width / 2, view->height / 2};
    CityPoint origin = TransferCityMapCameraCenter(city,
        (CityMapCamera){view->origin_x, view->origin_y, zoom, center.x * 2, center.y * 2, view->rotation},
        (CityMapCamera){0, 0, zoom, center.x * 2, center.y * 2, rotation});
    view->origin_x = origin.x;
    view->origin_y = origin.y;
    view->rotation = rotation;
    view->cache.valid = false;
    view->sprites.initialized = false;
    view->animations.count = 0;
    memset(drawing->cost_text_bounds[index + 1], 0, sizeof(drawing->cost_text_bounds[index + 1]));
    memset(drawing->demolition_bounds[index + 1], 0, sizeof(drawing->demolition_bounds[index + 1]));
    Sc2kDirtyMarkRect(&city->game->ui->desktop.dirty, window->bounds);
    return;
  }
  RotateCityMapView(city, drawing->default_map_view, clockwise);
}

void RotateFocusedCityMapView(City *city, bool clockwise) {
  u32 window = city->game->ui->map_view_focus_window;
  RotateCityMapViewport(city, window, clockwise);
}

// Navigation

// Remove the scrollbar strips from the host client rectangle.
int GetCityMapClientRect(Drawing *drawing, CityRect *rect) {
  ReadCityMapClientRect(drawing, rect);
  rect->bottom = (int)((u32)rect->bottom - (u32)drawing->map_state.scroll_height);
  rect->right = (int)((u32)rect->right - (u32)drawing->map_state.scroll_width);
  return rect->bottom;
}

// Inclusive camera-origin limits enclosing the projected map, with two tiles of border and room for elevated terrain.
// Camera dimensions are logical pixels at its zoom; rectangular maps swap axes with rotation. No map scan is needed.
static CityRect CityMapOriginBounds(const City *city, CityMapCamera camera) {
  int half_width = 4 << camera.zoom, half_height = 2 << camera.zoom;
  int margin_x = 2 * half_width, margin_y = 2 * half_height;
  int center_x = camera.width / 2, center_y = camera.height / 2;
  CityPoint size = CityMapCameraSize(city, camera.rotation);
  return (CityRect){center_x - half_width * (size.y - 1) - margin_x,
      center_y - half_height * (size.x + size.y - 2) - margin_y,
      center_x + half_width * (size.x - 1) + margin_x,
      center_y + CITY_ALTITUDE_LAND_MASK * CityMapHeightStep(camera.zoom) + margin_y};
}

// Keep the view center within the map's projected diamond, allowing the same border and elevation range as the
// scrollbar bounds. Clamping each slanted edge prevents scrolling into the empty corners of its bounding rectangle.
CityPoint ClampCityMapCameraOrigin(const City *city, CityMapCamera camera) {
  CityRect bounds = CityMapOriginBounds(city, camera);
  int x = IntClamp(camera.origin_x, bounds.left, bounds.right);
  int projected_x = camera.width / 2 - x;
  int half_height = 2 << camera.zoom;
  CityPoint size = CityMapCameraSize(city, camera.rotation);
  int top = abs(projected_x) / 2 - 2 * half_height - CITY_ALTITUDE_LAND_MASK * CityMapHeightStep(camera.zoom);
  int bottom = IntMin(2 * half_height * (size.x - 1) + projected_x / 2,
      2 * half_height * (size.y - 1) - projected_x / 2) + 2 * half_height;
  return (CityPoint){x, IntClamp(camera.origin_y, camera.height / 2 - bottom, camera.height / 2 - top)};
}

// Constrain the main camera and update both scrollbar positions from its logical viewport and rotated map dimensions.
int UpdateCityMapViewScrollPosition(City *city, CityMapViewConfig *view) {
  Drawing *drawing = city->game->drawing;
  CityMapCamera camera = {drawing->map_state.origin_x, drawing->map_state.origin_y, view->zoom,
      drawing->map_state.save_x * 2, drawing->map_state.save_y * 2, view->rotation};
  CityPoint origin = ClampCityMapCameraOrigin(city, camera);
  if (origin.x != camera.origin_x) drawing->scroll_pixels.x = 0;
  if (origin.y != camera.origin_y) drawing->scroll_pixels.y = 0;
  drawing->map_state.origin_x = origin.x;
  drawing->map_state.origin_y = origin.y;
  CityRect bounds = CityMapOriginBounds(city, camera);
  view->horizontal_scroll_position = (bounds.right - origin.x) * 100 / (bounds.right - bounds.left);
  view->vertical_scroll_position = (bounds.bottom - origin.y) * 100 / (bounds.bottom - bounds.top);
  return view->vertical_scroll_position;
}

// Optionally preserve stock camera alignment for compatibility.
void MaskCityMapOriginForZoom(Drawing *drawing, CityMapViewConfig *view) {
  if (!(drawing->options.fixes & (1 << DRAWING_FIX_PIXEL_SCROLLING)))
    drawing->map_state.origin_x &= -CityMapOriginAlignment(view->zoom);
}

// The miniature-map refresh is enabled by the dialog's +216 gate.
bool RefreshCityMapDataPreview(City *city) {
  if (city->game->ui->map_data.refresh_enabled)
    return InvalidateCityMapDataPreview(city);
  return false;
}

// Offset the map and constrain its extents for the current zoom.
void OffsetCityMapOrigin(City *city, CityMapViewConfig *view, int delta_x, int delta_y) {
  Drawing *drawing = city->game->drawing;
  CityRect bounds = CityMapOriginBounds(city, (CityMapCamera){0, 0, view->zoom,
      drawing->map_state.save_x * 2, drawing->map_state.save_y * 2, view->rotation});
  i64 x = (i64)drawing->map_state.origin_x + delta_x;
  i64 y = (i64)drawing->map_state.origin_y + delta_y;
  drawing->map_state.origin_x = x < bounds.left ? bounds.left : x > bounds.right ? bounds.right : (int)x;
  drawing->map_state.origin_y = y < bounds.top ? bounds.top : y > bounds.bottom ? bounds.bottom : (int)y;
  MaskCityMapOriginForZoom(drawing, view);
  UpdateCityMapViewScrollPosition(city, view);
  RefreshCityMapDataPreview(city);
}
