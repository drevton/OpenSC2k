#define WIN32_LEAN_AND_MEAN
#define MulDiv WindowsMulDiv
#include <windows.h>
#include <mmsystem.h>
#undef MulDiv
#include <SDL.h>
#include <limits.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "audio/windows_midi.h"
#include "base/byte_order.h"

// WinMM schedules the stream, while a worker refills two small buffers. Track cursors borrow Assets bytes;
// no complete-song event array, temporary MIDI file, window callback or mutable global is needed.
enum { kMidiTracks = 64, kMidiBufferWords = 256 };
typedef struct {
  const u8 *at, *end;
  ByteSpan sysex;
  u32 tick, message;
  u8 running, prefix;
  bool ready, ended;
} MidiTrack;
typedef struct {
  MIDIHDR header;
  DWORD words[kMidiBufferWords];
  bool prepared, queued;
} MidiBuffer;
struct WindowsMidi {
  HMIDISTRM stream;
  HANDLE wake;
  SDL_Thread *worker;
  SDL_atomic_t stop, status;
  MidiTrack tracks[kMidiTracks];
  MidiBuffer buffers[2];
  int track_count;
  u32 division, tempo, source_tick, output_tick, remainder;
  u64 source_us;
  double start_seconds;
};

// A MIDI variable-length quantity contains at most four seven-bit groups.
static bool ReadMidiQuantity(const u8 **at, const u8 *end, u32 *value) {
  *value = 0;
  for (int i = 0; i < 4 && *at < end; ++i) {
    u8 byte = *(*at)++;
    *value = *value << 7 | (byte & 127);
    if (!(byte & 128)) return true;
  }
  return false;
}

// Decode one timed channel, tempo, SysEx or end event, skipping other metadata without losing its tick deltas.
static bool AdvanceMidiTrack(MidiTrack *track) {
  track->ready = false;
  while (track->at < track->end) {
    u32 delta;
    if (!ReadMidiQuantity(&track->at, track->end, &delta) || delta > UINT32_MAX - track->tick ||
        track->at == track->end) return false;
    track->tick += delta;
    u8 status = *track->at;
    if (status < 128) status = track->running;
    else ++track->at;
    track->sysex = (ByteSpan){0};
    track->prefix = 0;
    if (status >= 0x80 && status < 0xF0) {
      int bytes = status >> 4 == 12 || status >> 4 == 13 ? 1 : 2;
      if (track->end - track->at < bytes || track->at[0] >= 128 || (bytes == 2 && track->at[1] >= 128))
        return false;
      track->running = status;
      track->message = status | (u32)track->at[0] << 8;
      if (bytes == 2) track->message |= (u32)track->at[1] << 16;
      track->at += bytes;
    } else if (status == 0xFF) {
      if (track->at == track->end) return false;
      int type = *track->at++;
      u32 bytes;
      if (!ReadMidiQuantity(&track->at, track->end, &bytes) || bytes > (size_t)(track->end - track->at))
        return false;
      const u8 *payload = track->at;
      track->at += bytes;
      if (type == 0x51) {
        if (bytes != 3) return false;
        u32 tempo = (u32)payload[0] << 16 | (u32)payload[1] << 8 | payload[2];
        if (!tempo) return false;
        track->message = (u32)MEVT_TEMPO << 24 | tempo;
      } else if (type == 0x2F) {
        if (bytes || track->at != track->end) return false;
        track->ended = true;
        track->message = (u32)MEVT_NOP << 24;
      } else continue;
    } else if (status == 0xF0 || status == 0xF7) {
      u32 bytes;
      if (!ReadMidiQuantity(&track->at, track->end, &bytes) || bytes > (size_t)(track->end - track->at) ||
          bytes + (status == 0xF0) > (kMidiBufferWords - 3) * sizeof(DWORD)) return false;
      track->running = 0;
      track->prefix = status == 0xF0;
      track->sysex = (ByteSpan){track->at, bytes};
      track->at += bytes;
      track->message = (u32)MEVT_LONGMSG << 24 | (bytes + track->prefix);
    } else return false;
    track->ready = true;
    return true;
  }
  return track->ended;
}

// Merge format-1 tracks by tick; equal-time events retain their file track order.
static int NextMidiTrack(const WindowsMidi *midi) {
  int selected = -1;
  for (int i = 0; i < midi->track_count; ++i)
    if (midi->tracks[i].ready && (selected < 0 || midi->tracks[i].tick < midi->tracks[selected].tick)) selected = i;
  return selected;
}

