#ifndef SC2K_CITY_SAVELOAD_RUNTIME_H
#define SC2K_CITY_SAVELOAD_RUNTIME_H

#include "city/saveload/path.h"
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

// Run synchronously inside a scratch and exception boundary. Errors unwind registered cleanups and report the
// failure while preserving partial City writes; nested commands restore the parent's workspace and exception frame.
typedef void (*CityFileAction)(void *context);
void RunCityFileCommand(CityFileRuntime *file, CityFileAction action, void *context);

// City Save

typedef enum NativeSaveOperation {
  NATIVE_SAVE_OPEN = 1,
  NATIVE_SAVE_WRITE = 2,
  NATIVE_SAVE_SEEK = 3,
  NATIVE_SAVE_CLOSE = 4,
  NATIVE_SAVE_REMOVE = 5,
  NATIVE_LOAD_OPEN = 6,
  NATIVE_LOAD_READ = 7,
  NATIVE_LOAD_SEEK = 8,
  NATIVE_LOAD_CLOSE = 9,
  NATIVE_LOAD_DIRECTORY = 10
} NativeSaveOperation;

// Platform observations, not guest mutations. Write result is the byte count;
// other results are 0/1. Input is valid only for the duration of the call.
typedef struct NativeSaveIOOutcome {
  NativeSaveOperation operation;
  u32 requested;
  u32 result;
  i32 error;
  const u8 *input;
  u32 input_size;
} NativeSaveIOOutcome;

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

// Save to an accepted host destination inside a private I/O attempt. The caller retains an active file operation
// and a mutable CityPath; serialization may normalize that path. The attempt copies destination before writing.
// The caller owns the chooser, name changes and final path commit. False includes serialization or close failure.
bool SaveCityToDestination(City *city, CityPath *path,
    const char *destination);
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

// File presentation

// Text is borrowed only for this call. The opened window owns its displayed copy through dismissal.
void PresentCityFileNotice(City *city, const char *text, bool error);
void SetCityHostWindowTitle(City *city, const char *text);
void InvalidateLoadedCityCanvas(City *city);

#endif
