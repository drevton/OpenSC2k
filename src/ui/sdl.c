#include "drawing/sprites/preparation.h"
#include "drawing/maps/map_view.h"
#include "base/version.h"
#include "ui/tools/types.h"
#include "ui/sdl.h"

#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "game/state.h"
#include "drawing/state.h"
#include "ui/state.h"
#include "ui/map.h"
#include "ui/cursor/cursor.h"
#include "drawing/cursors.h"
#include "ui/text/text.h"
#include "drawing/raster/screen_damage.h"

#ifdef __APPLE__
#include "ui/macos.h"
#endif

// Presentation and input only. All game and window pixels are drawn by drawing.c.

typedef struct {
  CanvasSDLEvent event;
  i32 ui_x, ui_y, ui_valid, pixel_x, pixel_y;
} CanvasDeferredEvent;

struct CanvasSDL {
  Game *game; // Borrowed cursor asset and raster owners; outlive the display.
  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Texture *texture;
  const u32 *texture_source; // NULL until a complete framebuffer has initialized this texture.
  SDL_Cursor *cursors[CITY_CANVAS_TOOL_COUNT + 1]; // Gold arrow, then tool badges.
  SDL_Cursor *previous_cursor, *installed_cursor;
  int cursor_size;
  u32 cursor_revision;
  i32 width;
  i32 height;
  i32 texture_scale, texture_width, texture_height;
  i32 direct_framebuffer, ui_scale, ui_x, ui_y, ui_valid, pixel_x, pixel_y;
  CanvasDeferredEvent *deferred_events;
  i32 deferred_count, deferred_capacity;
  i32 cursor_tool;
  bool cursor_badge, cursor_inside, cursor_focus;
  bool suppress_console_text;
  bool debug_zoom;
  CityPoint debug_zoom_center;
#ifdef __APPLE__
  void *gesture_monitor;
  u32 gesture_event;
#endif
};

static void UpdateCursor(CanvasSDL *display);

bool Sc2kSDLOpenURL(const char *url) {
  return SDL_OpenURL(url) == 0;
}

void Sc2kSDLPasteText(char *text, size_t capacity, int *caret, int *select_all) {
  char *clipboard = SDL_GetClipboardText();
  if (!clipboard) return;
  // The console prompt is one line. Paste never submits clipboard commands.
  for (char *at = clipboard; *at; ++at)
    if (*at == '\r' || *at == '\n' || *at == '\t') *at = ' ';
  InsertCityTextInput(text, capacity, caret, select_all, clipboard, false);
  SDL_free(clipboard);
}

// Native cursors track the pointer independently of rendering and VSync.
static SDL_Cursor *CreateCityCursor(CanvasSDL *display, int tool, int size) {
  SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!surface) return NULL;
  RunSprite *sprite = PrepareCityCursorSprite(GetCityCursorSprite(display->game, tool), size);
  bool copied = sprite && CopyRunSpriteRgba(sprite, surface->pixels, surface->pitch / sizeof(u32), NULL, NULL);
  FreeRunSprite(sprite);
  if (!copied) { SDL_FreeSurface(surface); return NULL; }
  SDL_Cursor *cursor = SDL_CreateColorCursor(surface, 0, 0);
  SDL_FreeSurface(surface);
  return cursor;
}

// Build the replacement before releasing host cursors so a failed preparation retains the installed image.
static int ConfigureCursorSize(CanvasSDL *display, int size) {
  if (display->cursor_size == size && display->cursor_revision == display->game->drawing->cursors.revision)
    return 0;
  SDL_Cursor *cursor = CreateCityCursor(display, CITY_CANVAS_TOOL_NONE, size);
  if (!cursor) return -1;
  if (!display->installed_cursor) display->previous_cursor = SDL_GetCursor();
  SDL_SetCursor(cursor);
  for (size_t index = 0; index < CITY_CANVAS_TOOL_COUNT + 1; ++index) {
    SDL_FreeCursor(display->cursors[index]);
    display->cursors[index] = NULL;
  }
  display->cursors[0] = cursor;
  display->installed_cursor = cursor;
  display->cursor_size = size;
  // Creating the arrow may initialize the composed tool cache. Retain the revision after that work.
  display->cursor_revision = display->game->drawing->cursors.revision;
  return 0;
}

// SDL's native cursor dimensions are window points, not Retina backing pixels.
static int ConfigureCursorScale(CanvasSDL *display, int scale) {
  int output_width, output_height, window_width, window_height;
  if (SDL_GetRendererOutputSize(display->renderer, &output_width, &output_height)) return -1;
  SDL_GetWindowSize(display->window, &window_width, &window_height);
  if (output_width < 1 || window_width < 1) return SDL_SetError("Invalid cursor display size");
  int pixel_size = Sc2kCanvasCursorBounds(0, 0, scale).right;
  int size = IntMax(1, ((i64)pixel_size * window_width + output_width - 1) / output_width);
  return ConfigureCursorSize(display, size);
}

