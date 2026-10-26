#ifndef SC2K_DOMAIN_SRC_DRAWING_FONTS_FONTS_H
#define SC2K_DOMAIN_SRC_DRAWING_FONTS_FONTS_H

#include "drawing/types.h"
#include "city/types.h"
#include "assets/fonts.h"

// Decode one UTF-8 character to a baked Windows-1252 glyph index, advancing the borrowed text cursor.
u8 ReadCityFontCharacter(const char **text);
// Fixed canvas-font widths in logical pixels; text is terminated and custom fonts supply 256 glyphs.
i32 CanvasFontTextWidth(const char *text, const CityCanvasGlyph *font);
i32 CanvasTextWidth(const char *text);

// Draw terminated text in logical canvas coordinates, restoring the caller's clip after wrapped text.
// A custom font has 256 glyphs. Wrapped measurement permits NULL canvas; scale must be positive.
void CanvasTextFont(Canvas *canvas, i32 x, i32 y, const char *text, u32 color, i32 scale,
    const CityCanvasGlyph *font);
void CanvasTextAt(Canvas *canvas, i32 x, i32 y, const char *text, u32 color);
// Draw at size/10 of the base font, using the canvas's ready interface font or the fixed canvas fallback.
// Requires a prepared canvas, positive size and terminated UTF-8 text. Raster/clip transforms stay local;
// no font allocation or loading occurs, and the caller's canvas configuration is unchanged.
void CanvasTextSized(Canvas *canvas, int x, int y, const char *text, u32 color, int size);
i32 CanvasTextWrappedScaled(Canvas *canvas, i32 x, i32 y, i32 right, i32 bottom,
    const char *text, u32 color, i32 scale, i32 line_height, i32 alignment);
void CanvasTextWrapped(Canvas *canvas, i32 x, i32 y, i32 right, i32 bottom,
    const char *text, u32 color);
void CanvasTextRight(Canvas *canvas, i32 right, i32 y, const char *value, u32 color);

int CityInterfaceTextWidth(const CityInterfaceFont *font, const char *text);
// Measure complete UTF-8 characters within the byte limit, using the same glyph advances as drawing.
int CityInterfaceTextPrefixWidth(const CityInterfaceFont *font, const char *text, int bytes);
// x is relative to the text origin. Return the clicked character's byte offset.
int CityInterfaceTextCaretAt(const CityInterfaceFont *font, const char *text, int x);
void DrawCityInterfaceText(const CityInterfaceFont *font, Canvas *canvas, int x, int y,
    const char *text, u32 color, bool shadow);
void DrawCityInterfaceTextSized(const CityInterfaceFont *font, Canvas *canvas, int x, int y,
    const char *text, u32 color, bool shadow, int size);
// Bold coverage with a two-physical-pixel shadow for small diagnostic overlays.
void DrawCityInterfaceTextBold(const CityInterfaceFont *font, Canvas *canvas, int x, int y,
    const char *text, u32 color, int size);
void DrawCityInterfaceTextCentered(const CityInterfaceFont *font, Canvas *canvas, CityRect bounds,
    const char *text, u32 color, bool shadow, int size);
void DrawCityInterfaceTextInRow(const CityInterfaceFont *font, Canvas *canvas, CityRect bounds,
    const char *text, u32 color);
void DrawCityInterfaceClose(const CityInterfaceFont *font, Canvas *canvas, CityRect bounds, u32 color);

// Text is terminated UTF-8. A negative byte limit uses all text; other limits keep complete characters.
// Face must be valid and size is positive logical pixels. Assets supplies glyph metrics and borrowed coverage;
// measurement and painting share advances from the master variant, independent of the selected raster detail.
int CityPublicationTextWidth(const CityInterfaceFont *font, const char *text, int bytes,
    CityPublicationFace face, int size);
void DrawCityPublicationText(const CityInterfaceFont *font, Canvas *canvas, int x, int y,
    const char *text, int bytes, CityPublicationFace face, int size, u32 color);


// Overlay Font

const CityOverlayGlyph *CityOverlayFontGlyphs(const CityOverlayFont *font, int zoom);
// Measure terminated byte-indexed overlay text in logical pixels by summing stored glyph advances. Unsupported
// bytes contribute zero; unsupported zooms use the base glyph set. Font and text are borrowed without mutation.
int CityOverlayTextWidth(const CityOverlayFont *font, int zoom, const char *text);


#endif
