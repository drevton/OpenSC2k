#ifndef SC2K_GAME_ERRORS_H
#define SC2K_GAME_ERRORS_H

#include "game/state.h"

// Named execution errors; the error record also accepts other nonzero backend codes.
typedef enum GameError {
  SC2K_ERROR_GUEST_INTEGER_DIVIDE_BY_ZERO = -3,
  SC2K_ERROR_GUEST_ALLOCATION_FAILURE = -4,
  SC2K_ERROR_GUEST_BUFFER_TOO_SMALL = -5,
  SC2K_ERROR_GUEST_INTEGER_DIVIDE_OVERFLOW = -6,
  SC2K_ERROR_HOST_INPUT_RECORDING_OVERFLOW = -7,
  SC2K_ERROR_UNRECORDED_TIMER_QUEUE = -8,
  SC2K_ERROR_UNRECORDED_APPLICATION_MESSAGE = -9, // Comparison only.
  SC2K_ERROR_STARTUP_ABORT = -10,
  SC2K_ERROR_HOST_ALLOCATION_FAILURE = -11,
  SC2K_ERROR_MAP_SPRITE_CAPACITY = -12,
  SC2K_ERROR_NUMBER_FORMAT = -130,
} GameError;

// Reset a recoverable operation's error record; a latched fatal execution failure remains set.
static inline void ClearGameError(Game *game) {
  game->reconstruction_error = 0;
}

static inline int GetGameError(const Game *game) {
  return game->reconstruction_error;
}

// Preserve the first error. Allocation failures always latch execution failure, even after another recorded error.
// This changes Game state only; it does not open a dialog or invoke callbacks.
static inline void SetGameError(Game *game, int error) {
  if (error != 0 && game->reconstruction_error == 0)
    game->reconstruction_error = error;
  // Allocation failures unwind into the normal shutdown path without opening another dialog.
  if (error == SC2K_ERROR_HOST_ALLOCATION_FAILURE || error == SC2K_ERROR_GUEST_ALLOCATION_FAILURE)
    game->execution_failed = true;
}

#endif
