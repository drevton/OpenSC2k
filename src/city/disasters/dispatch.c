#include "city/maps/labels.h"
#include "city/engine/random.h"
#include "city/military.h"
#include "audio/types.h"
#include "city/disasters/types.h"
#include "city/economy/types.h"
#include "city/engine/city_types.h"
#include "city/engine/command_types.h"
#include "city/events/news.h"
#include "city/maps/tile_storage.h"
#include "game/application.h"
#include "city/maps/layers.h"
#include "game/audio.h"
#include "city/presentation.h"
#include <stdio.h>
#include <string.h>

#include "game/observation.h"
#include "city/disasters/disasters.h"
#include "city/engine/commands.h"
#include "city/economy/economy.h"
#include "city/engine/simulation.h"
#include "city/events/events.h"
#include "city/state.h"
#include "city/saveload/file.h"
#include "game/state.h"
#include "assets/state.h"
#include "assets/catalog.h"
#include "base/text.h"

static const char *const kProblemDescriptions[] = {
  "More power plants needed",
  "Residents want roads and rail",
  "Residents want more police",
  "More fire protection needed",
  "City water shortage",
  "More hospitals needed",
  "Residents want more schools",
  "Industry needs a seaport",
  "Commerce needs an airport",
  "Residents want a zoo",
  "Residents want a stadium",
  "Residents want a marina",
  "Residents want more parks",
  "Industry needs outside links",
  "Commerce needs outside links",
};
static const char *const kWeatherWarnings[] = {
  "Industry needs a seaport",
  "Commerce needs an airport",
  "Residents want a zoo",
  "Residents want a stadium",
  "Residents want a marina",
  "Residents want more parks",
  "Industry needs outside links",
  "Commerce needs outside links",
  "Brownouts in the city",
  "Heavy snow expected",
  "Hurricane approaching",
  "Tornado risk reported",
};

// Dispatch

// The actual caller resumes this cleanup only after its outcome notice closes. Outcome must be the captured result;
// uses live state after dismissal and must not run while its notice still owns the caller.
void FinishCityMissionOutcome(City *city, int outcome) {
  city->mission.active = 0;
  if (outcome != CITY_OUTCOME_MISSION_WON) {
    city->game->city_active = false;
    city->game->idle_state = CITY_IDLE_STARTUP_MENU;
    city->game->idle_entry = 1;
  }
}

// The original helper writes two coordinates and has no semantic return value. Location must be writable; coordinates
// are stored without map validation so scheduling callers own their admission.
void SetDisasterLocation(CityDisasterLocation *location, int x, int y) {
  // Original signed-word inputs are widened into the persistent coordinates.
  location->y = y;
  location->x = x;
}

// Choose a deterministic random map location and queue the specified disaster kind. Kind must be supported by the
// dispatcher; selection changes City RNG and pending disaster state but does not yet apply the effects.
void ScheduleRandomDisaster(City *city, int kind) {
  int x = (i32)Sc2kCrtRandRange(city, city->map_width - 2) + 1;
  int y = (i32)Sc2kCrtRandRange(city, city->map_height - 2) + 1;
  SetDisasterLocation((&city->disasters.location), x, y);
  city->disasters.pending_type = kind;
}

// Queue the specified disaster in a 32-tile square around the developed center, offset by the supplied radius value.
// Kind and offset must be admitted by the selector; consumes the two deterministic coordinate draws without applying
// damage.
void ScheduleCenteredDisaster(City *city, int kind, int radius) {
  int x = city->geography.center_x + (Sc2kCrtRand(city) & CITY_CENTERED_DISASTER_ORIGIN_MASK) - radius;
  int y = city->geography.center_y + (Sc2kCrtRand(city) & CITY_CENTERED_DISASTER_ORIGIN_MASK) - radius;
  SetDisasterLocation((&city->disasters.location), x, y);
  city->disasters.pending_type = kind;
}

