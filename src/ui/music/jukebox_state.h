#ifndef SC2K_JUKEBOX_H
#define SC2K_JUKEBOX_H

#include "city/types.h"

// Programme indices are persisted in the jukebox preferences.
typedef enum JukeboxProgramme {
  JUKEBOX_PROGRAMME_ALL = 0,
  JUKEBOX_PROGRAMME_OLD_STYLE = 1,
  JUKEBOX_PROGRAMME_NEW_STYLE = 2,
  JUKEBOX_PROGRAMME_EZY_STREET = 3,
  JUKEBOX_PROGRAMME_CUSTOM_1 = 4,
  JUKEBOX_PROGRAMME_CUSTOM_2 = 5,
  JUKEBOX_PROGRAMMES = 6,
} JukeboxProgramme;
enum {
  JUKEBOX_TRACKS = 31, JUKEBOX_CAPACITY = 32,
  JUKEBOX_CUSTOM_PROGRAMMES = JUKEBOX_PROGRAMMES - JUKEBOX_PROGRAMME_CUSTOM_1,
};
enum {
  JUKEBOX_PLAYER_WIDTH = 250, JUKEBOX_PLAYER_HEIGHT = 72,
  JUKEBOX_PROGRAMME_WIDTH = 432, JUKEBOX_PROGRAMME_HEIGHT = 250,
  JUKEBOX_TITLE_HEIGHT = 14, JUKEBOX_SLIDER_WIDTH = 70,
  JUKEBOX_MUSIC_SLIDER_X = 93, JUKEBOX_EFFECTS_SLIDER_X = 171
};
typedef struct { const char *title; const char *file; } JukeboxTrack;
extern const JukeboxTrack jukebox_tracks[JUKEBOX_TRACKS];
extern const char *const jukebox_programmes[JUKEBOX_PROGRAMMES];

// Host-only state: music controls never consume the city's RNG or mapped memory.
typedef struct Jukebox {
  i32 programme, shuffle, playing, volume, effects_volume;
  i32 position, count;
  i32 interrupt_track; // Temporary playback; the playlist and its cursor remain unchanged.
  u8 order[JUKEBOX_CAPACITY];
  u8 custom[JUKEBOX_CUSTOM_PROGRAMMES][JUKEBOX_CAPACITY];
  i32 custom_count[JUKEBOX_CUSTOM_PROGRAMMES];
  u32 random;
  i32 collapsed, pinned;
  char error[96];
} Jukebox;

enum {
  JUKEBOX_NONE, JUKEBOX_PREVIOUS, JUKEBOX_NEXT, JUKEBOX_STOP, JUKEBOX_PLAY,
  JUKEBOX_SHUFFLE, JUKEBOX_PROGRAMME, JUKEBOX_CLEAR, JUKEBOX_COLLAPSE,
  JUKEBOX_PIN, JUKEBOX_CLOSE,
  JUKEBOX_SELECT = 100, JUKEBOX_ADD = 200, JUKEBOX_REMOVE = 300,
  JUKEBOX_MUSIC_VOLUME = 400, JUKEBOX_EFFECTS_VOLUME = 600,
  JUKEBOX_PLAY_TRACK = 900
};

struct Game;
void DirtyCityMusicWindows(struct Game *game);
void JukeboxInit(Jukebox *player, u32 seed);
void JukeboxSelect(Jukebox *player, i32 programme);
void SelectJukeboxTrack(Jukebox *player, int track);
void JukeboxShuffle(Jukebox *player);
void JukeboxStep(Jukebox *player, i32 direction);
void JukeboxAdd(Jukebox *player, i32 track);
void JukeboxRemove(Jukebox *player, i32 row);
void JukeboxClear(Jukebox *player);
i32 JukeboxCurrent(const Jukebox *player);

#endif
