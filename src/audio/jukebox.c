#include "audio/jukebox.h"

#include <string.h>

// Playlist edits and shuffle use the player's own RNG, independently of City.

const JukeboxTrack kJukeboxTracks[JUKEBOX_TRACKS] = {
  {"OpenTTD journey", "tttheme2.mid"},
  {"Keep on Rolling", "keep_on_rolling.mid"},
  {"Downtown cab ride", "ttsong_iv_imuh3.mid"},
  {"Modern Motion", "modern_motion.mid"},
  {"Busy Schedule", "busy_schedule.mid"},
  {"The Fast Route", "the_fast_route.mid"},
  {"Skydive", "ttsong_iii_imuh3.mid"},
  {"Train filled with cash", "train_filled_with_cash.mid"},
  {"Unidentifed Flying Scotsman", "flying_scotsman.mid"},
  {"Chugga Chugga Cha-ching", "chuggachugga.mid"},
  {"The Hobo", "the_hobo_redfarn.mid"},
  {"Ultimate run", "ultimate_run.mid"},
  {"Midnight snow run", "midnight_snow_run.mid"},
  {"Excitement! Run for your life", "run_for_your_life.mid"},
  {"Coconut run 2", "coconut_run2.mid"},
  {"Harp harmony 'n humor", "harp_harmony.mid"},
  {"Mighty giant run", "mighty_giant_run.mid"},
  {"Wood whistles", "wood_whistles.mid"},
  {"Linn's basket of treats", "linns_basket.mid"},
  {"Plain simple relax song", "relax_song.mid"},
  {"Deep down chemistry lab", "chemistry_lab.mid"},
  {"5 4 3 2 .. Gone", "5432gone_redfarn.mid"},
  {"Boogie Marabi", "boogi_marabi_redfarn.mid"},
  {"Moo Moo Boogie", "moo_redfarn.mid"},
  {"Say What", "say_what_redfarn.mid"},
  {"Be Sharp Boogie", "be_sharp_bw_redfarn.mid"},
  {"Careless Love", "careless_perc_redfarn.mid"},
  {"Mosey Along", "mosey_along_redfarn.mid"},
  {"Slow and Easy", "slow_neasy_redfarn.mid"},
  {"City Blues", "city_blues_redfarn.mid"},
  {"No Work Song", "no_work_song_redfarn.mid"},
};
const char *const kJukeboxProgrammes[JUKEBOX_PROGRAMMES] = {
  "All", "Old Style", "New Style", "Ezy Street", "Custom 1", "Custom 2"
};

static u32 NextJukeboxRandom(Jukebox *p) {
  u32 x = p->random;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  return p->random = x;
}

i32 JukeboxCurrent(const Jukebox *p) {
  return p->count && p->position >= 0 && p->position < p->count ? p->order[p->position] : -1;
}

static void RebuildJukeboxProgramme(Jukebox *p, i32 preserve) {
  p->count = 0;
  if (p->programme >= JUKEBOX_PROGRAMME_CUSTOM_1) {
    p->count = p->custom_count[p->programme - JUKEBOX_PROGRAMME_CUSTOM_1];
    memcpy(p->order, p->custom[p->programme - JUKEBOX_PROGRAMME_CUSTOM_1], (size_t)p->count);
  } else {
    i32 first = p->programme == JUKEBOX_PROGRAMME_ALL ? 1 :
        (p->programme - JUKEBOX_PROGRAMME_OLD_STYLE) * 10 + 1;
    i32 last = p->programme == JUKEBOX_PROGRAMME_ALL ? 30 : first + 9;
    for (int track = first; track <= last; ++track)
      p->order[p->count++] = (u8)track;
  }
  if (p->shuffle)
    for (i32 index = p->count - 1; index > 0; --index) {
      i32 other = (i32)(NextJukeboxRandom(p) % (u32)(index + 1));
      int track = p->order[index]; p->order[index] = p->order[other]; p->order[other] = track;
    }
  p->position = 0;
  for (i32 index = 0; index < p->count; ++index)
    if (p->order[index] == preserve) { p->position = index; break; }
}

void JukeboxInit(Jukebox *p, u32 seed) {
  memset(p, 0, sizeof(*p));
  p->interrupt_track = -1;
  p->random = seed ? seed : 1;
  p->volume = 50;
  p->effects_volume = 80;
  RebuildJukeboxProgramme(p, -1);
}

void JukeboxSelect(Jukebox *p, i32 programme) {
  if (programme < JUKEBOX_PROGRAMME_ALL || programme >= JUKEBOX_PROGRAMMES) return;
  p->programme = programme;
  RebuildJukeboxProgramme(p, -1);
}

void SelectJukeboxTrack(Jukebox *p, int track) {
  if (track < 0 || track >= JUKEBOX_TRACKS) return;
  for (int row = 0; row < p->count; ++row)
    if (p->order[row] == track) {
      p->position = row;
      p->playing = 1;
      return;
    }
  JukeboxSelect(p, JUKEBOX_PROGRAMME_ALL);
  for (int row = 0; row < p->count; ++row)
    if (p->order[row] == track) { p->position = row; p->playing = 1; return; }
  // The OpenMSX title theme is listed, but excluded from automatic programmes.
  if (track == 0) {
    p->position = p->count;
    p->order[p->count++] = 0;
    p->playing = 1;
  }
}

void JukeboxShuffle(Jukebox *p) {
  i32 current = JukeboxCurrent(p);
  p->shuffle = !p->shuffle;
  RebuildJukeboxProgramme(p, current);
}
void JukeboxStep(Jukebox *p, i32 direction) {
  if (p->count) p->position = (p->position + direction % p->count + p->count) % p->count;
}
void JukeboxAdd(Jukebox *p, i32 track) {
  if (p->programme < JUKEBOX_PROGRAMME_CUSTOM_1 || track < 0 || track >= JUKEBOX_TRACKS) return;
  i32 custom = p->programme - JUKEBOX_PROGRAMME_CUSTOM_1, count = p->custom_count[custom];
  if (count == JUKEBOX_CAPACITY) return;
  p->custom[custom][count] = (u8)track;
  ++p->custom_count[custom];
  // Insert into the shuffled order without changing the current song.
  i32 insert = p->shuffle && p->count ? (i32)(NextJukeboxRandom(p) % (u32)(p->count + 1)) : p->count;
  memmove(p->order + insert + 1, p->order + insert, (size_t)(p->count - insert));
  p->order[insert] = (u8)track;
  if (p->count && insert <= p->position) ++p->position;
  ++p->count;
}
void JukeboxRemove(Jukebox *p, i32 row) {
  if (p->programme < JUKEBOX_PROGRAMME_CUSTOM_1 || row < 0 || row >= p->count) return;
  i32 custom = p->programme - JUKEBOX_PROGRAMME_CUSTOM_1, index = 0;
  if (p->shuffle) {
    while (index < p->custom_count[custom] && p->custom[custom][index] != p->order[row]) ++index;
  } else index = row;
  if (index == p->custom_count[custom]) return;
  memmove(p->custom[custom] + index, p->custom[custom] + index + 1,
      (size_t)(--p->custom_count[custom] - index));
  memmove(p->order + row, p->order + row + 1, (size_t)(--p->count - row));
  if (row < p->position) --p->position;
  if (p->position >= p->count) p->position = 0;
}
void JukeboxClear(Jukebox *p) {
  if (p->programme < JUKEBOX_PROGRAMME_CUSTOM_1) return;
  p->custom_count[p->programme - JUKEBOX_PROGRAMME_CUSTOM_1] = p->count = p->position = 0;
}
