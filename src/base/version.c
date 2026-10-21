#include "base/version.h"

#include <stdio.h>

// The build patches the bytes after this 32-byte marker. Volatile reads prevent
// whole-program optimization from substituting the initial placeholder text.
static const volatile char kGitRevision[32 + SC2K_GIT_REVISION_CAPACITY] =
    "SC2K_GIT_REVISION_SLOT_V1:______"
    "UNSTAMPED";

void ReadBuildRevision(char *text, size_t capacity) {
  if (!capacity) return;
  size_t length = 0;
  while (length + 1 < capacity && length + 1 < SC2K_GIT_REVISION_CAPACITY) {
    char character = kGitRevision[32 + length];
    if (!character) break;
    text[length++] = character;
  }
  text[length] = 0;
}

void FormatBuildVersion(char *text, size_t capacity) {
  char revision[SC2K_GIT_REVISION_CAPACITY];
  ReadBuildRevision(revision, sizeof(revision));
  snprintf(text, capacity, SC2K_APP_TITLE " (%s)", revision);
}