const char *Sc2kSDLError(void) {
  return SDL_GetError();
}

void Sc2kSDLClose(CanvasSDL *display) {
  if (display != NULL) {
    CityInterfaceState *ui = display->game->ui;
    if (ui) {
      if (ui->display == display) ui->display = NULL;
      if (ui->command_display == display) ui->command_display = NULL;
    }
    if (display->installed_cursor && SDL_GetCursor() == display->installed_cursor)
      SDL_SetCursor(display->previous_cursor);
#ifdef __APPLE__
    RemoveMacCityGestures(display->gesture_monitor);
#endif
    for (size_t index = 0; index < CITY_CANVAS_TOOL_COUNT + 1; ++index)
      SDL_FreeCursor(display->cursors[index]);
    SDL_DestroyTexture(display->texture);
    SDL_DestroyRenderer(display->renderer);
    SDL_DestroyWindow(display->window);
    free(display->deferred_events);
    free(display);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
  }
}

static void FitWindowToDisplay(CanvasSDL *display) {
  if (SDL_GetWindowFlags(display->window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIDDEN)) return;
  SDL_Rect usable;
  int index = SDL_GetWindowDisplayIndex(display->window);
  if (index < 0 || SDL_GetDisplayUsableBounds(index, &usable)) return;
  int top = 0, left = 0, bottom = 0, right = 0;
  SDL_GetWindowBordersSize(display->window, &top, &left, &bottom, &right);
  int width, height, x, y;
  SDL_GetWindowSize(display->window, &width, &height);
  int max_width = IntMax(1, usable.w - left - right);
  int max_height = IntMax(1, usable.h - top - bottom);
  if (width <= max_width && height <= max_height) return;
  width = IntMin(width, max_width);
  height = IntMin(height, max_height);
  SDL_GetWindowPosition(display->window, &x, &y);
  x = IntClamp(x, usable.x + left, usable.x + usable.w - right - width);
  y = IntClamp(y, usable.y + top, usable.y + usable.h - bottom - height);
  SDL_SetWindowSize(display->window, width, height);
  SDL_SetWindowPosition(display->window, x, y);
}

CanvasSDL *Sc2kSDLOpen(Game *game, i32 width, i32 height, i32 scale, i32 hidden) {
  SDL_SetMainReady();
#ifdef __APPLE__
  SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "0");
#endif
  if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
    return NULL;
#ifdef __APPLE__
  EnableMacCityScrollMomentum();
#endif
  CanvasSDL *display = calloc(1, sizeof(*display));
  if (display == NULL) {
    SDL_SetError("Cannot allocate canvas display");
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return NULL;
  }
  display->game = game;
  display->width = width;
  display->height = height;
  display->cursor_tool = CITY_CANVAS_TOOL_NONE;
  display->cursor_focus = 1;
  u32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
  flags |= hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN;
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
  display->window = SDL_CreateWindow(SC2K_APP_NAME, SDL_WINDOWPOS_CENTERED,
      SDL_WINDOWPOS_CENTERED, width * scale, height * scale, flags);
  if (display->window != NULL) {
    FitWindowToDisplay(display);
    display->renderer = SDL_CreateRenderer(display->window, -1,
        hidden ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (display->renderer == NULL && !hidden)
      display->renderer = SDL_CreateRenderer(display->window, -1, SDL_RENDERER_SOFTWARE);
  }
  if (display->renderer != NULL && SDL_RenderSetLogicalSize(display->renderer, width, height) == 0) {
    SDL_RenderSetIntegerScale(display->renderer, SDL_TRUE);
    display->texture = SDL_CreateTexture(display->renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, width * 2, height * 2);
  }
  if (display->texture == NULL) {
    // Destruction may replace SDL's error; preserve the actual creation failure.
    char message[512];
    SDL_strlcpy(message, SDL_GetError(), sizeof(message));
    Sc2kSDLClose(display);
    SDL_SetError("%s", message);
    return NULL;
  }
  SDL_SetTextureBlendMode(display->texture, SDL_BLENDMODE_NONE);
  display->texture_scale = 2;
  display->texture_width = width * 2;
  display->texture_height = height * 2;
  i32 cursor_scale = Sc2kSDLPixelScale(display) * 100;
  cursor_scale = IntMin(cursor_scale, 500);
  if (ConfigureCursorScale(display, cursor_scale)) {
    char message[512];
    SDL_strlcpy(message, SDL_GetError(), sizeof(message));
    Sc2kSDLClose(display);
    SDL_SetError("%s", message);
    return NULL;
  }
#ifdef __APPLE__
  SetMacCityAppIcon();
  display->gesture_event = SDL_RegisterEvents(4);
  if (display->gesture_event != (u32)-1)
    display->gesture_monitor = InstallMacCityGestures(display->window, display->gesture_event);
#endif
  SDL_StartTextInput();
  if (!hidden) {
    SDL_ShowWindow(display->window);
    SDL_RaiseWindow(display->window);
    SDL_PumpEvents();
  }
  return display;
}

