#ifndef SC2K_GAME_AUDIO_H
#define SC2K_GAME_AUDIO_H

#include "audio/types.h"
#include "city/types.h"

typedef struct Game Game;

// Resolved requests notify Game's playback observer after the backend returns its actual result.
bool PlayCityEffect(Game *game, int sound_id, CityEffectPlayback playback);
bool PlayCitySpatialEffect(Game *game, int sound_id, CityEffectPlayback playback, u8 pan);
void StopCityPlatformSound(Game *game);

// Playlist changes and accepted interruption/completion attempts refresh the music windows.
void SetJukeboxPlaying(Game *game, bool playing);
// Returns zero when an interruption starts; refusals and playback failures return one.
u32 InterruptJukebox(Game *game, int track);
// Ignore completions with an old token or a notification other than finished/failed.
void CompleteJukeboxTrack(Game *game, int notification, u16 device);

// Music

// Poll the live backend once; accepted completions advance or resume the playlist.
bool PollCityMusic(Game *game);
i32 MaybePlayCityEventMusic(Game *game, int event_id);
u32 PlayCityMusicTrack(Game *game, int track);
bool PlayCityEventMusicById(Game *game, int event_id);
void StopCityMusicSequence(Game *game);
int CompleteCityMusic(Game *game, int notification, int token);
bool PlayNextCityEventMusicSequence(Game *game);
// Read Audio's playback gate before selecting music. Selection can consume City RNG, even when music is disabled.
void MaybePlayCityMusic(Game *game, bool advance_sequence);
// Select after an idle schedule is admitted. Comparison uses this when its recorded receiver supplies the gate;
// it does not replace or copy native Audio state. Sequence selection and random selection retain their RNG order.
void SelectScheduledCityMusic(Game *game, bool advance_sequence);

bool ToggleCityMusicPreference(Game *game);

// Sound

void StopCitySoundEvent(Game *game, i32 sound_id);
void SyncCityFireSoundPause(Game *game);
bool StopCitySound(Game *game, int sound_id);
// Tile is the sound source; city-wide/UI sounds use INVALID_TILE.
bool PlayCitySound(City *city, TileIndex tile, int sound_id);
// Reset the shared helicopter cue deadline to the current simulation frame time after city creation or spawning.
void ResetCityHelicopterSoundTiming(City *city);
// Routine city activity; shares recordings with tool sounds but has its own local preference. Helicopter flight
// cues share a five-second cooldown, advanced even when ambient sounds are muted.
bool PlayCityAmbientSound(City *city, TileIndex tile, int sound_id);
// Aircraft cues additionally require a visible plane in the live presentation.
void PlayCityAirplaneSound(City *city, int slot, int sound_id);
void PlayCitySoundEvent(City *city, TileIndex tile, int sound_id);
bool CacheCitySoundResource(Game *game, i32 sound_id);
// Select a resolved recording for tool playback. A cache hit succeeds; missing/invalid recordings keep the old ID.
bool PreloadCityToolSound(Game *game, int sound_id);
bool PlayCitySoundWithDelay(Game *game, i32 sound_id, i32 repetitions);
void CompleteCitySoundPlayback(Game *game);
void StopAllCitySound(Game *game);
void StopCitySoundIfLastMatches(Game *game, i32 sound_id);

bool ToggleCitySoundPreference(Game *game);

#endif