// Choose the pollution disaster's pending location/type from current City state. This is a scheduling effect with
// deterministic random draws, not a read-only eligibility query.
void SchedulePollutionDisaster(City *city) {
  city->disasters.location.y = -1;
  int strongest = 0;
  int chosen_x = 0;
  int chosen_y = 0;
  for (int y = 0; y < city->map_height / 2; ++y) {
    for (int x = 0; x < city->map_width / 2; ++x) {
      int value = ReadPollution(city, MapTileXY(city, (x) * 2, (y) * 2));
      if (value < 150 || value <= strongest)
        continue;
      if (NextCityMapRandomModulo(city, 10) != 0)
        continue;
      strongest = value;
      chosen_y = NextCityMapRandomModulo(city, 10) + 2 * y - 5;
      chosen_x = NextCityMapRandomModulo(city, 10) + 2 * x - 5;
    }
  }
  if (strongest == 0 || chosen_x < 0 || chosen_x >= city->map_width || chosen_y < 0 || chosen_y >= city->map_height)
    return;
  SetDisasterLocation((&city->disasters.location), chosen_x, chosen_y);
  city->disasters.pending_type = CITY_DISASTER_POLLUTION;
}

// City problem selection and the disaster gate. Requires current economy/weather metrics; selection may consume
// deterministic RNG and only queues effects for the later dispatcher.
void EvaluateCityProblemsAndRandomDisaster(City *city) {
  CityEconomyMonths *economy = &city->economy;
  CityStatistics *statistics = &city->statistics;
  int problem = CITY_PROBLEM_NONE;
  u32 population = city->indicators.population;
  int monthly_arcology_population = (i32)city->indicators.arcology_population / 12;
  if (city->weather.state >= CITY_WEATHER_BLIZZARD) {
    problem = CITY_PROBLEM_WEATHER;
    if (city->weather.state < CITY_WEATHER_COUNT)
      SetCityProblemNotice(city, kWeatherWarnings[city->weather.state]);
    else
      SetCityProblemNotice(city, ""); // Malformed saved weather has no description resource.
  } else if (city->infrastructure.power_percent > 98) {
    problem = CITY_PROBLEM_POWER;
  } else if (economy->sector[CITY_ECONOMY_ROADS].current_amount + statistics->value[TK_RAIL_STATION] +
          statistics->value[TK_SUBWAY_STATION] <=
      population / 100) {
    problem = CITY_PROBLEM_TRANSPORT;
  } else if (population < 1000) {
    problem = CITY_PROBLEM_NONE;
  } else if (statistics->value[TK_PRISON] / 16 + statistics->value[TK_POLICE_STATION] / 9 <=
      population / 20000) {
    problem = CITY_PROBLEM_POLICE;
  } else if (statistics->value[TK_FIRE_STATION] / 9 <= population / 20000) {
    problem = CITY_PROBLEM_FIRE_STATION;
  } else if (city->infrastructure.water_percent > 98) {
    problem = CITY_PROBLEM_WATER;
  } else if (population < 3000) {
    problem = CITY_PROBLEM_NONE;
  } else if (statistics->value[TK_HOSPITAL] / 9 <= population / 25000) {
    problem = CITY_PROBLEM_HOSPITAL;
  } else if (statistics->value[TK_SCHOOL] / 9 <= population / 20000) {
    problem = CITY_PROBLEM_SCHOOL;
  } else if (population < 8000) {
    problem = CITY_PROBLEM_NONE;
  } else if (city->infrastructure.service_bonus_a + statistics->value[TK_CRANE] <
      (int)(economy->sector[CITY_ECONOMY_INDUSTRIAL].current_amount - (u32)monthly_arcology_population) / 10000) {
    problem = city->geography.ocean != 0 || city->geography.river != 0 ? CITY_PROBLEM_SEAPORT
                                                                       : CITY_PROBLEM_INDUSTRIAL_CONNECTION;
  } else if (city->infrastructure.service_bonus_b + statistics->value[TK_RUNWAY_LAST] +
          statistics->value[TK_RUNWAY_FIRST] <
      (int)(economy->sector[CITY_ECONOMY_COMMERCIAL].current_amount - (u32)monthly_arcology_population) / 2000) {
    problem = (city->unlocks.tools.shared.policy_availability_flags & CITY_AVAILABILITY_DISASTER_CHOICE) == 0
        ? CITY_PROBLEM_COMMERCIAL_CONNECTION
        : CITY_PROBLEM_AIRPORT;
  } else if (statistics->value[TK_MARINA] + statistics->value[TK_ZOO] +
          statistics->value[TK_STADIUM] + statistics->value[TK_LARGE_PARK] / 3 <
      (int)(economy->sector[CITY_ECONOMY_RESIDENTIAL].current_amount - (u32)(2 * monthly_arcology_population)) / 1000) {
    problem = (Sc2kCrtRand(city) & 3) + CITY_PROBLEM_ZOO;
  }
  if (problem == CITY_PROBLEM_NONE)
    SetCityProblemNotice(city, "");
  if (problem >= 0) {
    SetCityProblemNotice(city, kProblemDescriptions[problem]);
    InsertRankedCityEvent(city, (u32)problem + CITY_NEWS_POWER_SHORTAGE, 0);
  }
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  if (city->indicators.event_flag != 0)
    return;
  int difficulty = city->indicators.difficulty;
  CityDisasterPeriods *periods = &city->disasters.periods;
  int months = periods->months[difficulty];
  int elapsed_months = (i32)city->clock.tick / 25;
  if (elapsed_months < months)
    return;
  int draw = Sc2kCrtRandRange(city, months);
  if (city->weather.state == CITY_WEATHER_HURRICANE && city->geography.ocean != 0 && draw < 15) {
    city->disasters.pending_type = CITY_DISASTER_HURRICANE;
    return;
  }
  if (city->weather.state == CITY_WEATHER_TORNADO && draw < 15) {
    ScheduleRandomDisaster(city, CITY_DISASTER_TORNADO);
    return;
  }
  if (draw == 0) {
    u32 choice = Sc2kCrtRandRange(city, CITY_DISASTER_COUNT);
    switch (choice) {
    case CITY_DISASTER_NONE:
    case CITY_DISASTER_CRASH:
    case CITY_DISASTER_VOLCANO:
    case CITY_DISASTER_FIRE_STORM:
    case CITY_DISASTER_HELICOPTER_CRASH:
      return;
    case CITY_DISASTER_FIRE: {
      u32 temperature_draw = (Sc2kCrtRand(city) & 127) + 127;
      if (temperature_draw > city->weather.temperature)
        return;
      ScheduleRandomDisaster(city, choice);
      return;
    }
    case CITY_DISASTER_FLOOD:
      if (city->geography.river == 0 && city->geography.ocean == 0)
        return;
      if (city->weather.state < CITY_WEATHER_FOGGY)
        return;
      ScheduleRandomDisaster(city, choice);
      return;
    case CITY_DISASTER_RIOTS:
      if ((i32)city->indicators.unemployment < 10)
        return;
      if (city->weather.temperature < 170)
        return;
      ScheduleCenteredDisaster(city, choice, 16);
      return;
    case CITY_DISASTER_POLLUTION:
      SchedulePollutionDisaster(city);
      return;
    case CITY_DISASTER_EARTHQUAKE:
      ScheduleRandomDisaster(city, choice);
      return;
    case CITY_DISASTER_TORNADO:
    case CITY_DISASTER_MAJOR_FLOOD:
      if (city->weather.state < (choice == CITY_DISASTER_TORNADO ? CITY_WEATHER_WINDY : CITY_WEATHER_BLIZZARD))
        return;
      if (choice == CITY_DISASTER_MAJOR_FLOOD && city->geography.river == 0 && city->geography.ocean == 0)
        return;
      ScheduleRandomDisaster(city, choice);
      return;
    case CITY_DISASTER_MONSTER:
      if (city->indicators.population < 45000)
        return;
      ScheduleCenteredDisaster(city, choice, 15);
      return;
    case CITY_DISASTER_MELTDOWN:
    case CITY_DISASTER_MICROWAVE: {
      CityTileCounts *counts = &city->statistics;
      if (counts->count[choice == CITY_DISASTER_MELTDOWN ? TK_NUCLEAR_POWER_PLANT
                                                         : TK_MICROWAVE_POWER_PLANT] == 0)
        return;
      city->disasters.pending_type = choice;
      return;
    }
    case CITY_DISASTER_MASS_RIOTS:
      if (city->indicators.population < 30000)
        return;
      if ((i32)city->indicators.unemployment < 10)
        return;
      if (city->weather.temperature < 170)
        return;
      ScheduleCenteredDisaster(city, choice, 16);
      return;
    case CITY_DISASTER_CHEMICAL_SPILL: {
      CityEconomyMonths *economy = &city->economy;
      if (economy->sector[CITY_ECONOMY_INDUSTRIAL].mission_metric < 10000)
        return;
      ScheduleCenteredDisaster(city, choice, 15);
      return;
    }
    case CITY_DISASTER_HURRICANE:
      if (city->weather.state < CITY_WEATHER_WINDY)
        return;
      if (city->geography.ocean == 0)
        return;
      city->disasters.pending_type = choice;
      return;
    case CITY_DISASTER_SCHEDULED_PLANE_CRASH: {
      CityTileCounts *counts = &city->statistics;
      if (counts->count[TK_RUNWAY_FIRST] == 0)
        return;
      ScheduleRandomDisaster(city, choice);
      return;
    }
    default:
      return;
    }
  }
}