void Sc2kSDLTitle(CanvasSDL *display, const char *title) {
  if (strcmp(SDL_GetWindowTitle(display->window), title))
    SDL_SetWindowTitle(display->window, title);
}

static SDL_Rect CanvasTextureDestination(const CanvasSDL *display, int width, int height) {
  int scale = display->debug_zoom ? 2 : 1;
  SDL_Rect rect = {0, 0, display->texture_width * scale, display->texture_height * scale};
  rect.x = (width - rect.w) / 2;
  rect.y = (height - rect.h) / 2;
  if (display->debug_zoom) {
    if (rect.w >= width)
      rect.x = IntClamp(width / 2 - display->debug_zoom_center.x * scale, width - rect.w, 0);
    if (rect.h >= height)
      rect.y = IntClamp(height / 2 - display->debug_zoom_center.y * scale, height - rect.h, 0);
  }
  return rect;
}

static i32 CopyCanvasTexture(CanvasSDL *display) {
  SDL_SetRenderDrawColor(display->renderer, 0, 0, 0, 255);
  if (SDL_RenderClear(display->renderer) != 0)
    return -1;
  if (display->direct_framebuffer) {
    i32 width, height;
    SDL_GetRendererOutputSize(display->renderer, &width, &height);
    SDL_Rect destination = CanvasTextureDestination(display, width, height);
    return SDL_RenderCopy(display->renderer, display->texture, NULL, &destination);
  }
  return SDL_RenderCopy(display->renderer, display->texture, NULL, NULL);
}

i32 Sc2kSDLPresent(CanvasSDL *display, const u32 *pixels, i32 stride) {
  i32 scale = 2;
  if (display->direct_framebuffer)
    return SDL_SetError("Use framebuffer presentation after configuring direct drawing");
  void *destination;
  i32 pitch;
  if (SDL_LockTexture(display->texture, NULL, &destination, &pitch) != 0)
    return -1;
  for (i32 y = 0; y < display->height * scale; ++y) {
    u32 *row = (u32 *)((u8 *)destination + y * pitch);
    for (i32 x = 0; x < display->width * scale; ++x)
      row[x] = pixels[(y / scale) * stride + x / scale];
  }
  SDL_UnlockTexture(display->texture);
  if (CopyCanvasTexture(display) != 0)
    return -1;
  SDL_RenderPresent(display->renderer);
  return 0;
}

i32 Sc2kSDLPixelScale(CanvasSDL *display) {
  i32 width, height;
  SDL_GetRendererOutputSize(display->renderer, &width, &height);
  i32 x = width / display->width, y = height / display->height;
  i32 scale = IntMin(x, y);
  return IntClamp(scale, 1, CITY_MAP_MAX_TILE_WIDTH / (8 << CITY_MAP_ZOOM_LARGE));
}

i32 Sc2kSDLConfigureFramebuffer(CanvasSDL *display, i32 ui_scale) {
  if (ui_scale < 100 || ui_scale > 500) return SDL_SetError("Invalid interface scale");
  if (ConfigureCursorScale(display, ui_scale)) return -1;
  display->ui_scale = ui_scale;
  display->direct_framebuffer = 1;
  // SDL must leave event coordinates in window points. Convert using actual
  // backing pixels ourselves, before rounding to map coordinates.
  SDL_RenderSetLogicalSize(display->renderer, 0, 0);
  SDL_RenderSetScale(display->renderer, 1, 1);
  SDL_RenderSetViewport(display->renderer, NULL);
  UpdateCursor(display);
  return 0;
}
enum { CITY_TEXTURE_UPLOAD_RECTS = 32 };
typedef struct {
  CityRect rects[CITY_TEXTURE_UPLOAD_RECTS];
  int count;
  u64 pixels;
  bool overflow;
} TextureUploadRects;

static void CollectTextureUpload(CityRect rect, void *context) {
  TextureUploadRects *upload = context;
  if (upload->count == CITY_TEXTURE_UPLOAD_RECTS) { upload->overflow = true; return; }
  upload->rects[upload->count++] = rect;
  upload->pixels += (u64)(rect.right - rect.left) * (rect.bottom - rect.top);
}

