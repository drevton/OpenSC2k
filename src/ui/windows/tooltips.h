#ifndef SC2K_UI_TOOLTIPS_H
#define SC2K_UI_TOOLTIPS_H

#include "city/types.h"

typedef struct Game Game;
typedef struct Canvas Canvas;
typedef struct CityCanvasDesktop CityCanvasDesktop;

// UI retains bounded tooltip text and host timing; no borrowed provider text survives a resolution call.
typedef struct CityTooltip {
  char text[256], lines[8][96];
  CityRect bounds;
  double show_at;
  u32 window_id;
  int pointer_x, pointer_y, line_count;
  bool pointer_valid, visible, shown_for_target;
  u8 buttons;
} CityTooltip;

// Pointer positions are framebuffer pixels; now is host real time in seconds.
void MoveCityTooltipPointer(Game *game, int x, int y, double now);
// Hide also clears the target and redisplay state, preserving the sampled pointer position.
void HideCityTooltip(Game *game);
// Resolve providers and queue damage; true means visible tooltip pixels changed.
bool UpdateCityTooltip(Game *game, double now);
// Paint the retained tooltip after other UI; bounds are logical desktop coordinates.
void DrawCityTooltip(const CityCanvasDesktop *desktop, Canvas *canvas);

#endif