// True means effects and cleanup completed and the final newspaper is due. Requires a matching disaster work lifetime;
// pending animation retains cleanup and the final newspaper remains the caller's responsibility.
i64 AdvanceCityDisasterEffects(City *city, bool terrain_water_allowed) {
  int succeeded = 1;
  if (city->disasters.pending_type != CITY_DISASTER_NONE || city->disaster_work.type) {
    bool resuming = city->disaster_work.type != 0;
    int disaster_type = resuming ? city->disaster_work.type : city->disasters.pending_type;
    if (!resuming && city->game->debug_city)
      fprintf(stderr, "disaster start type=%d location=(%d,%d) tick=%u\n", disaster_type,
          city->disasters.location.x, city->disasters.location.y, city->clock.tick);
    if (!resuming) city->disasters.pending_type = CITY_DISASTER_NONE;
    switch (disaster_type) {
    case CITY_DISASTER_FIRE:
    case CITY_DISASTER_FIRE_STORM:
      succeeded = ApplyCityFireDisaster(city, disaster_type == CITY_DISASTER_FIRE_STORM);
      break;
    case CITY_DISASTER_FLOOD:
      succeeded = ApplyCityFloodDisaster(city);
      break;
    case CITY_DISASTER_RIOTS:
      succeeded = ApplyCityRiotDisaster(city);
      if (succeeded != 0) {
        ApplyCityRiotDisaster(city);
        ApplyCityRiotDisaster(city);
      }
      break;
    case CITY_DISASTER_POLLUTION:
      succeeded = ApplyCityPollutionDisaster(city);
      break;
    case CITY_DISASTER_CRASH:
      succeeded = 1;
      break;
    case CITY_DISASTER_EARTHQUAKE:
      succeeded = ApplyCityEarthquakeDisaster(city);
      break;
    case CITY_DISASTER_TORNADO:
    case CITY_DISASTER_MONSTER:
      succeeded = ApplyCityMobileDisaster(city, disaster_type == CITY_DISASTER_MONSTER);
      break;
    case CITY_DISASTER_MELTDOWN:
      succeeded = ApplyCityMeltdownDisaster(city);
      break;
    case CITY_DISASTER_MICROWAVE:
      succeeded = ApplyCityMicrowaveDisaster(city);
      break;
    case CITY_DISASTER_VOLCANO:
      succeeded = ApplyCityVolcanoDisaster(city, terrain_water_allowed);
      break;
    case CITY_DISASTER_MASS_RIOTS:
    case CITY_DISASTER_MAJOR_FLOOD:
      succeeded = ApplyCityRandomDisasterStrikes(city, disaster_type == CITY_DISASTER_MAJOR_FLOOD);
      break;
    case CITY_DISASTER_CHEMICAL_SPILL:
      succeeded = ApplyCityChemicalSpillDisaster(city);
      break;
    case CITY_DISASTER_HURRICANE:
      succeeded = ApplyCityHurricaneDisaster(city);
      break;
    case CITY_DISASTER_HELICOPTER_CRASH:
      succeeded = 1;
      break;
    case CITY_DISASTER_SCHEDULED_PLANE_CRASH:
      succeeded = ApplyCityAirCrashDisaster(city);
      break;
    default:
      succeeded = 0;
      break;
    }

    if (succeeded == CITY_OPERATION_PENDING) return CITY_OPERATION_PENDING;
    if (succeeded != 0) {
      city->disasters.last_type = disaster_type;
      if (city->settings.timer_mode == CITY_TIMER_RUNNING)
        SetCityTimerMode(city, CITY_TIMER_EVENT);
      city->disasters.map_update_pending = 1;
      InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
      StopAllCitySound(city->game);
      PlayCitySoundWithDelay(city->game, CITY_SOUND_DISASTER_WARNING, 5);
      if (city->unlocks.military_base == CITY_MILITARY_BASE_UNAVAILABLE && NextCityMapRandomModulo(city, 4) == 0)
        CreateCityMapMaxisMan(city, city->disasters.last_type);
    } else if (city->disasters.map_update_pending == 0) {
      return false;
    }
  }

  if (city->disasters.map_update_pending != 0) {
    i64 scan = UpdateCityMapTileStates(city);
    city->disasters.map_update_pending = scan != 0;
    for (int slot = CITY_EVENT_SLOT_FIRST; slot < city->events.capacity; ++slot) {
      CityMapEvent *event = &city->events.event[slot];
      if (event->type == CITY_MAP_EVENT_MONSTER || event->type == CITY_MAP_EVENT_TORNADO ||
          event->type == CITY_MAP_EVENT_EXPLOSION ||
          (event->type == CITY_MAP_EVENT_HELICOPTER && event->next_slot == CITY_HELICOPTER_CRASH) ||
          (event->type == CITY_MAP_EVENT_AIRPLANE && event->next_slot == CITY_AIRPLANE_CRASH)) {
        city->disasters.map_update_pending = 1;
        break;
      }
    }
    if (city->disasters.map_update_pending != 0)
      return false;
  }

  StopAllCitySound(city->game);
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  city->disasters.last_event_slot = 0xFFFF;
  if (city->disasters.event_data == CITY_DISASTER_EVENT_DATA_NONE)
    city->disasters.event_data = 0;
  if (succeeded != 0) {
    static const u16 kDisasterNews[] = {
      [CITY_DISASTER_FIRE] = CITY_NEWS_FIRE,
      [CITY_DISASTER_FIRE_STORM] = CITY_NEWS_FIRE,
      [CITY_DISASTER_FLOOD] = CITY_NEWS_FLOOD,
      [CITY_DISASTER_MAJOR_FLOOD] = CITY_NEWS_FLOOD,
      [CITY_DISASTER_RIOTS] = CITY_NEWS_RIOTS,
      [CITY_DISASTER_MASS_RIOTS] = CITY_NEWS_RIOTS,
      [CITY_DISASTER_POLLUTION] = CITY_NEWS_POLLUTION_DISASTER,
      [CITY_DISASTER_CRASH] = CITY_NEWS_PLANE_CRASH,
      [CITY_DISASTER_SCHEDULED_PLANE_CRASH] = CITY_NEWS_PLANE_CRASH,
      [CITY_DISASTER_EARTHQUAKE] = CITY_NEWS_EARTHQUAKE,
      [CITY_DISASTER_TORNADO] = CITY_NEWS_TORNADO,
      [CITY_DISASTER_MONSTER] = CITY_NEWS_MONSTER,
      [CITY_DISASTER_MELTDOWN] = CITY_NEWS_MELTDOWN,
      [CITY_DISASTER_MICROWAVE] = CITY_NEWS_MICROWAVE,
      [CITY_DISASTER_VOLCANO] = CITY_NEWS_VOLCANO,
      [CITY_DISASTER_CHEMICAL_SPILL] = CITY_NEWS_CHEMICAL_SPILL,
      [CITY_DISASTER_HURRICANE] = CITY_NEWS_HURRICANE,
      [CITY_DISASTER_HELICOPTER_CRASH] = CITY_NEWS_HELICOPTER_CRASH,
    };
    int kind = city->disasters.last_type;
    if (kind < sizeof(kDisasterNews) / sizeof(*kDisasterNews) && kDisasterNews[kind])
      InsertRankedCityEvent(city, kDisasterNews[kind], city->disasters.event_data);
  }
  city->disasters.event_data = CITY_DISASTER_EVENT_DATA_NONE;

  for (int slot = CITY_EVENT_SLOT_FIRST; slot < city->events.capacity; ++slot) {
    int type = city->events.event[slot].type;
    if (type == CITY_MAP_EVENT_POLICE || type == CITY_MAP_EVENT_FIRE || type == CITY_MAP_EVENT_MILITARY)
      ClearCityMapEventSlot(city, slot);
  }

  // Invalidate the captured view before restoring timer mode.
  RefreshCityMapTiles(city);
  SetCityTimerMode(city, CITY_TIMER_RUNNING);
  return true;
}