static bool ReadMidiTracks(WindowsMidi *midi, ByteSpan song) {
  if (!song.data || song.size < 14 || memcmp(song.data, "MThd", 4)) return false;
  u32 size = ReadBE32(song.data + 4);
  u16 format = ReadBE16(song.data + 8), tracks = ReadBE16(song.data + 10), division = ReadBE16(song.data + 12);
  if (size < 6 || size > song.size - 8 || format > 1 || !tracks || tracks > kMidiTracks ||
      (!format && tracks != 1) || !division || (division & 0x8000)) return false;
  const u8 *at = song.data + 8 + size, *end = song.data + song.size;
  midi->track_count = tracks;
  midi->division = division;
  midi->tempo = 500000;
  midi->source_tick = midi->output_tick = midi->remainder = 0;
  midi->source_us = 0;
  for (int i = 0; i < tracks; ++i) {
    if (end - at < 8 || memcmp(at, "MTrk", 4)) return false;
    u32 bytes = ReadBE32(at + 4);
    at += 8;
    if (bytes > (size_t)(end - at)) return false;
    midi->tracks[i] = (MidiTrack){.at = at, .end = at + bytes};
    if (!AdvanceMidiTrack(&midi->tracks[i])) return false;
    at += bytes;
  }
  return at == end;
}

// Fill a reusable WinMM buffer. The driver never sees pointers into the MIDI file.
static bool FillMidiBuffer(WindowsMidi *midi, MidiBuffer *buffer) {
  int used = 0, selected;
  while ((selected = NextMidiTrack(midi)) >= 0) {
    MidiTrack *track = &midi->tracks[selected];
    int payload = track->message >> 24 == MEVT_LONGMSG ? (track->sysex.size + track->prefix + 3) / 4 : 0;
    if (used + 3 + payload > kMidiBufferWords) break;
    DWORD *event = buffer->words + used;
    event[0] = track->tick - midi->output_tick;
    event[1] = 0;
    event[2] = track->message;
    if (payload) {
      memset(event + 3, 0, payload * sizeof(DWORD));
      u8 *bytes = (u8 *)(event + 3);
      if (track->prefix) *bytes++ = 0xF0;
      memcpy(bytes, track->sysex.data, track->sysex.size);
    }
    used += 3 + payload;
    midi->output_tick = track->tick;
    if (!AdvanceMidiTrack(track)) return false;
  }
  buffer->header.dwBufferLength = buffer->header.dwBytesRecorded = used * sizeof(DWORD);
  if (used && midiStreamOut(midi->stream, &buffer->header, sizeof(buffer->header))) return false;
  buffer->queued = used != 0;
  return true;
}

static int RunWindowsMidi(void *context) {
  WindowsMidi *midi = context;
  while (!SDL_AtomicGet(&midi->stop)) {
    bool pending = false;
    for (int i = 0; i < 2; ++i) {
      MidiBuffer *buffer = &midi->buffers[i];
      if (buffer->queued && !(*(volatile DWORD *)&buffer->header.dwFlags & MHDR_DONE)) {
        pending = true;
        continue;
      }
      if (!FillMidiBuffer(midi, buffer)) {
        SDL_AtomicSet(&midi->status, CITY_MUSIC_FAILED);
        return 0;
      }
      pending |= buffer->queued;
    }
    if (!pending) {
      SDL_AtomicSet(&midi->status, CITY_MUSIC_FINISHED);
      return 0;
    }
    if (WaitForSingleObject(midi->wake, INFINITE) != WAIT_OBJECT_0) {
      SDL_AtomicSet(&midi->status, CITY_MUSIC_FAILED);
      return 0;
    }
  }
  return 0;
}

WindowsMidi *OpenWindowsMidi(char *error, size_t capacity) {
  WindowsMidi *midi = calloc(1, sizeof(*midi));
  if (!midi) { snprintf(error, capacity, "Not enough memory for MIDI playback"); return NULL; }
  midi->wake = CreateEventA(NULL, FALSE, FALSE, NULL);
  UINT device = MIDI_MAPPER;
  MMRESULT result = midi->wake ? midiStreamOpen(&midi->stream, &device, 1,
      (DWORD_PTR)midi->wake, 0, CALLBACK_EVENT) : MMSYSERR_NOMEM;
  if (result) {
    midiOutGetErrorTextA(result, error, capacity);
    if (midi->wake) CloseHandle(midi->wake);
    free(midi);
    return NULL;
  }
  return midi;
}

