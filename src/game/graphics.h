#ifndef SC2K_GAME_GRAPHICS_H
#define SC2K_GAME_GRAPHICS_H

#include "city/types.h"

typedef struct Game Game;
typedef struct CanvasSDL CanvasSDL;

// Startup graphics loading retains its partial results on failure. Cursor source changes invalidate Drawing's
// composed artwork after successful loading or release; failed source loading preserves it.
bool LoadApplicationGraphicsAssets(Game *game);
bool LoadCityCursorAssets(Game *game, const char *graphics_root);
void ReleaseCityCursorAssets(Game *game);

// Coordinate asset revision changes, invalidating dependent UI previews before Drawing rebuilds its sources.
// Sources can be prepared independently before startup workers prepare zooms and construction previews.
bool PrepareCityMapSpriteSources(Game *game);
bool PrepareCityMapSprites(Game *game);

// Prepare standard toolbar button sizes for the installed atlas/UI scale; UI supplies each icon's aspect.
// Cache hits/no artwork succeed. Failed preparation retains earlier entries and the installed source binding;
// the incomplete cache is released and rebuilt on the next call.
bool PrepareCityToolbarIcons(Game *game);

// Present all visible map windows when a presenter is installed; a canvas-free owner has nothing to flush.
bool PresentCityMapView(Game *game);

// Resolve the display size (or default canvas size without a display), prepare Drawing and dirty resized UI.
bool PrepareCityScreenBuffer(Game *game, CanvasSDL *display);
// Save the whole prepared framebuffer as PPM. Physical uses every pixel; logical samples once per map pixel scale.
// Include partial logical edge pixels. An unprepared framebuffer fails before opening the host file.
bool ScreenshotCityCanvas(const Game *game, const char *path, bool physical);

#endif
