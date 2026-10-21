#include "city/economy/facilities.h"
#include "city/economy/types.h"
#include "city/engine/command_types.h"
#include "city/maps/map_types.h"
#include "city/maps/tile_storage.h"
#include "city/tools/types.h"
#include "city/presentation.h"
#include "city/engine/questions.h"

#include <string.h>

#include "city/economy/economy.h"
#include "city/engine/commands.h"
#include "city/maps/layers.h"
#include "city/state.h"
#include "game/state.h"
#include "audio/state.h"
#include "assets/state.h"
#include "base/text.h"


// Return the number of pending shared decisions owned by City. The value includes reserved opening slots; it does not
// imply that every slot is ready for replies.
int CityQuestionCount(City *city) { return city->question_count; }

// Return a borrowed pending-question entry at the requested position. Out-of-range positions return NULL; callers must
// not retain the pointer across question insertion, removal or callbacks.
const CityQuestion *CityQuestionAt(City *city, int index) {
  return index >= 0 && index < city->question_count ? &city->questions[index] : NULL;
}

// Find a pending question by its identity and return a borrowed pointer. Missing identities return NULL; the pointer
// may be invalidated by any operation that changes the question array.
const CityQuestion *FindCityQuestion(City *city, int id) {
  for (int index = 0; index < city->question_count; ++index)
    if (city->questions[index].id == id)
      return &city->questions[index];
  return NULL;
}

// Remove a pending question and compact the remaining entries. Question must point into the City's live question
// array; no pointer into that array may survive this operation.
static void RemoveCityQuestion(City *city, const CityQuestion *question) {
  int index = question - city->questions;
  --city->question_count;
  memmove(city->questions + index, city->questions + index + 1,
      (city->question_count - index) * sizeof(*city->questions));
  city->questions[city->question_count] = (CityQuestion){0};
}

// Reserve the supplied identity before callbacks can run nested commands. Returns the captured facility overlay slot,
// or zero for invalid input/capacity; the reserved opening question cannot yet receive a reply.
static int ReserveCityFacilityNameQuestion(City *city, int id, TileIndex tile) {
  if (id <= 0 || !IsMapTileValid(city, tile) || FindCityQuestion(city, id) != NULL ||
      city->question_count == CITY_PENDING_QUESTION_CAPACITY)
    return 0;
  int slot = ReadCityFacilityOverlay(city, tile);
  if (!IsCityFacilityOverlay(slot) ||
      CityFacilityRecordAt(city, CityFacilityOverlaySlot(slot))->type == TK_CLEAR ||
      memchr(CityOverlayNameAt(city, slot)->character, 0, sizeof(CityOverlayName)) == NULL)
    return 0;
  // The ranking prefix may pump nested commands before the editor is ready.
  city->questions[city->question_count++] = (CityQuestion){.id = id,
      .kind = CITY_QUESTION_FACILITY_OPENING, .context.facility_name = {.slot = slot}};
  return slot;
}

// Complete the previously reserved name-question opening against live City state. Callers preserve the reservation
// identity across presentation; failure must not be repaired by repeating the original placement.
static i64 CompleteCityFacilityNameQuestion(City *city, int id) {
  const CityQuestion *question = FindCityQuestion(city, id);
  int slot = question->context.facility_name.slot;
  // Reads the captured slot again after the parade caller returns.
  if (CityFacilityRecordAt(city, CityFacilityOverlaySlot(slot))->type == TK_CLEAR) {
    RemoveCityQuestion(city, question);
    return CITY_FACILITY_OPENING_UNAVAILABLE;
  }
  int index = question - city->questions;
  city->questions[index] = (CityQuestion){
      .id = id, .kind = CITY_QUESTION_FACILITY_NAME,
      .context.facility_name = {slot, (*CityOverlayNameAt(city, slot))},
  };
  return CITY_FACILITY_OPENING_READY;
}

// Queue a shared facility-name decision after reserving its pending slot. Returns whether queuing succeeded;
// presentation callbacks may run nested commands and therefore cannot precede reservation.
bool QueueCityFacilityNameQuestion(City *city, int id, TileIndex tile) {
  int slot = ReserveCityFacilityNameQuestion(city, id, tile);
  if (!slot) return false;
  if (!city->individual_facilities && slot == CITY_OVERLAY_NEWSPAPER_RANKING_FACILITY)
    UpdateCityNewspaperRanksAndF3Records(city);
  if (CompleteCityFacilityNameQuestion(city, id) == CITY_FACILITY_OPENING_UNAVAILABLE) {
    ShowCityTileInformationDialog(city, tile);
    return false;
  }
  return true;
}

