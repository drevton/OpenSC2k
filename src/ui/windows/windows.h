#ifndef SC2K_UI_WINDOWS_H
#define SC2K_UI_WINDOWS_H

#include "drawing/types.h"
#include "ui/windows/types.h"
#include "ui/windows/controls.h"

// UI owns the window stack, focus and dirty regions. Window pointers are borrowed until the stack changes;
// retain IDs across callbacks or operations that open, close or focus windows.

typedef struct Game Game;
typedef struct CityInterfaceState CityInterfaceState;
typedef struct CanvasSDLEvent CanvasSDLEvent;

typedef enum WindowEventKind {
  WINDOW_PAINT, WINDOW_INPUT, WINDOW_DROPDOWN, WINDOW_ZOOM, WINDOW_DESTROY, WINDOW_TIMER
} WindowEventKind;

// Paint borrows a window-local canvas for this call only. Input keeps the host
// sample alongside logical desktop coordinates; wheel deltas stay in input.
// Procedures may open/close windows: retain IDs, not Window pointers, across callbacks.
// WINDOW_DESTROY borrows a detached record whose ID is already absent from the stack.
// Its owned data stays alive until the procedure returns.
// TIMER delivers host real-time seconds, including while dialogs pause City. Return true only for a presentation
// change; timers mark their own damage and may dismiss their window. No common timer behavior follows.
typedef struct WindowEvent {
  WindowEventKind kind;
  int x, y; // Logical desktop position for input/zoom; paint uses window-local coordinates.
  union {
    Canvas *canvas;
    const CanvasSDLEvent *input;
    int zoom_steps;
    double now; // WINDOW_TIMER host time; never a simulation advance.
    const CityDropdownSelection *selection;
  };
} WindowEvent;

// Procedures run first. Unhandled input reaches common chrome handling after a fresh lookup by ID;
// a window closed by its procedure consumes that input. Other unhandled event kinds return false.
// Borrowed window pointers must be reacquired after delivery, which may change the stack.
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

// Physical window origins are independent of scaled logical widget geometry.
void Sc2kCanvasDesktopPixelMetrics(CityCanvasDesktop *desktop, i32 scale, i32 map_scale,
    i32 width, i32 height);
CityRect Sc2kCanvasDesktopPixelBounds(const CityCanvasDesktop *desktop, u32 id);
void PositionCityWindowPixels(CityCanvasDesktop *desktop, u32 id, CityPoint position);
// Convert framebuffer pixels to logical desktop coordinates using the window's physical anchor.
// ID 0 chooses the topmost visible window under the point. A missing window uses the desktop scale;
// before pixel metrics are installed the point is unchanged. This query never changes capture or focus.
CityPoint CityWindowPointFromPixels(const CityCanvasDesktop *desktop, u32 window_id, int x, int y);
// Focus the visible physical hit without highlighting; an existing drag stays attached to its window.
u32 Sc2kCanvasDesktopFocusPixels(CityCanvasDesktop *desktop, i32 x, i32 y);
void Sc2kCanvasDesktopBeginPixelDrag(CityCanvasDesktop *desktop, i32 x, i32 y);
bool Sc2kCanvasDesktopMovePixelDrag(CityCanvasDesktop *desktop, i32 x, i32 y);

void Sc2kCanvasSetUIScale(CityCanvasDesktop *desktop, i32 numerator, i32 denominator);
void Sc2kCanvasSetMapZoom(CityCanvasDesktop *desktop, int level);
CityRect Sc2kCanvasUIBounds(const CityCanvasDesktop *desktop, CityRect bounds);
void Sc2kCanvasSetWindowSnapRadius(CityCanvasDesktop *desktop, i32 radius);
void DrawCityDesktopFramebuffer(Game *game, const CityCanvasDesktop *desktop, Canvas *framebuffer,
    const u8 *map_indices, i32 map_stride, const u32 *palette, i32 map_scale, i32 ui_scale);
// Translate logical damage to framebuffer pixels, respecting explicit window anchors.
CityRect CityCanvasDirtyPixelBounds(const CityCanvasDesktop *desktop, CityRect logical);
// Paint with a window-local canvas; return button-probe bounds in logical desktop coordinates.
void DrawCityCanvasWindow(Game *game, Canvas *canvas, const Window *window);
void DrawCityDesktopUI(Game *game, const CityCanvasDesktop *desktop, Canvas *canvas);
// Composite UI damage over the already painted map, then remember the painted preview bounds.
// The prepared framebuffer and main view are required. Consume desktop/placement dirty state after painting;
// keep final pixel damage for upload and F8. Game owns frame timing and presentation.
void ComposeCityDesktopFrame(City *city);
// With no active map, paint the startup backdrop and UI damage into the prepared framebuffer.
// Discard retained main-map/preview pixels and consume dirty state; do not prepare or present the display.
void ComposeCityStartupFrame(City *city);
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
// Open or focus a singleton. Allocate required Drawing caches before publishing the new window/ID.
// Returns its stack index, or -1 without inserting a window when capacity/allocation fails.
// New insertion adopts owned fields; failure or focusing an existing singleton leaves them with the caller.
// Picture-bearing windows require a bound Game/Drawing.
i32 Sc2kCanvasDesktopOpen(CityCanvasDesktop *desktop, Window window);
// Dismissal detaches before WINDOW_DESTROY; completion callbacks still wait for the outer UI loop.
bool Sc2kCanvasDesktopDismissTop(CityCanvasDesktop *desktop);
bool Sc2kCanvasDesktopDismissWindow(CityCanvasDesktop *desktop, u32 window_id);
// Window state is released before completion runs, outside any command.
// Attach before any call that can reconcile pauses or run nested UI work. Failure leaves the window unchanged;
// success retains callback delivery and borrows its context until completion. This API never frees the context.
bool AttachCityWindowCallback(Window *window, CityWindowCallback *callback, void *context);
// Game supplies the UI queue and command depth; continuations receive its captured City.
// Drain detached completion batches only outside commands. Window-owned data is already released.
// Windows resume in dismissal order; callbacks attached to one window resume newest first.
// Each record is freed before its callback runs; callbacks may open, close or reorder windows.
void CompleteCityWindowCallbacks(Game *game);

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
