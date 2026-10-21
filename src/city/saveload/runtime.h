#ifndef SC2K_CITY_SAVELOAD_RUNTIME_H
#define SC2K_CITY_SAVELOAD_RUNTIME_H

#include "city/engine/commands.h"
#include "city/saveload/file.h"
#include "city/types.h"

// City Bootstrap

typedef struct Game Game;

// Native startup initializes supplied owners without original-image transport.
bool InitializeGameCity(Game *game, const char *path);

enum { CITY_FILE_DIRECTORY_CAPACITY = 3 * CITY_FILE_DIALOG_PATH_LIMIT + 1 };
char *CityFileDirectory(const CityFileRuntime *file);
void SetCityFileDirectory(CityFileRuntime *file, const char *path);
bool ShouldBackupCitySave(const CityFileRuntime *file);

void ReleaseGameCityAssets(Game *game);

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

// Invoke ONLY from the unified CityCommand mutation handler. The caller owns
// an owned CityPath (existing Save path or Save As local). Destination is an explicit final filesystem path, including extension;
// it is kept separate so game path lowercasing cannot rename POSIX directory
// components. Cancellation happens before this call. This adapter invokes the
// complete SaveCityFile inside a new nested attempt; Save/Save As caller/name/
// document effects remain in the command handler. Returns false on any I/O failure, including close.
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

bool InitializeCityFileDirectory(Game *game);
// WM_COMMAND framework exception boundary; action retains its original ABI.
i32 ChangeCityLoadDirectory(CityFileRuntime *file, const char *path);
void NotifyCityLoaded(City *city);
bool ShowCityLoadResourceMessage(City *city, u32 resource, u32 flags, u32 help);

// File Dialog Actions

// Canvas continuations enter the same command dispatcher as ordinary input.
void RecordCityFileDialogCheckpoint(City *city, u32 kind, i32 token, i32 answer, const char *path);
void PresentCityFileNotice(City *city, const char *text, bool error);
void SetCityHostWindowTitle(City *city, const char *text);
void InvalidateLoadedCityCanvas(City *city);

#endif
