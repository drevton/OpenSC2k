#ifndef SC2K_UI_MESSAGES_H
#define SC2K_UI_MESSAGES_H

#include "city/types.h"

typedef struct Canvas Canvas;
typedef struct Game Game;
typedef struct Window Window;
typedef struct RunSprite RunSprite;
typedef struct CityCanvasDesktop CityCanvasDesktop;

typedef enum CityMessageStyle {
  CITY_MESSAGE_INFORMATION = 1, CITY_MESSAGE_WARNING = 2, CITY_MESSAGE_ERROR = 3
} CityMessageStyle;

typedef enum CityMessageButtons {
  CITY_MESSAGE_NO_BUTTONS = 0x10, CITY_MESSAGE_OK = 0x20,
  CITY_MESSAGE_YES_NO = 0x30, CITY_MESSAGE_YES_NO_CANCEL = 0x40
} CityMessageButtons;

// Open variants use the same flags and borrowed-content contract as OpenCityMessageBox.
i32 Sc2kCanvasDesktopOpenNotice(CityCanvasDesktop *desktop, CityRect bounds,
    const char *title, const char *message, u32 flags);
i32 Sc2kCanvasDesktopOpenConfirm(CityCanvasDesktop *desktop, CityRect bounds,
    const char *title, const char *message, u32 flags);
i32 Sc2kCanvasDesktopOpenAdvisor(CityCanvasDesktop *desktop, CityRect bounds,
    const char *title, const char *message, const RunSprite *portrait, u32 flags);

// Paint and hit rectangles use logical canvas coordinates; message painting restores its text clip.
void DrawCityMessageBox(Canvas *canvas, const Window *window);
void DrawCityMessageButton(Canvas *canvas, CityRect bounds, const char *label, CityMessageStyle style);
// Returns CITY_DIALOG_NONE outside a button; close chooses Cancel, No or OK for the configured button set.
int CityMessageAnswerAt(const Window *window, int x, int y);

// Flags combine one CityMessageStyle and one CityMessageButtons value. Returns a desktop array index, or -1.
// Copies the window record. Title/portrait stay borrowed; message text is borrowed unless owned_message owns it.
// Successful opening transfers owned_message to the window; failure leaves that field with the caller.
int OpenCityMessageBox(CityCanvasDesktop *desktop, Window window, u32 flags);
CityRect CityMessageBounds(const CityCanvasDesktop *desktop, const Window *window);
// After changing message text, resize and invalidate the old/new bounds. Advisor windows keep their shared size.
void ResizeCityMessageBox(CityCanvasDesktop *desktop, Window *window);

// Store a local blocker or INVALID_TILE, damaging its old and new marker before changing UI state.
void SetCityErrorTile(Game *game, TileIndex tile);

// Nonretained errors replace earlier errors. The detail is copied; the summary stays borrowed.
// Successful opening gives the window ownership of the copied detail; failed opening releases it.
int OpenCityCommandErrorWindow(Game *game, const char *summary, const char *detail, bool retained, TileIndex tile);
void CloseCityCommandErrorWindows(CityCanvasDesktop *desktop);
CityRect CityCommandErrorBounds(Game *game, const char *summary, const char *detail);

#endif
