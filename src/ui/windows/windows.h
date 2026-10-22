#ifndef SC2K_UI_WINDOWS_H
#define SC2K_UI_WINDOWS_H

#include "drawing/canvas.h"
#include "ui/windows/state.h"

// UI owns the window stack, focus and dirty regions. Window pointers are borrowed until the stack changes;
// retain IDs across callbacks or operations that open, close or focus windows.

typedef struct CanvasSDLEvent CanvasSDLEvent;

typedef enum WindowEventKind {
  WINDOW_PAINT, WINDOW_INPUT, WINDOW_DROPDOWN, WINDOW_ZOOM, WINDOW_DESTROY
} WindowEventKind;

// Paint borrows a window-local canvas for this call only. Input keeps the host
// sample alongside logical desktop coordinates; wheel deltas stay in input.
// Procedures may open/close windows: retain IDs, not Window pointers, across callbacks.
// WINDOW_DESTROY borrows a detached record whose ID is already absent from the stack.
// Its owned data stays alive until the procedure returns.
typedef struct WindowEvent {
  WindowEventKind kind;
  int x, y; // Logical desktop position for input/zoom; paint uses window-local coordinates.
  union {
    Canvas *canvas;
    const CanvasSDLEvent *input;
    int zoom_steps;
    const CityDropdownSelection *selection;
  };
} WindowEvent;

// Window pointers are borrowed until the next callback that can change the stack.
bool SendWindowEvent(Game *game, const Window *window, const WindowEvent *event);
bool DispatchCityWindowInput(Game *game, const CanvasSDLEvent *input);
Window *FindCityWindowAtPixels(CityCanvasDesktop *desktop, int x, int y);

typedef enum CityCaptionControl {
  CITY_CAPTION_NONE,
  CITY_CAPTION_SHADE, CITY_CAPTION_DEFAULT_SIZE, CITY_CAPTION_STICKY
} CityCaptionControl;
// Logical caption geometry. Callers decide which windows offer these controls and apply the action.
CityRect CityCaptionControlBounds(const Window *window, CityCaptionControl control);
CityCaptionControl CityCaptionControlAt(const Window *window, int x, int y);

// A logical coordinate view backed by the final framebuffer. The callback must
// use canvas drawing functions; stride and pixel storage remain physical.
typedef void (*CityCanvasScaledPaint)(Canvas *canvas, void *context);
void Sc2kCanvasPaintScaled(Canvas *framebuffer, i32 numerator, i32 denominator,
    CityCanvasScaledPaint paint, void *context);

// Rounding logical coordinates to cover physical pixels follows the desktop scale.
i32 CanvasScaleFloor(i32 value, i32 numerator, i32 denominator);
i32 CanvasScaleCeil(i32 value, i32 numerator, i32 denominator);
CityRect CanvasScaleCover(CityRect b, i32 numerator, i32 denominator);

// Physical window origins are independent of scaled logical widget geometry.
void Sc2kCanvasDesktopPixelMetrics(CityCanvasDesktop *desktop, i32 scale, i32 map_scale,
    i32 width, i32 height);
CityRect Sc2kCanvasDesktopPixelBounds(const CityCanvasDesktop *desktop, u32 id);
void PositionCityWindowPixels(CityCanvasDesktop *desktop, u32 id, CityPoint position);
void Sc2kCanvasDesktopPixelPointer(const CityCanvasDesktop *desktop, u32 capture,
    i32 x, i32 y, i32 *logical_x, i32 *logical_y);
// Focus the visible physical hit without highlighting; an existing drag stays attached to its window.
u32 Sc2kCanvasDesktopFocusPixels(CityCanvasDesktop *desktop, i32 x, i32 y);
void Sc2kCanvasDesktopBeginPixelDrag(CityCanvasDesktop *desktop, i32 x, i32 y);
bool Sc2kCanvasDesktopMovePixelDrag(CityCanvasDesktop *desktop, i32 x, i32 y);

void Sc2kCanvasSetUIScale(CityCanvasDesktop *desktop, i32 numerator, i32 denominator);
void Sc2kCanvasSetMapZoom(CityCanvasDesktop *desktop, int level);
CityRect Sc2kCanvasUIBounds(const CityCanvasDesktop *desktop, CityRect bounds);
void Sc2kCanvasSetWindowSnapRadius(CityCanvasDesktop *desktop, i32 radius);
void DrawCityDesktopMap(Game *game, const CityCanvasDesktop *desktop, Canvas *canvas,
    const u8 *map_indices, i32 map_stride, const u32 *palette);
