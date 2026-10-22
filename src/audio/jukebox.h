#ifndef SC2K_AUDIO_JUKEBOX_H
#define SC2K_AUDIO_JUKEBOX_H

#include "base/types.h"

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
typedef struct { const char *title; const char *file; } JukeboxTrack;
extern const JukeboxTrack kJukeboxTracks[JUKEBOX_TRACKS];
extern const char *const kJukeboxProgrammes[JUKEBOX_PROGRAMMES];

// Audio owns playback preferences, playlist order, its private RNG and the last backend error.
typedef struct Jukebox {
  i32 programme, shuffle, playing, volume, effects_volume;
  i32 position, count;
  i32 interrupt_track; // Temporary playback; the playlist and its cursor remain unchanged.
  u8 order[JUKEBOX_CAPACITY];
  u8 custom[JUKEBOX_CUSTOM_PROGRAMMES][JUKEBOX_CAPACITY];
  i32 custom_count[JUKEBOX_CUSTOM_PROGRAMMES];
  u32 random;
  char error[96];
} Jukebox;

// Initialize an independent player; zero seeds select the same nonzero fallback.
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
