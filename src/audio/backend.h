#ifndef SC2K_AUDIO_BACKEND_H
#define SC2K_AUDIO_BACKEND_H

#include "audio/state.h"
#include "game/state.h"
#include "city/types.h"
#include "ui/music/jukebox_state.h"

// Audio

// Initialize only in the live app; deterministic headless replay has no audio side effects.
bool NativeAudioOpen(CityAudioState *audio, Jukebox *player);
void NativeAudioClose(CityAudioState *audio);
bool NativeAudioPlay(CityAudioState *audio, Jukebox *player);
void NativeAudioStop(CityAudioState *audio);
bool SetCitySoundFont(CityAudioState *audio, const char *path, bool enabled);
// Short UI confirmation, mixed independently of music and game-effect completion.
void NativeAudioClick(CityAudioState *audio);
void NativeAudioVolume(CityAudioState *audio, const Jukebox *player);
void SetCityAudioFocus(CityAudioState *audio, bool focused);
bool NativeAudioMusicFinished(CityAudioState *audio);
bool NativeAudioSoundFinished(CityAudioState *audio);
// Live mono effects share recordings and mix directly into SDL's supplied buffer.
bool PlayCityMixedEffect(CityAudioState *audio, int sound_id, int repetitions);
void StopCityMixedEffect(CityAudioState *audio, int sound_id);
bool IsCityMixedEffectPlaying(const CityAudioState *audio, int sound_id);
// The single player reports completion to its local controller.
i32 NativeAudioMusicNotification(CityAudioState *audio, u16 *device);
void SetJukeboxPlaying(Game *game, bool playing);
u32 InterruptJukebox(Game *game, int track);
void CompleteJukeboxTrack(Game *game, int notification, u16 device);

// City Audio

enum { CITY_MUSIC_NO_TOKEN = UINT16_MAX };
typedef enum { CITY_MUSIC_PENDING = 0, CITY_MUSIC_FINISHED = 1, CITY_MUSIC_FAILED = 8 } CityMusicCompletion;

typedef enum {
  CITY_EFFECT_ONCE, CITY_EFFECT_LOOP, CITY_EFFECT_REPLAY_LOOP, CITY_EFFECT_STOP
} CityEffectPlayback;
bool PlayCityEffect(Game *game, int sound_id, CityEffectPlayback playback);
void StopCityPlatformSound(Game *game);

#endif
