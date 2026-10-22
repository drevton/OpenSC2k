#include "game/palette.h"

#include "assets/state.h"
#include "city/state.h"
#include "drawing/palette.h"
#include "drawing/state.h"
#include "game/application.h"
#include "game/state.h"
#include "game/timing.h"
#include "ui/state.h"
#include "ui/windows/windows.h"

// Apply the selected animation block using Game's current pause policy.
int AnimateCityPalette(Game *game, int slow_palette) {
  Drawing *drawing = game->drawing;
  CityPaletteState *state = &drawing->palette;
  int idle_state = game->idle_state;
  if (game->city->settings.speed == CITY_SPEED_PAUSED && state->direct_animation == 0 &&
      idle_state == CITY_IDLE_PLAY)
    return -1;
  if (drawing->display_palette != NULL)
    CaptureCityPalette(state, drawing->display_palette);
  ReorderCityPaletteEntries(state, slow_palette == 0);
  if (drawing->display_palette != NULL)
    ApplyAnimatedCityPalette(state, drawing->display_palette);
  return 0;
}

// Bracket one background-palette request without retaining presentation work.
bool RefreshActiveCityPalette(Game *game) {
  CityPaletteState *palette = &game->drawing->palette;
  if (palette->background_enabled == 0)
    return false;
  palette->refresh_active = 1;
  AnimateCityPalette(game, 0);
  palette->refresh_active = 0;
  return true;
}

// The caller supplies presentation time; ordinary frames use the unpaused clock. No City ticks are advanced.
bool AnimateCityCanvasPalette(Game *game, double now) {
  enum { kWaterPaletteTickMilliseconds = 27 };
  Drawing *drawing = game->drawing;
  if (!drawing->framebuffer.pixels) return false;
  u32 intervals = PollGameInterval(now, kWaterPaletteTickMilliseconds / 1000.0, &drawing->next_water_palette_tick);
  DrawingPaletteChanges changes = AdvanceDrawingAnimationPalette(drawing,
      game->resources->landscape_water_colors, intervals);
  u32 palette_changed = changes.global_buckets;
  // Water cycles only update opaque indexed pixels. Blended map edges remain
  // fixed RGB; interface bitmaps retain their ordinary global-palette dependencies.
  if (palette_changed && drawing->animation_composite_rows) {
    for (int y = 0; y < drawing->framebuffer.height;) {
      if (!(drawing->animation_composite_rows[y] & palette_changed)) { ++y; continue; }
      int first = y, left = drawing->framebuffer.width, right = 0;
      do {
        u32 bounds = drawing->animation_composite_bounds[y];
        left = IntMin(left, bounds >> 16);
        right = IntMax(right, bounds & 65535);
        drawing->animation_composite_rows[y++] = 0;
      } while (y < drawing->framebuffer.height && (drawing->animation_composite_rows[y] & palette_changed));
      CityRect rect = {left, first, right, y};
      MarkScreenDirty(&drawing->screen_damage, rect);
      Sc2kDirtyMarkRect(&game->ui->desktop.window_damage, rect);
    }
  }
  return ApplyDrawingAnimationPalette(drawing, changes);
}
