#include "game/graphics.h"

#include <stdio.h>

#include "assets/assets.h"
#include "assets/catalog.h"
#include "assets/cursors.h"
#include "assets/fonts.h"
#include "assets/graphics.h"
#include "assets/state.h"
#include "drawing/cursors.h"
#include "drawing/display.h"
#include "drawing/maps/sprite_cache.h"
#include "drawing/maps/tables.h"
#include "drawing/state.h"
#include "game/state.h"
#include "ui/sdl.h"
#include "ui/updates.h"

// Game coordinates immutable asset loading and the Drawing state that borrows those resources.
void InitializeGameDrawing(Game *game) {
  InitializeDrawing(game->drawing);
  InitializeCityOverlayFont(&game->resources->overlay_font);
}

static bool GraphicsAssetLoadFailure(const char *name) {
  fprintf(stderr, "Unable to load graphics asset: %s\n", name);
  return false;
}

bool LoadApplicationGraphicsAssets(Game *game) {
  CityAssets *assets = game->resources;
  if (!PreloadCityAssetPacks(assets, false)) return false;
  if (!LoadCityLandscapeAssets(assets, kCityMapKindSprites)) return GraphicsAssetLoadFailure("landscape");
  if (!LoadCityDemolitionAssets(assets)) return GraphicsAssetLoadFailure("demolition");
  if (!LoadCityCursorAssets(game, NULL)) return GraphicsAssetLoadFailure("cursors");
  if (!LoadCityMusicButtonAssets(assets, NULL)) return GraphicsAssetLoadFailure("music controls");
  if (!LoadCityOverlayFont(assets, NULL)) return GraphicsAssetLoadFailure("overlay font");
  if (!LoadCityInterfaceFont(assets, NULL)) return GraphicsAssetLoadFailure("interface fonts");
  if (!LoadCityPictureAssets(assets, CITY_ASSET_ILLUSTRATIONS)) return GraphicsAssetLoadFailure("illustrations");
  if (!LoadCityPictureAssets(assets, CITY_ASSET_ADVISORS)) return GraphicsAssetLoadFailure("advisors");
  if (!LoadCityPictureAssets(assets, CITY_ASSET_NEIGHBORS)) return GraphicsAssetLoadFailure("neighbors");
  if (!LoadCityInterfaceIcons(assets, CITY_ASSET_MAP_ICONS))
    return GraphicsAssetLoadFailure("map icons");
  if (!LoadCityInterfaceIcons(assets, CITY_ASSET_BRIDGES))
    return GraphicsAssetLoadFailure("bridge icons");
  if (!LoadCityInterfaceIcons(assets, CITY_ASSET_TOOLBAR))
    return GraphicsAssetLoadFailure("toolbar icons");
  return true;
}

// A successful source replacement invalidates composed cursor artwork. Failed loads retain the existing cache.
bool LoadCityCursorAssets(Game *game, const char *graphics_root) {
  if (!LoadCityCursorArtwork(game->resources, graphics_root)) return false;
  InvalidateCityCursorCache(&game->drawing->cursors);
  return true;
}

void ReleaseCityCursorAssets(Game *game) {
  ReleaseCityCursorArtwork(game->resources);
  InvalidateCityCursorCache(&game->drawing->cursors);
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
  if (resized) InvalidateCityDesktop(game->ui);
  return true;
}

bool DrawCityLoadingScreen(Game *game, CanvasSDL *display, CityIntroStage stage,
    int completed, int total, bool initial) {
  Drawing *drawing = game->drawing;
  int width = drawing->framebuffer.width, height = drawing->framebuffer.height;
  if (!PrepareCityScreenBuffer(game, display) || Sc2kSDLConfigureFramebuffer(display, 100)) return false;
  Canvas canvas = drawing->framebuffer;
  initial |= width != canvas.width || height != canvas.height;
  CityLoadingPaint paint = PaintCityLoadingScreen(&canvas, &game->resources->intro, stage, completed, total, initial);
  if (paint.failed) game->execution_failed = true;
  if (game->execution_failed) return false;
  ClearScreenDamage(&drawing->upload_damage);
  MarkScreenDirty(&drawing->upload_damage, paint.dirty);
  return Sc2kSDLPresentFramebuffer(display, &drawing->framebuffer, &drawing->upload_damage, (CityRect){0}) == 0;
}

bool ScreenshotCityCanvas(const Game *game, const char *path, bool physical) {
  const Drawing *drawing = game->drawing;
  const Canvas *canvas = &drawing->framebuffer;
  if (!canvas->pixels || canvas->width <= 0 || canvas->height <= 0) return false;
  FILE *file = fopen(path, "wb");
  if (file == NULL)
    return false;
  bool ok = WriteDrawingScreenshot(drawing, file, physical);
  return fclose(file) == 0 && ok;
}

// Invalidate cached UI previews before replacing the map cache for this asset revision. A failed rebuild keeps
// the old revision, so the next attempt repeats invalidation and replaces any partially installed cache.
bool PrepareCityMapSpriteSources(Game *game) {
  Drawing *drawing = game->drawing;
  const CityAssets *assets = game->resources;
  if (drawing->sprite_revision == assets->sprite_revision) return true;
  InvalidateCityMapSpritePreviews(game->ui);
  return InstallDrawingMapSpriteSources(drawing, assets);
}

bool PrepareCityMapSprites(Game *game) {
  return PrepareCityMapSpriteSources(game) && PrepareDrawingMapSprites(game->drawing);
}
