#include "game/settings.h"
#include <assert.h>

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "city/saveload/file.h"

#include "city/saveload/runtime.h"
#include "city/state.h"
#include "game/state.h"
#include "game/errors.h"
#include "game/state.h"
#include "audio/state.h"
#include "assets/state.h"
#include "assets/catalog.h"
#include "base/text.h"

// Ensure a CityPath can hold capacity bytes plus its terminator while preserving existing text. Requires initialized
// path metadata and capacity sufficient for retained text; returns borrowed owner storage or NULL for a negative
// capacity.
char *ReserveCityPath(Game *game, CityPath *path, int capacity) {
  if (capacity < 0) {
    SetGameError(game, SC2K_ERROR_GUEST_BUFFER_TOO_SMALL);
    return NULL;
  }
  if (path->overflow != NULL && capacity <= path->overflow_capacity) return path->overflow;
  if (path->overflow == NULL && capacity < CITY_PATH_CAPACITY) return path->text;
  char *text = AllocateText((size_t)capacity + 1);
  memcpy(text, CityPathText(path), (size_t)path->length + 1);
  free(path->overflow);
  path->overflow = text;
  path->overflow_capacity = capacity;
  return text;
}

// Copy a byte-counted host path and maintain its terminator and inline/overflow ownership. Nonempty input must provide
// length readable bytes; negative length returns false and records an error, while path owns any replacement storage.
bool SetCityPathBytes(Game *game, CityPath *path, int length, const char *text) {
  if (length < 0) {
    SetGameError(game, SC2K_ERROR_GUEST_BUFFER_TOO_SMALL);
    return false;
  }
  if (length < CITY_PATH_CAPACITY) {
    if (length != 0) memmove(path->text, text, (size_t)length);
    path->text[length] = 0;
    free(path->overflow);
    path->overflow = NULL;
    path->overflow_capacity = 0;
  } else if (path->overflow != NULL && length <= path->overflow_capacity) {
    memmove(path->overflow, text, (size_t)length);
    path->overflow[length] = 0;
  } else {
    char *replacement = AllocateText((size_t)length + 1);
    memcpy(replacement, text, (size_t)length);
    replacement[length] = 0;
    free(path->overflow);
    path->overflow = replacement;
    path->overflow_capacity = length;
  }
  path->length = length;
  return true;
}

// Release a path's owned overflow and reset the entire path value. Requires initialized storage; existing pointers to
// either active representation must not be used afterward.
void FreeCityPathStorage(CityPath *path) {
  free(path->overflow);
  *path = (CityPath){0};
}

// Release Game's owned host path strings and directory storage. Requires initialized path members; this does not close
// file streams or destroy the City.
void ReleaseCityFilePaths(Game *game) {
  FreeCityPathStorage(&game->city_path);
  free(game->file_directory);
  game->file_directory = NULL;
  FreeCityPathStorage(&game->city_working_directory);
  FreeCityPathStorage(&game->profile_base_directory);
  FreeCityPathStorage(&game->command_line_path);
}


// Services


// Push a caller-owned file runtime onto Game's operation stack and inherit its parent's error. Runtime storage must
// remain alive until EndCityFileOperation or an explicit suspension; nested operations keep separate cleanup and
// scratch state.
void BeginCityFileOperation(Game *game, CityFileRuntime *file) {
  *file = (CityFileRuntime){.game = game, .previous = game->file};
  if (file->previous != NULL) file->error = file->previous->error;
  game->file = file;
}

// Detach the current file operation while a presentation lifetime outlives its caller. Requires no active cleanup,
// exception, scratch or save-attempt state; clears the previous link because that parent may return before resume.
void SuspendCityFileOperation(CityFileRuntime *file) {
  assert(file->game->file == file);
  assert(file->cleanup == NULL && file->load_io.exception_frame == NULL && !file->load_io.unwinding);
  assert(file->scratch == NULL && file->save_size == NULL && !file->misc_owned && file->save_io.attempt == NULL);
  if (file->previous) file->previous->error = file->error;
  file->game->file = file->previous;
  file->previous = NULL; // The previous operation may return before the window closes.
}

// Reattach a suspended file runtime above the currently active operation. Its caller-owned storage and Game must
// remain alive; the new previous link is captured at resume time.
void ResumeCityFileOperation(CityFileRuntime *file) {
  file->previous = file->game->file;
  file->game->file = file;
}

// Pop the current operation and propagate its error to the parent. Asserts that streams, cleanups, exception frames,
// attempts and scratch ownership have already been released; does not clean them up implicitly.
void EndCityFileOperation(CityFileRuntime *file) {
  assert(file->game->file == file);
  assert(file->cleanup == NULL && file->load_io.load_stream_count == 0 && file->save_io.attempt == NULL);
  assert(file->scenario_input == NULL);
  assert(file->load_io.exception_frame == NULL && !file->load_io.unwinding);
  assert(file->scratch == NULL && file->save_size == NULL && !file->misc_owned);
  if (file->previous != NULL) file->previous->error = file->error;
  file->game->file = file->previous;
}

// Release a registered scratch buffer through the file owner captured in its cleanup context. Context must remain
// alive during unwinding and must identify a matching scratch allocation.
void ReleaseCityFileScratchBuffer(void *context) {
  CityFileScratchCleanup *buffer = context;
  FreeCityFileScratch(buffer->file, buffer->bytes);
}

// Push a caller-owned destructor record onto the operation's cleanup stack. Record and context must remain alive until
// popped or unwound; registration itself does not run the destructor.
void PushCityFileCleanup(CityFileRuntime *file, CityFileCleanup *cleanup) {
  cleanup->previous = file->cleanup;
  file->cleanup = cleanup;
}

// Remove exactly the top registered cleanup without invoking its destructor. Asserts LIFO order; normal-path
// destruction remains the caller's responsibility.
void PopCityFileCleanup(CityFileRuntime *file, CityFileCleanup *cleanup) {
  assert(file->cleanup == cleanup);
  file->cleanup = cleanup->previous;
}

// Return the operation's current cleanup-stack marker without changing it. The pointer is borrowed and is valid only
// while its registered caller frame remains alive.
CityFileCleanup *CityFileCleanupTop(const CityFileRuntime *file) { return file->cleanup; }

// Pop and invoke destructors until the captured cleanup marker is reached. Marker must be NULL or a live ancestor in
// this stack; entries are unlinked before callbacks so nested work cannot destroy them twice.
void UnwindCityFileCleanups(CityFileRuntime *file, CityFileCleanup *until) {
  while (file->cleanup != until) {
    CityFileCleanup *cleanup = file->cleanup;
    assert(cleanup != NULL);
    file->cleanup = cleanup->previous;
    cleanup->destroy(cleanup->context);
  }
}

// The creation date chooses the post-1993 thumbnail transparency convention. Path must be a terminated host path; an
// unreadable creation date returns false instead of using uninitialized file-status data.
bool CityFileUsesTransparentThumbnail(const char *path) {
  FileLocalDate date;
  if (!ReadFileCreationDate(path, &date))
    return false; // Defined replacement for stock uninitialized CFileStatus after a failed query.
  u32 packed_date = (u32)date.year * 10000 + (u32)date.month * 100 + (u32)date.day;
  return (int)packed_date >= 19931231;
}
