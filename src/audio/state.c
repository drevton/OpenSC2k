#include "city/engine/clock.h"
#include "audio/state.h"

#include <stdlib.h>
#include <string.h>

#include "audio/types.h"
#include "base/file.h"

// WAV durations are converted once to the original 200 ms cadence units.
static const u16 kCitySoundDurationMilliseconds[CITY_SOUND_COUNT + 1] = {
  492, 153, 1485, 129, 1156, 221, 1064, 1657,
  1510, 1511, 2264, 886, 2766, 2203, 473, 625,
  782, 1223, 2444, 1308, 2468, 971, 2194, 1414,
  2338, 1165, 1937, 2359, 1510, 1613, 250,
};

// Stock indexes the repeat gate with the raw sound ID. Its bounded read lands
// in immutable runtime-class text, producing these thresholds rather than
// the duration table. Preserve those playback decisions without image reads.
static const CitySoundReplayThresholds kCitySoundRepeatCompatibilityThresholds = {{
  1852786243, 116, 1970422339, 26739, 1852133443,
  0, 1768179523, 1701470799, 29795, 0,
  1767985219, 1128559726, 0, 1852397379, 1148677988,
  67, 1768702787, 1148481125, 67, 4408387,
  1702057283, 1668826482, 1769238629, 28271, 0,
  1936020035, 1668445551, 1668826469, 1769238629, 28271,
}};

// Startup only: initialize a fresh Audio owner before opening devices/buffers.
void InitializeCityAudioState(CityAudioState *audio) {
  *audio = (CityAudioState){
    .queued_sound = -1,
    .last_sound_id = -1,
    .cached_sound_id = -1,
    .preloaded_sound_id = -1,
    .transient_sound_id = -1,
    .overlapping_effects = false,
    .ambient_sounds = true,
    .music_device = CITY_MUSIC_NO_TOKEN,
    .interrupt_device = CITY_MUSIC_NO_TOKEN,
    .music_filename = "",
    .music_directory = {"./sounds"},
    .replay_thresholds = kCitySoundRepeatCompatibilityThresholds,
  };
  NormalizeFilePath(audio->music_directory.text);
  for (int index = 0; index <= CITY_SOUND_COUNT; ++index)
    audio->timing.ticks[index] = kCitySoundDurationMilliseconds[index] / CITY_CADENCE_INTERVAL_MS + 1;
}

bool SetCitySoundFontPath(CityAudioState *audio, const char *path) {
  char *copy = strdup(path);
  if (!copy) return false;
  free(audio->soundfont_path);
  audio->soundfont_path = copy;
  return true;
}