void DrawCityDesktopFramebuffer(Game *game, const CityCanvasDesktop *desktop, Canvas *framebuffer,
    const u8 *map_indices, i32 map_stride, const u32 *palette, i32 map_scale, i32 ui_scale);
// Translate logical damage to framebuffer pixels, respecting explicit window anchors.
CityRect CityCanvasDirtyPixelBounds(const CityCanvasDesktop *desktop, CityRect logical);
// Paint with a window-local canvas; return button-probe bounds in logical desktop coordinates.
void DrawCityCanvasWindow(Game *game, Canvas *canvas, const Window *window);
void DrawCityDesktopUI(Game *game, const CityCanvasDesktop *desktop, Canvas *canvas);
bool Sc2kCanvasClickWindow(Window *window, i32 x, i32 y);
// Logical paint bounds include the window border and any unshaded content.
CityRect CanvasWindowPaintRect(const Window *window);
// Window damage uses physical pixels; id 0 names the main map. Other ids use window-local pixels.
bool InvalidateCityCanvasWindow(Game *game, u32 window, const CityRect *rect);
void Sc2kDirtyClear(CityDirtyRegions *dirty);
void Sc2kDirtyMarkRect(CityDirtyRegions *dirty, CityRect rect);
// Move bounds into the desktop while preserving enough caption to drag an oversized window.
void KeepCityWindowCaptionVisible(const CityCanvasDesktop *desktop, CityRect *bounds);
// Button feedback uses host time and the UI sound channel.
void PlayCityButtonBeep(Game *game);
void BeginCityButtonFeedback(Game *game, int pixel_x, int pixel_y, double now);
bool PollCityButtonFeedback(Game *game, double now);
void BeginCityButtonRepeat(Game *game, const Window *window, const WindowEvent *event);
void ResetCityWindowSize(Window *window);
void Sc2kCanvasDesktopInit(CityCanvasDesktop *desktop);
i32 Sc2kCanvasDesktopOpen(CityCanvasDesktop *desktop, Window window);
// Dismissal detaches before WINDOW_DESTROY; completion callbacks still wait for the outer UI loop.
bool Sc2kCanvasDesktopDismissTop(CityCanvasDesktop *desktop);
bool Sc2kCanvasDesktopDismissWindow(CityCanvasDesktop *desktop, u32 window_id);
// IDs in back-to-front order for one pass. Newly opened windows wait for the next pass.
// Reacquire each ID before use because callbacks may close or reorder windows.
typedef struct WindowIds {
  u32 ids[CITY_CANVAS_WINDOW_CAPACITY];
  u8 count;
} WindowIds;
WindowIds SnapshotCityWindowIds(const CityCanvasDesktop *desktop);
Window *FindCityCanvasWindow(CityCanvasDesktop *desktop, u32 id);
// Bring an existing visible window forward; return its index, or -1 when absent.
int FocusCityCanvasWindow(CityCanvasDesktop *desktop, u32 id);
int FocusCityCanvasWindowKind(CityCanvasDesktop *desktop, WindowKind kind);
bool PollCityWindowHighlights(CityCanvasDesktop *desktop, u32 milliseconds);
bool Sc2kCanvasDesktopPointerDown(CityCanvasDesktop *desktop, i32 x, i32 y);
bool Sc2kCanvasDesktopPointerMove(CityCanvasDesktop *desktop, i32 x, i32 y);
void Sc2kCanvasDesktopPointerUp(CityCanvasDesktop *desktop);

void VisitCanvasMapPixels(const CityCanvasDesktop *desktop, u32 window_id, CityRect area,
    CityCanvasVisiblePaint paint, void *context);
// Repaints run beneath toolbar/status controls; the compositor restores them afterward.
void VisitCanvasMapRepaintPixels(const CityCanvasDesktop *desktop, u32 window_id, CityRect area,
    CityCanvasVisiblePaint paint, void *context);
void Sc2kCanvasDesktopVisitMap(const CityCanvasDesktop *desktop, CityRect area,
    CityCanvasVisiblePaint paint, void *context);
// Local content pixels of an additional map viewport, excluding windows above it.
void Sc2kCanvasDesktopVisitViewport(const CityCanvasDesktop *desktop, u32 window_id, CityRect area,
    CityCanvasVisiblePaint paint, void *context);
void DrawCityDesktop(Game *game, CityCanvasDesktop *desktop, Canvas *canvas,
    const u8 *map_indices, i32 map_stride, const u32 *palette);

int CountCityCanvasWindows(const CityInterfaceState *ui);

#endif