// Toggle the City's shared random-disaster suppression rule. Reserved payload must be zero; TEST changes neither City
// nor local preferences.
i64 CmdToggleNoDisasters(City *city, u32 flags, int reserved1, int reserved2, int reserved3) {
  if (reserved1 != 0 || reserved2 != 0 || reserved3 != 0)
    return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) return 0;
  city->indicators.event_flag = city->indicators.event_flag == 0;
  return 1;
}

// Queue the explicitly selected disaster through its deterministic selector. Rejects unsupported kinds and reserved
// input; TEST performs no location selection or random draws.
i64 CmdStartDisaster(City *city, u32 flags, TileIndex location,
    int kind, int parameter2, int parameter3) {
  int invalid = CITY_COMMAND_COST_INVALID;
  static const struct { int kind; void (*select)(City *); } kSelections[] = {
    {CITY_DISASTER_FIRE, SelectCityFireDisaster},
    {CITY_DISASTER_FLOOD, SelectCityFloodDisaster},
    {CITY_DISASTER_SCHEDULED_PLANE_CRASH, SelectCityAirCrashDisaster},
    {CITY_DISASTER_TORNADO, SelectCityTornadoDisaster},
    {CITY_DISASTER_EARTHQUAKE, SelectCityEarthquakeDisaster},
    {CITY_DISASTER_MONSTER, SelectCityMonsterDisaster},
    {CITY_DISASTER_HURRICANE, SelectCityHurricaneDisaster},
    {CITY_DISASTER_RIOTS, SelectCityRiotsDisaster},
    {CITY_DISASTER_MICROWAVE, SelectCityMicrowaveDisaster},
    {CITY_DISASTER_POLLUTION, SelectCityPollutionDisaster},
    {CITY_DISASTER_VOLCANO, SelectCityVolcanoDisaster},
    {CITY_DISASTER_FIRE_STORM, SelectCityFireStormDisaster},
    {CITY_DISASTER_MAJOR_FLOOD, SelectCityMajorFloodDisaster},
    {CITY_DISASTER_MASS_RIOTS, SelectCityMassRiotsDisaster},
    {CITY_DISASTER_MELTDOWN, SelectCityMeltdownDisaster},
  };
  if (!parameter2 && !parameter3) for (int index = 0; index < sizeof(kSelections) / sizeof(*kSelections); ++index) {
    if (kind != kSelections[index].kind) continue;
    if (flags & CITY_COMMAND_TEST) return 0;
    kSelections[index].select(city);
    return 1;
  }
  if (location != INVALID_TILE || parameter2 || parameter3 ||
      (kind != CITY_DISASTER_CRASH && kind != CITY_DISASTER_HELICOPTER_CRASH &&
       kind != CITY_DISASTER_CHEMICAL_SPILL)) return invalid;
  TileIndex tile = MapTileXYOrInvalid(city, city->geography.center_x, city->geography.center_y);
  if (tile == INVALID_TILE) return invalid;
  int slot = kind == CITY_DISASTER_CHEMICAL_SPILL ? 0 : FindFreeCityEventSlot(city);
  if (kind != CITY_DISASTER_CHEMICAL_SPILL && !slot) return invalid;
  if (flags & CITY_COMMAND_TEST) return 0;
  if (slot) {
    CityMapEvent *event = &city->events.event[slot];
    memset(event, 0, sizeof(*event));
    event->tile = tile;
    event->previous_overlay = ReadCityStockBaseOverlay(city, tile);
    if (kind == CITY_DISASTER_HELICOPTER_CRASH) {
      event->type = CITY_MAP_EVENT_HELICOPTER;
      event->state = event->next_slot = CITY_HELICOPTER_CRASH;
      event->height = 16;
    } else {
      event->type = CITY_MAP_EVENT_EXPLOSION;
      event->next_slot = CITY_DISASTER_CRASH;
      event->direction = CITY_EXPLOSION_START;
      event->explosion.trigger_disaster = 1;
    }
    LinkCityMapEvent(city, slot);
    InvalidateCityMapEvent(city, slot);
  }
  SetDisasterLocation(&city->disasters.location, city->geography.center_x, city->geography.center_y);
  city->disasters.pending_type = kind;
  return 1;
}

