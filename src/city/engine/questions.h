#ifndef SC2K_CITY_QUESTIONS_H
#define SC2K_CITY_QUESTIONS_H

#include "city/economy/types.h"
#include "city/maps/map_types.h"
#include "city/types.h"

// Native nonmodal questions retain the original caller's pending game context.
// The UI reads these records and sends immutable answer commands; it does not own
// the context or register a continuation callback. This is not a guest ABI type.
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
  i32 portrait_resource;
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

typedef struct {
  int answer;
  const u8 *text;
  int length;
  int selection;
} CityQuestionReply;

enum { CITY_PENDING_QUESTION_CAPACITY = 16 };

int CityQuestionCount(const City *city);
const CityQuestion *CityQuestionAt(const City *city, int index);
const CityQuestion *FindCityQuestion(const City *city, int id);
bool QueueCityFacilityNameQuestion(City *city, int id, TileIndex tile);
enum {
  CITY_FACILITY_OPENING_READY = 1,
  CITY_FACILITY_OPENING_NOTICE = 2,
  CITY_FACILITY_OPENING_UNAVAILABLE = 3,
};
i64 BeginCityFacilityNameQuestion(City *city, int id, TileIndex tile);
i64 ResumeCityFacilityNameQuestion(City *city, int id, bool resume);
bool QueueCityStadiumTeamQuestion(City *city, int id, TileIndex tile);
bool QueueCityBudgetAdvisorQuestion(City *city, int id, i32 budget_session,
    CityBudgetCategory category);
// Command dispatch is the mutation boundary. Valid replies consume the ID once;
// invalid, duplicate or stale replies leave both game state and the queue intact.
// A stadium OK with no selection reports a warning and keeps the question open.
i64 ReplyToCityQuestion(City *city, int id, CityQuestionReply reply);
bool ReplyToQuestion(City *city, int id, CityQuestionReply reply);
bool CanReplyToCityQuestion(City *city, int id, CityQuestionReply reply);

// Resolved scalar reply command. TEST validates only; execution preserves the retained caller's result.
i64 CmdReplyToQuestion(City *city, u32 flags, int id, int answer, int selection);

#endif
