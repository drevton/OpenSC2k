#include "game/graphics.h"

#include <stdio.h>

#include "assets/assets.h"
#include "assets/catalog.h"
#include "assets/cursors.h"
#include "assets/fonts.h"
#include "assets/graphics.h"
#include "assets/state.h"
#include "city/infrastructure/types.h"
#include "drawing/bitmaps.h"
#include "drawing/display.h"
#include "drawing/state.h"
#include "game/state.h"
#include "ui/menus/menu.h"
#include "ui/sdl.h"
#include "ui/state.h"
#include "ui/windows/windows.h"

// Game coordinates immutable asset loading and the Drawing state that borrows those resources.
static bool GraphicsAssetLoadFailure(const char *name) {
  fprintf(stderr, "Unable to load graphics asset: %s\n", name);
  return false;
}

bool LoadApplicationGraphicsAssets(Game *game) {
  CityAssets *assets = game->resources;
  if (!PreloadCityAssetPacks(assets, false)) return false;
  if (!LoadCityLandscapeAssets(assets)) return GraphicsAssetLoadFailure("landscape");
  if (!LoadCityDemolitionAssets(assets)) return GraphicsAssetLoadFailure("demolition");
  if (!LoadCityCursorAssets(game, NULL)) return GraphicsAssetLoadFailure("cursors");
  if (!LoadCityMusicButtonAssets(assets, NULL)) return GraphicsAssetLoadFailure("music controls");
  if (!LoadCityOverlayFont(&game->drawing->overlay_font, assets, NULL)) return GraphicsAssetLoadFailure("overlay font");
  if (!LoadCityInterfaceFont(assets, NULL)) return GraphicsAssetLoadFailure("interface fonts");
  if (!LoadCityPictureAssets(assets, CITY_ASSET_ILLUSTRATIONS)) return GraphicsAssetLoadFailure("illustrations");
  if (!LoadCityPictureAssets(assets, CITY_ASSET_ADVISORS)) return GraphicsAssetLoadFailure("advisors");
  if (!LoadCityPictureAssets(assets, CITY_ASSET_NEIGHBORS)) return GraphicsAssetLoadFailure("neighbors");
  if (!LoadCityInterfaceIcons(assets, &assets->map_category_icons, CITY_ASSET_MAP_ICONS, CITY_MAP_ICON_COUNT))
    return GraphicsAssetLoadFailure("map icons");
  if (!LoadCityInterfaceIcons(assets, &assets->bridge_choice_icons, CITY_ASSET_BRIDGES, CITY_BRIDGE_CHOICE_ICON_COUNT))
    return GraphicsAssetLoadFailure("bridge icons");
  if (!LoadCityInterfaceIcons(assets, &assets->main_toolbar_icons, CITY_ASSET_TOOLBAR, CITY_TOOLBAR_ICON_COUNT))
    return GraphicsAssetLoadFailure("toolbar icons");
  return true;
}

// A successful source replacement invalidates composed cursor artwork. Failed loads retain the existing cache.
bool LoadCityCursorAssets(Game *game, const char *graphics_root) {
  if (!LoadCityCursorArtwork(game->resources, graphics_root)) return false;
  game->drawing->cursors.initialized = false;
  ++game->drawing->cursors.revision;
  return true;
}

void ReleaseCityCursorAssets(Game *game) {
  ReleaseCityCursorArtwork(game->resources);
  game->drawing->cursors.initialized = false;
  ++game->drawing->cursors.revision;
}

// Flush through the installed native map presenter.
bool PresentCityMapView(Game *game) {
  if (game->present_map != NULL)
    return game->present_map(game);
  // A canvas-free native owner has nothing to flush.
  return true;
}

