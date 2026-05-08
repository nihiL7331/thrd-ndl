#pragma once

#include <stdint.h>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <time.h>
#endif

// default stack size for each thread
#define THRD_STACK_SIZE 1024 * 64 // 64 KB

// return codes
#define THRD_SUCCESS 0
#define THRD_OOM -1

static inline uint64_t get_os_time(void) {
#ifdef _WIN32
  static LARGE_INTEGER win_freq = {0};
  if (win_freq.QuadPart == 0)
    QueryPerformanceFrequency(&win_freq);

  LARGE_INTEGER ticks;
  QueryPerformanceCounter(&ticks);

  return (ticks.QuadPart * 1000ULL) / win_freq.QuadPart;
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);

  return (ts.tv_sec * 1000ULL) + (ts.tv_nsec / 1000000ULL);
#endif
}