// Explicit disaster requests

// Fire starts within a 40-tile square around the developed city center. Queues the selected location/type and consumes
// the original CRT draws; does not apply the fire effect immediately.
void SelectCityFireDisaster(City *city) {
  city->disasters.pending_type = CITY_DISASTER_FIRE;
  int x = city->geography.center_x + (int)Sc2kCrtRandRange(city, CITY_FIRE_ORIGIN_SPAN) - CITY_FIRE_ORIGIN_RADIUS;
  int y = city->geography.center_y + (int)Sc2kCrtRandRange(city, CITY_FIRE_ORIGIN_SPAN) - CITY_FIRE_ORIGIN_RADIUS;
  SetDisasterLocation((&city->disasters.location), x, y);
}

// Flooding targets the exact city center and consumes no random values. Queues only the pending location/type; the
// later dispatcher owns map damage and timing.
void SelectCityFloodDisaster(City *city) {
  city->disasters.pending_type = CITY_DISASTER_FLOOD;
  SetDisasterLocation((&city->disasters.location), city->geography.center_x, city->geography.center_y);
}

// The dispatcher creates the airplane; this selector only queues it. Changes the pending disaster request only;
// existing event capacity and spawn admission are checked when dispatching.
void SelectCityAirCrashDisaster(City *city) { city->disasters.pending_type = CITY_DISASTER_SCHEDULED_PLANE_CRASH; }

