#ifndef SC2K_GAME_GRAPHICS_H
#define SC2K_GAME_GRAPHICS_H

#include "city/types.h"

typedef struct Game Game;
typedef struct CanvasSDL CanvasSDL;

// Fresh Game startup only, before resources or drawing buffers are loaded. Initialize the Drawing owner and seed
// Assets fallback glyphs. Per-city resets use InitializeDrawingDefaults without replacing loaded fonts/preferences.
void InitializeGameDrawing(Game *game);

// Startup graphics loading retains its partial results on failure. Cursor source changes invalidate Drawing's
// composed artwork after successful loading or release; failed source loading preserves it.
bool LoadApplicationGraphicsAssets(Game *game);
bool LoadCityCursorAssets(Game *game, const char *graphics_root);
void ReleaseCityCursorAssets(Game *game);

// Coordinate asset revision changes, invalidating dependent UI previews before Drawing rebuilds its sources.
// Sources can be prepared independently before startup workers prepare zooms and construction previews.
bool PrepareCityMapSpriteSources(Game *game);
bool PrepareCityMapSprites(Game *game);

// Present all visible map windows when a presenter is installed; a canvas-free owner has nothing to flush.
bool PresentCityMapView(Game *game);

// Resolve the display size (or default canvas size without a display), prepare Drawing and dirty resized UI.
bool PrepareCityScreenBuffer(Game *game, CanvasSDL *display);
// Save the whole prepared framebuffer as PPM. Physical uses every pixel; logical samples once per map pixel scale.
// Include partial logical edge pixels. An unprepared framebuffer fails before opening the host file.
bool ScreenshotCityCanvas(const Game *game, const char *path, bool physical);

#endif
