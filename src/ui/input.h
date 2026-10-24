#ifndef SC2K_UI_INPUT_H
#define SC2K_UI_INPUT_H

#include <stddef.h>

#include "city/types.h"

typedef struct CanvasSDLEvent CanvasSDLEvent;

// Original mouse-message low-byte flags, separate from recorded sample flags.
enum {
  CITY_MOUSE_LEFT_BUTTON = 0x01, CITY_MOUSE_RIGHT_BUTTON = 0x02,
  CITY_MOUSE_SHIFT = 0x04, CITY_MOUSE_CONTROL = 0x08,
  CITY_MOUSE_MIDDLE_BUTTON = 0x10,
};

typedef struct CityInterfaceState CityInterfaceState;
typedef struct Game Game;

// Printable keys use uppercase ASCII; named keys and modifiers have stable integer codes.
typedef u32 CityHotkey;
enum {
  CITY_KEY_NONE = 0, CITY_KEY_BACKSPACE = 8, CITY_KEY_TAB = 9, CITY_KEY_RETURN = 13,
  CITY_KEY_ESCAPE = 27, CITY_KEY_SPACE = 32, CITY_KEY_DELETE = 127,
  CITY_KEY_LEFT = 256, CITY_KEY_RIGHT, CITY_KEY_UP, CITY_KEY_DOWN,
  CITY_KEY_HOME, CITY_KEY_END, CITY_KEY_PAGE_UP, CITY_KEY_PAGE_DOWN, CITY_KEY_INSERT,
  CITY_KEY_F1, CITY_KEY_F2, CITY_KEY_F3, CITY_KEY_F4, CITY_KEY_F5, CITY_KEY_F6,
  CITY_KEY_F7, CITY_KEY_F8, CITY_KEY_F9, CITY_KEY_F10, CITY_KEY_F11, CITY_KEY_F12,
  CITY_KEY_KEYPAD_PLUS, CITY_KEY_KEYPAD_MINUS, CITY_KEY_LEFT_ALT, CITY_KEY_RIGHT_ALT,
  CITY_KEY_CONSOLE, // Physical key immediately left of 1, independent of its printed character.
  CITY_HOTKEY_KEY_MASK = 0xFFFF, CITY_HOTKEY_SHIFT = 1 << 16,
  CITY_HOTKEY_CTRL = 1 << 17, CITY_HOTKEY_ALT = 1 << 18,
};

// Strings exist only at display, script and preference boundaries. No allocation.
char *CityHotkeyToString(CityHotkey key, char *text, size_t capacity);
CityHotkey CityHotkeyFromString(const char *text);

enum { CITY_KEY_BINDING_CAPACITY = 64 };
typedef struct CityKeyBinding { CityHotkey key, action; } CityKeyBinding;

// Parse two whitespace-separated hotkeys, rejecting reserved source keys. Failure leaves binding unchanged.
bool ParseCityKeyBinding(const char *text, CityKeyBinding *binding);
// Parse and replace the first binding for its key, or append one. Invalid text/full capacity leaves UI unchanged.
// This updates UI only; the caller decides when to store configuration.
bool BindCityKey(CityInterfaceState *ui, const char *text);
// Remove the first matching binding; an unbound key is unchanged.
void UnbindCityKey(CityInterfaceState *ui, CityHotkey key);
// Apply the first matching override; unbound keys retain their original action.
CityHotkey RemapCityKey(const CityInterfaceState *ui, CityHotkey key);

enum {
  CITY_KEY_MODIFIER_ALT = 1,
  CITY_KEY_MODIFIER_SHIFT = 2,
  CITY_KEY_MODIFIER_CONTROL = 4,
};

bool IsAltKeyPressed(const CityInterfaceState *ui);
bool IsShiftKeyPressed(const CityInterfaceState *ui);
bool IsCtrlKeyPressed(const CityInterfaceState *ui);
int ReadCityBorderConnectionAnswer(const CityInterfaceState *ui);
int ReadCitySpanChoice(const CityInterfaceState *ui);

// Values supplied by the unified command path and the SDL event loop.
void SetCityAltKeyPressed(CityInterfaceState *ui, bool pressed);
void SetCityShiftKeyPressed(CityInterfaceState *ui, bool pressed);
void SetCityCtrlKeyPressed(CityInterfaceState *ui, bool pressed);
void SetCityBorderConnectionAnswer(CityInterfaceState *ui, bool accept);
void SetCitySpanChoice(CityInterfaceState *ui, i16 choice);

// Convert SDL logical coordinates to framebuffer pixels, then to the captured window's logical coordinates.
void CityInterfacePixels(Game *game, const CanvasSDLEvent *event, i32 *x, i32 *y);
CanvasSDLEvent TransformCityInterfaceEvent(Game *game, const CanvasSDLEvent *event);
void BeginCityCanvasButtonFeedback(Game *game, const CanvasSDLEvent *event);
void HandleCityCanvasPointerDown(City *city, const CanvasSDLEvent *map_event);
void HandleCityCanvasPointerMove(City *city, const CanvasSDLEvent *map_event);
void HandleCityCanvasPointerUp(City *city, const CanvasSDLEvent *event);
void ScrollCityCanvas(City *city, const CanvasSDLEvent *event);
void DispatchCityCanvasEvent(City *city, CanvasSDLEvent *event);

#endif