// Resolve host dimensions and update desktop damage only after framebuffer preparation succeeds.
bool PrepareCityScreenBuffer(Game *game, CanvasSDL *display) {
  Drawing *drawing = game->drawing;
  int width = kDefaultCityCanvasWidth * drawing->map_pixel_scale;
  int height = kDefaultCityCanvasHeight * drawing->map_pixel_scale;
  if (display && Sc2kSDLOutputSize(display, &width, &height)) return false;
  bool resized = drawing->framebuffer.width != width || drawing->framebuffer.height != height;
  if (!PrepareDrawingFramebuffer(drawing, width, height)) return false;
  if (resized) {
    CityCanvasDesktop *desktop = &game->ui->desktop;
    Sc2kDirtyMarkRect(&desktop->dirty, (CityRect){0, 0, desktop->canvas_width, desktop->canvas_height});
  }
  return true;
}

bool ScreenshotCityCanvas(const Game *game, const char *path, bool physical) {
  const Drawing *drawing = game->drawing;
  const Canvas *canvas = &drawing->framebuffer;
  if (!canvas->pixels || canvas->width <= 0 || canvas->height <= 0) return false;
  FILE *file = fopen(path, "wb");
  if (file == NULL)
    return false;
  int step = physical ? 1 : drawing->map_pixel_scale;
  // Include a final partial logical pixel, but sample only within the physical framebuffer.
  int output_width = (canvas->width - 1) / step + 1;
  int output_height = (canvas->height - 1) / step + 1;
  bool ok = fprintf(file, "P6\n%d %d\n255\n", output_width, output_height) > 0;
  for (int y = 0; ok && y < output_height; ++y) {
    const u32 *row = canvas->pixels + (size_t)y * step * canvas->stride;
    for (int x = 0; ok && x < output_width; ++x) {
      u32 color = row[x * step];
      u8 rgb[] = {(u8)(color >> 16), (u8)(color >> 8), (u8)color};
      ok = fwrite(rgb, 1, sizeof(rgb), file) == sizeof(rgb);
    }
  }
  return fclose(file) == 0 && ok;
}

// Invalidate cached UI previews before replacing the map cache for this asset revision. A failed rebuild keeps
// the old revision, so the next attempt repeats invalidation and replaces any partially installed cache.
bool PrepareCityMapSpriteSources(Game *game) {
  Drawing *drawing = game->drawing;
  const CityAssets *assets = game->resources;
  if (drawing->sprite_revision == assets->sprite_revision) return true;
  CityInterfaceState *ui = game->ui;
  if (ui->tree_picker.drawing) InvalidateCityWindowBitmaps(&ui->tree_picker.drawing->base);
  for (int i = 0; i < CITY_STARTUP_WINDOW_LIMIT; ++i)
    if (ui->startup_windows[i].layout.drawing)
      InvalidateCityWindowBitmaps(&ui->startup_windows[i].layout.drawing->base);
  return InstallDrawingMapSpriteSources(drawing, assets);
}

bool PrepareCityMapSprites(Game *game) {
  return PrepareCityMapSpriteSources(game) && PrepareDrawingMapSprites(game->drawing);
}

bool PrepareCityToolbarIcons(Game *game) {
  Drawing *drawing = game->drawing;
  const CityInterfaceIcons *source = &game->resources->main_toolbar_icons;
  int scale = drawing->options.scale;
  if (drawing->toolbar_icon_scale == scale && drawing->toolbar_icon_data == source->data.data) return true;
  ReleaseCityToolbarIcons(drawing);
  if (!source->data.data) return true;
  drawing->toolbar_icon_source = source;
  drawing->toolbar_icon_scale = scale;
  // Position-independent image dimensions need one prepared size per UI scale.
  int size = (20 * scale + 99) / 100;
  for (int icon = 0; icon < CITY_TOOLBAR_ICON_COUNT; ++icon) {
    int width = (20 * CityToolbarIconColumns(icon) * scale + 99) / 100;
    if (!PrepareCityToolbarIcon(drawing, icon, width, size, true)) return false;
  }
  // Publish readiness only after every button succeeds. A failed attempt is rebuilt on the next call.
  drawing->toolbar_icon_data = source->data.data;
  return true;
}
