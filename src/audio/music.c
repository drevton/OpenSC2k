#include "audio/types.h"
#include "game/settings.h"
#include "game/observation.h"
#include "audio/backend.h"
#include <string.h>

#include "audio/audio.h"
#include "city/saveload/services.h"
#include "audio/state.h"
#include "city/engine/simulation.h"
#include "game/errors.h"
#include "ui/state.h"

// Event cues interrupt the jukebox and resume its saved cursor on completion.

void StopCityMusicSequence(Game *game) {
  if (game->observation && game->observation->stop_music) {
    game->observation->stop_music(game);
    return;
  }
  CityAudioState *audio = game->audio;
  if (audio->interrupt_device != CITY_MUSIC_NO_TOKEN)
    CompleteJukeboxTrack(game, CITY_MUSIC_FINISHED, audio->interrupt_device);
}

int CompleteCityMusic(Game *game, int notification, int token) {
  if (game->observation && game->observation->complete_music)
    return game->observation->complete_music(game, notification, token);
  CompleteJukeboxTrack(game, notification, token);
  return 0;
}

u32 PlayCityMusicTrack(Game *game, int track) {
  CityAudioState *audio = game->audio;
  if (!audio->music_enabled) return 0;
  if (audio->music_playback_blocked) return audio->music_playback_blocked;
  u32 result;
  if (game->observation && game->observation->play_music_track)
    result = game->observation->play_music_track(game, track);
  else {
    result = InterruptJukebox(game, track);
    if (!result) audio->music_filename = jukebox_tracks[track].file;
  }
  if (game->music_track_played) game->music_track_played(game, track, result);
  return result;
}

// Play an event cue when music is enabled and the main window is available.
i32 MaybePlayCityEventMusic(Game *game, int event_id) {
  CityAudioState *audio = game->audio;
  const CityInterfaceState *ui = game->ui;
  if (audio->music_enabled == 0)
    return 0;
  if (game->ranked_events_suppressed != 0)
    return game->ranked_events_suppressed;
  int iconic = ui->main_window_iconic;
  if (iconic != 0)
    return iconic;
  if (event_id < CITY_MUSIC_FIRST || event_id > CITY_MUSIC_LAST)
    return 0;

  // Event cue IDs are independent of the soundtrack's filenames.
  int track = event_id - CITY_MUSIC_FIRST;
  return (i32)PlayCityMusicTrack(game, track);
}

bool PlayCityEventMusicById(Game *game, int event_id) {
  MaybePlayCityEventMusic(game, event_id);
  return true;
}

// Advance the five-entry event-music sequence and dispatch its track.
bool PlayNextCityEventMusicSequence(Game *game) {
  CityAudioState *audio = game->audio;
  static const u16 tracks[CITY_MUSIC_SEQUENCE_COUNT] = {
    CITY_MUSIC_TRACK_01, CITY_MUSIC_TRACK_04, CITY_MUSIC_TRACK_08, CITY_MUSIC_TRACK_12, CITY_MUSIC_TRACK_18
  };
  int sequence = audio->music_sequence_index % CITY_MUSIC_SEQUENCE_COUNT;
  audio->music_sequence_index = sequence + 1;
  if (sequence < 0)
    return true;
  return PlayCityEventMusicById(game, tracks[sequence]);
}

// State

// WAV durations are converted once to the original 200 ms cadence units.
static const u16 kCitySoundDurationMilliseconds[CITY_SOUND_COUNT + 1] = {
  492, 153, 1485, 129, 1156, 221, 1064, 1657,
  1510, 1511, 2264, 886, 2766, 2203, 473, 625,
  782, 1223, 2444, 1308, 2468, 971, 2194, 1414,
  2338, 1165, 1937, 2359, 1510, 1613, 250,
};

// Stock indexes the repeat gate with the raw sound ID. Its bounded read lands
// in immutable runtime-class text, producing these thresholds rather than
// the duration table. Preserve those playback decisions without image reads.
static const CitySoundReplayThresholds kCitySoundRepeatCompatibilityThresholds = {{
  1852786243, 116, 1970422339, 26739, 1852133443,
  0, 1768179523, 1701470799, 29795, 0,
  1767985219, 1128559726, 0, 1852397379, 1148677988,
  67, 1768702787, 1148481125, 67, 4408387,
  1702057283, 1668826482, 1769238629, 28271, 0,
  1936020035, 1668445551, 1668826469, 1769238629, 28271,
}};

// Startup only: initialize a fresh Audio owner before opening devices/buffers.
void InitializeCityAudioState(CityAudioState *audio) {
  *audio = (CityAudioState){
    .queued_sound = -1,
    .last_sound_id = -1,
    .cached_sound_id = -1,
    .preloaded_sound_id = -1,
    .transient_sound_id = -1,
    // Native device open enables overlapping effects; headless comparison
    // owners explicitly retain the original single-effect controller.
    .overlapping_effects = false,
    .music_device = CITY_MUSIC_NO_TOKEN,
    .interrupt_device = CITY_MUSIC_NO_TOKEN,
    .music_filename = "",
    .music_directory = {"./sounds"},
    .replay_thresholds = kCitySoundRepeatCompatibilityThresholds,
  };
  NormalizeFilePath(audio->music_directory.text);
  for (int index = 0; index <= CITY_SOUND_COUNT; ++index)
    audio->timing.ticks[index] = kCitySoundDurationMilliseconds[index] / CITY_CADENCE_INTERVAL_MS + 1;
}

bool ToggleCityMusicPreference(Game *game) {
  CityAudioState *audio = game->audio;
  audio->music_enabled = audio->music_enabled == 0;
  if (audio->live_open) {
    SetJukeboxPlaying(game, audio->music_enabled != 0);
  } else if (audio->music_enabled != 0)
    PlayNextCityEventMusicSequence(game);
  else
    StopCityMusicSequence(game);
  StoreGameSettings(game);
  return true;
}
