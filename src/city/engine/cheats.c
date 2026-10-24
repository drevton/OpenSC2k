#include "city/engine/cheats.h"

#include <string.h>

#include "game/audio.h"
#include "audio/types.h"
#include "city/disasters/disasters.h"
#include "city/economy/budget.h"
#include "city/economy/economy.h"
#include "city/economy/facilities.h"
#include "city/engine/commands.h"
#include "city/engine/random.h"
#include "city/maps/buildings.h"
#include "city/maps/labels.h"
#include "city/maps/layers.h"
#include "city/maps/map.h"
#include "city/military.h"
#include "city/presentation.h"
#include "city/state.h"
#include "game/errors.h"
#include "game/update.h"

// The original cash-only menu command has no other game or host effects. Uses clamped signed treasury arithmetic;
// caller submits the shared action and owns any status refresh.
static void Add500000ToCityCash(City *city) {
  city->indicators.cash = AddInt64Clamped(city->indicators.cash, CITY_CASH_MENU_BONUS);
}

// Grant rewards and cash; update the owned city status presentation. Unlocks all rewards and adds the fixed cash bonus
// with clamping; requires attached presentation owners for its feedback.
void GrantAllSpecialFacilitiesAndCash(City *city) {
  memset(&city->economic_tables.milestone_years, 0, sizeof(city->economic_tables.milestone_years));
  city->unlocks.disaster_level = 6;
  RebuildCityUnlockState(city);
  for (int facility = 0; facility < 5; ++facility)
    SetSpecialFacilityAvailability(city, facility, 1);
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  PlayCitySound(city, INVALID_TILE, CITY_SOUND_CIVIC_PLACEMENT);
  city->indicators.cash = AddInt64Clamped(city->indicators.cash, CITY_CASH_MENU_BONUS);
  RefreshCityRewardToolbar(city);
}

// Find or create the cheat's bed-and-breakfast house using deterministic City state. May consume RNG and edit tiles;
// the Boolean result reports whether the house effect succeeded.
static bool BuildCheatHouse(City *city) {
  TileIndex house = city->cheats.house ? city->cheats.house - 1 : INVALID_TILE;
  if (IsMapTileValid(city, house) && city->tile[house].kind == TK_BED_BREAKFAST_INN &&
      !ReadCityZoneKind(&city->tile[house])) {
    CityPoint point = MapTilePt(city, house);
    CenterCityMapOnEvent(city, point.x, point.y);
    return true;
  }
  for (int attempt = 0; attempt < 100; ++attempt) {
    int x = 1 + Sc2kCrtRandRange(city, city->map_width - 1);
    int y = 1 + Sc2kCrtRandRange(city, city->map_height - 1);
    TileIndex tile = MapTileXY(city, x, y), wind = MapTileXY(city, x - 1, y), sign = MapTileXY(city, x, y - 1);
    TileIndex cells[] = {tile, wind, sign};
    bool available = true;
    int height = ReadCityLandHeight(&city->tile[tile]);
    for (int i = 0; i < 3; ++i) {
      const TileInfo *tp = &city->tile[cells[i]];
      if (!IsFlatDryUnbuiltTile(tp) || ReadUnderground(city, cells[i]) || ReadCityZoneKind(tp) ||
          ReadCityLandHeight(tp) != height) available = false;
    }
    if (!available || PlaceSpecialZoneFootprint(city, tile, TK_BED_BREAKFAST_INN, 1) < 0) continue;
    PlaceSpecialZoneFootprint(city, wind, TK_WIND_POWER, 1);
    int slot = FindFreeOverlayNameSlot(city);
    if (slot > 0) CmdRenameCitySign(city, 0, sign, slot, (const u8 *)"Ilona's House", 13);
    city->cheats.house = tile + 1;
    CenterCityMapOnEvent(city, x, y);
    return true;
  }
  PresentCityCheatNotice(city, "Sorry, no room to build Ilona's house!");
  return false;
}

