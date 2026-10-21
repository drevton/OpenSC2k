#include "audio/types.h"
#include "city/engine/city_types.h"
#include "city/tools/types.h"
#include "game/settings.h"
#include "audio/backend.h"
#include "assets/assets.h"
#include <stdio.h>

#include "audio/audio.h"
#include "city/saveload/services.h"
#include "city/state.h"
#include "game/errors.h"
#include "ui/text/text.h"
#include <stdlib.h>
#include "ui/state.h"

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

// The controller checks the current playback flag before requesting a tool sound.
bool IsCitySoundPlaying(const CityAudioState *audio) {
  return audio->live_open && audio->overlapping_effects ?
      IsCityMixedEffectPlaying(audio, CITY_SOUND_NONE) : audio->sound_playing;
}

bool CanPlayCityToolSound(const CityAudioState *audio, int sound_id) {
  return audio->live_open && audio->overlapping_effects ?
      !IsCityMixedEffectPlaying(audio, sound_id) : !audio->sound_playing;
}

// Resource identities are cached; the immutable PCM stays owned by Assets.
bool CacheCitySoundResource(Game *game, int sound_id) {
  CityAudioState *audio = game->audio;
  if (!audio->assets || !IsCitySoundEffectId(sound_id) ||
      !GetAsset(audio->assets, CITY_ASSET_SOUND_FIRST + sound_id - CITY_SOUND_FIRST).data) return false;
  audio->cached_sound_id = sound_id;
  return true;
}

// Choose a placement sound; unchanged tools retain the current preload.
// Rail variant 2 and unsupported civic variants read an uninitialized stack slot in
// the binary. Retain the current sound there instead of introducing host C UB.
int GetCityToolPlacementSoundId(City *city, const CityInterfaceState *ui, const CityAudioState *audio) {
  if (city->settings.timer_mode == CITY_TIMER_STOPPED)
    return ui->terrain_tool >= CITY_TERRAIN_TOOL_TREE && ui->terrain_tool <= CITY_TERRAIN_TOOL_FOREST ?
        CITY_SOUND_TREE_OR_ZONE_PLACEMENT : CITY_SOUND_WATER;
  int group = ui->tool_group;
  switch (group) {
    case CITY_TOOL_GROUP_TREE_WATER:
      return ui->selection.value[group] == CITY_TREE_WATER_TREES ? CITY_SOUND_TREE_OR_ZONE_PLACEMENT : CITY_SOUND_WATER;
    case CITY_TOOL_GROUP_DISPATCH:
      if (ui->selection.value[group] == CITY_DISPATCH_POLICE)
        return CITY_SOUND_POLICE_OR_HOSPITAL;
      return ui->selection.value[group] == CITY_DISPATCH_FIRE ? CITY_SOUND_FIRE_DISPATCH : CITY_SOUND_MILITARY_DISPATCH;
    case CITY_TOOL_GROUP_POWER:
      return ui->selection.value[group] == CITY_POWER_LINES ? CITY_SOUND_POWER_LINES : CITY_SOUND_CONSTRUCTION;
    case CITY_TOOL_GROUP_WATER:
    case CITY_TOOL_GROUP_PORT: return CITY_SOUND_CONSTRUCTION;
    case CITY_TOOL_GROUP_REWARDS: return CITY_SOUND_CIVIC_PLACEMENT;
    case CITY_TOOL_GROUP_ROAD:
      return ui->selection.value[group] == CITY_ROAD_BUS_DEPOT ? CITY_SOUND_BUS_DEPOT : CITY_SOUND_CONSTRUCTION;
    case CITY_TOOL_GROUP_RAIL:
      return ui->selection.value[group] == CITY_RAIL_DEPOT ? audio->preloaded_sound_id : CITY_SOUND_CONSTRUCTION;
    case CITY_TOOL_GROUP_RESIDENTIAL:
    case CITY_TOOL_GROUP_COMMERCIAL:
    case CITY_TOOL_GROUP_INDUSTRIAL: return CITY_SOUND_TREE_OR_ZONE_PLACEMENT;
    case CITY_TOOL_GROUP_EDUCATION: return CITY_SOUND_EDUCATION;
    case CITY_TOOL_GROUP_CIVIC:
      switch (ui->selection.value[group]) {
        case CITY_CIVIC_POLICE:
        case CITY_CIVIC_HOSPITAL: return CITY_SOUND_POLICE_OR_HOSPITAL;
        case CITY_CIVIC_FIRE: return CITY_SOUND_FIRE_DISPATCH;
        case CITY_CIVIC_PRISON: return CITY_SOUND_PRISON;
        default: return audio->preloaded_sound_id;
      }
    case CITY_TOOL_GROUP_RECREATION:
      return ui->selection.value[group] == CITY_RECREATION_ZOO ? CITY_SOUND_SAILBOAT_OR_ZOO :
          CITY_SOUND_CIVIC_PLACEMENT;
    default: return audio->preloaded_sound_id;
  }
}

// Cache hits succeed without I/O; failed reads preserve the previous ID.
bool PreloadSelectedCityToolSound(City *city) {
  CityAudioState *audio = city->game->audio;
  const CityInterfaceState *ui = city->game->ui;
  int sound_id = GetCityToolPlacementSoundId(city, ui, audio);
  if (audio->preloaded_sound_id == sound_id)
    return true;
  if (!audio->assets || !IsCitySoundEffectId(sound_id) ||
      !GetAsset(audio->assets, CITY_ASSET_SOUND_FIRST + sound_id - CITY_SOUND_FIRST).data) return false;
  audio->preloaded_sound_id = sound_id;
  return true;
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
  if (audio->live_open && audio->overlapping_effects) {
    if (PlayCityMixedEffect(audio, sound_id, 1)) {
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
  audio->sound_playing = PlayCityEffect(game, sound_id, cached ? CITY_EFFECT_LOOP : CITY_EFFECT_ONCE);
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
    if (PlayCityMixedEffect(audio, sound_id, repetitions <= 0 ? -1 : repetitions))
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
        CacheCitySoundResource(game, sound_id);
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
  bool stop_current = audio->cached_sound_id == sound_id && audio->last_sound_id == sound_id;
  if (audio->cached_sound_id == sound_id)
    audio->cached_sound_playing = 0;
  // A stopped tool must not resume after a warning or another effect finishes.
  if (audio->queued_sound == sound_id)
    audio->queued_sound = CITY_SOUND_NONE;
  if (audio->cached_sound_id == sound_id)
    audio->retry_delay = 0;
  stop_current = audio->last_sound_id == sound_id;
  if (stop_current)
    audio->sound_delay = 0;
  if (stop_current) {
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
