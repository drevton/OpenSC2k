#include "city/saveload/runtime.h"

#include <assert.h>
#include <stdlib.h>

#include "base/file.h"
#include "base/text.h"
#include "city/saveload/services.h"
#include "city/saveload/state.h"
#include "game/state.h"

// Replace Game's relative-file directory with the current host directory. A failed query preserves the old value.
bool InitializeCityFileDirectory(Game *game) {
  char *directory = ReadWorkingDirectory();
  if (directory == NULL) return false;
  free(game->file_directory);
  game->file_directory = directory;
  return true;
}

// Borrow the current directory until Game replaces or releases its owned path.
const char *CityFileDirectory(const CityFileRuntime *file) { return file->game->file_directory; }

// Copy a new relative-file directory into Game's owned host state.
void SetCityFileDirectory(CityFileRuntime *file, const char *path) { SetText(&file->game->file_directory, path); }

// Read the local backup preference from the file operation's Game.
bool ShouldBackupCitySave(const CityFileRuntime *file) { return file->game->backup_saves; }

// File operation lifetime

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
