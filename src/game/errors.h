#ifndef SC2K_GAME_ERRORS_H
#define SC2K_GAME_ERRORS_H

#include "base/types.h"

typedef struct Game Game;

// Named execution errors; the error record also accepts other nonzero backend codes.
typedef enum GameError {
  SC2K_ERROR_GUEST_INTEGER_DIVIDE_BY_ZERO = -3,
  SC2K_ERROR_GUEST_ALLOCATION_FAILURE = -4,
  SC2K_ERROR_GUEST_BUFFER_TOO_SMALL = -5,
  SC2K_ERROR_GUEST_INTEGER_DIVIDE_OVERFLOW = -6,
  SC2K_ERROR_HOST_INPUT_RECORDING_OVERFLOW = -7,
  SC2K_ERROR_UNRECORDED_TIMER_QUEUE = -8,
  SC2K_ERROR_UNRECORDED_APPLICATION_MESSAGE = -9,
  SC2K_ERROR_STARTUP_ABORT = -10,
  SC2K_ERROR_HOST_ALLOCATION_FAILURE = -11,
  SC2K_ERROR_MAP_SPRITE_CAPACITY = -12,
  SC2K_ERROR_NUMBER_FORMAT = -130,
} GameError;

// Reset a recoverable operation's error record; a latched fatal execution failure remains set.
SC2K_PRESERVE_MOST void ClearGameError(Game *game);

SC2K_PRESERVE_MOST int GetGameError(const Game *game);

// Preserve the first error. Allocation failures always latch execution failure, even after another recorded error.
// This changes Game state only; it does not open a dialog or invoke callbacks.
SC2K_PRESERVE_MOST void SetGameError(Game *game, int error);

#endif
