#ifndef SC2K_GAME_COMMANDS_H
#define SC2K_GAME_COMMANDS_H

#include "city/engine/commands.h"

typedef struct Game Game;
typedef struct CityPointerInput CityPointerInput;

// Host completion data is local to Game. It is not part of the shared City command packet.
typedef struct CityCommandResult {
  i64 value; // Negative gameplay error on failure; nonnegative command value on success.
  i64 cost, error; // Net treasury debit over the call, and gameplay failure, separately from admission.
  i32 host_error;
  bool succeeded, cancelled;
} CityCommandResult;

typedef void CityCommandCallback(Game *game, u32 id, const CityCommandResult *result,
    TileIndex tile, int parameter1, int parameter2, int parameter3, void *context);

// RunCityCommand is a player/UI action and displays returned errors by default.
// DoCityCommand executes or queries internally without opening an error window.
// Single-player execution starts immediately. Timed work and dialogs
// retain completion until their final return; network transport may also defer it.
// Immutable data borrowed for the synchronous call; a queued transport must own
// its copy. Callback/context are local presentation state, never wire payload.
typedef struct {
  const u8 *text;
  u32 length;
} CityCommandPayload;

bool RunCityCommand(Game *game, CityCommandId id, TileIndex tile, int parameter1, int parameter2,
    int parameter3, CityCommandCallback *callback, void *context);
bool RunCityCommandWithPayload(Game *game, CityCommandId id, TileIndex tile, int parameter1, int parameter2,
    int parameter3, const CityCommandPayload *payload, CityCommandCallback *callback, void *context);

// Packet execution preserves the numerical handler return; WithInput reports only whether that value is nonnegative.
// Optional comparison may supply pointer samples; native execution accepts none.
// Ordinary UI actions use RunCityCommand.
i64 ExecuteRecordedCityCommandWithPayload(Game *game, CityCommand packet, const CityPointerInput *input, u32 count,
    const u8 *text, u32 length);
bool ExecuteRecordedCityCommandWithInput(Game *game, CityCommand packet, const CityPointerInput *input, u32 count);
i64 ExecuteRecordedCityCommand(Game *game, u32 id, i32 a, i32 b, i32 c);

// A suspended animation resumes its retained command before presenting any resulting notices.
bool StepCityAnimation(Game *game);
// Teardown releases pending caller records without invoking their callbacks or freeing callback contexts.
void ReleaseCityPostedCommands(Game *game);

// Borrowed observation inputs for one command suspended at an animation boundary.
// The feedback record and descriptor need remain alive only until RetainCityCommandObservation returns.
typedef struct {
  const struct GameObservation *observer;
  void *scope;
  u32 before_tick;
  CityCommand packet;
  const struct CityCommandFeedback *feedback;
} CityCommandObservationScope;

// Allocate a Game-owned record containing a copy of the observation. Success transfers responsibility for ending
// its scope and consuming feedback notices to Game. Requires non-NULL observer and feedback; observer remains borrowed
// until completion/teardown. Failure leaves Game and errors unchanged so the caller can cancel its live tick before
// reporting failure. The descriptor and feedback record are not retained.
bool RetainCityCommandObservation(Game *game, const CityCommandObservationScope *observation);

// Complete one retained callback, or keep it attached when result.value identifies a live tick continuation.
// Final completion recomputes its net treasury debit, merges earlier errors/cancellation, and consumes the record
// before calling user code. The caller supplies result.error separately from result.value. Unknown tokens are ignored.
// Callback/context remain borrowed from RunCityCommand; completion never releases the context.
void CompletePendingCityCommand(Game *game, int token, CityCommandResult result);

// Deliver pending simulation notices/decisions to this UI after deterministic work returns.
void DeliverCitySimulationEvents(Game *game, int token, i64 result, bool cancelled);

#endif
