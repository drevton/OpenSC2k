#ifndef SC2K_CITY_ECONOMY_FACILITIES_H
#define SC2K_CITY_ECONOMY_FACILITIES_H

#include "city/maps/tile_storage.h"
#include "city/maps/map_types.h"
#include "city/types.h"

// Facility information classes used by the tile lookup table. The byte lookup
// is independent of surface tile IDs and military statistic indices.
typedef enum CityFacilityInformationClass {
  CITY_FACILITY_INFO_DEFAULT = 0,
  CITY_FACILITY_INFO_POWER_PLANT = 1,
  CITY_FACILITY_INFO_CITY_HALL = 2,
  CITY_FACILITY_INFO_HOSPITAL = 3,
  CITY_FACILITY_INFO_POLICE = 4,
  CITY_FACILITY_INFO_FIRE = 5,
  CITY_FACILITY_INFO_SCHOOL = 6,
  CITY_FACILITY_INFO_STADIUM = 7,
  CITY_FACILITY_INFO_PRISON = 8,
  CITY_FACILITY_INFO_COLLEGE = 9,
  CITY_FACILITY_INFO_ZOO = 10,
  CITY_FACILITY_INFO_STATUE = 11,
  CITY_FACILITY_INFO_MAYORS_HOUSE = 12,
  CITY_FACILITY_INFO_WATER_TREATMENT = 13,
  CITY_FACILITY_INFO_DESALINATION = 14,
  CITY_FACILITY_INFO_ARCOLOGY = 15,
  CITY_FACILITY_INFO_LLAMA_DOME = 16,
  CITY_FACILITY_INFO_BUS_DEPOT = 17,
  CITY_FACILITY_INFO_RAIL_DEPOT = 18,
  CITY_FACILITY_INFO_SUBWAY_STATION = 19,
  CITY_FACILITY_INFO_WIND_POWER = 20,
  CITY_FACILITY_INFO_HYDRO_POWER = 21,
  CITY_FACILITY_INFO_LARGE_PARK = 22,
  CITY_FACILITY_INFO_MUSEUM = 23,
  CITY_FACILITY_INFO_LIBRARY = 24,
  CITY_FACILITY_INFO_MARINA = 25,
  CITY_FACILITY_INFO_COUNT = 26,
  CITY_FACILITY_INFO_LINE_COUNT = 5,
  CITY_FACILITY_INFO_SHARED_RECORD_FIRST = CITY_FACILITY_INFO_BUS_DEPOT,
} CityFacilityInformationClass;

// Original XLAB slots 251..255 and string resources 401..405.
typedef enum CityTeamNameSlot {
  CITY_TEAM_NAME_LLAMAS = 251, CITY_TEAM_NAME_ALPACAS = 252,
  CITY_TEAM_NAME_CAMELS = 253, CITY_TEAM_NAME_DROMEDARIES = 254,
  CITY_TEAM_NAME_ARMY_ANTS = 255, CITY_TEAM_NAME_FIRST = CITY_TEAM_NAME_LLAMAS,
  CITY_TEAM_NAME_COUNT = 5
} CityTeamNameSlot;

// Zero-based facility stadium_team, distinct from the XLAB name-slot IDs.
typedef enum CityStadiumTeam {
  CITY_TEAM_LLAMAS = 0, CITY_TEAM_ALPACAS = 1, CITY_TEAM_CAMELS = 2,
  CITY_TEAM_DROMEDARIES = 3, CITY_TEAM_ARMY_ANTS = 4,
} CityStadiumTeam;

// City.infrastructure.stadium_team_mask tracks already allocated team indices.
enum {
  CITY_TEAM_MASK_NONE = 0,
  CITY_TEAM_MASK_LLAMAS = 0x01, CITY_TEAM_MASK_ALPACAS = 0x02,
  CITY_TEAM_MASK_CAMELS = 0x04, CITY_TEAM_MASK_DROMEDARIES = 0x08,
  CITY_TEAM_MASK_ARMY_ANTS = 0x10, CITY_TEAM_MASK_ALL = 0x1F,
  CITY_TEAM_MASK_FIRST_TWO = 0x03, CITY_TEAM_MASK_FIRST_THREE = 0x07,
  CITY_TEAM_MASK_FIRST_FOUR = 0x0F,
};

