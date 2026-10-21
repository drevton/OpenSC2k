#ifndef SC2K_DOMAIN_SRC_CITY_ENGINE_SIMULATION_H
#define SC2K_DOMAIN_SRC_CITY_ENGINE_SIMULATION_H

#include "city/economy/facilities.h"
#include "city/military.h"
#include "city/economy/economy.h"
#include "city/types.h"

// Scheduler

typedef struct Game Game;

// One logical scheduling frame represents one millisecond of original game
// time. This counter advances only when the runner steps it, never on a clock
// read. Simulation phases and presentation frames have separate counters.
enum { CITY_SCHEDULER_FRAME_MS = 1, CITY_CADENCE_INTERVAL_MS = 200 };

typedef enum CityClockChange {
  CITY_CLOCK_INITIAL = 0,
  CITY_CLOCK_EXTERNAL_TIME = 1,
  CITY_CLOCK_CADENCE = 2,
  CITY_CLOCK_DELAY = 3,
  CITY_CLOCK_SCREEN_SHAKE = 4,
  CITY_CLOCK_EXPLICIT_ADVANCE = 5,
} CityClockChange;

typedef struct {
  u32 counter;
  u16 parity;
  u32 tick_due;
} TimerCadenceCounter;

u64 SimulationFrameCount(City *city);
u32 SimulationFrameTime(City *city);
void AdvanceSimulationFrames(City *city, u32 frames);
void AdvanceCityClock(City *city, CityClockChange kind, u32 frames);
// Reference replay supplies the same logical time before executing a command.
void SetSimulationFrameTime(City *city, u32 milliseconds);

bool AdvanceGameDelayFrame(Game *game);
void PumpGameDelayCadence(Game *game);
void PumpGameActivation(Game *game);
u32 ReadCityScreenShakeTime(City *city);
u32 ReadCityAnimationTime(City *city);

// Dispatch

void HandleApplicationActivation(Game *game, int active);
bool RefreshActiveCityPalette(City *city);
bool PrepareCityAnnualBudget(City *city);

// Idle

typedef struct City City;

// Random

u32 Sc2kCrtRand(City *city);

// Always consume one draw, including for an empty range.
static inline u32 Sc2kCrtRandRange(City *city, int n) {
  int draw = Sc2kCrtRand(city);
  return n ? draw % n : 0;
}

void Sc2kSeedCrtRandom(City *city, u32 seed);
void SeedCityMapRandomState(City *city, u32 seed);
void SeedCityEventRandomState(City *city, u32 seed);
int AdvanceCityMapSeed(u32 seed);
int NextCityMapRandomModulo(City *city, int modulus);
int NextEventOrientationRandom(City *city, int modulus);

// Simulation

// Pending algorithm state belongs to City. Zero phase means no suspended work.
// Order-sensitive scans retain the original scan, queue and random draw ordering.
typedef struct {
  u32 available, demand, transit;
  int source_x, source_y;
  u8 stage;
} CityUtilityTraversal;

typedef struct {
  int row, stage, count, center_x, center_y;
  i64 sum_x, sum_y;
} CityEnvironmentScan;

typedef struct {
  u32 phase;
  // Decay and utility stage 0 count sections; other scans count tile positions.
  int cursor;
  u8 stage;
  bool large_city_effect, running;
  CityUtilityTraversal network;
  CityEnvironmentScan environment;
} CitySimulationWork;

