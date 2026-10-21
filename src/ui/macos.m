#include "ui/macos.h"

#include <math.h>

#include <SDL.h>
#include <SDL_syswm.h>

#import <Cocoa/Cocoa.h>

void EnableMacCityScrollMomentum(void) {
  // SDL2 registers NO during video initialization. Override that process-local
  // default before creating a window, so AppKit supplies the native momentum tail.
  [[NSUserDefaults standardUserDefaults] registerDefaults:@{@"AppleMomentumScrollSupported": @YES}];
}

void SetMacCityAppIcon(void) {
  NSString *path = [[NSBundle mainBundle] pathForResource:@"OpenSC2K" ofType:@"icns"];
  if (!path) return;
  NSImage *icon = [[NSImage alloc] initWithContentsOfFile:path];
  if (icon) [NSApp setApplicationIconImage:icon];
  [icon release];
}

// Keep AppKit's point-resolution scrolling and magnification in SDL's queue,
// in order with pointer and window events. Ordinary mouse wheels stay with SDL.
void *InstallMacCityGestures(SDL_Window *window, uint32_t event_type) {
  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  if (!SDL_GetWindowWMInfo(window, &info)) return NULL;
  NSWindow *native_window = info.info.cocoa.window;
  uint32_t window_id = SDL_GetWindowID(window);
  id monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskMagnify | NSEventMaskScrollWheel
      handler:^NSEvent *(NSEvent *input) {
    if (input.window != native_window) return input;
    SDL_Event event = {0};
    if (input.type == NSEventTypeScrollWheel) {
      if (!input.hasPreciseScrollingDeltas) return input;
      // Preserve wheel units for lists/zoom; panning consumes the raw AppKit point deltas.
      event.type = event_type + 3;
      event.wheel.windowID = window_id;
      event.wheel.x = (int)lround(-input.deltaX * 1024);
      event.wheel.y = (int)lround(input.deltaY * 1024);
      event.wheel.preciseX = input.scrollingDeltaX;
      event.wheel.preciseY = input.scrollingDeltaY;
      event.wheel.direction = input.isDirectionInvertedFromDevice ? SDL_MOUSEWHEEL_FLIPPED : SDL_MOUSEWHEEL_NORMAL;
      NSPoint point = [input locationInWindow];
      event.wheel.mouseX = (int)point.x;
      event.wheel.mouseY = (int)(native_window.contentView.bounds.size.height - point.y);
      return SDL_PushEvent(&event) > 0 ? nil : input;
    }
    event.user.windowID = window_id;
    if (input.phase & NSEventPhaseBegan) {
      event.type = event_type;
      SDL_PushEvent(&event);
    }
    event.type = event_type + 1;
    event.user.code = (int)lround(input.magnification * 65536.0);
    SDL_PushEvent(&event);
    if (input.phase & (NSEventPhaseEnded | NSEventPhaseCancelled)) {
      event.type = event_type + 2;
      event.user.code = 0;
      SDL_PushEvent(&event);
    }
    return nil;
  }];
  return [monitor retain];
}

void RemoveMacCityGestures(void *monitor) {
  if (!monitor) return;
  [NSEvent removeMonitor:(id)monitor];
  [(id)monitor release];
}
