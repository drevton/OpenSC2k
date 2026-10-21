#ifndef SC2K_AUDIO_STATE_H
#define SC2K_AUDIO_STATE_H

#include "city/types.h"

typedef struct { char text[260]; } CityMusicDirectory;

typedef struct {
  i32 ticks[31];
} CitySoundTimingTable;

// The stock repeat gate indexes durations by raw sound ID, reading this later span.
typedef struct {
  i32 threshold[30];
} CitySoundReplayThresholds;

enum { CITY_EFFECT_VOICES = 8 };
typedef struct {
  const u8 *samples; // Borrows the immutable Assets-owned mono recording.
  u32 frames, position;
  i32 repetitions; // Zero is finished; -1 repeats until stopped.
  i16 sound_id;
  bool looping;
} CityEffectVoice;

// Audio remains outside saved City state. Music activity gates economy RNG;
// sound state controls tool requests, replay and interruption. Both have
// independent native checkpoint providers. Deadlines use frame time.
typedef struct CityAudioState {
  CityAssets *assets; // Borrows immutable recordings and MIDI loaded before playback.
  u32 helicopter_sound_deadline;
  u32 sound_delay;
  i32 retry_delay;
  i32 queued_sound;
  i32 music_sequence_index;
  u32 music_playback_blocked;
  i32 sound_playing;
  i32 last_sound_id;
  i32 cached_sound_playing;
  i32 cached_sound_id;
  i32 preloaded_sound_id;
  i32 transient_sound_id;
  u32 music_active;
  u16 music_device;
  const char *music_filename; // Borrows a catalog name or comparison-owned transport text.
  CityMusicDirectory music_directory;
  CitySoundTimingTable timing;
  CitySoundReplayThresholds replay_thresholds;
  // Source of the last accepted playback; INVALID_TILE denotes a city-wide/UI sound.
  TileIndex sound_tile;
  u32 enabled;
  u32 music_enabled;
  // Native device handles and callback state; independent of recorded City audio fields.
  struct WindowsMidi *windows_midi;
  struct OpaqueMusicPlayer *music_player;
  struct OpaqueMusicSequence *music_sequence;
  struct OpaqueAUGraph *music_graph;
  void *music_mixer, *music_synth;
  double music_end_beat, resume_music_seconds;
  i16 resume_music_track;
  u16 interrupt_device, next_music_device;
  u8 music_notify, midi_available, live_open;
  u32 effects_device;
  u8 effect_volume;
  bool music_background, music_focused, overlapping_effects;
  bool fire_sound_paused, resume_fire_sound;
  u8 music_volume, master_volume;
  bool custom_soundfont;
  char *soundfont_path;
  CityEffectVoice effect, click;
  _Atomic int effect_finished;
  CityEffectVoice effect_voices[CITY_EFFECT_VOICES];
  float mix_gain;
} CityAudioState;

void InitializeCityAudioState(CityAudioState *audio);

#endif