// Choose an interior map location and queue the requested disaster type. Type must be admitted by its public selector;
// coordinate selection consumes City RNG in the retained order.
static void SelectCityInteriorDisaster(City *city, int type) {
  city->disasters.pending_type = type;
  int x = (int)Sc2kCrtRandRange(city, city->map_width - 2) + 1;
  int y = (int)Sc2kCrtRandRange(city, city->map_width - 2) + 1;
  SetDisasterLocation((&city->disasters.location), x, y);
}

// Select an interior tile, drawing X before Y from the CRT generator. Delegates to SelectCityInteriorDisaster;
// arguments must satisfy that function's contract.
void SelectCityTornadoDisaster(City *city) {
  SelectCityInteriorDisaster(city, CITY_DISASTER_TORNADO);
}

// Same interior selection as tornado, with the earthquake queue value. Delegates to SelectCityInteriorDisaster;
// arguments must satisfy that function's contract.
void SelectCityEarthquakeDisaster(City *city) {
  SelectCityInteriorDisaster(city, CITY_DISASTER_EARTHQUAKE);
}

// Monster origin uses a 32-tile square, offset by 15 from the center. Queues a location and consumes its deterministic
// coordinate draws; active-monster admission occurs during effect dispatch.
void SelectCityMonsterDisaster(City *city) {
  ScheduleCenteredDisaster(city, CITY_DISASTER_MONSTER, CITY_CENTERED_DISASTER_ORIGIN_RADIUS);
}

