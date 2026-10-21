#include "audio/types.h"
#include "audio/backend.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <stdatomic.h>

#ifdef _WIN32
#include "audio/windows_midi.h"
#endif

#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
#endif

#include "audio/audio.h"
#include "base/byte_order.h"
#include "assets/assets.h"
#include "ui/state.h"
#include "city/saveload/services.h"

// Native MIDI synthesis and independently mixed game effects.

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
  int effects = 0;
  for (int index = 0; index < CITY_EFFECT_VOICES; ++index)
    effects += ReadCityEffectVoice(&audio->effect_voices[index]);
  int mixed = single + effects * audio->effect_volume / 100 +
      ((ReadCityEffectVoice(&audio->click) * click_volume) >> 16);
  return (CityMixedFrame){mixed, mixed};
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
  int frames = bytes / (2 * sizeof(i16)), written = 0;
  i16 *output = (i16 *)stream;
  while (written < frames && voice->repetitions) {
    int amount = IntMin(frames - written, voice->frames - voice->position);
    const u8 *source = voice->samples + voice->position * sizeof(i16);
    for (int frame = 0; frame < amount; ++frame) {
      int sample = (i16)ReadLE16(source + frame * sizeof(i16));
      int tone = click ? (sample * volume) >> 16 : sample * volume / 100;
      output[(written + frame) * 2] = output[(written + frame) * 2 + 1] = tone;
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
  if (!audio->effects_device || !audio->click.samples) return;
  SDL_LockAudioDevice(audio->effects_device);
  audio->click.position = 0;
  audio->click.repetitions = 1;
  SDL_UnlockAudioDevice(audio->effects_device);
}

static CityEffectVoice CityAssetEffectVoice(CityAudioState *audio, int sound_id, int repetitions) {
  if (!audio->assets || !IsCitySoundEffectId(sound_id)) return (CityEffectVoice){0};
  ByteSpan source = GetAsset(audio->assets, CITY_ASSET_SOUND_FIRST + sound_id - CITY_SOUND_FIRST);
  return (CityEffectVoice){.samples = source.data, .frames = source.size / sizeof(i16),
      .repetitions = repetitions <= 0 ? -1 : repetitions, .sound_id = sound_id, .looping = repetitions != 1};
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

bool PlayCityMixedEffect(CityAudioState *audio, int sound_id, int repetitions) {
  if (!audio->effects_device) return false;
  CityEffectVoice resource = CityAssetEffectVoice(audio, sound_id, repetitions);
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
static bool PlayCityAudioResource(CityAudioState *audio, int id, bool loop) {
  if (!audio->effects_device) return false;
  if (id == CITY_SOUND_NONE) { StopEffect(audio); return true; }
  CityEffectVoice resource = CityAssetEffectVoice(audio, id, loop ? -1 : 1);
  if (!resource.samples) return false;
  SDL_LockAudioDevice(audio->effects_device);
  audio->effect = resource;
  atomic_store(&audio->effect_finished, 0);
  SDL_UnlockAudioDevice(audio->effects_device);
  return true;
}
bool PlayCityEffect(Game *game, int sound_id, CityEffectPlayback playback) {
  bool result = PlayCityAudioResource(game->audio, playback == CITY_EFFECT_STOP ? CITY_SOUND_NONE : sound_id,
      playback == CITY_EFFECT_LOOP || playback == CITY_EFFECT_REPLAY_LOOP);
  if (game->sound_played) game->sound_played(game, sound_id, playback, result);
  return result;
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

#ifdef __APPLE__
static OSStatus MakeGraph(CityAudioState *audio) {
  AUNode instrument, mix, output;
  AudioUnit mixer = NULL, synth = NULL;
  AudioComponentDescription desc = {kAudioUnitType_MusicDevice, kAudioUnitSubType_DLSSynth,
      kAudioUnitManufacturer_Apple, 0, 0};
  OSStatus status = NewAUGraph(&audio->music_graph);
  if (!status) status = AUGraphAddNode(audio->music_graph, &desc, &instrument);
  desc.componentType = kAudioUnitType_Mixer; desc.componentSubType = kAudioUnitSubType_MultiChannelMixer;
  if (!status) status = AUGraphAddNode(audio->music_graph, &desc, &mix);
  desc.componentType = kAudioUnitType_Output; desc.componentSubType = kAudioUnitSubType_DefaultOutput;
  if (!status) status = AUGraphAddNode(audio->music_graph, &desc, &output);
  if (!status) status = AUGraphOpen(audio->music_graph);
  if (!status) status = AUGraphNodeInfo(audio->music_graph, mix, NULL, &mixer);
  audio->music_mixer = mixer;
  if (!status) status = AUGraphNodeInfo(audio->music_graph, instrument, NULL, &synth);
  audio->music_synth = synth;
  UInt32 inputs = 1;
  if (!status) status = AudioUnitSetProperty(audio->music_mixer, kAudioUnitProperty_ElementCount,
      kAudioUnitScope_Input, 0, &inputs, sizeof(inputs));
  // Newly created mixer buses are silent until their input gain is set.
  // The player's volume controls the output bus independently.
  if (!status) status = AudioUnitSetParameter(audio->music_mixer, kMultiChannelMixerParam_Volume,
      kAudioUnitScope_Input, 0, 1.0f, 0);
  if (!status) status = AUGraphConnectNodeInput(audio->music_graph, instrument, 0, mix, 0);
  if (!status) status = AUGraphConnectNodeInput(audio->music_graph, mix, 0, output, 0);
  if (!status) status = AUGraphInitialize(audio->music_graph);
  if (!status) status = AUGraphStart(audio->music_graph);
  return status;
}
#endif

bool NativeAudioOpen(CityAudioState *audio, Jukebox *p) {
  audio->live_open = 1;
  audio->overlapping_effects = true;
  audio->mix_gain = 1;
  audio->music_background = audio->music_focused = true;
  // Background and in-game music share the selected jukebox player.
  p->playing = 1;
  audio->interrupt_device = CITY_MUSIC_NO_TOKEN;
  audio->resume_music_track = -1;
  audio->effect_volume = 80;
  audio->master_volume = 100;
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
#ifdef __APPLE__
  OSStatus status = NewMusicPlayer(&audio->music_player);
  if (!status) status = MakeGraph(audio);
  audio->midi_available = status == noErr;
  if (!audio->midi_available) snprintf(p->error, sizeof(p->error), "Audio error %d", (int)status);
#elif defined(_WIN32)
  audio->windows_midi = OpenWindowsMidi(p->error, sizeof(p->error));
  audio->midi_available = audio->windows_midi != NULL;
#else
  snprintf(p->error, sizeof(p->error), "No MIDI backend for this platform");
#endif
  audio->music_enabled = p->playing != 0;
  NativeAudioVolume(audio, p);
  return audio->midi_available;
}
void NativeAudioStop(CityAudioState *audio) {
  audio->music_active = audio->music_notify = 0;
  audio->music_device = CITY_MUSIC_NO_TOKEN;
#ifdef __APPLE__
  if (audio->music_player) MusicPlayerStop(audio->music_player);
  if (audio->music_synth)
    for (i32 channel = 0; channel < 16; ++channel) {
      MusicDeviceMIDIEvent(audio->music_synth, 0xB0 | channel, 64, 0, 0);
      MusicDeviceMIDIEvent(audio->music_synth, 0xB0 | channel, 120, 0, 0);
      MusicDeviceMIDIEvent(audio->music_synth, 0xB0 | channel, 123, 0, 0);
    }
  if (audio->music_player) MusicPlayerSetSequence(audio->music_player, NULL);
  if (audio->music_sequence) { DisposeMusicSequence(audio->music_sequence); audio->music_sequence = NULL; }
#elif defined(_WIN32)
  if (!StopWindowsMidi(audio->windows_midi)) audio->midi_available = 0;
#endif
}
void NativeAudioVolume(CityAudioState *audio, const Jukebox *p) {
  if (audio->effects_device) SDL_LockAudioDevice(audio->effects_device);
  audio->effect_volume = p->effects_volume * audio->master_volume / 100;
  audio->music_volume = p->volume * audio->master_volume / 100;
  if (audio->effects_device) SDL_UnlockAudioDevice(audio->effects_device);
#ifdef __APPLE__
  if (audio->music_mixer) AudioUnitSetParameter(audio->music_mixer, kMultiChannelMixerParam_Volume,
      kAudioUnitScope_Output, 0, audio->music_background || audio->music_focused ? audio->music_volume / 100.0f : 0, 0);
#elif defined(_WIN32)
  SetWindowsMidiVolume(audio->windows_midi, audio->music_background || audio->music_focused ? audio->music_volume : 0);
#endif
}
void SetCityAudioFocus(CityAudioState *audio, bool focused) {
  if (audio->effects_device) SDL_LockAudioDevice(audio->effects_device);
  audio->music_focused = focused;
  if (audio->effects_device) SDL_UnlockAudioDevice(audio->effects_device);
#ifdef __APPLE__
  if (audio->music_mixer) AudioUnitSetParameter(audio->music_mixer, kMultiChannelMixerParam_Volume,
      kAudioUnitScope_Output, 0, audio->music_background || focused ? audio->music_volume / 100.0f : 0, 0);
#elif defined(_WIN32)
  SetWindowsMidiVolume(audio->windows_midi, audio->music_background || focused ? audio->music_volume : 0);
#endif
}

static bool PlayMusicTrack(CityAudioState *audio, Jukebox *p, int current, double seconds) {
  NativeAudioStop(audio);
  if (current < 0) return true;
  if (!p->playing) return true;
#ifdef __APPLE__
  if (!audio->midi_available || !audio->assets) return false;
  ByteSpan track = GetAsset(audio->assets, CITY_ASSET_MUSIC_FIRST + current);
  CFDataRef midi = track.data && track.size <= LONG_MAX ?
      CFDataCreateWithBytesNoCopy(NULL, track.data, track.size, kCFAllocatorNull) : NULL;
  OSStatus status = midi ? NewMusicSequence(&audio->music_sequence) : -1;
  if (!status) status = MusicSequenceFileLoadData(audio->music_sequence, midi, kMusicSequenceFile_MIDIType, 0);
  if (midi) CFRelease(midi);
  if (!status) status = MusicSequenceSetAUGraph(audio->music_sequence, audio->music_graph);
  UInt32 tracks = 0;
  if (!status) status = MusicSequenceGetTrackCount(audio->music_sequence, &tracks);
  audio->music_end_beat = 0;
  for (UInt32 index = 0; !status && index < tracks; ++index) {
    MusicTrack track;
    MusicTimeStamp length = 0;
    UInt32 size = sizeof(length);
    status = MusicSequenceGetIndTrack(audio->music_sequence, index, &track);
    if (!status) status = MusicTrackGetProperty(track, kSequenceTrackProperty_TrackLength, &length, &size);
    if (length > audio->music_end_beat) audio->music_end_beat = length;
  }
  if (!status && audio->music_end_beat <= 0) status = -1;
  if (!status) status = MusicPlayerSetSequence(audio->music_player, audio->music_sequence);
  MusicTimeStamp beat = 0;
  if (!status && seconds) status = MusicSequenceGetBeatsForSeconds(audio->music_sequence, seconds, &beat);
  if (!status) status = MusicPlayerSetTime(audio->music_player, beat);
  if (!status) status = MusicPlayerPreroll(audio->music_player);
  if (!status) status = MusicPlayerStart(audio->music_player);
  if (status) {
    NativeAudioStop(audio);
    snprintf(p->error, sizeof(p->error), "MIDI error %d", (int)status);
    fprintf(stderr, "%s: %s\n", jukebox_tracks[current].file, p->error);
    return false;
  }
#elif defined(_WIN32)
  if (!audio->midi_available || !audio->assets ||
      !PlayWindowsMidi(audio->windows_midi, GetAsset(audio->assets, CITY_ASSET_MUSIC_FIRST + current), seconds,
          p->error, sizeof(p->error))) return false;
#else
  return false;
#endif
  p->error[0] = 0;
  if (++audio->next_music_device == 0 || audio->next_music_device == CITY_MUSIC_NO_TOKEN) audio->next_music_device = 1;
  audio->music_device = audio->next_music_device;
  audio->music_active = audio->music_notify = 1;
  return true;
}

bool NativeAudioPlay(CityAudioState *audio, Jukebox *p) {
  audio->interrupt_device = CITY_MUSIC_NO_TOKEN;
  audio->resume_music_track = p->interrupt_track = -1;
  if (JukeboxCurrent(p) < 0) p->playing = 0;
  bool played = PlayMusicTrack(audio, p, JukeboxCurrent(p), 0);
  if (!played) p->playing = 0;
  audio->music_enabled = p->playing != 0;
  return played;
}

static int MusicCompletion(CityAudioState *audio) {
#ifdef __APPLE__
  if (!audio->music_sequence || !audio->music_player) return 0;
  MusicTimeStamp beat;
  Boolean playing;
  if (MusicPlayerGetTime(audio->music_player, &beat) || MusicPlayerIsPlaying(audio->music_player, &playing))
    return CITY_MUSIC_FAILED;
  return !playing || beat >= audio->music_end_beat + 1 ? CITY_MUSIC_FINISHED : 0;
#elif defined(_WIN32)
  return WindowsMidiCompletion(audio->windows_midi);
#else
  return 0;
#endif
}

bool NativeAudioMusicFinished(CityAudioState *audio) {
  return MusicCompletion(audio) != 0;
}

void NativeAudioClose(CityAudioState *audio) {
  audio->interrupt_device = CITY_MUSIC_NO_TOKEN;
  audio->resume_music_track = -1;
  audio->midi_available = 0;
  NativeAudioStop(audio);
#ifdef __APPLE__
  if (audio->music_player) { DisposeMusicPlayer(audio->music_player); audio->music_player = NULL; }
  if (audio->music_graph) {
    AUGraphStop(audio->music_graph);
    DisposeAUGraph(audio->music_graph);
    audio->music_graph = NULL;
  }
  audio->music_mixer = audio->music_synth = NULL;
#elif defined(_WIN32)
  CloseWindowsMidi(audio->windows_midi);
  audio->windows_midi = NULL;
#endif
  StopEffect(audio);
  if (audio->effects_device) { SDL_CloseAudioDevice(audio->effects_device); audio->effects_device = 0; }
  free(audio->soundfont_path);
  audio->soundfont_path = NULL;
  audio->custom_soundfont = false;
  audio->click = (CityEffectVoice){0};
  audio->live_open = 0;
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void StopCityPlatformSound(Game *game) { PlayCityEffect(game, CITY_SOUND_NONE, CITY_EFFECT_STOP); }

void SetJukeboxPlaying(Game *game, bool playing) {
  game->ui->jukebox.playing = playing;
  NativeAudioPlay(game->audio, &game->ui->jukebox);
  DirtyCityMusicWindows(game);
}

// A short interruption stores a cursor, never a second player or decoded audio buffer.
static bool ResumeJukebox(Game *game) {
  CityAudioState *audio = game->audio;
  Jukebox *player = &game->ui->jukebox;
  int track = audio->resume_music_track;
  double seconds = audio->resume_music_seconds;
  audio->interrupt_device = CITY_MUSIC_NO_TOKEN;
  audio->resume_music_track = player->interrupt_track = -1;
  bool played = PlayMusicTrack(audio, player, track, seconds);
  if (!played) player->playing = 0;
  return played;
}

u32 InterruptJukebox(Game *game, int track) {
  CityAudioState *audio = game->audio;
  if (!audio->live_open) return 1;
  Jukebox *player = &game->ui->jukebox;
  if (!player->playing) return 1;
  if (track < 0 || track >= JUKEBOX_TRACKS) return 1;
  if (audio->interrupt_device == CITY_MUSIC_NO_TOKEN) {
    double seconds = 0;
#ifdef __APPLE__
    if (audio->music_sequence) {
      MusicTimeStamp beat;
      if (MusicPlayerGetTime(audio->music_player, &beat) ||
          MusicSequenceGetSecondsForBeats(audio->music_sequence, beat, &seconds)) return 1;
    }
#elif defined(_WIN32)
    if (!ReadWindowsMidiPosition(audio->windows_midi, &seconds)) return 1;
#endif
    audio->resume_music_track = JukeboxCurrent(player);
    audio->resume_music_seconds = seconds;
  }
  bool played = PlayMusicTrack(audio, player, track, 0);
  if (played) {
    audio->interrupt_device = audio->music_device;
    player->interrupt_track = track;
  } else {
    ResumeJukebox(game);
  }
  DirtyCityMusicWindows(game);
  return played ? 0 : 1;
}

void CompleteJukeboxTrack(Game *game, int notification, u16 device) {
  CityAudioState *audio = game->audio;
  if (device == CITY_MUSIC_NO_TOKEN || device != audio->music_device ||
      (notification != CITY_MUSIC_FINISHED && notification != CITY_MUSIC_FAILED)) return;
  Jukebox *player = &game->ui->jukebox;
  if (audio->interrupt_device == device) ResumeJukebox(game);
  else {
    if (notification == CITY_MUSIC_FINISHED) JukeboxStep(player, 1);
    else player->playing = 0;
    NativeAudioPlay(audio, player);
  }
  DirtyCityMusicWindows(game);
}

// Soundtrack and interruption completions share the same generation-token guard.
i32 NativeAudioMusicNotification(CityAudioState *audio, u16 *device) {
  if (!audio->music_active || !audio->music_notify || !device) return 0;
  int notification = MusicCompletion(audio);
  if (notification) {
    *device = audio->music_device;
    audio->music_notify = 0;
  }
  return notification;
}

// CoreAudio supplies native MIDI synthesis; its DLS instrument also accepts SF2 banks.
bool SetCitySoundFont(CityAudioState *audio, const char *path, bool enabled) {
  if (!path) path = audio->soundfont_path;
  if (enabled && (!path || !path[0] || strlen(path) >= 4096 || strchr(path, '\n') || strchr(path, '\r')))
    return false;
#ifdef __APPLE__
  char *copy = enabled && path != audio->soundfont_path ? strdup(path) : NULL;
  if (enabled && path != audio->soundfont_path && !copy) return false;
  const char *selected = enabled ? path :
      "/System/Library/Components/CoreAudio.component/Contents/Resources/gs_instruments.dls";
  CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)selected, strlen(selected), false);
  FSRef reference;
  bool loaded = url && audio->music_synth && CFURLGetFSRef(url, &reference) &&
      AudioUnitSetProperty(audio->music_synth, kMusicDeviceProperty_SoundBankFSRef,
          kAudioUnitScope_Global, 0, &reference, sizeof(reference)) == noErr;
  if (url) CFRelease(url);
  if (!loaded) { free(copy); return false; }
  if (copy) {
    free(audio->soundfont_path);
    audio->soundfont_path = copy;
  }
  audio->custom_soundfont = enabled;
  return true;
#else
  return false;
#endif
}
