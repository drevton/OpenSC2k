#include "game/audio.h"

#include "audio/backend.h"
#include "audio/state.h"
#include "city/engine/clock.h"
#include "city/engine/random.h"
#include "city/engine/simulation.h"
#include "city/presentation.h"
#include "city/state.h"
#include "game/errors.h"
#include "game/observation.h"
#include "game/settings.h"
#include "game/state.h"
#include "game/timing.h"
#include "ui/map_visibility.h"
#include "ui/music/jukebox.h"
#include "ui/state.h"

// Game reports playback results and updates the UI around Audio-owned devices and cursors.
static bool PlayCitySpatialEffect(Game *game, int sound_id, CityEffectPlayback playback, u8 pan) {
  bool result = NativeAudioPlayEffect(game->audio, playback == CITY_EFFECT_STOP ? CITY_SOUND_NONE : sound_id,
      playback == CITY_EFFECT_LOOP || playback == CITY_EFFECT_REPLAY_LOOP, pan);
  if (game->sound_played) game->sound_played(game, sound_id, playback, result);
  return result;
}

bool PlayCityEffect(Game *game, int sound_id, CityEffectPlayback playback) {
  return PlayCitySpatialEffect(game, sound_id, playback, 128);
}

void StopCityPlatformSound(Game *game) { PlayCityEffect(game, CITY_SOUND_NONE, CITY_EFFECT_STOP); }

void SetJukeboxPlaying(Game *game, bool playing) {
  game->audio->jukebox.playing = playing;
  NativeAudioPlay(game->audio);
  DirtyCityMusicWindows(game);
}

u32 InterruptJukebox(Game *game, int track) {
  CityMusicInterruptResult result = NativeAudioInterrupt(game->audio, track);
  if (result != CITY_MUSIC_INTERRUPT_IGNORED) DirtyCityMusicWindows(game);
  return result == CITY_MUSIC_INTERRUPT_STARTED ? 0 : 1;
}

void CompleteJukeboxTrack(Game *game, CityMusicCompletion notification, u16 device) {
  if (NativeAudioCompleteMusic(game->audio, notification, device)) DirtyCityMusicWindows(game);
}

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
  CompleteJukeboxTrack(game, (CityMusicCompletion)notification, token);
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
    if (!result) audio->music_filename = kJukeboxTracks[track].file;
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
  static const u16 kSequenceTracks[CITY_MUSIC_SEQUENCE_COUNT] = {
    CITY_MUSIC_TRACK_01, CITY_MUSIC_TRACK_04, CITY_MUSIC_TRACK_08, CITY_MUSIC_TRACK_12, CITY_MUSIC_TRACK_18
  };
  int sequence = audio->music_sequence_index % CITY_MUSIC_SEQUENCE_COUNT;
  audio->music_sequence_index = sequence + 1;
  if (sequence < 0)
    return true;
  return PlayCityEventMusicById(game, kSequenceTracks[sequence]);
}

// Select the next cue or consume the speed-scaled random choice after the caller admits an idle schedule.
// Disabled music still consumes the original selection draws; playback uses Game's Audio owner.
void SelectScheduledCityMusic(Game *game, bool advance_sequence) {
  if (advance_sequence) {
    PlayNextCityEventMusicSequence(game);
    return;
  }
  City *city = game->city;
  int speed = city->settings.speed;
  if (speed == CITY_SPEED_PAUSED)
    speed = CITY_SPEED_SLOW;
  int divisor = 8 * (3 * speed - 3);
  if (Sc2kCrtRandRange(city, divisor) == 0)
    PlayCityEventMusicById(game, Sc2kCrtRandRange(city, CITY_MUSIC_TRACK_COUNT));
}