// Start a facility-name interaction while retaining the initiating caller's state. Returns the action/continuation
// result; a pending question must be resumed through its identity rather than restarted.
i64 BeginCityFacilityNameQuestion(City *city, int id, TileIndex tile) {
  int slot = ReserveCityFacilityNameQuestion(city, id, tile);
  if (!slot) return 0;
  if (!city->individual_facilities && slot == CITY_OVERLAY_NEWSPAPER_RANKING_FACILITY) {
    if (BeginCityNewspaperProblemRankings(city)) return CITY_FACILITY_OPENING_NOTICE;
    FinishCityNewspaperRanksAndF3Records(city);
  }
  return CompleteCityFacilityNameQuestion(city, id);
}

// Resume the retained facility-name caller after its opening or reply callback. Requires the matching pending
// identity; live City state is re-read rather than restoring an earlier City snapshot.
i64 ResumeCityFacilityNameQuestion(City *city, int id, bool resume) {
  const CityQuestion *question = FindCityQuestion(city, id);
  if (!question || question->kind != CITY_QUESTION_FACILITY_OPENING ||
      question->context.facility_name.slot != CITY_OVERLAY_NEWSPAPER_RANKING_FACILITY) return 0;
  if (!resume) {
    RemoveCityQuestion(city, question);
    return 1;
  }
  FinishCityNewspaperRanksAndF3Records(city);
  return CompleteCityFacilityNameQuestion(city, id);
}

// Reserve a shared team-selection decision for a valid stadium tile. Requires a positive unused identity and available
// question capacity; false leaves no replyable new question, while success captures the allowed team choices.
bool QueueCityStadiumTeamQuestion(City *city, int id, TileIndex tile) {
  if (id <= 0 || !IsMapTileValid(city, tile) || FindCityQuestion(city, id) != NULL ||
      city->question_count == CITY_PENDING_QUESTION_CAPACITY)
    return false;
  int slot = ReadCityFacilityOverlay(city, tile);
  if (!IsCityFacilityOverlay(slot) ||
      CityFacilityRecordAt(city, CityFacilityOverlaySlot(slot))->type != TK_STADIUM)
    return false;
  city->questions[city->question_count++] = (CityQuestion){
      .id = id,
      .kind = CITY_QUESTION_STADIUM_TEAM,
      .context.stadium_team = {tile, slot,
          city->infrastructure.stadium_team_mask == CITY_TEAM_MASK_ALL
              ? CITY_TEAM_MASK_ALL
              : ~city->infrastructure.stadium_team_mask & CITY_TEAM_MASK_ALL},
  };
  return true;
}

// Reserve a shared budget-advisor question tied to its current budget and caller context. Rejects invalid identities
// or unavailable capacity; captured parent/helper state must survive until the matching reply.
bool QueueCityBudgetAdvisorQuestion(City *city, int id, i32 budget_session,
    CityBudgetCategory category) {
  const CityBudgetState *session = FindCityBudget(city, budget_session);
  if (id <= 0 || FindCityQuestion(city, id) || city->question_count == CITY_PENDING_QUESTION_CAPACITY || category < 0 ||
      category >= CITY_BUDGET_CATEGORY_COUNT || !session || session->close_requested)
    return false;
  int parent_question = session->advisor_question;
  bool helper_enabled = session->helper_stage != CITY_BUDGET_HELPER_DISABLED;
  SuspendCityBudgetHelperForAdvisor(city, budget_session, id);
  if (!BeginCityDialogPause(city, id)) {
    ResumeCityBudgetHelperAfterAdvisor(city, budget_session, id, parent_question, helper_enabled);
    return false;
  }
  CityBudgetAdvice advice = SelectCityBudgetAdvisorMessage(city, category);
  city->questions[city->question_count++] = (CityQuestion){.id = id,
      .kind = CITY_QUESTION_BUDGET_ADVISOR,
      .context.budget_advisor = {budget_session, category, CITY_BUDGET_ADVISOR_PORTRAIT_FIRST + category,
          advice, parent_question, helper_enabled}};
  return true;
}

