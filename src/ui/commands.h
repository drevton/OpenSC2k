#ifndef SC2K_UI_COMMANDS_H
#define SC2K_UI_COMMANDS_H

#include "city/engine/cheats.h"
#include "city/engine/command_types.h"
#include "city/types.h"

typedef struct Game Game;
typedef struct CityInterfaceState CityInterfaceState;

typedef struct CityResourceNotice {
  struct CityResourceNotice *next;
  int bitmap, text;
  u32 window_id;
} CityResourceNotice;

// Borrowed by UI for one player command. Native gameplay errors come from its
// return value; this scope carries presentation details and legacy diagnostic feedback.
// The caller restores the enclosing scope after nested work. Collected notices remain
// owned by this scope until transferred to PresentCityCommandNotices or discarded.
typedef struct CityCommandFeedback {
  i64 error;
  TileIndex blocker, stadium_tile, inspection_tile;
  CityResourceNotice *notices;
  bool construction, reported, cancelled, notice_shown;
} CityCommandFeedback;

// Present action-specific construction sounds after the caller admits feedback. Arguments are borrowed;
// succeeded is the captured command outcome and cost is its net treasury debit, not a new price query.
// Other commands are silent. This does not submit another command or change deterministic City state.
void PresentCityCommandAudio(City *city, CityCommandId id, const CityCommandArguments *args,
    bool succeeded, i64 cost);

// Failure text is borrowed immutable presentation data.
const char *CityCommandFailureSummary(const CityCommand *command);
const char *CityCommandFailureReason(i64 error);
bool IsCityCommandFailureResource(u32 resource);

// Resolved command prompts and their retained presentation notices. Prompt text retains its opening quote;
// accepted actions use current City admission. Reply records live through deferred window completion.
i64 RequestCityCheat(Game *game, CityCheat cheat);
void PresentCityBondConfirmation(City *city, u32 id, int rate);
void PresentCityTunnelConfirmation(City *city, TileIndex tile);
void PresentCityNeighborConfirmation(City *city, CityCommand command, int cost);
i64 CheckCityConstructionCommand(City *city, CityCommand command);
// Record the reply and capture the command before closing/reconciling, then execute the resolved decision.
// This may close or reorder other windows; retain IDs across the call.
void AnswerCityConfirmation(City *city, u32 window_id, bool accept);
// Consume a detached command-notice list: present it in command order, or discard it before any window opens.
void PresentCityCommandNotices(City *city, CityResourceNotice *notices);
void DiscardCityCommandNotices(CityResourceNotice *notices);
// Clear queued notices; the open window's completion callback keeps ownership of its current notice.
void ClearCityResourceNotices(CityInterfaceState *ui);

// Record legacy tool feedback in the current UI scope without presenting a window. Keep the first error unless
// funds run out; blocker is an explicit refusing tile. No scope means no effect. Native commands return their errors.
void ReportCityCommandFailure(City *city, i64 error, TileIndex blocker);
void ReportCityCommandResult(City *city, i64 error);

#endif
