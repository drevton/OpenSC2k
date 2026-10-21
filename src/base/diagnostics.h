#ifndef SC2K_BASE_DIAGNOSTICS_H
#define SC2K_BASE_DIAGNOSTICS_H

#include "base/types.h"

typedef struct Diagnostics Diagnostics;
enum { DIAGNOSTICS_LOG = 1, DIAGNOSTICS_CRASH = 2 };
// Replaces the owned resources and returns the flags successfully enabled.
u32 OpenDiagnostics(Diagnostics **owned, const char *log_path, u32 flags);
void CloseDiagnostics(Diagnostics *state);

#endif
