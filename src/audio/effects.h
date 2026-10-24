#ifndef SC2K_AUDIO_EFFECTS_H
#define SC2K_AUDIO_EFFECTS_H

typedef struct CityAudioState CityAudioState;

// Internal device lifecycle used by the native audio backend. Open requires a fresh or closed mixer;
// it borrows Assets-owned recordings and leaves effects_device zero if SDL cannot open the device.
// Close locks out the callback to stop voices, closes the device, then releases SDL audio.
void OpenCityEffectMixer(CityAudioState *audio);
void CloseCityEffectMixer(CityAudioState *audio);
// Effective percentage in 0..100 after master-volume adjustment. The device lock protects callback-visible gain.
void SetCityEffectMixerVolume(CityAudioState *audio, int volume);

#endif
