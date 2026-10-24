#ifndef SC2K_CANVAS_SDL_H
#define SC2K_CANVAS_SDL_H

#include <stddef.h>

#include "drawing/types.h"
#include "city/types.h"
#include "ui/input.h"

typedef struct CanvasSDL CanvasSDL;
typedef struct Game Game;
typedef struct DrawingOptions DrawingOptions;
struct ScreenDamage;

typedef enum CanvasSdlMouseButton {
  CANVAS_SDL_BUTTON_LEFT = 1, CANVAS_SDL_BUTTON_MIDDLE = 2, CANVAS_SDL_BUTTON_RIGHT = 3,
} CanvasSdlMouseButton;

enum {
  CANVAS_SDL_NONE,
  CANVAS_SDL_CLOSE,
  CANVAS_SDL_EXPOSE,
  CANVAS_SDL_FOCUS_LOST,
  CANVAS_SDL_FOCUS_GAINED,
  CANVAS_SDL_POINTER_DOWN,
  CANVAS_SDL_POINTER_UP,
  CANVAS_SDL_POINTER_MOVE,
  CANVAS_SDL_KEY_DOWN,
  CANVAS_SDL_KEY_UP,
  CANVAS_SDL_TEXT_INPUT,
  CANVAS_SDL_SCROLL, // x/y retain OS scroll direction in 1/1024 units; scroll_flipped records device inversion.
  CANVAS_SDL_POINTER_LEAVE,
  CANVAS_SDL_ZOOM_BEGIN,
  CANVAS_SDL_ZOOM, // y is pinch magnification delta in 1/65536 units.
  CANVAS_SDL_ZOOM_END
};

typedef struct CanvasSDLEvent {
  i32 kind;
  i32 x;
  i32 y;
  union {
    u8 button;
    u8 scroll_flipped; // CANVAS_SDL_SCROLL only; shares the pointer-button byte.
  };
  u8 alt;
  u8 shift;
  u8 ctrl; // Key events: bit0 Control, bit1 macOS Command; pointer events retain the original Control flag.
  CityHotkey key;
  union {
    char text[32]; // UTF-8 text input, independent of key-down events.
    struct {
      i32 pixels_x, pixels_y; // Precise content displacement in 1/1024 framebuffer pixels, with OS direction applied.
      bool precise;
    } scroll;
  };
} CanvasSDLEvent;

_Static_assert(sizeof(CanvasSDLEvent) == 52, "SDL host event size matches ctypes");
_Static_assert(offsetof(CanvasSDLEvent, ctrl) == 15, "SDL host Ctrl offset matches ctypes");
_Static_assert(offsetof(CanvasSDLEvent, key) == 16, "SDL host key offset matches ctypes");

// Game, Assets, Drawing and any attached UI must outlive the display. Standalone canvases may omit UI.
// Opening does not attach the display to UI; failed creation leaves existing UI attachments unchanged.
CanvasSDL *Sc2kSDLOpen(Game *game, i32 width, i32 height, i32 scale, i32 hidden);
// Detach matching UI display references before releasing cursors, gestures and host window resources.
void Sc2kSDLClose(CanvasSDL *display);
bool Sc2kSDLIsMinimized(CanvasSDL *display);
const char *Sc2kSDLError(void);
void Sc2kSDLTitle(CanvasSDL *display, const char *title);
// Borrow the open display's logical width x height pixels; stride is in u32 pixels and must cover its width.
// Requires the original logical-canvas mode and expands each source pixel to the fixed 2x texture.
// Both presentation APIs return zero on success or -1 with an SDL error on failure.
i32 Sc2kSDLPresent(CanvasSDL *display, const u32 *pixels, i32 stride);
// Deliver deferred input first. Positive wait_ms waits up to that many milliseconds; other values poll.
// True means an event was consumed; CANVAS_SDL_NONE can report an event with no native action.
bool Sc2kSDLEvent(CanvasSDL *display, CanvasSDLEvent *event, i32 wait_ms);
// Main-loop pacing while a City continuation permits only timer delivery.
// Keep user input in SDL's existing queue until that caller completes.
void WaitForCityCanvasFrame(int milliseconds);
// Filter activation for the busy dialog gate; retain other canvas input in FIFO order.
int PollCanvasActivation(CanvasSDL *display, bool *active);
i32 Sc2kSDLOutputSize(CanvasSDL *display, i32 *width, i32 *height);
i32 Sc2kSDLPixelScale(CanvasSDL *display);
// Switch to physical framebuffer presentation and configure cursor/UI scale in 100..500.
i32 Sc2kSDLConfigureFramebuffer(CanvasSDL *display, i32 ui_scale);
// Requires framebuffer configuration. Borrow already rasterized physical pixels for this call; SDL only uploads them.
// NULL damage requests a full upload. restore also uploads pixels beneath the previous debug overlay.
i32 Sc2kSDLPresentFramebuffer(CanvasSDL *display, const Canvas *canvas,
    const struct ScreenDamage *damage, CityRect restore);
// Cached custom native cursors track movement independently of frame rendering.
// The gold arrow is used over controls/without a selected tool; map tools use
// the white pointer and yellow badge. Both follow interface scale.
void Sc2kSDLCursorTool(CanvasSDL *display, i32 tool, i32 over_map);
bool Sc2kSDLUIPointer(CanvasSDL *display, i32 *x, i32 *y);
bool Sc2kSDLFramebufferPointer(CanvasSDL *display, i32 *x, i32 *y);
void Sc2kSDLDisplayOptions(CanvasSDL *display, DrawingOptions *options);
i32 Sc2kSDLFullscreen(CanvasSDL *display, i32 enabled);
i32 Sc2kSDLReadPixels(CanvasSDL *display, u32 *pixels, i32 stride);
void Sc2kSDLResize(CanvasSDL *display, i32 width, i32 height);
void Sc2kSDLPasteText(char *text, size_t capacity, int *caret, int *select_all);
bool Sc2kSDLOpenURL(const char *url);
u32 Sc2kSDLWindowId(CanvasSDL *display);

#endif
