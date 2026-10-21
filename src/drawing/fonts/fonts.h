#ifndef SC2K_DOMAIN_SRC_DRAWING_FONTS_FONTS_H
#define SC2K_DOMAIN_SRC_DRAWING_FONTS_FONTS_H

#include "drawing/canvas.h"
#include "city/types.h"
#include "assets/fonts.h"

int CityInterfaceTextWidth(const CityInterfaceFont *font, const char *text);
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

int CityPublicationTextWidth(const CityInterfaceFont *font, const char *text, int bytes,
    CityPublicationFace face, int size);
void DrawCityPublicationText(const CityInterfaceFont *font, Canvas *canvas, int x, int y,
    const char *text, int bytes, CityPublicationFace face, int size, u32 color);


// Overlay Font

const CityOverlayGlyph *CityOverlayFontGlyphs(const CityOverlayFont *font, int zoom);


#endif
