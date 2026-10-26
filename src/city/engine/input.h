#ifndef SC2K_SIMULATION_COMMAND_INPUT_H
#define SC2K_SIMULATION_COMMAND_INPUT_H

#include "city/economy/types.h"
#include "city/maps/map_types.h"
#include "city/types.h"

// Active command input and retained question records; operations live in commands.h and questions.h.

// Shared construction working state. Nested commands retain original overwrites;
// these fields affect costs and map mutations, not just preview presentation.
typedef struct CityConstructionState {
  u32 terrain_surcharge_count;
  u16 zone_code;
  i16 direction;
  u32 min_x, min_y;
  u32 max_x, max_y;
  u32 cost;
  i16 endpoint_x, endpoint_y;
  u32 cell_count;
  u16 stage;
} CityConstructionState;

// Immutable command text, independent of the canvas editor that collects it.
typedef struct CityNameCommandInput {
  CityOverlayName name;
  int length;
} CityNameCommandInput;

// Explicit construction choices are command input, independent of an active window.
typedef struct {
  int span;
  bool border, supplied, endpoint_supplied;
  TileIndex end;
  int unit_cost;
  int terrain_options;
} CityConstructionChoices;

// Native nonmodal questions retain the original caller's pending game context.
// The UI reads these records and sends immutable answer commands; it does not own
// the context or register a continuation callback.
typedef enum CityQuestionKind {
  CITY_QUESTION_FACILITY_NAME = 1,
  CITY_QUESTION_STADIUM_TEAM = 2,
  CITY_QUESTION_BUDGET_ADVISOR = 3,
  // Not replyable: reserves the ID/slot before rankings, captures the name afterward.
  CITY_QUESTION_FACILITY_OPENING = 4,
} CityQuestionKind;

typedef struct {
  int slot;
  CityOverlayName original_name;
} CityFacilityNameQuestion;

typedef struct {
  TileIndex tile;
  int slot;
  int available_teams;
} CityStadiumTeamQuestion;

typedef struct {
  i32 budget_session;
  CityBudgetCategory category;
  CityBudgetAdvice advice;
  i32 parent_question;
  bool helper_enabled;
} CityBudgetAdvisorQuestion;

typedef struct {
  int id;
  CityQuestionKind kind;
  union {
    CityFacilityNameQuestion facility_name;
    CityStadiumTeamQuestion stadium_team;
    CityBudgetAdvisorQuestion budget_advisor;
  } context;
} CityQuestion;

// Reply text is borrowed for synchronous validation/application; length excludes a terminator.
// Embedded zero bytes are rejected, and no input text pointer is retained by City.
typedef struct {
  int answer;
  const u8 *text;
  int length;
  int selection;
} CityQuestionReply;

enum { CITY_PENDING_QUESTION_CAPACITY = 16 };

#endif
