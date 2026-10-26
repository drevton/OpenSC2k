#ifndef SC2K_CITY_QUESTIONS_H
#define SC2K_CITY_QUESTIONS_H

#include "city/engine/input.h"

// Count includes reserved opening slots, which are not yet replyable.
int CityQuestionCount(const City *city);

// Borrow by identity, or return NULL. A found opening reservation is not an admitted reply.
// Insertion, removal and callbacks can invalidate the pointer; retain the ID and look it up again afterward.
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
// Read-only TEST admission; leaves question order, helper state, RNG and presentation unchanged.
bool CanReplyToCityQuestion(const City *city, int id, CityQuestionReply reply);

// Validate and apply a reply with a nonnull stadium action for this call only; no action pointer is retained.
// An accepted team action must preserve the pending question array until this call consumes its question.
// Selection -1 may run nested commands in the action; this call returns without reading or consuming the question
// again. Other admitted replies resume and consume their actual context. Invalid replies return false without effects.
bool ApplyCityQuestionReply(City *city, int id, CityQuestionReply reply,
    void (*assign_team)(City *, TileIndex, int, const char *));

// Resolved scalar reply command. TEST validates only; execution preserves the retained caller's result.
i64 CmdReplyToQuestion(City *city, u32 flags, int id, int answer, int selection);

#endif
