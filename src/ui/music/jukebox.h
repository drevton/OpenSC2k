#ifndef SC2K_UI_JUKEBOX_H
#define SC2K_UI_JUKEBOX_H

#include "audio/jukebox.h"

typedef struct Game Game;
typedef struct Window Window;
typedef struct CityInterfaceState CityInterfaceState;

enum {
  JUKEBOX_PLAYER_WIDTH = 250, JUKEBOX_PLAYER_HEIGHT = 72,
  JUKEBOX_PROGRAMME_WIDTH = 432, JUKEBOX_PROGRAMME_HEIGHT = 250,
  JUKEBOX_TITLE_HEIGHT = 14, JUKEBOX_SLIDER_WIDTH = 70,
  JUKEBOX_MUSIC_SLIDER_X = 93, JUKEBOX_EFFECTS_SLIDER_X = 171
};
enum {
  JUKEBOX_NONE, JUKEBOX_PREVIOUS, JUKEBOX_NEXT, JUKEBOX_STOP, JUKEBOX_PLAY,
  JUKEBOX_SHUFFLE, JUKEBOX_PROGRAMME, JUKEBOX_CLEAR, JUKEBOX_COLLAPSE,
  JUKEBOX_PIN, JUKEBOX_CLOSE,
  JUKEBOX_SELECT = 100, JUKEBOX_ADD = 200, JUKEBOX_REMOVE = 300,
  JUKEBOX_MUSIC_VOLUME = 400, JUKEBOX_EFFECTS_VOLUME = 600,
  JUKEBOX_PLAY_TRACK = 900
};

// Identifies the volume slider currently holding pointer capture.
typedef enum CityCanvasVolumeSlider {
  CITY_CANVAS_VOLUME_SLIDER_NONE = 0,
  CITY_CANVAS_VOLUME_SLIDER_MUSIC = 1, CITY_CANVAS_VOLUME_SLIDER_EFFECTS = 2,
} CityCanvasVolumeSlider;

typedef struct CityMusicPress {
  u32 window_id, release_time;
  i32 x, y, start_x, start_y;
  u8 active, released;
} CityMusicPress;

typedef struct CityMusicTitle {
  char text[100];
  u32 started_at;
  u16 offset;
  bool active;
} CityMusicTitle;

// UI window actions and feedback never advance City or use its RNG.
void OpenCityMusicWindow(Game *game, int programme);
// Close and collapse require their live window; playlist and volume actions permit a null window.
void ApplyCityMusicAction(Game *game, int action, Window *window);
void DirtyCityMusicWindows(Game *game);
// Borrow the displayed player for this UI; a null pointer hides playback information.
void Sc2kCanvasSetJukeboxState(CityInterfaceState *ui, const Jukebox *player);
i32 Sc2kCanvasJukeboxActionAt(const CityInterfaceState *ui, const Window *window, i32 x, i32 y);
// Pointer feedback stays relative to the pressed window; null releases capture.
i32 Sc2kCanvasJukeboxPress(CityInterfaceState *ui, const Window *window, i32 x, i32 y);
// Released transport buttons remain pressed for 150ms. Time is wrapping host milliseconds.
void Sc2kCanvasJukeboxRelease(CityInterfaceState *ui, u32 milliseconds);
bool Sc2kCanvasJukeboxTick(CityInterfaceState *ui, u32 milliseconds);
bool UpdateCityJukeboxTitle(CityInterfaceState *ui, u32 milliseconds);

// Returns borrowed hover help for these controls, or null when no help applies.
const char *CityMusicTooltipAt(Game *game, const Window *window, int x, int y);

#endif
