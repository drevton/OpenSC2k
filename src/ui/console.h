#ifndef SC2K_UI_CONSOLE_H
#define SC2K_UI_CONSOLE_H

#include "ui/input.h"

typedef struct CanvasSDLEvent CanvasSDLEvent;

// UI owns console history and editing; Game owns the Lua state. Shutdown closes scripting before releasing UI text.
void OpenCityDeveloperConsole(Game *game);
bool HandleCityDeveloperShortcut(Game *game, const CanvasSDLEvent *event);
CityHotkey RemapCityKey(const CityInterfaceState *ui, CityHotkey key);
void CloseCityScripting(Game *game);
void ReleaseCityConsole(CityInterfaceState *ui);

#endif
