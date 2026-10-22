#ifndef SC2K_AUDIO_BACKEND_H
#define SC2K_AUDIO_BACKEND_H

#include "audio/types.h"
#include "base/types.h"

typedef struct CityAudioState CityAudioState;

// Live device lifecycle. Open reports MIDI availability; effects can still be open when MIDI is unavailable.
// Initialize audio->jukebox before Open. Close releases both backends; headless replay does not open devices.
bool NativeAudioOpen(CityAudioState *audio);
void NativeAudioClose(CityAudioState *audio);
// Read the current track cursor in seconds. Returns false on a position-read error.
bool NativeAudioMusicPosition(CityAudioState *audio, double *seconds);
// Replace the current MIDI sequence with a track from kJukeboxTracks and a starting cursor.
// A negative track clears playback.
bool NativeAudioPlayTrack(CityAudioState *audio, int track, double seconds);
// Restart the selected playlist track, replacing any temporary cue.
bool NativeAudioPlay(CityAudioState *audio);
void NativeAudioStop(CityAudioState *audio);
bool SetCitySoundFont(CityAudioState *audio, const char *path, bool enabled);
// Short UI confirmation, mixed independently of music and game-effect completion.
void NativeAudioClick(CityAudioState *audio);
// Apply the playlist's music/effects percentages together with Audio's master volume.
void NativeAudioVolume(CityAudioState *audio);
void SetCityAudioFocus(CityAudioState *audio, bool focused);
bool NativeAudioMusicFinished(CityAudioState *audio);
bool NativeAudioSoundFinished(CityAudioState *audio);
// Replace the single effect voice. CITY_SOUND_NONE stops effect playback; failure retains the previous voice.
bool NativeAudioPlayEffect(CityAudioState *audio, int sound_id, bool loop, u8 pan);
// Live mono effects share recordings and mix directly into SDL's supplied buffer.
bool PlayCityMixedEffect(CityAudioState *audio, int sound_id, int repetitions, u8 pan);
void StopCityMixedEffect(CityAudioState *audio, int sound_id);
bool IsCityMixedEffectPlaying(const CityAudioState *audio, int sound_id);
// The single player reports completion to its local controller.
i32 NativeAudioMusicNotification(CityAudioState *audio, u16 *device);

#endif
