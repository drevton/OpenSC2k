#ifndef SC2K_MACOS_H
#define SC2K_MACOS_H

#include <stdint.h>

typedef struct SDL_Window SDL_Window;

void SetMacCityAppIcon(void);
void EnableMacCityScrollMomentum(void);
void *InstallMacCityGestures(SDL_Window *window, uint32_t event_type);
void RemoveMacCityGestures(void *monitor);

#endif