// Join the feeder before resetting or releasing headers; queued memory remains owned until WinMM returns it.
bool StopWindowsMidi(WindowsMidi *midi) {
  if (!midi) return true;
  SDL_AtomicSet(&midi->stop, 1);
  SetEvent(midi->wake);
  if (midi->worker) { SDL_WaitThread(midi->worker, NULL); midi->worker = NULL; }
  bool valid = midiStreamStop(midi->stream) == MMSYSERR_NOERROR;
  valid = midiOutReset((HMIDIOUT)midi->stream) == MMSYSERR_NOERROR && valid;
  for (int channel = 0; channel < 16; ++channel) {
    valid = midiOutShortMsg((HMIDIOUT)midi->stream, 0xB0 | channel | 64 << 8) == MMSYSERR_NOERROR && valid;
    valid = midiOutShortMsg((HMIDIOUT)midi->stream, 0xB0 | channel | 120 << 8) == MMSYSERR_NOERROR && valid;
  }
  for (int i = 0; i < 2; ++i) {
    MidiBuffer *buffer = &midi->buffers[i];
    if (buffer->prepared) {
      if (midiOutUnprepareHeader((HMIDIOUT)midi->stream, &buffer->header, sizeof(buffer->header))) valid = false;
      else buffer->prepared = false;
    }
    buffer->queued = false;
  }
  SDL_AtomicSet(&midi->status, valid ? CITY_MUSIC_PENDING : CITY_MUSIC_FAILED);
  return valid;
}

void CloseWindowsMidi(WindowsMidi *midi) {
  if (!midi) return;
  if (!StopWindowsMidi(midi) || midiStreamClose(midi->stream)) {
    // A failed driver must not retain a pointer into freed queue memory.
    fputs("Could not release the Windows MIDI stream\n", stderr);
    return;
  }
  CloseHandle(midi->wake);
  free(midi);
}

// During a seek, send instrument setup SysEx before chasing later channel state, using the stopped queue's storage.
static bool ChaseMidiSysex(WindowsMidi *midi, const MidiTrack *track) {
  MidiBuffer *buffer = &midi->buffers[0];
  size_t size = track->sysex.size + track->prefix;
  if (!size) return true;
  u8 *bytes = (u8 *)buffer->words;
  if (track->prefix) *bytes++ = 0xF0;
  memcpy(bytes, track->sysex.data, track->sysex.size);
  buffer->header = (MIDIHDR){.lpData = (LPSTR)buffer->words, .dwBufferLength = size};
  if (midiOutPrepareHeader((HMIDIOUT)midi->stream, &buffer->header, sizeof(buffer->header))) return false;
  buffer->prepared = true;
  bool valid = midiOutLongMsg((HMIDIOUT)midi->stream, &buffer->header, sizeof(buffer->header)) == MMSYSERR_NOERROR;
  while (valid && !(*(volatile DWORD *)&buffer->header.dwFlags & MHDR_DONE))
    valid = WaitForSingleObject(midi->wake, 1000) == WAIT_OBJECT_0;
  if (!valid) midiOutReset((HMIDIOUT)midi->stream);
  if (midiOutUnprepareHeader((HMIDIOUT)midi->stream, &buffer->header, sizeof(buffer->header))) return false;
  buffer->prepared = false;
  return valid;
}

// Resume by chasing program/controllers and held notes through the tempo map. Seeking never advances City time.
static bool SeekWindowsMidi(WindowsMidi *midi, u64 target) {
  u8 notes[16][128] = {{0}}, sustain[16] = {0};
  int selected;
  while ((selected = NextMidiTrack(midi)) >= 0) {
    MidiTrack *track = &midi->tracks[selected];
    u64 elapsed = (u64)(track->tick - midi->source_tick) * midi->tempo + midi->remainder;
    if (midi->source_us + elapsed / midi->division > target) break;
    midi->source_us += elapsed / midi->division;
    midi->remainder = elapsed % midi->division;
    midi->source_tick = track->tick;
    u32 message = track->message;
    if (message >> 24 == MEVT_TEMPO) midi->tempo = message & 0xFFFFFF;
    else if (message >> 24 == MEVT_LONGMSG) {
      if (!ChaseMidiSysex(midi, track)) return false;
    }
    else if (message >> 24 == MEVT_SHORTMSG) {
      int channel = message & 15, type = message & 0xF0;
      int a = message >> 8 & 127, b = message >> 16 & 127;
      if (type == 0x80 || (type == 0x90 && !b)) {
        if (sustain[channel]) notes[channel][a] |= 128;
        else notes[channel][a] = 0;
      } else if (type == 0x90) notes[channel][a] = b;
      else {
        if (type == 0xB0) {
          if (a == 64 || a == 121) {
            sustain[channel] = a == 64 && b >= 64;
            if (!sustain[channel])
              for (int key = 0; key < 128; ++key) if (notes[channel][key] & 128) notes[channel][key] = 0;
          }
          if (a == 120 || (a >= 123 && !sustain[channel])) memset(notes[channel], 0, sizeof(notes[channel]));
          else if (a >= 123)
            for (int key = 0; key < 128; ++key) if (notes[channel][key]) notes[channel][key] |= 128;
        }
        if (midiOutShortMsg((HMIDIOUT)midi->stream, message)) return false;
      }
    }
    if (!AdvanceMidiTrack(track)) return false;
  }
  if (selected < 0) midi->output_tick = midi->source_tick;
  else {
    u64 ticks = (target - midi->source_us) * midi->division / midi->tempo;
    u32 remaining = midi->tracks[selected].tick - midi->source_tick;
    if (ticks > remaining) ticks = remaining;
    midi->output_tick = midi->source_tick + ticks;
  }
  midi->start_seconds = (midi->source_us +
      (u64)(midi->output_tick - midi->source_tick) * midi->tempo / midi->division) / 1000000.0;
  for (int channel = 0; channel < 16; ++channel)
    for (int key = 0; key < 128; ++key) {
      int note = notes[channel][key];
      if (!(note & 127)) continue;
      if (midiOutShortMsg((HMIDIOUT)midi->stream,
          0x90 | channel | (u32)key << 8 | (u32)(note & 127) << 16)) return false;
      if ((note & 128) && midiOutShortMsg((HMIDIOUT)midi->stream, 0x80 | channel | (u32)key << 8)) return false;
    }
  return true;
}