// Riots use the same centered origin distribution as the monster. Queues the selected location/type and consumes the
// same deterministic coordinate draws as its centered counterpart.
void SelectCityRiotsDisaster(City *city) {
  ScheduleCenteredDisaster(city, CITY_DISASTER_RIOTS, CITY_CENTERED_DISASTER_ORIGIN_RADIUS);
}

// Hurricane coordinates are chosen later by the disaster dispatcher. Stores only the pending type; later dispatch
// selects the route and owns its effect lifetime.
void SelectCityHurricaneDisaster(City *city) { city->disasters.pending_type = CITY_DISASTER_HURRICANE; }

// The dispatcher finds a microwave-plant origin later. Stores only the pending type; plant search and damage occur
// when the dispatcher consumes it.
void SelectCityMicrowaveDisaster(City *city) {
  city->disasters.pending_type = CITY_DISASTER_MICROWAVE;
  SetDisasterLocation((&city->disasters.location), 0, 0);
}

// Pollution starts at the current developed city center. Queues the centered location/type without applying damage or
// opening a notice.
void SelectCityPollutionDisaster(City *city) {
  city->disasters.pending_type = CITY_DISASTER_POLLUTION;
  SetDisasterLocation((&city->disasters.location), city->geography.center_x, city->geography.center_y);
}

