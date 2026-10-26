#ifndef SC2K_AUDIO_BACKEND_H
#define SC2K_AUDIO_BACKEND_H

#include "audio/types.h"
#include "base/types.h"

typedef struct CityAudioState CityAudioState;

// Live device lifecycle. Open reports MIDI availability; effects can still be open when MIDI is unavailable.
// Open requires a closed owner with an initialized jukebox; partial MIDI startup remains owned until Close.
// Close releases both backends and preference text, even without Open.
// Headless replay does not open devices; Close only touches devices after Open.
bool NativeAudioOpen(CityAudioState *audio);
void NativeAudioClose(CityAudioState *audio);
// Restart the selected playlist track, replacing any temporary cue.
bool NativeAudioPlay(CityAudioState *audio);
void NativeAudioStop(CityAudioState *audio);

// An ignored request leaves playback unchanged; a failed attempt may resume the interrupted track.
// Both attempted outcomes can change the displayed track/error. No result triggers UI work inside Audio.
typedef enum CityMusicInterruptResult {
  CITY_MUSIC_INTERRUPT_IGNORED, CITY_MUSIC_INTERRUPT_FAILED, CITY_MUSIC_INTERRUPT_STARTED
} CityMusicInterruptResult;
// Temporarily play a valid catalog track only while the live jukebox is playing. Save the current cursor
// once across consecutive interruptions; resume it on completion or after a failed interruption attempt.
CityMusicInterruptResult NativeAudioInterrupt(CityAudioState *audio, int track);
// Resume the saved cursor, advance a finished track, or stop a failed track. Ignore stale/no-device tokens
// and notifications other than finished/failed; return true only when a completion was accepted.
bool NativeAudioCompleteMusic(CityAudioState *audio, CityMusicCompletion notification, u16 device);
// Apply a custom bank, reusing the configured path when path is NULL; disabling selects the platform default.
// A successful custom selection retains a copy of a replacement path. Failure preserves the path and preference flag.
bool SetCitySoundFont(CityAudioState *audio, const char *path, bool enabled);
// Short UI confirmation, mixed independently of music and game-effect completion.
// A closed backend or missing recording is a no-op; callers do not inspect device state.
void NativeAudioClick(CityAudioState *audio);
// Apply the playlist's music/effects percentages together with Audio's master volume.
void NativeAudioVolume(CityAudioState *audio);
// Change the music focus gate without stopping playback or changing effect gain.
void SetCityAudioFocus(CityAudioState *audio, bool focused);
// Select already-loaded recording IDs for the replay controller or current tool. These perform no I/O,
// allocation or playback. Missing/invalid recordings return false and preserve the previous selected ID.
// PreloadCityToolSound also succeeds immediately when the requested ID is already selected.
bool CacheCitySoundResource(CityAudioState *audio, int sound_id);
bool PreloadCityToolSound(CityAudioState *audio, int sound_id);
// Live overlapping effects query the mixer; closed/single-effect playback uses the controller flag.
// In live overlapping mode, the tool gate permits a different ID while suppressing another copy of the same ID.
bool IsCitySoundPlaying(const CityAudioState *audio);
bool CanPlayCityToolSound(const CityAudioState *audio, int sound_id);
// Consume the single effect's completion once; accepted completion stops game-effect voices.
// UI click completion is independent of this controller.
bool NativeAudioSoundFinished(CityAudioState *audio);
// Replace the single effect voice. CITY_SOUND_NONE stops single/mixed game effects but leaves UI clicks alone;
// failure retains the previous voice. Mono recordings are borrowed from Assets for their full playback lifetime.
bool NativeAudioPlayEffect(CityAudioState *audio, int sound_id, bool loop, u8 pan);
// Add a live effect: positive repetitions count complete plays, nonpositive repetitions loop until stopped.
// Repeated loop requests preserve the cursor; rapid duplicates or a full protected voice set can reject a play.
// Mono effects share Assets recordings and mix directly into SDL's supplied buffer.
// Pan is 0 left, 128 center, 255 right.
bool PlayCityMixedEffect(CityAudioState *audio, int sound_id, int repetitions, u8 pan);
// Stop every mixed voice matching this sound. CITY_SOUND_NONE is not a wildcard for stopping.
void StopCityMixedEffect(CityAudioState *audio, int sound_id);
// CITY_SOUND_NONE queries any mixed voice. Single-effect and UI click voices are excluded.
bool IsCityMixedEffectPlaying(const CityAudioState *audio, int sound_id);
// Poll one completion while notifications are enabled, returning its status and token together.
// Pending/disabled consumes nothing and returns CITY_MUSIC_NO_TOKEN; a completion consumes the notification
// once and captures the current token before the caller can advance or resume playback.
CityMusicNotification NativeAudioMusicNotification(CityAudioState *audio);

#endif
