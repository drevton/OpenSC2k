#ifndef SC2K_DOMAIN_SRC_CITY_ENGINE_CITY_TYPES_H
#define SC2K_DOMAIN_SRC_CITY_ENGINE_CITY_TYPES_H

#include <stddef.h>

#include "city/types.h"

// Persistent difficulty is one-based; the setup radio selection is zero-based.
typedef enum CityDifficulty {
  CITY_DIFFICULTY_EASY = 1, CITY_DIFFICULTY_MEDIUM = 2, CITY_DIFFICULTY_HARD = 3,
} CityDifficulty;

typedef enum CityStartYear {
  CITY_START_YEAR_1900 = 1900, CITY_START_YEAR_1950 = 1950,
  CITY_START_YEAR_2000 = 2000, CITY_START_YEAR_2050 = 2050,
} CityStartYear;

// Simulation

// Original cadence values. Unlimited retains tick_due between idle deliveries.
typedef enum CitySimulationSpeed {
  CITY_SPEED_PAUSED = 1,
  CITY_SPEED_SLOW = 2,
  CITY_SPEED_NORMAL = 3,
  CITY_SPEED_FAST = 4,
  CITY_SPEED_UNLIMITED = 5
} CitySimulationSpeed;

// Original zero-based calendar, independent of host dates.
typedef enum CityMonth {
  CITY_MONTH_JANUARY = 0, CITY_MONTH_FEBRUARY = 1, CITY_MONTH_MARCH = 2,
  CITY_MONTH_APRIL = 3, CITY_MONTH_MAY = 4, CITY_MONTH_JUNE = 5,
  CITY_MONTH_JULY = 6, CITY_MONTH_AUGUST = 7, CITY_MONTH_SEPTEMBER = 8,
  CITY_MONTH_OCTOBER = 9, CITY_MONTH_NOVEMBER = 10, CITY_MONTH_DECEMBER = 11,
  CITY_MONTH_COUNT = 12
} CityMonth;

// Dispatcher: each month's sixteen tile scans visit one x/y residue.
typedef enum CitySimulationPhase {
  CITY_PHASE_MONTHLY_BOOKKEEPING = 0,
  CITY_PHASE_POWER = 1,
  CITY_PHASE_ENVIRONMENT = 2,
  CITY_PHASE_TILE_SCAN_Y0_X0 = 3, CITY_PHASE_TILE_SCAN_Y0_X1 = 4,
  CITY_PHASE_TILE_SCAN_Y0_X2 = 5, CITY_PHASE_TILE_SCAN_Y0_X3 = 6,
  CITY_PHASE_TILE_SCAN_Y1_X0 = 7, CITY_PHASE_TILE_SCAN_Y1_X1 = 8,
  CITY_PHASE_TILE_SCAN_Y1_X2 = 9, CITY_PHASE_TILE_SCAN_Y1_X3 = 10,
  CITY_PHASE_TILE_SCAN_Y2_X0 = 11, CITY_PHASE_TILE_SCAN_Y2_X1 = 12,
  CITY_PHASE_TILE_SCAN_Y2_X2 = 13, CITY_PHASE_TILE_SCAN_Y2_X3 = 14,
  CITY_PHASE_TILE_SCAN_Y3_X0 = 15, CITY_PHASE_TILE_SCAN_Y3_X1 = 16,
  CITY_PHASE_TILE_SCAN_Y3_X2 = 17, CITY_PHASE_TILE_SCAN_Y3_X3 = 18,
  CITY_PHASE_TILE_SCAN_FIRST = CITY_PHASE_TILE_SCAN_Y0_X0,
  CITY_PHASE_TILE_SCAN_LAST = CITY_PHASE_TILE_SCAN_Y3_X3,
  CITY_PHASE_TRAFFIC_DECAY = 19,
  CITY_PHASE_WATER_NETWORK = 20,
  CITY_PHASE_ECONOMY = 21,
  CITY_PHASE_MISSION_UNLOCK = 22,
  CITY_PHASE_VIEW_REFRESH = 23,
  CITY_PHASE_EVENTS_DISASTERS = 24,
  CITY_PHASE_COUNT = 25,
  CITY_TICKS_PER_YEAR = CITY_PHASE_COUNT * CITY_MONTH_COUNT
} CitySimulationPhase;

// Result selection: bankruptcy differs from a failed active mission.
typedef enum CitySimulationOutcome {
  CITY_OUTCOME_MISSION_FAILED = 0,
  CITY_OUTCOME_MISSION_WON = 1,
  CITY_OUTCOME_BANKRUPT = 2,
} CitySimulationOutcome;

enum {
  CITY_TIMER_STOPPED = 0, CITY_TIMER_RUNNING = 1, CITY_TIMER_EVENT = 2,
  CITY_TIMER_INITIALIZING = 0xFFFF,
};

// City Name

typedef struct Game Game;

// Editors and saves use 31 characters. Legacy imports can contain longer names.
enum { CITY_NAME_LIMIT = 31, CITY_NAME_CAPACITY = CITY_NAME_LIMIT + 1 };
typedef struct {
  char text[CITY_NAME_CAPACITY];
  int length;
  char *overflow;
  int overflow_capacity; // Allocation metadata, excluded from simulation comparison.
} CityName;

static inline const char *CityNameText(const CityName *name) {
  return name->overflow != NULL ? name->overflow : name->text;
}

// Clock

// Provenance reported by City's logical-clock observer.
typedef enum CityClockChange {
  CITY_CLOCK_INITIAL = 0,
  CITY_CLOCK_EXTERNAL_TIME = 1,
  CITY_CLOCK_CADENCE = 2,
  CITY_CLOCK_DELAY = 3,
  CITY_CLOCK_SCREEN_SHAKE = 4,
  CITY_CLOCK_EXPLICIT_ADVANCE = 5,
} CityClockChange;

// Compact native state. The upper month word is read by the original monthly
// bookkeeping routine; keep it without retaining the surrounding global span.
typedef struct {
  u32 tick;
  union {
    u32 month_word;
    struct {
      u16 month;
      u16 month_upper_word;
    };
  };
  u16 year;
  u16 quarter;
} SimulationClock;

// Settings

// Persisted options and opaque MISC values; presentation handles live elsewhere.
typedef struct {
  i16 speed; // MISC stores the full signed 16-bit speed value.
  u16 loaded_speed; // Last speed copied during load; stock transient mirror.
  u32 auto_goto;
  u32 misc_14;
  u32 misc_15;
  u16 timer_mode;
  u16 misc_1033;
} CitySavedSettings;

// Mission

// Native mission state is naturally aligned; the SCEN record has its own codec.
typedef struct {
  u16 remaining_ticks;
  u32 minimum_population;
  i32 minimum_sector_metric[3];
  i32 minimum_net_funds;
  u32 minimum_city_metric;
  i16 minimum_service_metric[2];
  i32 maximum_environment_metric[3];
  u8 statistic_index[2];
  i16 minimum_statistic[2];
} MissionCriteria;

typedef struct {
  u16 disaster_type;
  u8 disaster_y;
  u8 disaster_x;
  MissionCriteria criteria;
} CityScenarioMetadata;

typedef struct {
  u32 active;
  CityScenarioMetadata metadata;
} CityMissionState;

#endif
