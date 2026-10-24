#ifndef SC2K_UI_CURSOR_H
#define SC2K_UI_CURSOR_H

#include "drawing/canvas.h"

typedef struct Game Game;

// Borrowed pixels: composed tool images and the missing-artwork fallback live in Drawing; loaded plain arrows
// live in Assets. Source replacement or cache rebuilding invalidates the borrowed image.
// Negative/out-of-range tool IDs use the plain gold arrow. Selected tools
// retain the white pointer over their gold badge.
const u32 *GetCityCursorSprite(Game *game, int tool);
// Initialize base badges, then replace them from loaded map/interface artwork using the supplied palette.
// Missing source/palette stops after initialization. Earlier badges survive a later failure, reported on Game.
// Preparation performs no resource I/O or City mutation.
void PrepareCityToolCursors(Game *game, const u32 palette[256]);
// Select the tool image for software captures and propagate preparation failure to Game.
void DrawCityCursor(Game *game, Canvas *canvas, i32 tool, i32 x, i32 y, i32 scale);

// Update the logical map cursor from mode, tool and modifiers. Reads the client
// rectangle and pointer in order; returns zero when the pointer is outside.
u32 UpdateCityMapCursor(Game *game);
// Select the host tool cursor using current hover, window and dialog state.
void UpdateCityToolCursor(City *city);

#endif