// One idle command advances a bounded batch, without advancing the calendar
// again while the current phase is pending. Small cities use the stock path.
bool StepCitySimulationWork(City *city);
void FinishCitySimulationWork(City *city);
// Explicit native tick options. A result >=2 identifies suspended tick work.
enum {
  CITY_TICK_DEFER_DIALOGS = 1,
  CITY_TICK_FROM_IDLE = 2,
  CITY_TICK_ALLOW_TERRAIN_WATER = 4,
  // Retained event-idle tail; not accepted by the public advance-tick command.
  CITY_TICK_FROM_EVENT_IDLE = 8,
  CITY_TICK_EVENT_VIEW_ACTIVE = 16,
};
typedef enum CityTickDialog {
  CITY_TICK_DIALOG_BUDGET, CITY_TICK_DIALOG_ECONOMY_NOTICE,
  CITY_TICK_DIALOG_MONTHLY_NEWSPAPER, CITY_TICK_DIALOG_MISSION,
  CITY_TICK_DIALOG_REWARD_NEWSPAPER,
  CITY_TICK_DIALOG_MILITARY_OFFER, CITY_TICK_DIALOG_MILITARY_RESULT,
  CITY_TICK_DIALOG_DISASTER_NEWSPAPER,
  CITY_TICK_DIALOG_MILESTONE_NEWSPAPER,
  CITY_TICK_DIALOG_AGING_NEWSPAPER,
  CITY_TICK_DIALOG_NEWSPAPER_INITIALIZATION,
  CITY_TICK_ANIMATION,
} CityTickDialog;
enum {
  CITY_NEWSPAPER_INITIALIZATION_ENDED = 1,
  CITY_NEWSPAPER_INITIALIZATION_SKIP_TAIL = 2,
};
typedef struct CityTickContinuation {
  struct CityTickContinuation *next;
  int token;
  u32 options;
  u8 dialog, edition, outcome;
  u16 notice;
  union {
    int arcology_count;
    int milestone;
    CityMilitaryPlacement military;
    CityAnnualFacilityWork annual;
  };
} CityTickContinuation;
i64 AdvanceCitySimulationPhaseWithOptions(City *city, u32 options);
i64 AdvanceCityDisasterWithOptions(City *city, u32 options);
i64 AdvanceCityDemolition(City *city, TileIndex tile, bool animate);
void SelectCityNewspaperEdition(City *city, int edition);
i64 BeginCityNewspaperOpening(City *city, int edition);
const CityTickContinuation *FindCityTickContinuation(const City *city, int token);
i64 ResumeCityTick(City *city, int token, bool resume, int answer);
void ReleaseCityTickContinuations(City *city);
bool StepCityPowerGrid(City *city, CitySimulationWork *work, int budget);
bool StepCityWaterNetwork(City *city, int budget);
void BeginCityUtilityTraversal(City *city, CityUtilityTraversal *network, int x, int y);
bool StepCityWaterComponent(City *city, CityUtilityTraversal *network, int *budget);
bool StepCityEnvironment(City *city, CityEnvironmentScan *scan, int rows);
// first/count are multiples of 32 quarter-grid positions (128 full-resolution columns).
void ScanCityTileBatch(City *city, int quarter_x, int quarter_y,
    int first, int count, bool large_city_effect);

// Phase routines operate on City. Presentation uses the Game-owned drawing configuration.
void ScanPowerGridPhase(City *city);
void UpdateCityEnvironmentMaps(City *city);
void ScanCityMapPhase20(City *city);
void ScanQuarterCityTiles(City *city, int quarter_x, int quarter_y);
void UpdateMonthlyEconomyBookkeeping(City *city);
void UpdateCityEconomyPrimary(City *city);
void UpdateCityEconomySecondary(City *city);
void Sc2kPhase21History(City *city);
void EvaluateCityProblemsAndRandomDisaster(City *city);
void AdvanceCitySimulationPhase(City *city);

// Timer
i16 UpdateCityCadence(City *city);

void SetCityTimerMode(City *city, int mode);

// Weather

typedef struct {
  u8 state;
  u8 temperature;
  u8 wind;
  u8 rainfall;
} CityWeatherState;

typedef struct {
  u8 next_state[8];
} CityWeatherTransitionRow;

typedef struct {
  CityWeatherTransitionRow quarter[4][12];
} CityWeatherTransitions;

typedef struct {
  u8 temperature[16];
  u8 wind[16];
  u8 rainfall[16];
} CityWeatherProperties;

_Static_assert(sizeof(CityWeatherState) == 4, "weather state stores four independent bytes");
_Static_assert(sizeof(CityWeatherTransitions) == 4 * 12 * 8, "weather transition table width");
_Static_assert(sizeof(CityWeatherProperties) == 3 * 16, "weather property table width");

extern const CityWeatherTransitions city_weather_transitions;
extern const CityWeatherProperties city_weather_properties;

#endif
