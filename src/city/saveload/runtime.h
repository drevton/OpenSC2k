#ifndef SC2K_CITY_SAVELOAD_RUNTIME_H
#define SC2K_CITY_SAVELOAD_RUNTIME_H

#include <stddef.h>

#include "city/saveload/path.h"
#include "city/saveload/types.h"
#include "city/types.h"

// File context

typedef struct Game Game;
typedef struct CityFileRuntime CityFileRuntime;
typedef struct CityFileExceptionState CityFileExceptionState;

// Query the host working directory and copy it into Game; failure preserves the previous directory.
bool InitializeCityFileDirectory(Game *game);
// Borrowed Game storage, valid until the directory is replaced or released. Set copies a terminated host path.
const char *CityFileDirectory(const CityFileRuntime *file);
void SetCityFileDirectory(CityFileRuntime *file, const char *path);
bool ShouldBackupCitySave(const CityFileRuntime *file);

// File operation lifetime

// Cleanup records and their contexts are caller-owned. Keep them alive until popped or unwound.
// Pop removes only the top record without running it; unwind unlinks each record before calling its destructor.
typedef struct CityFileCleanup {
  void (*destroy)(void *context);
  void *context;
  struct CityFileCleanup *previous;
} CityFileCleanup;
// Native temporary buffers are released before an escaping file exception.
typedef struct {
  CityFileRuntime *file;
  void *bytes;
} CityFileScratchCleanup;
void ReleaseCityFileScratchBuffer(void *context);
// Begin initializes caller storage, inherits its parent's error and installs it on Game. End requires all
// streams, cleanups, scratch and retained scenario state to be released, then restores the parent and its error.
void BeginCityFileOperation(Game *game, CityFileRuntime *file);
void EndCityFileOperation(CityFileRuntime *file);
// Window-owned operations may retain streams, but never stack cleanup/exception frames.
void SuspendCityFileOperation(CityFileRuntime *file);
void ResumeCityFileOperation(CityFileRuntime *file);
void PushCityFileCleanup(CityFileRuntime *file, CityFileCleanup *cleanup);
void PopCityFileCleanup(CityFileRuntime *file, CityFileCleanup *cleanup);
CityFileCleanup *CityFileCleanupTop(const CityFileRuntime *file);
void UnwindCityFileCleanups(CityFileRuntime *file, CityFileCleanup *until);

// File workspace

// Borrow a zeroed command-pool slot or allocate zeroed fallback storage. Free only through FreeCityFileScratch
// with this operation, before its scratch scope ends. Failure returns NULL and records a Game allocation error.
void *AllocateCityFileScratch(CityFileRuntime *file, size_t size);
void FreeCityFileScratch(CityFileRuntime *file, void *pointer);
// Replace MISC with zeroed scratch or heap storage, releasing the previous workspace first. Prepare may fail;
// release detaches borrowed storage or frees owned storage and clears the operation's workspace state.
bool PrepareCityMiscWorkspace(CityFileRuntime *file);
void ReleaseCityMiscWorkspace(CityFileRuntime *file);

// Run synchronously inside a scratch and exception boundary. Errors unwind registered cleanups and report the
// failure while preserving partial City writes; nested commands restore the parent's workspace and exception frame.
typedef void (*CityFileAction)(void *context);
void RunCityFileCommand(CityFileRuntime *file, CityFileAction action, void *context);

// City Save

// Begin before the complete Save/Save As command caller. A picker may select
// its final native path later, before the first open. Nested attempts isolate
// destinations and first errno, then restore their parent on end. These scopes
// own host memory only. Begin returns NULL and sets errno on failure.
typedef struct NativeCitySaveAttempt NativeCitySaveAttempt;
NativeCitySaveAttempt *BeginCitySaveAttempt(CityFileRuntime *file);
bool SelectCitySaveDestination(CityFileRuntime *file, NativeCitySaveAttempt *attempt,
    const char *destination);
i32 GetCitySaveAttemptError(const CityFileRuntime *file, const NativeCitySaveAttempt *attempt);
// Returns the attempt's first errno, or 0 if no operation failed. End requires
// the active top scope and no open streams; invalid order returns EBUSY and
// leaves every scope intact. NULL returns EINVAL. A valid end frees the pointer
// even when its saved I/O errno is nonzero.
i32 EndCitySaveAttempt(CityFileRuntime *file, NativeCitySaveAttempt *attempt);

// Zero after successful platform operations, otherwise the first errno from
// active attempt, or the last ended attempt if none is active. Close/remove
// errors are visible here even when the original
// ordinary body would have left via a platform exception instead of returning.
i32 GetCitySaveIOError(const CityFileRuntime *file);
// Translate host errno into the game-visible file error fields.
void FillCitySaveFileError(const CityFileRuntime *file, CityFileExceptionState *exception);

// City Load

// Resolve and replace Game's directory for relative city paths. Returns zero on success or -1 on failure and
// records the I/O outcome; path is borrowed for the call.
i32 ChangeCityLoadDirectory(CityFileRuntime *file, const char *path);

#endif