// Select an interior tile, drawing X before Y. Delegates to SelectCityInteriorDisaster; arguments must satisfy that
// function's contract.
void SelectCityVolcanoDisaster(City *city) {
  SelectCityInteriorDisaster(city, CITY_DISASTER_VOLCANO);
}

// Firestorm starts at the current developed city center. Stores the centered pending request; its later spiral effect
// owns traversal and random-strike state.
void SelectCityFireStormDisaster(City *city) {
  city->disasters.pending_type = CITY_DISASTER_FIRE_STORM;
  SetDisasterLocation((&city->disasters.location), city->geography.center_x, city->geography.center_y);
}

// Major flooding selects an interior tile, drawing X before Y. Delegates to SelectCityInteriorDisaster; arguments must
// satisfy that function's contract.
void SelectCityMajorFloodDisaster(City *city) {
  SelectCityInteriorDisaster(city, CITY_DISASTER_MAJOR_FLOOD);
}

// Mass riots start at the current developed city center. Queues the centered request; later dispatch owns repeated
// strikes and temporary update-gate cleanup.
void SelectCityMassRiotsDisaster(City *city) {
  city->disasters.pending_type = CITY_DISASTER_MASS_RIOTS;
  SetDisasterLocation((&city->disasters.location), city->geography.center_x, city->geography.center_y);
}

// The dispatcher finds a nuclear-plant origin later. Stores the pending request; nuclear-plant search and damage
// belong to dispatch rather than this selector.
void SelectCityMeltdownDisaster(City *city) {
  city->disasters.pending_type = CITY_DISASTER_MELTDOWN;
  SetDisasterLocation((&city->disasters.location), 0, 0);
}

// Tables

const CityDisasterPeriods kCityDefaultDisasterPeriods = {{0, 100, 60, 30}};
