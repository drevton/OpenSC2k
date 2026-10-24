#include "audio/effects.h"

#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <stdatomic.h>

#include "assets/assets.h"
#include "audio/backend.h"
#include "audio/state.h"
#include "base/byte_order.h"

// Mix immutable mono recordings directly into SDL's signed 16-bit stereo buffer.
// The callback runs under SDL's device lock; host operations lock before touching shared voice state.

typedef struct { int left, right; } CityMixedFrame;

static inline void AdvanceCityEffectVoice(CityEffectVoice *voice, int frames) {
  voice->position += frames;
  if (voice->position == voice->frames) {
    if (voice->repetitions > 0) --voice->repetitions;
    if (voice->repetitions) voice->position = 0;
  }
}

static inline int ReadCityEffectVoice(CityEffectVoice *voice) {
  if (!voice->samples || !voice->repetitions) return 0;
  int sample = (i16)ReadLE16(voice->samples + voice->position * sizeof(i16));
  AdvanceCityEffectVoice(voice, 1);
  return sample;
}

static inline CityMixedFrame MixCityAudioFrame(CityAudioState *audio, int click_volume) {
  int single = ReadCityEffectVoice(&audio->effect) * audio->effect_volume / 100;
  if (audio->effect.samples && !audio->effect.repetitions) atomic_store(&audio->effect_finished, 1);
  int left = single * (audio->effect.pan > 128 ? 255 - audio->effect.pan : 128) / 128;
  int right = single * (audio->effect.pan < 128 ? audio->effect.pan : 128) / 128;
  int effects_left = 0, effects_right = 0;
  for (int index = 0; index < CITY_EFFECT_VOICES; ++index) {
    CityEffectVoice *voice = &audio->effect_voices[index];
    int sample = ReadCityEffectVoice(voice);
    effects_left += sample * (voice->pan > 128 ? 255 - voice->pan : 128) / 128;
    effects_right += sample * (voice->pan < 128 ? voice->pan : 128) / 128;
  }
  int click = (ReadCityEffectVoice(&audio->click) * click_volume) >> 16;
  return (CityMixedFrame){left + effects_left * audio->effect_volume / 100 + click,
      right + effects_right * audio->effect_volume / 100 + click};
}

// A single source bypasses the voice scan and limiter without changing its cursor contract.
static bool MixSingleCityAudioSource(CityAudioState *audio, u8 *stream, int bytes, bool unity_gain) {
  CityEffectVoice *voice = NULL;
  int count = 0;
  if (audio->effect.samples && audio->effect.repetitions) { voice = &audio->effect; ++count; }
  else if (audio->effect.samples) atomic_store(&audio->effect_finished, 1);
  if (audio->click.samples && audio->click.repetitions) { voice = &audio->click; ++count; }
  for (int index = 0; index < CITY_EFFECT_VOICES; ++index) {
    CityEffectVoice *candidate = &audio->effect_voices[index];
    if (candidate->samples && candidate->repetitions) { voice = candidate; ++count; }
  }
  if (!count) {
    memset(stream, 0, bytes);
    audio->mix_gain = 1;
    return true;
  }
  if (count != 1 || !unity_gain) return false;
  bool click = voice == &audio->click;
  int volume = click ? 7183 * audio->effect_volume / 100 : audio->effect_volume;
  int left_gain = voice->pan > 128 ? 255 - voice->pan : 128;
  int right_gain = voice->pan < 128 ? voice->pan : 128;
  int frames = bytes / (2 * sizeof(i16)), written = 0;
  i16 *output = (i16 *)stream;
  while (written < frames && voice->repetitions) {
    int amount = IntMin(frames - written, voice->frames - voice->position);
    const u8 *source = voice->samples + voice->position * sizeof(i16);
    if (voice->pan == 128) {
      for (int frame = 0; frame < amount; ++frame) {
        int sample = (i16)ReadLE16(source + frame * sizeof(i16));
        int tone = click ? (sample * volume) >> 16 : sample * volume / 100;
        output[(written + frame) * 2] = output[(written + frame) * 2 + 1] = tone;
      }
    } else {
      for (int frame = 0; frame < amount; ++frame) {
        int sample = (i16)ReadLE16(source + frame * sizeof(i16)) * volume / 100;
        output[(written + frame) * 2] = sample * left_gain / 128;
        output[(written + frame) * 2 + 1] = sample * right_gain / 128;
      }
    }
    AdvanceCityEffectVoice(voice, amount);
    written += amount;
  }
  if (written < frames) memset(output + written * 2, 0, (frames - written) * 2 * sizeof(i16));
  if (voice == &audio->effect && !voice->repetitions) atomic_store(&audio->effect_finished, 1);
  audio->mix_gain = 1;
  return true;
}

