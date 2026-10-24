#ifndef SC2K_BASE_DIAGNOSTICS_H
#define SC2K_BASE_DIAGNOSTICS_H

#include "base/types.h"

typedef struct Diagnostics Diagnostics;
enum { DIAGNOSTICS_LOG = 1, DIAGNOSTICS_CRASH = 2 };
// Close the previous record first, then attempt the requested log redirection and fatal-signal handlers.
// Return the successfully enabled flags. Initialize *owned to NULL; even a zero result may leave a record to close.
// The path is borrowed for this call only. Partial handler installation restores the handlers already replaced.
u32 OpenDiagnostics(Diagnostics **owned, const char *log_path, u32 flags);
// Restore installed handlers and stderr, then free state. NULL is a no-op; the caller clears its owning pointer.
void CloseDiagnostics(Diagnostics *state);

#endif
