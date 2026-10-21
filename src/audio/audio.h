#ifndef SC2K_DOMAIN_SRC_AUDIO_AUDIO_H
#define SC2K_DOMAIN_SRC_AUDIO_AUDIO_H

#include "audio/types.h"
#include "audio/state.h"

typedef struct Game Game;
typedef struct CityInterfaceState CityInterfaceState;

// Music

i32 MaybePlayCityEventMusic(Game *game, int event_id);
u32 PlayCityMusicTrack(Game *game, int track);
bool PlayCityEventMusicById(Game *game, int event_id);
void StopCityMusicSequence(Game *game);
int CompleteCityMusic(Game *game, int notification, int token);
bool PlayNextCityEventMusicSequence(Game *game);

// Sound

static inline bool IsCitySoundEffectId(int sound_id) {
  return (sound_id >= CITY_SOUND_FIRST && sound_id < CITY_SOUND_END) ||
      sound_id == CITY_SOUND_BRIDGE_CONSTRUCTION;
}

void StopCitySoundEvent(Game *game, i32 sound_id);
void SyncCityFireSoundPause(Game *game);
bool StopCitySound(Game *game, int sound_id);
// Tile is the sound source; city-wide/UI sounds use INVALID_TILE.
bool PlayCitySound(City *city, TileIndex tile, int sound_id);
void PlayCitySoundEvent(City *city, TileIndex tile, int sound_id);
bool IsCitySoundPlaying(const CityAudioState *audio);
bool CanPlayCityToolSound(const CityAudioState *audio, int sound_id);
bool CacheCitySoundResource(Game *game, i32 sound_id);
i32 GetCityToolPlacementSoundId(City *city, const CityInterfaceState *ui, const CityAudioState *audio);
bool PreloadSelectedCityToolSound(City *city);
bool PlayCitySoundWithDelay(Game *game, i32 sound_id, i32 repetitions);
void CompleteCitySoundPlayback(Game *game);
void StopAllCitySound(Game *game);
void StopCitySoundIfLastMatches(Game *game, i32 sound_id);

bool ToggleCityMusicPreference(Game *game);
bool ToggleCitySoundPreference(Game *game);

#endif
