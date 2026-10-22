#ifndef SC2K_CITY_CHEATS_H
#define SC2K_CITY_CHEATS_H

#include "city/types.h"

typedef enum CityCheat {
  CITY_CHEAT_FUND, CITY_CHEAT_CASS, CITY_CHEAT_THE_WORKS, CITY_CHEAT_NOAH, CITY_CHEAT_MOSES,
  CITY_CHEAT_FIRE_STORM, CITY_CHEAT_DEBUG, CITY_CHEAT_MILITARY, CITY_CHEAT_JOKE, CITY_CHEAT_WEBB,
  CITY_CHEAT_OOPS, CITY_CHEAT_REPENT, CITY_CHEAT_COUNT
} CityCheat;

enum {
  CITY_CHEAT_CONFIRM_DEMOLITION = 1,
  CITY_CHEAT_ACCEPT_MILITARY = 2,
  CITY_CHEAT_DECLINE_MILITARY = 4,
};

// These handlers receive supported command flags and do not use the command's map location.
// Cheat choices are resolved command input. TEST validates against live City state without RNG or effects;
// demolition and military choices can require confirmation. Execution retains each cheat's original result.
i64 CmdCheat(City *city, u32 flags, int cheat, int options, int reserved);

// The other cheat actions require all three reserved fields to be zero.
// TEST only checks admission. Debug mode changes cash, newspaper and ranking state; inventions honor their fix.
i64 CmdEnableDebugCity(City *city, u32 flags, int reserved1, int reserved2, int reserved3);
i64 CmdAllInventions(City *city, u32 flags, int reserved1, int reserved2, int reserved3);
i64 CmdAddCityCashBonus(City *city, u32 flags, int reserved1, int reserved2, int reserved3);
i64 CmdGrantSpecialFacilitiesAndCash(City *city, u32 flags, int reserved1, int reserved2, int reserved3);

// Unconditional reward/cash effect used by the cheat command and the original comparison menu adapter.
// The caller owns admission and command completion. Unlock/status/sound feedback precedes cash;
// the final toolbar refresh follows it.
void GrantAllSpecialFacilitiesAndCash(City *city);

#endif