// Apply the selected cheat with explicit gameplay options and its original admission/result behavior. Rejects
// unsupported cheat values; CITY_COMMAND_TEST checks the action without RNG or effects and execution may change
// multiple City systems.
i64 CmdCheat(City *city, u32 flags, int cheat, int options, int reserved) {
  if (reserved || cheat < 0 || cheat >= CITY_CHEAT_COUNT) return CITY_COMMAND_COST_INVALID;
  bool demolish = cheat == CITY_CHEAT_REPENT && city->cheats.church == 2;
  bool military = cheat == CITY_CHEAT_MILITARY;
  int allowed = demolish ? CITY_CHEAT_CONFIRM_DEMOLITION :
      military ? CITY_CHEAT_ACCEPT_MILITARY | CITY_CHEAT_DECLINE_MILITARY : 0;
  if ((options & ~allowed) || (military && options == allowed)) return CITY_COMMAND_COST_INVALID;
  if (!(city->fixes & (1ull << CITY_FIX_RESTORED_CHEATS)))
    return CITY_COMMAND_COST_BLOCKED;
  if ((demolish || military) && !options)
    return flags & CITY_COMMAND_TEST ? CITY_COMMAND_NEEDS_CONFIRMATION : CITY_COMMAND_COST_REJECTED;
  if (flags & CITY_COMMAND_TEST)
    return cheat == CITY_CHEAT_FUND && city->indicators.bond_count >= 50 ? CITY_COMMAND_COST_BLOCKED : 0;
  const char *notice = NULL;
  switch (cheat) {
  case CITY_CHEAT_FUND:
    if (city->indicators.bond_count >= 50) return -CITY_NOTICE_BOND_LIMIT_REACHED;
    ApplyCityBondIssue(city, 25);
    break;
  case CITY_CHEAT_CASS:
    city->indicators.cash = AddInt64Clamped(city->indicators.cash, 250);
    if (Sc2kCrtRandRange(city, 16)) break;
    // The unlucky cash code starts a firestorm at the center.
    // fall through
  case CITY_CHEAT_FIRE_STORM:
  case CITY_CHEAT_NOAH:
    city->disasters.pending_type = cheat == CITY_CHEAT_NOAH ? CITY_DISASTER_MAJOR_FLOOD : CITY_DISASTER_FIRE_STORM;
    SetDisasterLocation(&city->disasters.location, city->geography.center_x, city->geography.center_y);
    break;
  case CITY_CHEAT_THE_WORKS: GrantAllSpecialFacilitiesAndCash(city); break;
  case CITY_CHEAT_MOSES:
    if (city->settings.timer_mode == CITY_TIMER_EVENT && (city->disasters.last_type == CITY_DISASTER_FLOOD ||
        city->disasters.last_type == CITY_DISASTER_MAJOR_FLOOD ||
        city->disasters.last_type == CITY_DISASTER_HURRICANE)) {
      city->disasters.event_decay_ticks = 0;
      city->disasters.event_cleanup_ticks = 0;
    }
    break;
  case CITY_CHEAT_DEBUG: EnableCityDeveloperTools(city); break;
  case CITY_CHEAT_MILITARY: {
    CityMilitaryPlacement placement = BeginCityMilitaryPlacement(city, options == CITY_CHEAT_ACCEPT_MILITARY);
    FinishCityMilitaryPlacement(city, placement);
    if (placement.notice) ReportCityResourceNotice(city, 0, placement.notice);
    break;
  }
  case CITY_CHEAT_JOKE:
    notice = "Why did the mayor cross the road? To rezone the other side.";
    break;
  case CITY_CHEAT_WEBB: if (!BuildCheatHouse(city)) return CITY_COMMAND_COST_BLOCKED; break;
  case CITY_CHEAT_OOPS:
    city->cheats.church = IntMin(city->cheats.church + 1, 2);
    notice = "Same to you, buddy!";
    break;
  case CITY_CHEAT_REPENT:
    if (demolish) {
      city->cheats.church = 1;
      for (TileIndex tile = 0; tile < (u32)city->map_cell_count; ++tile)
        if (city->tile[tile].kind == TK_CHURCH && !ReadCityZoneKind(&city->tile[tile])) {
          if (!ReadTerrainCode(&city->tile[tile])) WriteZoneByte(city, tile, ZONE_RESIDENTIAL_HIGH);
          ReplaceCityTileVariant(city, tile, TK_RUBBLE, Sc2kCrtRandRange(city, 4));
          InvalidateTile(city, tile);
        }
    } else {
      city->cheats.church = 0;
      notice = "Oh, go on...";
    }
    break;
  default: return CITY_COMMAND_COST_INVALID;
  }
  city->cheats.used = 1;
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  if (notice) PresentCityCheatNotice(city, notice);
  return true;
}

// Enable the shared debug-building state for this City. All payload values are reserved and must be zero; TEST returns
// admission without changing state.
i64 CmdEnableDebugCity(City *city, u32 flags, int reserved1, int reserved2, int reserved3) {
  if (reserved1 || reserved2 || reserved3) return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) return 0;
  city->cheats.debug_buildings = true;
  city->cheats.used = 1;
  if (city->indicators.cash < 1000000000) city->indicators.cash = 1000000000;
  city->newspaper.enabled = 0;
  city->indicators.ranked_disaster_gate = 0;
  memset(&city->economic_tables.milestone_years, 0, sizeof(city->economic_tables.milestone_years));
  RebuildCityUnlockState(city);
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  return true;
}

// Unlock the supported invention state when its gameplay fix permits the action. Reserved payload values must be zero;
// TEST has no effects and execution preserves the handler's result.
i64 CmdAllInventions(City *city, u32 flags, int reserved1, int reserved2, int reserved3) {
  if (!(city->fixes & (1ull << CITY_FIX_ALL_INVENTIONS)) || reserved1 || reserved2 ||
      reserved3) return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) return 0;
  memset(&city->economic_tables.milestone_years, 0, sizeof(city->economic_tables.milestone_years));
  RebuildCityUnlockState(city);
  city->cheats.used = 1;
  PlayCitySound(city, INVALID_TILE, CITY_SOUND_CIVIC_PLACEMENT);
  return true;
}

// Apply the fixed shared cash bonus with clamped treasury arithmetic. Payload is reserved and must be zero; TEST
// reports admission without crediting cash.
i64 CmdAddCityCashBonus(City *city, u32 flags, int reserved1, int reserved2, int reserved3) {
  if (reserved1 != 0 || reserved2 != 0 || reserved3 != 0)
    return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) return 0;
  Add500000ToCityCash(city);
  return 1;
}

// Apply the shared reward-unlock and cash grant action. Requires zero reserved payload; TEST produces no reward,
// treasury or presentation effects.
i64 CmdGrantSpecialFacilitiesAndCash(City *city, u32 flags, int reserved1, int reserved2, int reserved3) {
  if (reserved1 != 0 || reserved2 != 0 || reserved3 != 0)
    return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) return 0;
  GrantAllSpecialFacilitiesAndCash(city);
  return GetGameError(city->game) ? CITY_COMMAND_COST_REJECTED : 1;
}