static int UploadTextureRect(CanvasSDL *display, const Canvas *canvas, CityRect area) {
  SDL_Rect rect = {area.left, area.top, area.right - area.left, area.bottom - area.top};
  void *destination;
  int pitch;
  if (SDL_LockTexture(display->texture, &rect, &destination, &pitch)) return -1;
  // Locks are write-only. Fill the whole locked rectangle, including merged
  // unchanged pixels; the rest of the texture retains its previous contents.
  for (int y = 0; y < rect.h; ++y)
    memcpy((u8 *)destination + (size_t)y * pitch,
        canvas->pixels + (size_t)(area.top + y) * canvas->stride + area.left, (size_t)rect.w * sizeof(u32));
  SDL_UnlockTexture(display->texture);
  return 0;
}

i32 Sc2kSDLPresentFramebuffer(CanvasSDL *display, const Canvas *canvas,
    const ScreenDamage *damage, CityRect restore) {
  if (!display->direct_framebuffer || !canvas || !canvas->pixels || canvas->width < 1 ||
      canvas->height < 1 || canvas->stride < canvas->width)
    return SDL_SetError("Invalid direct framebuffer");
  if (!display->texture || display->texture_width != canvas->width || display->texture_height != canvas->height) {
    SDL_Texture *texture = SDL_CreateTexture(display->renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, canvas->width, canvas->height);
    if (!texture) return -1;
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
    SDL_DestroyTexture(display->texture);
    display->texture = texture;
    display->texture_source = NULL;
    display->texture_width = canvas->width;
    display->texture_height = canvas->height;
  }
  TextureUploadRects upload = {0};
  bool full = !damage || display->texture_source != canvas->pixels ||
      damage->width != canvas->width || damage->height != canvas->height;
  if (!full) {
    VisitScreenDamage(damage, CollectTextureUpload, &upload);
    restore = IntersectCityRects(restore, (CityRect){0, 0, canvas->width, canvas->height});
    if (restore.left < restore.right && restore.top < restore.bottom) CollectTextureUpload(restore, &upload);
    // Limit lock/submission overhead when damage is fragmented or widespread.
    full = upload.overflow || upload.pixels * 2 >= (u64)canvas->width * canvas->height;
  }
  if (full) {
    if (UploadTextureRect(display, canvas, (CityRect){0, 0, canvas->width, canvas->height})) return -1;
  } else for (int i = 0; i < upload.count; ++i)
    if (UploadTextureRect(display, canvas, upload.rects[i])) return -1;
  if (CopyCanvasTexture(display)) return -1;
  SDL_RenderPresent(display->renderer);
  display->texture_source = canvas->pixels;
  return 0;
}