bool PlayWindowsMidi(WindowsMidi *midi, ByteSpan song, double seconds, char *error, size_t capacity) {
  assert(midi && !midi->worker && !midi->buffers[0].prepared && !midi->buffers[1].prepared);
  if (!ReadMidiTracks(midi, song)) {
    snprintf(error, capacity, "Invalid or unsupported MIDI file");
    return false;
  }
  assert(seconds >= 0);
  midi->start_seconds = 0;
  if (seconds > 0 && !SeekWindowsMidi(midi, (u64)(seconds * 1000000))) {
    snprintf(error, capacity, "Could not restore MIDI playback position");
    StopWindowsMidi(midi);
    return false;
  }
  MIDIPROPTIMEDIV division = {sizeof(division), midi->division};
  MIDIPROPTEMPO tempo = {sizeof(tempo), midi->tempo};
  MMRESULT result = midiStreamProperty(midi->stream, (LPBYTE)&division, MIDIPROP_SET | MIDIPROP_TIMEDIV);
  if (!result) result = midiStreamProperty(midi->stream, (LPBYTE)&tempo, MIDIPROP_SET | MIDIPROP_TEMPO);
  for (int i = 0; !result && i < 2; ++i) {
    MidiBuffer *buffer = &midi->buffers[i];
    buffer->header = (MIDIHDR){.lpData = (LPSTR)buffer->words, .dwBufferLength = sizeof(buffer->words)};
    result = midiOutPrepareHeader((HMIDIOUT)midi->stream, &buffer->header, sizeof(buffer->header));
    if (!result) buffer->prepared = true;
    if (!result && !FillMidiBuffer(midi, buffer)) result = MMSYSERR_ERROR;
  }
  SDL_AtomicSet(&midi->status, CITY_MUSIC_PENDING);
  SDL_AtomicSet(&midi->stop, 0);
  if (!result) {
    midi->worker = SDL_CreateThread(RunWindowsMidi, "MIDI stream", midi);
    if (!midi->worker) result = MMSYSERR_NOMEM;
  }
  if (!result) result = midiStreamRestart(midi->stream);
  if (result) {
    midiOutGetErrorTextA(result, error, capacity);
    StopWindowsMidi(midi);
    return false;
  }
  error[0] = 0;
  return true;
}

void SetWindowsMidiVolume(WindowsMidi *midi, int volume) {
  if (!midi) return;
  DWORD gain = volume * 65535 / 100;
  midiOutSetVolume((HMIDIOUT)midi->stream, gain | gain << 16);
}

bool ReadWindowsMidiPosition(WindowsMidi *midi, double *seconds) {
  if (!midi) return false;
  MMTIME position = {.wType = TIME_MS};
  if (midiStreamPosition(midi->stream, &position, sizeof(position)) || position.wType != TIME_MS) return false;
  *seconds = midi->start_seconds + position.u.ms / 1000.0;
  return true;
}

CityMusicCompletion WindowsMidiCompletion(WindowsMidi *midi) {
  return midi ? SDL_AtomicGet(&midi->status) : CITY_MUSIC_FAILED;
}
