#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#include "base/time.h"

// Return elapsed host seconds with sub-millisecond precision. The arbitrary origin
// is shared by all callers; this clock never changes City simulation state.
double MonotonicSeconds(void) {
#ifdef _WIN32
  LARGE_INTEGER counter, frequency;
  QueryPerformanceCounter(&counter);
  QueryPerformanceFrequency(&frequency);
  return (double)counter.QuadPart / frequency.QuadPart;
#else
  struct timespec time;
  clock_gettime(CLOCK_MONOTONIC, &time);
  return (double)time.tv_sec + (double)time.tv_nsec / 1e9;
#endif
}
