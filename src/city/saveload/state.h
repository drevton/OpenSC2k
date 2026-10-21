// State
#ifndef SC2K_FILE_STATE_H
#define SC2K_FILE_STATE_H

#include "city/saveload/types.h"
#include "city/saveload/path.h"
#include "city/types.h"

typedef void (*CityFileAction)(void *context);

// I/O errors and serialization scratch are operation state, not saved City state.
typedef struct CityFileExceptionState {
  i32 cause;
  i32 os_error; // Native errno; comparison adapters retain stock Win32 error values.
} CityFileExceptionState;

typedef struct {
  u32 words[CITY_MISC_WORD_COUNT];
} CityMiscBuffer;

enum { CITY_FILE_SCRATCH_COUNT = 2, CITY_FILE_SCRATCH_CAPACITY = 32768 };
typedef struct {
  CityMiscBuffer misc;
  u32 save_size;
  u8 bytes[CITY_FILE_SCRATCH_COUNT][CITY_FILE_SCRATCH_CAPACITY];
  bool used[CITY_FILE_SCRATCH_COUNT];
} CityFileScratch;

// Host Save scopes are isolated per file owner, never shared through the root.
typedef struct {
  struct NativeCitySaveAttempt *attempt;
  int last_error;
} CitySaveIOState;

// Transient filesystem handles and unwind context belong to the active operation.
// The old directory-bearing service layout remains only in reference builds.
typedef struct {
  struct CityFileExceptionFrame *exception_frame;
  int unwinding;
  int load_stream_count;
  int open_error;
} CityLoadIOState;
typedef struct Game Game;
typedef struct CityFileRuntime {
  Game *game;
  struct CityFileRuntime *previous;
  void *scenario_input; // Retained until the instructions continuation finishes.
  char scenario_instructions[CITY_SCENARIO_INSTRUCTION_BYTES + 1];
  CityMiscBuffer *misc; // Borrowed from the active operation, or owned fallback.
  bool misc_active, misc_owned;
  u32 *save_size; // Borrowed byte count of the active Save attempt.
  CityFileExceptionState error;
  // Borrowed only while a save/load operation owns its temporary buffers.
  CityFileScratch *scratch;
  struct CityFileCleanup *cleanup;
  CitySaveIOState save_io;
  CityLoadIOState load_io;
} CityFileRuntime;
#endif