// Native scheduling reads its playback gate from the Audio owner.
void MaybePlayCityMusic(Game *game, bool advance_sequence) {
  if (!game->audio->music_active) SelectScheduledCityMusic(game, advance_sequence);
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

bool PollCityMusic(Game *game) {
  if (!game->audio->live_open) return false;
  u16 device;
  CityMusicCompletion notification = NativeAudioMusicNotification(game->audio, &device);
  if (notification == CITY_MUSIC_PENDING) return false;
  CompleteCityMusic(game, notification, device);
  return true;
}

// City sound admission, audibility and pause policy coordinate the existing Audio controller.

static int CitySoundDuration(Game *game, int sound_id) {
  const CityAudioState *audio = game->audio;
  // The OpenSFX bridge cue lasts 2.43 seconds, rounded up to 200 ms cadence ticks.
  if (sound_id == CITY_SOUND_BRIDGE_CONSTRUCTION) return 13;
  if (sound_id < CITY_SOUND_FIRST || sound_id > CITY_SOUND_END) {
    SetGameError(game, SC2K_ERROR_GUEST_BUFFER_TOO_SMALL);
    return 0;
  }
  return audio->timing.ticks[sound_id - CITY_SOUND_FIRST];
}

static int CitySoundReplayThreshold(const CityAudioState *audio, int sound_id) {
  if (sound_id == CITY_SOUND_BRIDGE_CONSTRUCTION) sound_id = CITY_SOUND_CONSTRUCTION;
  return audio->replay_thresholds.threshold[sound_id - CITY_SOUND_FIRST];
}

// Play a sound event, interrupting or queuing playback as needed.
void PlayCitySoundEvent(City *city, TileIndex tile, int sound_id) {
  Game *game = city->game;
  CityAudioState *audio = game->audio;
  const CityInterfaceState *ui = game->ui;
  if (audio->enabled == 0)
    return;
  if ((game->ranked_events_suppressed != 0 || !ui->main_window_available || ui->main_window_iconic) && sound_id !=
      CITY_SOUND_DISASTER_WARNING)
    return;
  if (!IsCitySoundEffectId(sound_id))
    return;
  u8 pan = audio->live_open && tile != INVALID_TILE ? CityMapSoundPan(city, tile) : 128;
  if (audio->live_open && audio->overlapping_effects) {
    if (PlayCityMixedEffect(audio, sound_id, 1, pan)) {
      audio->sound_tile = tile;
      audio->last_sound_id = sound_id;
    }
    return;
  }
  if (audio->sound_playing != 0 && audio->last_sound_id == sound_id &&
      (int)((u32)CitySoundReplayThreshold(audio, sound_id) - audio->sound_delay) < 3)
    return;
  audio->retry_delay =
      audio->cached_sound_id == audio->last_sound_id && audio->cached_sound_playing == 1 ?
      (int)audio->sound_delay : 0;
  bool cached = audio->cached_sound_id == sound_id && audio->preloaded_sound_id != sound_id &&
      sound_id != CITY_SOUND_QUERY && sound_id != CITY_SOUND_EXPLOSION;
  audio->transient_sound_id = sound_id;
  audio->sound_playing = PlayCitySpatialEffect(game, sound_id, cached ? CITY_EFFECT_LOOP : CITY_EFFECT_ONCE, pan);
  if (cached) audio->cached_sound_playing = audio->sound_playing;
  if (audio->sound_playing != 0) {
    audio->sound_tile = tile;
    audio->last_sound_id = sound_id;
    audio->sound_delay = audio->cached_sound_playing != 0 && audio->cached_sound_id == sound_id ?
        0u : (u32)CitySoundDuration(game, sound_id);
  } else {
    CompleteCitySoundPlayback(game);
  }
}

bool PlayCitySound(City *city, TileIndex tile, int sound_id) {
  PlayCitySoundEvent(city, tile, sound_id);
  return true;
}

void ResetCityHelicopterSoundTiming(City *city) {
  city->game->audio->helicopter_sound_deadline = SimulationFrameTime(city);
}

bool PlayCityAmbientSound(City *city, TileIndex tile, int sound_id) {
  CityAudioState *audio = city->game->audio;
  if (audio->live_open && (sound_id == CITY_SOUND_EXPLOSION || sound_id == CITY_SOUND_AIRCRAFT_CRASH)) {
    if (!audio->ambient_sounds || !IsCityMapSoundVisible(city, tile)) return true;
    u32 now = (u32)(u64)(ReadGameTime(city->game, GAME_TIME_UNPAUSED) * 1000);
    if (audio->ambient_impact_deadline && (i32)(now - audio->ambient_impact_deadline) < 0) return true;
    // A crash and its following explosion are one audible incident. Share the gate across event slots and IDs.
    audio->ambient_impact_deadline = now + 10000;
  }
  if (sound_id == CITY_SOUND_HELICOPTER_FLIGHT) {
    u32 now = SimulationFrameTime(city);
    if ((i32)(now - audio->helicopter_sound_deadline) <= 0) return true;
    audio->helicopter_sound_deadline = now + 5000;
  }
  if (audio->ambient_sounds) PlayCitySoundEvent(city, tile, sound_id);
  return true;
}

void PlayCityAirplaneSound(City *city, int slot, int sound_id) {
  if (city->game->audio->live_open && !IsCityAirplaneVisible(city, slot)) return;
  TileIndex tile = city->events.event[slot].tile;
  PlayCityAmbientSound(city, tile, sound_id);
}

// Cached replay and queued interruptions share explicit resource identities.
bool PlayCitySoundWithDelay(Game *game, int sound_id, int repetitions) {
  CityAudioState *audio = game->audio;
  const CityInterfaceState *ui = game->ui;
  if (audio->enabled == 0)
    return false;
  if ((game->ranked_events_suppressed != 0 || ui->main_window_iconic) && sound_id != CITY_SOUND_DISASTER_WARNING)
    return true;
  if (audio->live_open && sound_id == CITY_SOUND_FIRE &&
      (game->city->settings.speed == CITY_SPEED_PAUSED || ui->dialog_pause_depth)) {
    audio->resume_fire_sound = true;
    return true;
  }
  if (audio->live_open && audio->overlapping_effects) {
    if (PlayCityMixedEffect(audio, sound_id, repetitions <= 0 ? -1 : repetitions, 128))
      audio->last_sound_id = sound_id;
    return true;
  }
  if (sound_id != audio->cached_sound_id || audio->cached_sound_playing == 0 ||
      audio->sound_playing == 0) {
    if (audio->cached_sound_id == CITY_SOUND_DISASTER_WARNING && audio->cached_sound_playing != 0 &&
        audio->sound_playing != 0) {
      audio->queued_sound = sound_id;
    } else {
      if (sound_id != audio->cached_sound_id)
        CacheCitySoundResource(audio, sound_id);
      audio->cached_sound_playing = 1;
      if (audio->cached_sound_id == sound_id) {
        if (PlayCityEffect(game, sound_id, CITY_EFFECT_REPLAY_LOOP)) {
          audio->cached_sound_id = sound_id;
          audio->last_sound_id = sound_id;
          audio->sound_playing = 1;
          audio->cached_sound_playing = 1;
          audio->sound_delay = repetitions <= 0 ? 0u : (u32)repetitions * (u32)CitySoundDuration(game, sound_id);
        } else {
          audio->cached_sound_playing = 0;
        }
      } else {
        audio->sound_playing = PlayCityEffect(game, sound_id, CITY_EFFECT_LOOP);
        // Native resources need no original WAV byte buffer, but the controller
        // must still identify the loop so interruptions and stops can find it.
        if (audio->sound_playing)
          audio->cached_sound_id = sound_id;
        audio->sound_delay = repetitions <= 0 ? 0u : (u32)repetitions * (u32)CitySoundDuration(game, sound_id);
        audio->last_sound_id = sound_id;
        audio->cached_sound_playing = audio->sound_playing;
      }
    }
  }
  return true;
}

// Clear playback state and resume a queued or cached sound when appropriate.
void CompleteCitySoundPlayback(Game *game) {
  CityAudioState *audio = game->audio;
  if (audio->live_open && audio->overlapping_effects) {
    audio->sound_playing = 0;
    audio->sound_delay = 0;
    audio->last_sound_id = CITY_SOUND_NONE;
    return;
  }
  audio->sound_playing = 0;
  if (audio->cached_sound_playing != 0) {
    int sound_id = audio->cached_sound_id;
    if (audio->last_sound_id != sound_id && audio->retry_delay != 0) {
      int duration = CitySoundDuration(game, sound_id);
      if (duration == 0) {
        SetGameError(game, SC2K_ERROR_GUEST_INTEGER_DIVIDE_BY_ZERO);
        return;
      }
      if (audio->retry_delay == (-2147483647 - 1) && duration == -1) {
        SetGameError(game, SC2K_ERROR_GUEST_INTEGER_DIVIDE_OVERFLOW);
        return;
      }
      int repetitions = audio->retry_delay / duration;
      audio->retry_delay = 0;
      PlayCitySoundWithDelay(game, audio->cached_sound_id, repetitions + 1);
    } else if (audio->queued_sound > 0) {
      audio->cached_sound_id = CITY_SOUND_NONE;
      PlayCitySoundWithDelay(game, audio->queued_sound, -1);
      audio->queued_sound = CITY_SOUND_NONE;
    } else if (audio->last_sound_id == sound_id) {
      audio->sound_delay = 0;
      audio->last_sound_id = CITY_SOUND_NONE;
    } else {
      PlayCitySoundWithDelay(game, sound_id, -1);
    }
  } else {
    audio->sound_delay = 0;
    audio->last_sound_id = CITY_SOUND_NONE;
  }
}

// Stop the platform sound, clear cached replay, then run ordinary completion.
void StopAllCitySound(Game *game) {
  CityAudioState *audio = game->audio;
  audio->resume_fire_sound = false;
  StopCityPlatformSound(game);
  audio->cached_sound_playing = 0;
  CompleteCitySoundPlayback(game);
}

// Match the last sound ID rather than the cached sound ID.
void StopCitySoundIfLastMatches(Game *game, int sound_id) {
  CityAudioState *audio = game->audio;
  if (audio->last_sound_id == sound_id) {
    if (audio->live_open && audio->overlapping_effects) {
      StopCityMixedEffect(audio, sound_id);
      audio->last_sound_id = CITY_SOUND_NONE;
    } else StopAllCitySound(game);
  }
}

// Stop a matching sound and clear its cached or queued playback.
void StopCitySoundEvent(Game *game, int sound_id) {
  CityAudioState *audio = game->audio;
  if (sound_id == CITY_SOUND_FIRE) audio->resume_fire_sound = false;
  if (audio->live_open && audio->overlapping_effects) {
    StopCityMixedEffect(audio, sound_id);
    if (audio->last_sound_id == sound_id) audio->last_sound_id = CITY_SOUND_NONE;
    return;
  }
  if (audio->cached_sound_id == sound_id)
    audio->cached_sound_playing = 0;
  // A stopped tool must not resume after a warning or another effect finishes.
  if (audio->queued_sound == sound_id)
    audio->queued_sound = CITY_SOUND_NONE;
  if (audio->cached_sound_id == sound_id)
    audio->retry_delay = 0;
  if (audio->last_sound_id == sound_id) {
    audio->sound_delay = 0;
    StopCityPlatformSound(game);
    audio->sound_playing = 0;
    audio->last_sound_id = CITY_SOUND_NONE;
  }
}

bool StopCitySound(Game *game, int sound_id) {
  StopCitySoundEvent(game, sound_id);
  return true;
}

// Fire follows world pause; UI effects and music keep their independent playback.
void SyncCityFireSoundPause(Game *game) {
  CityAudioState *audio = game->audio;
  if (!audio->live_open) return;
  bool paused = game->city->settings.speed == CITY_SPEED_PAUSED || game->ui->dialog_pause_depth;
  if (paused == audio->fire_sound_paused) return;
  audio->fire_sound_paused = paused;
  if (paused) {
    bool playing = audio->overlapping_effects ? IsCityMixedEffectPlaying(audio, CITY_SOUND_FIRE) :
        (audio->last_sound_id == CITY_SOUND_FIRE && audio->sound_playing) ||
        (audio->cached_sound_id == CITY_SOUND_FIRE && audio->cached_sound_playing) ||
        audio->queued_sound == CITY_SOUND_FIRE;
    bool resume = playing || audio->resume_fire_sound;
    if (playing) StopCitySoundEvent(game, CITY_SOUND_FIRE);
    audio->resume_fire_sound = resume;
  } else if (audio->resume_fire_sound) {
    audio->resume_fire_sound = false;
    PlayCitySoundWithDelay(game, CITY_SOUND_FIRE, -1);
  }
}

bool ToggleCitySoundPreference(Game *game) {
  game->audio->enabled = game->audio->enabled == 0;
  if (game->audio->enabled != 0)
    CompleteCitySoundPlayback(game);
  else
    StopAllCitySound(game);
  StoreGameSettings(game);
  return true;
}