// Cache each badge at the current interface scale. Pointer motion neither
// redraws the canvas nor presents an extra frame while draining input events.
static void UpdateCursor(CanvasSDL *display) {
  if (ConfigureCursorSize(display, display->cursor_size)) return;
  int tool = display->cursor_badge && display->cursor_inside && display->cursor_focus ?
      display->cursor_tool : CITY_CANVAS_TOOL_NONE;
  int index = tool + 1;
  SDL_Cursor *cursor = display->cursors[index];
  if (!cursor) {
    cursor = CreateCityCursor(display, tool, display->cursor_size);
    display->cursors[index] = cursor;
  }
  if (!cursor) cursor = display->cursors[0];
  if (cursor == display->installed_cursor) return;
  SDL_SetCursor(cursor);
  display->installed_cursor = cursor;
}
void Sc2kSDLCursorTool(CanvasSDL *display, i32 tool, i32 over_map) {
  if (!display) return;
  display->cursor_tool = tool >= 0 && tool < CITY_CANVAS_TOOL_COUNT ? tool : CITY_CANVAS_TOOL_NONE;
  display->cursor_badge = over_map && display->cursor_tool >= 0;
  UpdateCursor(display);
}
bool Sc2kSDLUIPointer(CanvasSDL *display, i32 *x, i32 *y) {
  if (!display->ui_valid) return false;
  *x = display->ui_x; *y = display->ui_y;
  return true;
}
bool Sc2kSDLFramebufferPointer(CanvasSDL *display, i32 *x, i32 *y) {
  if (!display->ui_valid) return false;
  *x = display->pixel_x; *y = display->pixel_y;
  return true;
}
static void FramebufferPointer(CanvasSDL *display, CanvasSDLEvent *event) {
  display->ui_valid = 0;
  if (!display->direct_framebuffer) return;
  i32 output_width, output_height, window_width, window_height;
  SDL_GetRendererOutputSize(display->renderer, &output_width, &output_height);
  SDL_GetWindowSize(display->window, &window_width, &window_height);
  if (window_width < 1 || window_height < 1) return;
  i32 map_scale = Sc2kSDLPixelScale(display);
  i64 x = (i64)event->x * output_width / window_width;
  i64 y = (i64)event->y * output_height / window_height;
  SDL_Rect destination = CanvasTextureDestination(display, output_width, output_height);
  int scale = display->debug_zoom ? 2 : 1;
  x -= destination.x; y -= destination.y;
  x = x / scale - (x % scale < 0);
  y = y / scale - (y % scale < 0);
  display->pixel_x = (i32)x; display->pixel_y = (i32)y;
  display->cursor_inside = event->x >= 0 && event->y >= 0 &&
      event->x < window_width && event->y < window_height;
  display->ui_x = (i32)(x * 100 / display->ui_scale - (x * 100 % display->ui_scale < 0));
  display->ui_y = (i32)(y * 100 / display->ui_scale - (y * 100 % display->ui_scale < 0));
  display->ui_valid = 1;
  event->x = (i32)(x / map_scale - (x % map_scale < 0));
  event->y = (i32)(y / map_scale - (y % map_scale < 0));
}
void Sc2kSDLDisplayOptions(CanvasSDL *display, DrawingOptions *options) {
  SDL_GetRendererOutputSize(display->renderer, &options->window_width, &options->window_height);
  SDL_DisplayMode mode;
  options->refresh_rate = SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(display->window),
      &mode) ? 0 : mode.refresh_rate;
  options->fullscreen = !!(SDL_GetWindowFlags(display->window) & SDL_WINDOW_FULLSCREEN);
  SDL_strlcpy(options->driver, SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "SDL",
      sizeof(options->driver));
}
i32 Sc2kSDLFullscreen(CanvasSDL *display, i32 enabled) {
  return SDL_SetWindowFullscreen(display->window, enabled ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

static CityHotkey SDLHotkey(SDL_Keycode key, SDL_Keymod modifiers) {
  CityHotkey code = CITY_KEY_NONE;
  if (key >= 32 && key < 127) code = key >= 'a' && key <= 'z' ? key + 'A' - 'a' : key;
  else switch (key) {
    case SDLK_BACKSPACE: code = CITY_KEY_BACKSPACE; break;
    case SDLK_TAB: code = CITY_KEY_TAB; break;
    case SDLK_RETURN: case SDLK_KP_ENTER: code = CITY_KEY_RETURN; break;
    case SDLK_ESCAPE: code = CITY_KEY_ESCAPE; break;
    case SDLK_DELETE: code = CITY_KEY_DELETE; break;
    case SDLK_LEFT: code = CITY_KEY_LEFT; break;
    case SDLK_RIGHT: code = CITY_KEY_RIGHT; break;
    case SDLK_UP: code = CITY_KEY_UP; break;
    case SDLK_DOWN: code = CITY_KEY_DOWN; break;
    case SDLK_HOME: code = CITY_KEY_HOME; break;
    case SDLK_END: code = CITY_KEY_END; break;
    case SDLK_PAGEUP: code = CITY_KEY_PAGE_UP; break;
    case SDLK_PAGEDOWN: code = CITY_KEY_PAGE_DOWN; break;
    case SDLK_INSERT: code = CITY_KEY_INSERT; break;
    case SDLK_KP_PLUS: code = CITY_KEY_KEYPAD_PLUS; break;
    case SDLK_KP_MINUS: code = CITY_KEY_KEYPAD_MINUS; break;
    case SDLK_LALT: code = CITY_KEY_LEFT_ALT; break;
    case SDLK_RALT: code = CITY_KEY_RIGHT_ALT; break;
    default: if (key >= SDLK_F1 && key <= SDLK_F12) code = CITY_KEY_F1 + key - SDLK_F1; break;
  }
  if (!code) return CITY_KEY_NONE;
  return code | (modifiers & KMOD_SHIFT ? CITY_HOTKEY_SHIFT : 0) |
      (modifiers & (KMOD_CTRL | KMOD_GUI) ? CITY_HOTKEY_CTRL : 0) |
      (modifiers & KMOD_ALT ? CITY_HOTKEY_ALT : 0);
}

static bool PollSDLEvent(CanvasSDL *display, CanvasSDLEvent *event, i32 wait_ms) {
  SDL_Event input;
  memset(event, 0, sizeof(*event));
  if (!(wait_ms > 0 ? SDL_WaitEventTimeout(&input, wait_ms) : SDL_PollEvent(&input)))
    return false;
  u32 window_id = SDL_GetWindowID(display->window);
  display->ui_valid = 0;
#ifdef __APPLE__
  bool precise_scroll = display->gesture_monitor && input.type == display->gesture_event + 3;
  if (precise_scroll) input.type = SDL_MOUSEWHEEL;
  if (display->gesture_monitor && input.type >= display->gesture_event && input.type <= display->gesture_event + 2) {
    if (input.user.windowID == window_id) {
      CanvasSDLEvent pointer = {0};
      SDL_GetMouseState(&pointer.x, &pointer.y);
      FramebufferPointer(display, &pointer);
      event->kind = CANVAS_SDL_ZOOM_BEGIN + (input.type - display->gesture_event);
      event->y = input.user.code;
    }
    return true;
  }
#endif
  switch (input.type) {
    case SDL_QUIT:
      event->kind = CANVAS_SDL_CLOSE;
      break;
    case SDL_DISPLAYEVENT:
      FitWindowToDisplay(display);
      event->kind = CANVAS_SDL_EXPOSE;
      break;
    case SDL_RENDER_TARGETS_RESET:
      display->texture_source = NULL;
      event->kind = CANVAS_SDL_EXPOSE;
      break;
    case SDL_RENDER_DEVICE_RESET:
      SDL_DestroyTexture(display->texture);
      display->texture = NULL;
      display->texture_source = NULL;
      event->kind = CANVAS_SDL_EXPOSE;
      break;
    case SDL_WINDOWEVENT:
      if (input.window.windowID != window_id)
        break;
      switch (input.window.event) {
        case SDL_WINDOWEVENT_CLOSE: event->kind = CANVAS_SDL_CLOSE; break;
        case SDL_WINDOWEVENT_LEAVE:
          display->cursor_inside = 0; event->kind = CANVAS_SDL_POINTER_LEAVE; break;
        case SDL_WINDOWEVENT_ENTER: {
          CanvasSDLEvent pointer = {0};
          SDL_GetMouseState(&pointer.x, &pointer.y);
          FramebufferPointer(display, &pointer);
          event->kind = CANVAS_SDL_EXPOSE; break;
        }
        case SDL_WINDOWEVENT_FOCUS_LOST:
          display->suppress_console_text = false;
          display->cursor_focus = 0; event->kind = CANVAS_SDL_FOCUS_LOST; break;
        case SDL_WINDOWEVENT_FOCUS_GAINED:
          display->cursor_focus = 1; event->kind = CANVAS_SDL_FOCUS_GAINED; break;
        case SDL_WINDOWEVENT_DISPLAY_CHANGED:
          FitWindowToDisplay(display);
          event->kind = CANVAS_SDL_EXPOSE;
          break;
        case SDL_WINDOWEVENT_EXPOSED:
        case SDL_WINDOWEVENT_SIZE_CHANGED: event->kind = CANVAS_SDL_EXPOSE; break;
      }
      break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
      if (input.button.windowID != window_id)
        break;
      event->kind = input.type == SDL_MOUSEBUTTONDOWN ? CANVAS_SDL_POINTER_DOWN : CANVAS_SDL_POINTER_UP;
      event->x = input.button.x;
      event->y = input.button.y;
      event->button = input.button.button;
      FramebufferPointer(display, event);
      event->alt = (SDL_GetModState() & KMOD_ALT) != 0;
      event->shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
      event->ctrl = (SDL_GetModState() & KMOD_CTRL) != 0;
      break;
    case SDL_MOUSEMOTION:
      if (input.motion.windowID != window_id)
        break;
      event->kind = CANVAS_SDL_POINTER_MOVE;
      event->x = input.motion.x;
      event->y = input.motion.y;
      FramebufferPointer(display, event);
      event->alt = (SDL_GetModState() & KMOD_ALT) != 0;
      event->shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
      event->ctrl = (SDL_GetModState() & KMOD_CTRL) != 0;
      break;
    case SDL_MOUSEWHEEL:
      if (input.wheel.windowID != window_id)
        break;
      event->kind = CANVAS_SDL_SCROLL;
      // Keep device inversion for wheel zoom; precise panning uses AppKit content direction.
      event->scroll_flipped = input.wheel.direction == SDL_MOUSEWHEEL_FLIPPED;
      CanvasSDLEvent pointer = {.x = input.wheel.mouseX, .y = input.wheel.mouseY};
      FramebufferPointer(display, &pointer);
#ifdef __APPLE__
      if (precise_scroll) {
        // OpenTTD's Cocoa default is half the precise delta times a scroll speed of five.
        // These are rendered pixels, independent of the display's backing scale.
        float pixels_per_point = 2.5f * 1024 / (display->debug_zoom ? 2 : 1);
        event->scroll.pixels_x = (i32)SDL_roundf(input.wheel.preciseX * pixels_per_point);
        event->scroll.pixels_y = (i32)SDL_roundf(input.wheel.preciseY * pixels_per_point);
        event->scroll.precise = true;
        event->x = input.wheel.x;
        event->y = input.wheel.y;
        event->ctrl = (SDL_GetModState() & KMOD_CTRL) != 0;
        break;
      }
#endif
      // Some event producers supply only the integer wheel fields. Keep precise touchpad input when present.
      float wheel_x = input.wheel.preciseX != 0.0f ? input.wheel.preciseX : (float)input.wheel.x;
      float wheel_y = input.wheel.preciseY != 0.0f ? input.wheel.preciseY : (float)input.wheel.y;
      event->x = (i32)(wheel_x * 1024.0f);
      event->y = (i32)(wheel_y * 1024.0f);
      event->ctrl = (SDL_GetModState() & KMOD_CTRL) != 0;
      break;
    case SDL_TEXTINPUT:
      if (input.text.windowID != window_id || display->suppress_console_text)
        break;
      event->kind = CANVAS_SDL_TEXT_INPUT;
      SDL_strlcpy(event->text, input.text.text, sizeof(event->text));
      break;
    case SDL_KEYDOWN:
    case SDL_KEYUP:
      if (input.key.windowID != window_id)
        break;
      if (display->game->debug_city && input.type == SDL_KEYDOWN && input.key.repeat &&
          (input.key.keysym.sym == SDLK_F6 || input.key.keysym.sym == SDLK_F7 ||
              input.key.keysym.sym == SDLK_F8 || input.key.keysym.sym == SDLK_F9) &&
          !(input.key.keysym.mod & (KMOD_SHIFT | KMOD_CTRL | KMOD_ALT | KMOD_GUI))) break;
      // Inspection magnification changes only SDL's final copy. Reuse the same
      // framebuffer/texture and keep all gameplay zoom and UI layout intact.
      if (display->direct_framebuffer && display->game->debug_city && input.key.keysym.sym == SDLK_F11 &&
          !(input.key.keysym.mod & (KMOD_SHIFT | KMOD_CTRL | KMOD_ALT | KMOD_GUI))) {
        if (input.type == SDL_KEYDOWN && !input.key.repeat) {
          CanvasSDLEvent pointer = {0};
          SDL_GetMouseState(&pointer.x, &pointer.y);
          FramebufferPointer(display, &pointer);
          display->debug_zoom_center = (CityPoint){display->pixel_x, display->pixel_y};
          display->debug_zoom = !display->debug_zoom;
          display->ui_valid = 0;
          event->kind = CANVAS_SDL_EXPOSE;
        }
        break;
      }
      event->kind = input.type == SDL_KEYDOWN ? CANVAS_SDL_KEY_DOWN : CANVAS_SDL_KEY_UP;
      event->alt = (input.key.keysym.mod & KMOD_ALT) != 0;
      event->shift = (input.key.keysym.mod & KMOD_SHIFT) != 0;
      event->ctrl = (u8)(((input.key.keysym.mod & KMOD_CTRL) != 0) |
          (((input.key.keysym.mod & KMOD_GUI) != 0) << 1));
      event->key = SDLHotkey(input.key.keysym.sym, input.key.keysym.mod);
      if (input.type == SDL_KEYDOWN) {
        display->suppress_console_text = input.key.keysym.scancode == SDL_SCANCODE_GRAVE &&
            !(input.key.keysym.mod & (KMOD_SHIFT | KMOD_CTRL | KMOD_ALT | KMOD_GUI));
        if (display->suppress_console_text) {
          event->key = CITY_KEY_CONSOLE;
          // Holding the toggle key must neither reopen the window nor type its character.
          if (input.key.repeat) event->kind = CANVAS_SDL_NONE;
        }
      }
      break;
  }
  UpdateCursor(display);
  return true;
}

void WaitForCityCanvasFrame(int milliseconds) {
  SDL_PumpEvents();
  if (milliseconds > 0) SDL_Delay(milliseconds);
}

bool Sc2kSDLEvent(CanvasSDL *display, CanvasSDLEvent *event, i32 wait_ms) {
  if (display->deferred_count) {
    CanvasDeferredEvent saved = display->deferred_events[0];
    *event = saved.event;
    display->ui_x = saved.ui_x; display->ui_y = saved.ui_y; display->ui_valid = saved.ui_valid;
    display->pixel_x = saved.pixel_x; display->pixel_y = saved.pixel_y;
    if (event->kind == CANVAS_SDL_POINTER_MOVE || event->kind == CANVAS_SDL_POINTER_DOWN ||
        event->kind == CANVAS_SDL_POINTER_UP) {
      int width, height;
      SDL_GetRendererOutputSize(display->renderer, &width, &height);
      display->cursor_inside = saved.pixel_x >= 0 && saved.pixel_y >= 0 &&
          saved.pixel_x < width && saved.pixel_y < height;
    } else if (event->kind == CANVAS_SDL_POINTER_LEAVE) display->cursor_inside = 0;
    --display->deferred_count;
    memmove(display->deferred_events, display->deferred_events + 1,
        (size_t)display->deferred_count * sizeof(*display->deferred_events));
    UpdateCursor(display);
    return true;
  }
  return PollSDLEvent(display, event, wait_ms);
}

int PollCanvasActivation(CanvasSDL *display, bool *active) {
  *active = false;
  for (i32 index = 0; index < 64; ++index) {
    CanvasSDLEvent event;
    if (!PollSDLEvent(display, &event, 0))
      return 0;
    if (event.kind == CANVAS_SDL_FOCUS_GAINED || event.kind == CANVAS_SDL_FOCUS_LOST) {
      *active = event.kind == CANVAS_SDL_FOCUS_GAINED;
      return 1;
    }
    if (event.kind == CANVAS_SDL_NONE)
      continue;
    if (display->deferred_count == display->deferred_capacity) {
      if (display->deferred_capacity >= 65536) {
        SDL_SetError("Deferred canvas event queue is full");
        return -1;
      }
      i32 capacity = display->deferred_capacity ? display->deferred_capacity * 2 : 64;
      CanvasDeferredEvent *events = realloc(display->deferred_events, (size_t)capacity * sizeof(*events));
      if (events == NULL) {
        SDL_SetError("Cannot retain deferred canvas input");
        return -1;
      }
      display->deferred_events = events;
      display->deferred_capacity = capacity;
    }
    display->deferred_events[display->deferred_count++] =
        (CanvasDeferredEvent){event, display->ui_x, display->ui_y, display->ui_valid,
            display->pixel_x, display->pixel_y};
  }
  return 0;
}

i32 Sc2kSDLOutputSize(CanvasSDL *display, i32 *width, i32 *height) {
  return SDL_GetRendererOutputSize(display->renderer, width, height);
}

i32 Sc2kSDLReadPixels(CanvasSDL *display, u32 *pixels, i32 stride) {
  if (CopyCanvasTexture(display) != 0)
    return -1;
  SDL_Rect output = {0, 0, 0, 0};
  if (SDL_GetRendererOutputSize(display->renderer, &output.w, &output.h) != 0)
    return -1;
  SDL_Rect viewport;
  float scale_x, scale_y;
  SDL_RenderGetViewport(display->renderer, &viewport);
  SDL_RenderGetScale(display->renderer, &scale_x, &scale_y);
  SDL_RenderSetScale(display->renderer, 1.0f, 1.0f);
  SDL_RenderSetViewport(display->renderer, NULL);
  i32 status = SDL_RenderReadPixels(display->renderer, &output, SDL_PIXELFORMAT_ARGB8888,
      pixels, stride * (i32)sizeof(u32));
  SDL_RenderSetScale(display->renderer, scale_x, scale_y);
  SDL_RenderSetViewport(display->renderer, &viewport);
  return status;
}

void Sc2kSDLResize(CanvasSDL *display, i32 width, i32 height) {
  SDL_SetWindowSize(display->window, width, height);
}

u32 Sc2kSDLWindowId(CanvasSDL *display) {
  return SDL_GetWindowID(display->window);
}

bool Sc2kSDLIsMinimized(CanvasSDL *display) {
  return (SDL_GetWindowFlags(display->window) & SDL_WINDOW_MINIMIZED) != 0;
}

void SetCityMapNavigationPresentation(Game *game, bool enabled, bool interactive) {
  Drawing *drawing = game->drawing;
  drawing->map_navigation_enabled = enabled && game->ui != NULL;
  drawing->map_navigation_interactive = interactive;
  if (!drawing->map_navigation_enabled) {
    if (drawing->map_capture_active && SDL_WasInit(SDL_INIT_VIDEO))
      SDL_CaptureMouse(SDL_FALSE);
    drawing->map_capture_active = 0;
  }
}

bool CaptureCityMapPointer(Game *game) {
  Drawing *drawing = game->drawing;
  // SDL rejects capture after focus loss. The original activation caller still
  // proceeds with SetCapture's previous-window result in that situation.
  // Keep the logical capture/journal lifetime, without requesting OS capture
  // until a window has keyboard focus. Focused backend failures remain errors.
  if (drawing->map_navigation_interactive && SDL_GetKeyboardFocus() != NULL && SDL_CaptureMouse(SDL_TRUE) != 0 &&
      SDL_GetKeyboardFocus() != NULL) {
    return false;
  }
  drawing->map_capture_active = 1;
  if (game->cursor_observed != NULL)
    game->cursor_observed(game, (CityCursorInputRecord){CITY_CURSOR_INPUT_CAPTURE, 0, {0, 0, 0, 0}});
  return true;
}

int ReleaseCityMapWindowPointer(Game *game) {
  Drawing *drawing = game->drawing;
  int result = 1;
  if (drawing->map_navigation_interactive && SDL_CaptureMouse(SDL_FALSE) != 0) {
    return 0;
  }
  drawing->map_capture_active = 0;
  if (game->cursor_observed != NULL)
    game->cursor_observed(game, (CityCursorInputRecord){CITY_CURSOR_INPUT_RELEASE, 0, {result, 0, 0, 0}});
  return result;
}