static void EffectMix(void *context, u8 *stream, int bytes) {
  CityAudioState *audio = context;
  i16 *output = (i16 *)stream;
  int frames = bytes / (2 * sizeof(i16)), frame = 0;
  // OpenTTD selection beep's centered gain; independent of game-effect voices.
  const int kSelectionSoundGain = 7183;
  int click_volume = kSelectionSoundGain * audio->effect_volume / 100;
  float gain = audio->mix_gain > 0 ? audio->mix_gain : 1;
  if (MixSingleCityAudioSource(audio, stream, bytes, gain == 1)) return;
  while (frame < frames) {
    if (gain == 1) {
      // Normal mixing uses only integers. The range check is the only limiter
      // work until a frame actually overloads; do not advance that frame twice.
      for (; frame < frames; ++frame) {
        CityMixedFrame mixed = MixCityAudioFrame(audio, click_volume);
        if (mixed.left < -32760 || mixed.left > 32760 || mixed.right < -32760 || mixed.right > 32760) {
          int peak = IntMax(abs(mixed.left), abs(mixed.right));
          gain = 32760.0f / peak;
          output[frame * 2] = mixed.left * gain;
          output[frame * 2 + 1] = mixed.right * gain;
          ++frame;
          break;
        }
        output[frame * 2] = mixed.left;
        output[frame * 2 + 1] = mixed.right;
      }
    } else {
      // Instant attack prevents clipping; the 100 ms release runs only while
      // attenuating. Snap back to unity within 0.009 dB, then resume the fast loop.
      for (; frame < frames; ++frame) {
        CityMixedFrame mixed = MixCityAudioFrame(audio, click_volume);
        gain += (1 - gain) / 4410;
        int peak = IntMax(abs(mixed.left), abs(mixed.right));
        if (peak * gain > 32760) gain = 32760.0f / peak;
        output[frame * 2] = mixed.left * gain;
        output[frame * 2 + 1] = mixed.right * gain;
        if (gain >= .999f && peak <= 32760) {
          gain = 1;
          ++frame;
          break;
        }
      }
    }
  }
  audio->mix_gain = gain;
}
void NativeAudioClick(CityAudioState *audio) {
  if (!audio->live_open || !audio->effects_device || !audio->click.samples) return;
  SDL_LockAudioDevice(audio->effects_device);
  audio->click.position = 0;
  audio->click.repetitions = 1;
  SDL_UnlockAudioDevice(audio->effects_device);
}

static CityEffectVoice CityAssetEffectVoice(CityAudioState *audio, int sound_id, int repetitions) {
  if (!audio->assets || !IsCitySoundEffectId(sound_id)) return (CityEffectVoice){0};
  ByteSpan source = GetAsset(audio->assets, CITY_ASSET_SOUND_FIRST + sound_id - CITY_SOUND_FIRST);
  return (CityEffectVoice){.samples = source.data, .frames = source.size / sizeof(i16),
      .repetitions = repetitions <= 0 ? -1 : repetitions, .sound_id = sound_id, .looping = repetitions != 1,
      .pan = 128};
}

static void LoadCitySelectionSound(CityAudioState *audio) {
  audio->click = CityAssetEffectVoice(audio, CITY_SOUND_QUERY, 1);
  audio->click.repetitions = 0;
  audio->click.position = audio->click.frames;
}

// Caller holds SDL's device lock. The recording remains owned by Assets.
static void ReleaseCityEffectVoice(CityAudioState *audio, int index) {
  audio->effect_voices[index] = (CityEffectVoice){0};
}

static void ReapCityEffectVoices(CityAudioState *audio) {
  for (int index = 0; index < CITY_EFFECT_VOICES; ++index)
    if (!audio->effect_voices[index].repetitions) ReleaseCityEffectVoice(audio, index);
}

bool IsCityMixedEffectPlaying(const CityAudioState *audio, int sound_id) {
  if (!audio->effects_device) return false;
  SDL_LockAudioDevice(audio->effects_device);
  bool playing = false;
  for (int index = 0; index < CITY_EFFECT_VOICES; ++index) {
    const CityEffectVoice *voice = &audio->effect_voices[index];
    if (voice->samples && voice->repetitions && (sound_id == CITY_SOUND_NONE || voice->sound_id == sound_id)) {
      playing = true;
      break;
    }
  }
  SDL_UnlockAudioDevice(audio->effects_device);
  return playing;
}

void StopCityMixedEffect(CityAudioState *audio, int sound_id) {
  if (!audio->effects_device) return;
  SDL_LockAudioDevice(audio->effects_device);
  for (int index = 0; index < CITY_EFFECT_VOICES; ++index)
    if (audio->effect_voices[index].sound_id == sound_id) ReleaseCityEffectVoice(audio, index);
  SDL_UnlockAudioDevice(audio->effects_device);
}

