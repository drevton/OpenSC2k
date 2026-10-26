#ifndef SC2K_GAME_AUDIO_H
#define SC2K_GAME_AUDIO_H

#include "audio/types.h"
#include "city/types.h"

typedef struct Game Game;

// Resolved requests notify Game's playback observer after the backend returns its actual result.
bool PlayCityEffect(Game *game, int sound_id, CityEffectPlayback playback);
void StopCityPlatformSound(Game *game);

// Playlist changes and accepted interruption/completion attempts refresh the music windows.
void SetJukeboxPlaying(Game *game, bool playing);
// Coordinate Audio playback and UI refresh. Return zero when an interruption starts; otherwise return one.
u32 InterruptJukebox(Game *game, int track);
// Ignore completions with an old token or a notification other than finished/failed.
void CompleteJukeboxTrack(Game *game, CityMusicCompletion notification, u16 device);

// Music

// Poll the live backend once; accepted completions advance or resume the playlist.
bool PollCityMusic(Game *game);
// Apply event-cue admission and return the focus/iconic suppression value or track-attempt result.
// Disabled music and invalid cue IDs return zero; zero therefore does not establish that playback started.
i32 MaybePlayCityEventMusic(Game *game, int event_id);
u32 PlayCityMusicTrack(Game *game, int track);
void StopCityMusicSequence(Game *game);
// Preserve the raw host notification and observer's result; the native controller rejects unknown values.
int CompleteCityMusic(Game *game, int notification, int token);
// Advance the cue sequence even when playback is suppressed; a negative sequence index skips the request.
void PlayNextCityEventMusicSequence(Game *game);
// Read Audio's playback gate before selecting music. Selection can consume City RNG, even when music is disabled.
void MaybePlayCityMusic(Game *game, bool advance_sequence);
void SelectScheduledCityMusic(Game *game, bool advance_sequence);

// Toggle the local preference, apply playback policy and store Game settings.
void ToggleCityMusicPreference(Game *game);

// Saved preferences

// Stock saves retain two raw flags. Reading them does not query devices or change playback.
typedef struct { u32 sound_enabled, music_enabled; } CitySavedAudioFields;
CitySavedAudioFields ReadCitySavedAudio(const Game *game);
// Apply each saved word when the codec reaches it, without playing, stopping or storing global settings.
void ApplyCitySavedSoundPreference(Game *game, u32 enabled);
void ApplyCitySavedMusicPreference(Game *game, u32 enabled);
// At load's music-restoration point, keep a live jukebox's choice; otherwise keep the saved flag.
// A disabled result stops the event sequence through the ordinary playback/observation path.
void RestoreCityLoadedMusicPreference(Game *game);

// Sound

void SyncCityFireSoundPause(Game *game);
// Stop matching playback and remove its queued/cached replay, without reporting a device result.
void StopCitySound(Game *game, int sound_id);
bool PlayCitySoundWithDelay(Game *game, i32 sound_id, i32 repetitions);
void CompleteCitySoundPlayback(Game *game);
// One delivered cadence decrements a nonzero sound countdown and completes playback exactly on reaching zero.
// Completion retains the ordinary queued/cached replay and observer behavior; no host clock is sampled here.
void AdvanceCitySoundCadence(Game *game);
void StopAllCitySound(Game *game);
void StopCitySoundIfLastMatches(Game *game, i32 sound_id);

// Toggle the local preference, apply playback policy and store Game settings.
void ToggleCitySoundPreference(Game *game);

#endif
