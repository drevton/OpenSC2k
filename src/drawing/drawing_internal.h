#ifndef SC2K_DRAWING_INTERNAL_H
#define SC2K_DRAWING_INTERNAL_H

#include "city/maps/map_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "city/state.h"
#include "city/engine/commands.h"
#include "game/errors.h"
#include "game/state.h"
#include "audio/state.h"
#include "assets/state.h"
#include "ui/state.h"
#include "drawing/drawing.h"
#include "drawing/maps/map_terrain.h"
#include "drawing/maps/tables.h"
#include "drawing/raster/canvas_raster_internal.h"
#include "drawing/state.h"
#include "ui/charts/charts.h"
#include "ui/commands.h"
#include "ui/dialogs/dialogs.h"
#include "ui/startup/startup.h"
#include "ui/text/text.h"

// Shared implementation details for the separately compiled drawing/UI subsystems.
// Public game and canvas entry points remain in drawing.h.

struct CityCanvasInformationCursor {
  Canvas *canvas;
  i32 x;
  i32 y;
  i32 origin_x;
  i32 origin_y;
  u32 alignment;
  u32 color;
  i32 compact_tile;
  i32 currency_part;
};

typedef struct {
  Canvas *canvas;
  const u8 *indices;
  i32 stride;
  const u32 *palette;
  const Window *window;
  const CityCanvasDesktop *desktop;
  Game *game;
} CityCanvasPaintContext;

typedef struct {
  const CityCanvasDesktop *desktop;
  const u8 *indices;
  i32 stride;
  const u32 *palette;
  Game *game;
} CanvasFramebufferPaint;

extern const u32 canvas_dark;
extern const u32 canvas_edge;
extern const u32 canvas_face;
extern const u32 canvas_light;
extern const u32 canvas_title;
extern const u32 canvas_text;

typedef enum CityWindowControlIcon {
  CITY_WINDOW_ICON_SHADE, CITY_WINDOW_ICON_DEFAULT_SIZE, CITY_WINDOW_ICON_STICKY
} CityWindowControlIcon;

void DrawCityWindowControlIcon(Canvas *canvas, CityRect bounds, CityWindowControlIcon icon, bool pressed);

bool CanvasBlitIndexedBlocks(Canvas *canvas, const u8 *indices,
    i32 source_stride, const u32 *palette, CityRect area);
i32 CanvasFontTextWidth(const char *text, const CityCanvasGlyph *font);
i32 CanvasTextWidth(const char *text);
void CanvasTextFont(Canvas *canvas, i32 x, i32 y, const char *text, u32 color, i32 scale,
    const CityCanvasGlyph *font);
void CanvasTextAt(Canvas *canvas, i32 x, i32 y, const char *text, u32 color);
i32 CanvasTextWrappedScaled(Canvas *canvas, i32 x, i32 y, i32 right, i32 bottom,
    const char *text, u32 color, i32 scale, i32 line_height, i32 alignment);
void CanvasTextWrapped(Canvas *canvas, i32 x, i32 y, i32 right, i32 bottom,
    const char *text, u32 color);
bool CityWindowCloseAt(const Window *window, int x, int y);
CityRect CityWindowResizeGrip(const Window *window);
bool CanvasCloseAt(CityRect bounds, i32 x, i32 y);
void CanvasPanelFrame(Canvas *canvas, CityRect bounds, const char *title);
void CanvasBudgetAmount(char *output, size_t size, i64 value);
void CanvasTextRight(Canvas *canvas, i32 right, i32 y, const char *value, u32 color);
void CanvasBeveledBox(Canvas *canvas, i32 left, i32 top, i32 right, i32 bottom,
    u32 fill, i32 raised);
// Enabled buttons share hit collection, click feedback and automatic release.
// Panels, text fields, disabled controls and scrollbar thumbs use plain frames.
bool RegisterCityCanvasButton(Canvas *canvas, CityRect bounds);
bool CityCanvasButtonPressed(const Canvas *canvas, CityRect bounds);
bool DrawCanvasButtonFrame(Canvas *canvas, CityRect bounds, u32 fill, u32 light, u32 dark, bool raised);
bool DrawCanvasButtonBox(Canvas *canvas, i32 left, i32 top, i32 right, i32 bottom, u32 fill, i32 raised);
void DrawCanvasFrame(Canvas *canvas, CityRect bounds, u32 fill, u32 light, u32 dark, bool raised);
void DrawCityCheckbox(Canvas *canvas, CityRect bounds, bool checked, u32 face, u32 light, u32 dark);
CityRect CityCommandErrorBounds(Game *game, const char *summary, const char *detail);
void DrawCityWindowCaption(Canvas *canvas, CityRect bounds, const char *title,
    u32 face, u32 light, u32 dark, int controls);
void CanvasDrawSpanChoices(Canvas *canvas, const Window *window);
void CanvasDrawNewspaperPage(Canvas *canvas, const Window *window);
void CanvasDrawLabelEditor(Canvas *canvas, const Window *window);
void CanvasDrawStadiumTeamEditor(Canvas *canvas, const Window *window);
void DrawCityFacilityInformationWindow(City *city, Canvas *canvas, const Window *window);
void DrawCityTileInformationWindow(Game *game, Canvas *canvas, const Window *window);
void CanvasDrawToolStatus(Canvas *canvas, const Window *window);
void CanvasDrawJukebox(Game *game, Canvas *c, const Window *w);
void DrawGameOptions(Game *game, Canvas *c, int left, int top, int right, int bottom);
CityRect CanvasWindowPaintRect(const Window *window);
CityRect CityCanvasDirtyPixelBounds(const CityCanvasDesktop *desktop, CityRect logical);
i32 CanvasScaleFloor(i32 value, i32 numerator, i32 denominator);
i32 CanvasScaleCeil(i32 value, i32 numerator, i32 denominator);
CityRect CanvasScaleCover(CityRect b, i32 numerator, i32 denominator);
CityRect CanvasPixelMapCover(const CityCanvasDesktop *desktop, u32 id, CityRect logical);
void CanvasSnapCandidate(i32 target, i32 original, i32 *snapped, i32 *radius);
void CanvasSnapToRect(CityRect other, i32 width, i32 height, i32 x, i32 y, i32 *left,
                                i32 *top, i32 *horizontal, i32 *vertical);

void Sc2kCanvasDrawLandUse(Canvas *canvas, const Window *window);

#endif
