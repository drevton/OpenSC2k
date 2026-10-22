#ifndef SC2K_GAME_ERRORS_H
#define SC2K_GAME_ERRORS_H

#include "game/state.h"

enum {
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
};

// Preserve the first failure until the caller starts another operation.
static inline void ClearGameError(Game *game) {
  game->reconstruction_error = 0;
}

static inline int GetGameError(const Game *game) {
  return game->reconstruction_error;
}

static inline void SetGameError(Game *game, int error) {
  if (error != 0 && game->reconstruction_error == 0)
    game->reconstruction_error = error;
  // Allocation failures unwind into the normal shutdown path without opening another dialog.
  if (error == SC2K_ERROR_HOST_ALLOCATION_FAILURE || error == SC2K_ERROR_GUEST_ALLOCATION_FAILURE)
    game->execution_failed = true;
}

#endif
