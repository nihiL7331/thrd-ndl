#include "platform.h"
#include <stdint.h>

#if defined(_WIN32)
  #define NOMINMAX
  #include <windows.h>
  #include <memoryapi.h>
#else
  #include <sys/mman.h>
  #include <unistd.h>
  #include <time.h>
#endif

uint64_t get_os_time(void) {
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

// OS call for memory page
void* os_alloc(size_t size) {
#if defined(_WIN32)
  void* ptr = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  return ptr;
#else
  void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
  return (ptr == MAP_FAILED) ? NULL : ptr;
#endif
}

// OS call for free
void os_free(void* ptr, size_t size) {
  if (ptr == NULL) 
    return;

#if defined(_WIN32)
  (void)size;
  VirtualFree(ptr, 0, MEM_RELEASE);
#else
  munmap(ptr, size);
#endif
}

int protect_page(void* ptr, size_t size) {
#if defined (_WIN32)
  DWORD old_prot = 0;
  BOOL success = (int)VirtualProtect(ptr, size, PAGE_NOACCESS, &old_prot);
  return success ? 0 : -1;
#else
  return mprotect(ptr, size, PROT_NONE);
#endif
}

size_t page_size(void) {
  static size_t cached_page_size = 0;
  if (cached_page_size == 0) {
#if defined(_WIN32)
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    return (size_t)sysInfo.dwPageSize;
#else
    return (size_t)sysconf(_SC_PAGESIZE);
#endif
  }
  return cached_page_size;
}
