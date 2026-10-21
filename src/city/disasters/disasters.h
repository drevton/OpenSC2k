#ifndef SC2K_DOMAIN_SRC_CITY_DISASTERS_DISASTERS_H
#define SC2K_DOMAIN_SRC_CITY_DISASTERS_DISASTERS_H

#include "city/types.h"
#include "city/engine/simulation.h"

// State

typedef struct {
  i32 y;
  i32 x;
} CityDisasterLocation;

typedef struct {
  i16 months[4];
} CityDisasterPeriods;

typedef struct {
  CityDisasterLocation location;
  CityDisasterPeriods periods;
  u32 map_update_pending;
  u16 pending_type;
  u16 last_type;
  u16 last_event_slot;
  i16 event_data;
  i16 tile237_count;
  i16 tile248_count;
  i16 tornado_count;
  i16 monster_count;
  i16 type1_count;
  i16 type2_count;
  i16 type16_count;
  i16 border_water_count;
  u16 border_water_x;
  u16 border_water_y;
  i16 event_decay_ticks;
  u16 event_cleanup_ticks;
} CityDisasterState;

// Explicit operation locals retained across animation boundaries. No C stack
// or tile snapshot is retained; resumption uses the live City.
enum { CITY_OPERATION_PENDING = -2 };
typedef struct {
  u8 type, phase;
  i16 iteration, attempts;
  int origin_x, origin_y;
  int x, y, direction, run_length, run_position, remaining;
  u32 update_gate;
  bool applied, terrain_water_allowed;
  TileIndex strike;
  i64 cash_restore;
  u32 label_restore, redraw_counter;
} CityDisasterWork;

void CancelCityDisasterWork(City *city);

void SetDisasterLocation(CityDisasterLocation *location, int x, int y);

// Tables

extern const CityDisasterPeriods kCityDefaultDisasterPeriods;
extern const u8 kEventDataPriority[67];
extern const i8 kDisasterType10DirectionY[8];
extern const i8 kDisasterType10DirectionX[8];
extern const i8 kCardinalNeighborY[4];
extern const i8 kCardinalNeighborX[4];

// Dispatch

void FailRadio(City *city, i32 outcome);
void FinishCityMissionOutcome(City *city, int outcome);
void ScheduleRandomDisaster(City *city, int kind);
void ScheduleCenteredDisaster(City *city, int kind, int radius);
void SchedulePollutionDisaster(City *city);
void EvaluateCityProblemsAndRandomDisaster(City *city);
i64 AdvanceCityDisasterEffects(City *city, bool terrain_water_allowed);
i64 ProcessPendingCityDisaster(City *city, bool terrain_water_allowed);

// Effects

typedef struct City City;
i64 ApplyCityFireDisaster(City *city, bool fire_storm);
bool ApplyCityMobileDisaster(City *city, bool monster);
i64 ApplyCityRandomDisasterStrikes(City *city, bool delayed);
bool ApplyDisasterCellEffect(City *city, TileIndex tile_index, int allow_low_tiles);
void DemolishCityFootprintForDisaster(City *city, TileIndex tile, int animate,
    int clear_overlays);
void ResolveDestroyedCityMapCell(City *city, TileIndex tile_index);
void SpreadCityMapFireToCell(City *city, TileIndex tile_index, int maximum_power_level);
void SetCityMapOverlayIfReplaceable(City *city, TileIndex tile_index, u8 overlay);
void ClearTransientCityMapOverlay(City *city, TileIndex tile_index);
bool ApplyDisasterType3(City *city);
i64 ApplyDisasterType10(City *city);
bool ApplyDisasterType2(City *city);
bool ApplyDisasterType4(City *city);
int ApplyDisasterType15(City *city);
i64 ApplyDisasterType9(City *city);
i64 ApplyDisasterType16(City *city);
i64 ApplyDisasterType11(City *city, bool terrain_water_allowed);
i64 ApplyDisasterType6(City *city);
bool ApplyDisasterType18(City *city);

// Menu

void SelectCityFireDisaster(City *city);
void SelectCityFloodDisaster(City *city);
void SelectCityAirCrashDisaster(City *city);
void SelectCityTornadoDisaster(City *city);
void SelectCityEarthquakeDisaster(City *city);
void SelectCityMonsterDisaster(City *city);
void SelectCityRiotsDisaster(City *city);
void SelectCityHurricaneDisaster(City *city);
void SelectCityMicrowaveDisaster(City *city);
void SelectCityPollutionDisaster(City *city);
void SelectCityVolcanoDisaster(City *city);
void SelectCityFireStormDisaster(City *city);
void SelectCityMajorFloodDisaster(City *city);
void SelectCityMassRiotsDisaster(City *city);
void SelectCityMeltdownDisaster(City *city);

#endif
