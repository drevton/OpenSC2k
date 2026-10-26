#ifndef SC2K_CANVAS_PRIMITIVES_H
#define SC2K_CANVAS_PRIMITIVES_H

#include "drawing/types.h"

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
// Clip logical bounds through the canvas transform, then fill alternating framebuffer pixels with two colors.
// Foreground pixels have even x+y in framebuffer coordinates; the pattern stays at physical detail at every scale.
// Requires prepared pixels and a valid transform. Overwritten palette identities are cleared; no blending occurs.
void DrawCanvasStipple(Canvas *canvas, CityRect bounds, u32 background, u32 foreground);
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
// Small round/square choice marker at a logical row's left edge. Colors supply its center and rim;
// final-resolution edge coverage blends them with the framebuffer once, preserving the canvas clip.
// Requires prepared pixels and a valid transform. Does not register a button or change selection state.
void DrawCanvasChoiceMarker(Canvas *canvas, CityRect row, bool circle, u32 fill, u32 rim);
// Loading-screen pill in physical pixels, with percent in 0..100 and completed/empty RGB colors.
// Requires a prepared, untransformed framebuffer with a full-frame clip and positive bounds dimensions.
// Writes RGB within framebuffer bounds; the caller owns progress calculation, damage and presentation.
void DrawCanvasLoadingProgress(Canvas *canvas, CityRect bounds, int percent, u32 fill, u32 empty);
void DrawCityWindowCaption(Canvas *canvas, CityRect bounds, const char *title,
    u32 face, u32 light, u32 dark, int controls);

#endif