// Facility slots 0..9 are fixed; CreateFacilityInstance allocates from slot 10 onward.
// Slot60 (overlay111) triggers the newspaper F3/ranking refresh in label editors.
enum {
  CITY_FACILITY_RECORD_RESERVED_COUNT = 10,
  CITY_FACILITY_RECORD_FIRST_DYNAMIC = CITY_FACILITY_RECORD_RESERVED_COUNT,
  CITY_FACILITY_RECORD_COUNT = CITY_OVERLAY_FACILITY_COUNT,
  CITY_OVERLAY_RESERVED_FACILITY_LAST = CITY_OVERLAY_FACILITY_FIRST + CITY_FACILITY_RECORD_RESERVED_COUNT - 1,
  CITY_OVERLAY_NEWSPAPER_RANKING_FACILITY = 111,
};

// XMIC records: tracked widths retain aging and service-metric wrap behavior.
typedef struct {
  u8 type;
  u8 age;
  u16 value_02;
  union {
    u16 value_04;
    u16 stadium_team;
  };
  union {
    u16 value_06;
    u16 stadium_team_name_slot;
  };
} CityAnnualFacilityRecord;
_Static_assert(sizeof(CityAnnualFacilityRecord) == 8 && offsetof(CityAnnualFacilityRecord, value_02) == 2 &&
    offsetof(CityAnnualFacilityRecord, stadium_team) == 4 &&
    offsetof(CityAnnualFacilityRecord, stadium_team_name_slot) == 6, "XMIC facility record layout");

typedef struct {
  TileIndex tile;
  u32 next, years;
  CityOverlayName name;
} CityFacilityDetails;

typedef struct {
  // The stock prefix is also the bounded SC2 projection. Extra slots use the same simulation record layout.
  CityAnnualFacilityRecord record[CITY_FACILITY_RECORD_COUNT];
  CityAnnualFacilityRecord *extra;
  CityFacilityDetails *details;
  u32 *buckets;
  int capacity, used, free_slot;
} CityAnnualFacilities;

_Static_assert(offsetof(CityAnnualFacilities, record) == 0 && sizeof(CityAnnualFacilityRecord) == 8,
    "stock microsim record width");


// Unlisted tile kinds have no specialized information template.
extern const u8 kFacilityClassByTile[TK_COUNT];
// Allocate/aggregate the facility record and copy its asset name into City label storage. Some kinds consume RNG.
// Returns an overlay association, or zero when no record is needed/available. Full arcology allocation may reuse a slot.
int CreateFacilityInstance(City *city, TileIndex anchor, u8 kind);
bool SetCityIndividualFacilities(City *city, bool individual);

// Before replacing a footprint, restore reward availability or release its live stadium team association.
// Kind and the valid northeast corner are captured by the caller. Overlay record cleanup follows during removal.
void PrepareCityFacilityRemoval(City *city, TileIndex anchor, int kind);

// Stock signed-word inputs and unsigned-word result; a zero divisor selects the existing population fallback.
int ClampAnnualFacilityMetric(City *city, int requested, int divisor);
// Advance aging, service metrics and arcologies through their existing notices and compensation sequence.
void AdvanceAnnualFacilityState(City *city);
int UpdateCityAnnualFacilities(City *city);
typedef struct CityAnnualFacilityWork {
  u32 arcology_population;
  int average, ratio_divisor;
  u32 index;
  u16 value_sum, arcology_count;
  i16 prior_ratio_flag;
  u8 low_school_quality, high_prison_load, resume_power, notice_data;
} CityAnnualFacilityWork;
// Advance returns false at an aging notice; present notice_data, then resume the same work. True permits Finish.
// Finish commits totals once and returns the captured exodus count, or -1 when none begins. The simulation caller
// retains this deterministic continuation through notices.
CityAnnualFacilityWork BeginCityAnnualFacilities(City *city);
bool AdvanceCityAnnualFacilities(City *city, CityAnnualFacilityWork *work);
int FinishCityAnnualFacilities(City *city, const CityAnnualFacilityWork *work);
// Consume the captured count after the exodus notice; nested commands may have changed live cash and population.
void CompleteCityArcologyExodus(City *city, int arcology_count);

// Update resolved reward availability, or reconstruct it after a load, and notify the toolbar presentation.
void SetSpecialFacilityAvailability(City *city, int facility, i32 enabled);
void RebuildCityUnlockState(City *city);

#endif
