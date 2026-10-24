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

// Forward Drawing's physical interface damage synchronously; no window procedure runs here.
static void MarkPaletteWindowDamage(CityRect rect, void *context) {
  Game *game = context;
  Sc2kDirtyMarkRect(&game->ui->desktop.window_damage, rect);
}

// The caller supplies presentation time; ordinary frames use the unpaused clock. No City ticks are advanced.
bool AnimateCityCanvasPalette(Game *game, double now) {
  enum { kWaterPaletteTickMilliseconds = 27 };
  Drawing *drawing = game->drawing;
  if (!drawing->framebuffer.pixels) return false;
  u32 intervals = PollGameInterval(now, kWaterPaletteTickMilliseconds / 1000.0, &drawing->next_water_palette_tick);
  DrawingPaletteChanges changes = AdvanceDrawingAnimationPalette(drawing,
      game->resources->landscape_water_colors, intervals);
  ConsumeDrawingPaletteDamage(drawing, changes, MarkPaletteWindowDamage, game);
  return ApplyDrawingAnimationPalette(drawing, changes);
}