// Check identity, reply readiness and the selected answer against the pending question. Opening reservations and
// unsupported choices are rejected without changing City or consuming the question.
bool CanReplyToCityQuestion(City *city, int id, CityQuestionReply reply) {
  const CityQuestion *question = FindCityQuestion(city, id);
  if (question == NULL || (reply.answer != CITY_DIALOG_OK && reply.answer != CITY_DIALOG_CANCEL) || reply.length < 0 ||
      reply.length > CITY_OVERLAY_NAME_MAX_LENGTH ||
      (reply.length != 0 && (reply.text == NULL || memchr(reply.text, 0, reply.length) != NULL)))
    return false;
  switch (question->kind) {
  case CITY_QUESTION_FACILITY_NAME:
    return reply.selection == 0;
  case CITY_QUESTION_STADIUM_TEAM:
    return reply.selection >= -1 && reply.selection < CITY_TEAM_NAME_COUNT &&
        (reply.answer == CITY_DIALOG_CANCEL || reply.selection == -1 ||
            (question->context.stadium_team.available_teams & (1 << reply.selection)));
  case CITY_QUESTION_BUDGET_ADVISOR: {
    const CityBudgetState *session = FindCityBudget(city, question->context.budget_advisor.budget_session);
    return !reply.length && !reply.selection && session && session->advisor_question == id;
  }
  default:
    return false;
  }
}

// Apply one admitted reply using the captured pending question context. Returns false for a rejected choice; callbacks
// may change City, so the real continuation uses live state and does not retry the initiating action.
static bool ApplyCityQuestionReply(City *city, int id, CityQuestionReply reply,
    void (*assign_team)(City *, TileIndex, int, const char *)) {
  if (!CanReplyToCityQuestion(city, id, reply)) return false;
  const CityQuestion *question = FindCityQuestion(city, id);
  switch (question->kind) {
  case CITY_QUESTION_FACILITY_NAME: {
    CityOverlayName name = question->context.facility_name.original_name;
    if (reply.answer == CITY_DIALOG_OK) {
      if (reply.length != 0)
        memcpy(name.character, reply.text, reply.length);
      name.character[reply.length] = 0;
    }
    // Copies the captured dialog name even on cancellation.
    CopyText((char *)CityOverlayNameAt(city, question->context.facility_name.slot)->character,
        sizeof(name.character), (const char *)name.character);
    break;
  }
  case CITY_QUESTION_BUDGET_ADVISOR: {
    EndCityDialogPause(city, id);
    ResumeCityBudgetHelperAfterAdvisor(city, question->context.budget_advisor.budget_session, id,
        question->context.budget_advisor.parent_question, question->context.budget_advisor.helper_enabled);
    break;
  }
  case CITY_QUESTION_STADIUM_TEAM: {
    const CityStadiumTeamQuestion *stadium = &question->context.stadium_team;
    if (reply.answer == CITY_DIALOG_CANCEL)
      break;
    const TileIndex tile_index = stadium->tile;
    // Resolves the live overlay on acceptance; the captured slot does not restrict replies.
    char name[CITY_OVERLAY_NAME_BYTES];
    if (reply.length != 0)
      memcpy(name, reply.text, reply.length);
    name[reply.length] = 0;
    assign_team(city, tile_index, reply.selection, name);
    if (reply.selection == -1)
      return true;
    break;
  }
  default:
    return false;
  }
  bool refresh_map = question->kind == CITY_QUESTION_FACILITY_NAME;
  RemoveCityQuestion(city, question);
  if (refresh_map)
    RefreshCityMapViews(city->game);
  return true;
}

// Apply a validated shared decision and complete only its matching continuation. Returns a signed action/error result;
// rejected replies leave the pending decision available and UI reports errors after return.
i64 ReplyToCityQuestion(City *city, int id, CityQuestionReply reply) {
  const CityQuestion *question = FindCityQuestion(city, id);
  bool rejected_team = question && question->kind == CITY_QUESTION_STADIUM_TEAM &&
      reply.answer != CITY_DIALOG_CANCEL && reply.selection == -1;
  if (!ApplyCityQuestionReply(city, id, reply, ApplyCityStadiumTeamChoice)) return CITY_COMMAND_COST_INVALID;
  return rejected_team ? -CITY_NOTICE_SELECT_VALID_STADIUM_SPORT : 1;
}

// Diagnostic callers retain the original blocking warning and consumed result. Returns the result of
// ApplyCityQuestionReply unchanged; its input and side-effect contract applies.
bool ReplyToQuestion(City *city, int id, CityQuestionReply reply) {
  return ApplyCityQuestionReply(city, id, reply, AcceptCityStadiumTeamDialog);
}
