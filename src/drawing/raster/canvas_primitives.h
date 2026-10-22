#ifndef SC2K_CANVAS_PRIMITIVES_H
#define SC2K_CANVAS_PRIMITIVES_H

#include "drawing/canvas.h"

// Shared canvas colors and frames. Geometry follows the canvas transform and clip.
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

bool CanvasPointInRect(CityRect rect, i32 x, i32 y);
void DrawCanvasLine(Canvas *canvas, int x0, int y0, int x1, int y1, u32 color, int width);
void Sc2kCanvasBlitIndexed(Canvas *canvas, const u8 *indices, i32 source_stride,
    const u32 *palette, CityRect area);
bool CanvasCloseAt(CityRect bounds, i32 x, i32 y);
void CanvasPanelFrame(Canvas *canvas, CityRect bounds, const char *title);
void CanvasBeveledBox(Canvas *canvas, i32 left, i32 top, i32 right, i32 bottom,
    u32 fill, i32 raised);
// Enabled buttons share hit collection, click feedback and automatic release.
// Panels, text fields, disabled controls and scrollbar thumbs use plain frames.
bool RegisterCityCanvasButton(Canvas *canvas, CityRect bounds);
bool DrawCanvasButtonFrame(Canvas *canvas, CityRect bounds, u32 fill, u32 light, u32 dark, bool raised);
bool DrawCanvasButtonBox(Canvas *canvas, i32 left, i32 top, i32 right, i32 bottom, u32 fill, i32 raised);
void DrawCanvasFrame(Canvas *canvas, CityRect bounds, u32 fill, u32 light, u32 dark, bool raised);
void DrawCityCheckbox(Canvas *canvas, CityRect bounds, bool checked, u32 face, u32 light, u32 dark);
void DrawCityWindowCaption(Canvas *canvas, CityRect bounds, const char *title,
    u32 face, u32 light, u32 dark, int controls);

#endif
