#include "platform.h"
#include <stdint.h>
#include <thrd_ndl/thrd_ndl.h>

#ifdef _WIN32
  #define NOMINMAX
  #include <windows.h>
  #include <memoryapi.h>
#else
  #include <sys/mman.h>
  #include <unistd.h>
  #include <time.h>
  #include <signal.h>
  #include <sys/time.h>

  static int preempt_cnt = 0;
  #define PREEMPT_TIMER_INTERVAL 10000
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
#ifdef _WIN32
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

#ifdef _WIN32
  (void)size;
  VirtualFree(ptr, 0, MEM_RELEASE);
#else
  munmap(ptr, size);
#endif
}

int protect_page(void* ptr, size_t size) {
#ifdef _WIN32
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
#ifdef _WIN32
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    return (size_t)sysInfo.dwPageSize;
#else
    return (size_t)sysconf(_SC_PAGESIZE);
#endif
  }
  return cached_page_size;
}

void preempt_disable(void) {
#ifdef _WIN32
  #error "TODO" 
#else
  if (preempt_cnt++ == 0) {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGVTALRM);
    sigprocmask(SIG_BLOCK, &sigset, NULL);
  }
#endif
}

void preempt_enable(void) {
#ifdef _WIN32
  #error "TODO"
#else
  if (--preempt_cnt == 0) {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGVTALRM);
    sigprocmask(SIG_UNBLOCK, &sigset, NULL);
  }
#endif
}

#ifndef _WIN32
static void signal_handler(int num) {
  (void)num;
  thrd_yield();
}
#endif

void timer_init(void) {
#ifdef _WIN32
  #error "TODO"
#else
  struct sigaction action = {
    .sa_handler = signal_handler,
    .sa_flags = SA_NODEFER,
  };
  sigaction(SIGVTALRM, &action, NULL);

  struct itimerval timer = {
    .it_value = {
      .tv_sec = 0,
      .tv_usec = PREEMPT_TIMER_INTERVAL,
    },
    .it_interval = {
      .tv_sec = 0,
      .tv_usec = PREEMPT_TIMER_INTERVAL,
    },
  };
  setitimer(ITIMER_VIRTUAL, &timer, NULL);
#endif
}
