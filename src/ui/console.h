#ifndef SC2K_UI_CONSOLE_H
#define SC2K_UI_CONSOLE_H

#include "base/types.h"

typedef struct Game Game;
typedef struct CityInterfaceState CityInterfaceState;
typedef struct CanvasSDLEvent CanvasSDLEvent;

// UI owns console history, editing and output; Game owns script execution.
void OpenCityDeveloperConsole(Game *game);
bool HandleCityDeveloperShortcut(Game *game, const CanvasSDLEvent *event);
// Append terminated, non-NULL text and a newline to the bounded output buffer, discarding old lines as needed.
// Also logs to stderr. Does not open a window, change focus or mutate City.
void AppendCityConsoleOutput(Game *game, const char *text);
void ReleaseCityConsole(CityInterfaceState *ui);

#endif
