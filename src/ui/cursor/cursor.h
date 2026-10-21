#ifndef SC2K_DOMAIN_SRC_UI_CURSOR_CURSOR_H
#define SC2K_DOMAIN_SRC_UI_CURSOR_CURSOR_H

#include "ui/tools/types.h"
#include "drawing/canvas.h"
#include "city/types.h"
#include "assets/cursors.h"

typedef struct {
  u32 tools[CITY_CANVAS_TOOL_COUNT][SC2K_CURSOR_ASSET_SPRITE_PIXELS];
  u8 initialized;
} CityCursorCache;

// Cursor
typedef struct Game Game;

// Compact pointer artwork: three source pixels span one logical interface pixel.
// The arrow tip is the hotspot (0,0); its position is never quantized by scale.
enum { SC2K_CURSOR_SOURCE_SIZE = 72, SC2K_CURSOR_SCALE_DENOMINATOR = 300 };
// Negative/out-of-range tool IDs use the plain gold arrow. Selected tools
// retain the white pointer over their gold badge.
const u32 *GetCityCursorSprite(Game *game, int tool);
// Owned prepared image for SDL export or diagnostic software drawing.
struct RunSprite *PrepareCityCursorSprite(Game *game, int tool, int size);
void LoadCityCursorToolIcons(Game *game, const u32 palette[256]);
CityRect Sc2kCanvasCursorBounds(i32 x, i32 y, i32 scale);
void DrawCityCursor(Game *game, Canvas *canvas, i32 tool, i32 x, i32 y, i32 scale);

// Cursor Internal
void InitializeCityCursor(Game *game);
void ComposeCityCursorIcon(Game *game, int tool, const u32 *icon);

#endif
