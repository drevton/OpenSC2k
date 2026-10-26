#include "game/errors.h"

#include "game/state.h"

// Reset a recoverable operation's error record; a latched fatal execution failure remains set.
SC2K_PRESERVE_MOST void ClearGameError(Game *game) {
  game->reconstruction_error = 0;
}

SC2K_PRESERVE_MOST int GetGameError(const Game *game) {
  return game->reconstruction_error;
}

// Preserve the first error. Allocation failures always latch execution failure, even after another recorded error.
// This changes Game state only; it does not open a dialog or invoke callbacks.
SC2K_PRESERVE_MOST void SetGameError(Game *game, int error) {
  if (error != 0 && game->reconstruction_error == 0)
    game->reconstruction_error = error;
  // Allocation failures unwind into the normal shutdown path without opening another dialog.
  if (error == SC2K_ERROR_HOST_ALLOCATION_FAILURE || error == SC2K_ERROR_GUEST_ALLOCATION_FAILURE)
    game->execution_failed = true;
}
