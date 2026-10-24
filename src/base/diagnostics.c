#include "base/diagnostics.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "base/file.h"

static const int kFatalSignals[] = {SIGSEGV, SIGILL, SIGABRT, SIGFPE,
#ifndef _WIN32
  SIGBUS,
#endif
};
enum { kFatalSignalCount = sizeof(kFatalSignals) / sizeof(kFatalSignals[0]) };

// The owner releases installed handlers and its saved stderr descriptor. The signal
// handler uses only its arguments and stderr, with no mutable globals or unsafe stdio.
struct Diagnostics {
  int stderr_copy;
  bool handlers_installed;
#ifdef _WIN32
  void (*previous[kFatalSignalCount])(int);
#else
  struct sigaction previous[kFatalSignalCount];
#endif
};

#ifdef _WIN32
static void ReportFatalSignal(int signal_number) {
#else
static void ReportFatalSignal(int signal_number, siginfo_t *info, void *context) {
  (void)context;
#endif
  char message[96] = "Fatal signal ";
  int length = 13;
  message[length++] = '0' + signal_number / 10;
  message[length++] = '0' + signal_number % 10;
#ifndef _WIN32
  const char suffix[] = " at 0x";
  memcpy(message + length, suffix, sizeof(suffix) - 1);
  length += sizeof(suffix) - 1;
  uintptr_t address = (uintptr_t)info->si_addr;
  for (int shift = sizeof(address) * 8 - 4; shift >= 0; shift -= 4)
    message[length++] = "0123456789abcdef"[(address >> shift) & 15];
#endif
  message[length++] = '\n';
  ssize_t written = write(STDERR_FILENO, message, length);
  (void)written;
#ifdef _WIN32
  signal(signal_number, SIG_DFL);
  raise(signal_number);
#else
  // Send the signal again after SA_RESETHAND resets its disposition.
  kill(getpid(), signal_number);
#endif
}

void CloseDiagnostics(Diagnostics *state) {
  if (!state) return;
  if (state->handlers_installed)
    for (int i = 0; i < kFatalSignalCount; ++i) {
#ifdef _WIN32
      signal(kFatalSignals[i], state->previous[i]);
#else
      sigaction(kFatalSignals[i], &state->previous[i], NULL);
#endif
    }
  if (state->stderr_copy >= 0) RestoreErrorLog(state->stderr_copy);
  free(state);
}

u32 OpenDiagnostics(Diagnostics **owned, const char *log_path, u32 flags) {
  CloseDiagnostics(*owned);
  *owned = NULL;
  if (!flags) return 0;
  Diagnostics *state = calloc(1, sizeof(*state));
  if (!state) return 0;
  state->stderr_copy = -1;
  *owned = state;
  if (flags & DIAGNOSTICS_LOG) {
    state->stderr_copy = RedirectErrorLog(log_path);
    if (state->stderr_copy >= 0) fprintf(stderr, "\nDiagnostics: %s\n", log_path);
    else {
      flags &= ~DIAGNOSTICS_LOG;
      fprintf(stderr, "Could not open the diagnostic log.\n");
    }
  }
  if (flags & DIAGNOSTICS_CRASH) {
    int installed = 0;
#ifdef _WIN32
    for (; installed < kFatalSignalCount; ++installed) {
      state->previous[installed] = signal(kFatalSignals[installed], ReportFatalSignal);
      if (state->previous[installed] == SIG_ERR) break;
    }
#else
    struct sigaction action = {0};
    action.sa_sigaction = ReportFatalSignal;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    for (; installed < kFatalSignalCount; ++installed)
      if (sigaction(kFatalSignals[installed], &action, &state->previous[installed])) break;
#endif
    state->handlers_installed = installed == kFatalSignalCount;
    if (!state->handlers_installed) {
      while (installed) {
        --installed;
#ifdef _WIN32
        signal(kFatalSignals[installed], state->previous[installed]);
#else
        sigaction(kFatalSignals[installed], &state->previous[installed], NULL);
#endif
      }
      flags &= ~DIAGNOSTICS_CRASH;
      fprintf(stderr, "Could not install crash diagnostics.\n");
    }
  }
  return flags;
}