bool PlayCityMixedEffect(CityAudioState *audio, int sound_id, int repetitions, u8 pan) {
  if (!audio->effects_device) return false;
  CityEffectVoice resource = CityAssetEffectVoice(audio, sound_id, repetitions);
  resource.pan = pan;
  if (!resource.samples) return false;
  int same = 0;
  SDL_LockAudioDevice(audio->effects_device);
  ReapCityEffectVoices(audio);
  for (int index = 0; index < CITY_EFFECT_VOICES; ++index) {
    CityEffectVoice *voice = &audio->effect_voices[index];
    if (!voice->samples || voice->sound_id != sound_id) continue;
    // A repeated loop request must not restart its sample clock. Limit rapid
    // one-shot duplicates, while permitting two staggered explosions or cues.
    if (voice->looping || repetitions != 1) {
      if (!voice->looping) {
        voice->looping = true;
        voice->repetitions = repetitions <= 0 ? -1 : repetitions;
      }
      SDL_UnlockAudioDevice(audio->effects_device);
      return true;
    }
    if (voice->position < 4410 || ++same >= 2) {
      SDL_UnlockAudioDevice(audio->effects_device);
      return false;
    }
  }
  int selected = -1;
  for (int index = 0; index < CITY_EFFECT_VOICES; ++index) {
    const CityEffectVoice *voice = &audio->effect_voices[index];
    if (!voice->samples || !voice->repetitions) { selected = index; break; }
    // When full, replace the oldest ordinary one-shot. Persistent loops and
    // disaster warnings keep their voices rather than being cut off by tools.
    if (!voice->looping && voice->sound_id != CITY_SOUND_DISASTER_WARNING &&
        (selected < 0 || voice->position > audio->effect_voices[selected].position)) selected = index;
  }
  if (selected >= 0) {
    audio->effect_voices[selected] = resource;
  }
  SDL_UnlockAudioDevice(audio->effects_device);
  return selected >= 0;
}

static void StopEffect(CityAudioState *audio) {
  if (!audio->effects_device) return;
  SDL_LockAudioDevice(audio->effects_device);
  audio->effect = (CityEffectVoice){0};
  atomic_store(&audio->effect_finished, 0);
  for (int index = 0; index < CITY_EFFECT_VOICES; ++index) ReleaseCityEffectVoice(audio, index);
  SDL_UnlockAudioDevice(audio->effects_device);
}

bool NativeAudioPlayEffect(CityAudioState *audio, int id, bool loop, u8 pan) {
  if (!audio->effects_device) return false;
  if (id == CITY_SOUND_NONE) { StopEffect(audio); return true; }
  CityEffectVoice resource = CityAssetEffectVoice(audio, id, loop ? -1 : 1);
  resource.pan = pan;
  if (!resource.samples) return false;
  SDL_LockAudioDevice(audio->effects_device);
  audio->effect = resource;
  atomic_store(&audio->effect_finished, 0);
  SDL_UnlockAudioDevice(audio->effects_device);
  return true;
}

bool NativeAudioSoundFinished(CityAudioState *audio) {
  if (audio->effects_device) {
    SDL_LockAudioDevice(audio->effects_device);
    ReapCityEffectVoices(audio);
    SDL_UnlockAudioDevice(audio->effects_device);
  }
  if (atomic_exchange(&audio->effect_finished, 0) != 1) return false;
  StopEffect(audio);
  return true;
}

// Resource identities are cached; the immutable PCM stays owned by Assets.
bool CacheCitySoundResource(CityAudioState *audio, int sound_id) {
  if (!audio->assets || !IsCitySoundEffectId(sound_id) ||
      !GetAsset(audio->assets, CITY_ASSET_SOUND_FIRST + sound_id - CITY_SOUND_FIRST).data) return false;
  audio->cached_sound_id = sound_id;
  return true;
}

// Record a resolved tool cue only when its decoded recording is available; cache hits need no asset lookup.
bool PreloadCityToolSound(CityAudioState *audio, int sound_id) {
  if (audio->preloaded_sound_id == sound_id)
    return true;
  if (!audio->assets || !IsCitySoundEffectId(sound_id) ||
      !GetAsset(audio->assets, CITY_ASSET_SOUND_FIRST + sound_id - CITY_SOUND_FIRST).data) return false;
  audio->preloaded_sound_id = sound_id;
  return true;
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

// Audio initializes its owner and borrows Assets before opening the live backend.
void OpenCityEffectMixer(CityAudioState *audio) {
  audio->mix_gain = 1;
  audio->effect_volume = 80;
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    SDL_AudioSpec desired = {0};
    desired.freq = 44100; desired.format = AUDIO_S16SYS; desired.channels = 2;
    desired.samples = 1024; desired.callback = EffectMix; desired.userdata = audio;
    SDL_AudioSpec obtained;
    audio->effects_device = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
    if (audio->effects_device) {
      LoadCitySelectionSound(audio);
      SDL_PauseAudioDevice(audio->effects_device, 0);
    }
  }
}

void CloseCityEffectMixer(CityAudioState *audio) {
  StopEffect(audio);
  if (audio->effects_device) { SDL_CloseAudioDevice(audio->effects_device); audio->effects_device = 0; }
  audio->click = (CityEffectVoice){0};
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void SetCityEffectMixerVolume(CityAudioState *audio, int volume) {
  if (audio->effects_device) SDL_LockAudioDevice(audio->effects_device);
  audio->effect_volume = volume;
  if (audio->effects_device) SDL_UnlockAudioDevice(audio->effects_device);
}
