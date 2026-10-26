#include "audio/backend.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include "audio/windows_midi.h"
#endif

#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
#endif

#include "audio/effects.h"
#include "audio/state.h"
#include "assets/assets.h"

// Coordinate native audio devices and platform MIDI playback.

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

bool NativeAudioOpen(CityAudioState *audio) {
  Jukebox *p = &audio->jukebox;
  audio->live_open = 1;
  audio->overlapping_effects = true;
  audio->music_background = audio->music_focused = true;
  // Background and in-game music share the selected jukebox player.
  p->playing = 1;
  audio->interrupt_device = CITY_MUSIC_NO_TOKEN;
  audio->resume_music_track = -1;
  audio->master_volume = 100;
  OpenCityEffectMixer(audio);
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
  NativeAudioVolume(audio);
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

// Apply only the music bus gain. Focus changes leave playback cursors and effect volume alone.
static void ApplyCityMusicVolume(CityAudioState *audio) {
#ifdef __APPLE__
  if (audio->music_mixer) AudioUnitSetParameter(audio->music_mixer, kMultiChannelMixerParam_Volume,
      kAudioUnitScope_Output, 0, audio->music_background || audio->music_focused ? audio->music_volume / 100.0f : 0, 0);
#elif defined(_WIN32)
  SetWindowsMidiVolume(audio->windows_midi, audio->music_background || audio->music_focused ? audio->music_volume : 0);
#else
  (void)audio;
#endif
}

void NativeAudioVolume(CityAudioState *audio) {
  const Jukebox *p = &audio->jukebox;
  SetCityEffectMixerVolume(audio, p->effects_volume * audio->master_volume / 100);
  audio->music_volume = p->volume * audio->master_volume / 100;
  ApplyCityMusicVolume(audio);
}

void SetCityAudioFocus(CityAudioState *audio, bool focused) {
  audio->music_focused = focused;
  ApplyCityMusicVolume(audio);
}

// Read the current track cursor in seconds. Returns false on a position-read error.
static bool NativeAudioMusicPosition(CityAudioState *audio, double *seconds) {
  *seconds = 0;
#ifdef __APPLE__
  if (audio->music_sequence) {
    MusicTimeStamp beat;
    if (MusicPlayerGetTime(audio->music_player, &beat) ||
        MusicSequenceGetSecondsForBeats(audio->music_sequence, beat, seconds)) return false;
  }
#elif defined(_WIN32)
  if (!ReadWindowsMidiPosition(audio->windows_midi, seconds)) return false;
#endif
  return true;
}

// Replace the current sequence with a catalog track and starting cursor. A negative track clears playback.
static bool NativeAudioPlayTrack(CityAudioState *audio, int current, double seconds) {
  Jukebox *p = &audio->jukebox;
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
    fprintf(stderr, "%s: %s\n", kJukeboxTracks[current].file, p->error);
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

bool NativeAudioPlay(CityAudioState *audio) {
  Jukebox *p = &audio->jukebox;
  audio->interrupt_device = CITY_MUSIC_NO_TOKEN;
  audio->resume_music_track = p->interrupt_track = -1;
  int current = JukeboxCurrent(p);
  if (current < 0) p->playing = 0;
  bool played = NativeAudioPlayTrack(audio, current, 0);
  if (!played) p->playing = 0;
  audio->music_enabled = p->playing != 0;
  return played;
}

// A short interruption stores a cursor, never a second player or decoded audio buffer.
static bool ResumeJukebox(CityAudioState *audio) {
  Jukebox *player = &audio->jukebox;
  int track = audio->resume_music_track;
  double seconds = audio->resume_music_seconds;
  audio->interrupt_device = CITY_MUSIC_NO_TOKEN;
  audio->resume_music_track = player->interrupt_track = -1;
  bool played = NativeAudioPlayTrack(audio, track, seconds);
  if (!played) player->playing = 0;
  return played;
}

CityMusicInterruptResult NativeAudioInterrupt(CityAudioState *audio, int track) {
  if (!audio->live_open) return CITY_MUSIC_INTERRUPT_IGNORED;
  Jukebox *player = &audio->jukebox;
  if (!player->playing) return CITY_MUSIC_INTERRUPT_IGNORED;
  if (track < 0 || track >= JUKEBOX_TRACKS) return CITY_MUSIC_INTERRUPT_IGNORED;
  if (audio->interrupt_device == CITY_MUSIC_NO_TOKEN) {
    double seconds;
    if (!NativeAudioMusicPosition(audio, &seconds)) return CITY_MUSIC_INTERRUPT_IGNORED;
    audio->resume_music_track = JukeboxCurrent(player);
    audio->resume_music_seconds = seconds;
  }
  bool played = NativeAudioPlayTrack(audio, track, 0);
  if (played) {
    audio->interrupt_device = audio->music_device;
    player->interrupt_track = track;
  } else {
    ResumeJukebox(audio);
  }
  return played ? CITY_MUSIC_INTERRUPT_STARTED : CITY_MUSIC_INTERRUPT_FAILED;
}

bool NativeAudioCompleteMusic(CityAudioState *audio, CityMusicCompletion notification, u16 device) {
  if (device == CITY_MUSIC_NO_TOKEN || device != audio->music_device ||
      (notification != CITY_MUSIC_FINISHED && notification != CITY_MUSIC_FAILED)) return false;
  Jukebox *player = &audio->jukebox;
  if (audio->interrupt_device == device) ResumeJukebox(audio);
  else {
    if (notification == CITY_MUSIC_FINISHED) JukeboxStep(player, 1);
    else player->playing = 0;
    NativeAudioPlay(audio);
  }
  return true;
}

static CityMusicCompletion MusicCompletion(CityAudioState *audio) {
#ifdef __APPLE__
  if (!audio->music_sequence || !audio->music_player) return CITY_MUSIC_PENDING;
  MusicTimeStamp beat;
  Boolean playing;
  if (MusicPlayerGetTime(audio->music_player, &beat) || MusicPlayerIsPlaying(audio->music_player, &playing))
    return CITY_MUSIC_FAILED;
  return !playing || beat >= audio->music_end_beat + 1 ? CITY_MUSIC_FINISHED : CITY_MUSIC_PENDING;
#elif defined(_WIN32)
  return WindowsMidiCompletion(audio->windows_midi);
#else
  return CITY_MUSIC_PENDING;
#endif
}

void NativeAudioClose(CityAudioState *audio) {
  if (audio->live_open) {
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
    CloseCityEffectMixer(audio);
  }
  free(audio->soundfont_path);
  audio->soundfont_path = NULL;
  audio->custom_soundfont = false;
  audio->live_open = 0;
}

// Polling captures the token; the completion controller checks whether it still identifies live playback.
CityMusicNotification NativeAudioMusicNotification(CityAudioState *audio) {
  CityMusicNotification result = {CITY_MUSIC_PENDING, CITY_MUSIC_NO_TOKEN};
  if (!audio->music_active || !audio->music_notify) return result;
  result.completion = MusicCompletion(audio);
  if (result.completion != CITY_MUSIC_PENDING) {
    result.token = audio->music_device;
    audio->music_notify = 0;
  }
  return result;
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
  // DLSMusicDevice exposes its bank URL as read-only; bank selection still requires the FSRef setter.
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
