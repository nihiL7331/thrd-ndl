#include "platform.h"
#include <stdint.h>
#include <thrd_ndl/thrd_ndl.h>

#ifdef _WIN32
  #define NOMINMAX
  #include <windows.h>
  #include <memoryapi.h>

  static HANDLE main_thrd;
  static HANDLE timer_thrd;
#else
  #include <sys/mman.h>
  #include <unistd.h>
  #include <time.h>
  #include <signal.h>
  #include <sys/time.h>
  #include <stdio.h>
  #include <stdlib.h>
  #include <string.h>

#endif

#define PREEMPT_TIMER_INTERVAL 13370
static volatile int preempt_cnt = 0;

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
  preempt_cnt++;
#else
  if (preempt_cnt++ == 0) {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGALRM);
    sigprocmask(SIG_BLOCK, &sigset, NULL);
  }
#endif
}

void preempt_enable(void) {
#ifdef _WIN32
  preempt_cnt--;
#else
  if (--preempt_cnt == 0) {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &sigset, NULL);
  }
#endif
}

#ifdef _WIN32
LONG WINAPI signal_handler(PEXCEPTION_POINTERS except_info) {
  if (except_info->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP) {
    except_info->ContextRecord->EFlags &= ~0x100ULL;

    if (preempt_cnt == 0)
      thrd_yield();

    return EXCEPTION_CONTINUE_EXECUTION;
  }

  return EXCEPTION_CONTINUE_SEARCH;
}

DWORD WINAPI timer_loop(LPVOID arg) {
  (void)arg;
  while (1) {
    Sleep(PREEMPT_TIMER_INTERVAL / 1000);

    if (preempt_cnt == 0) {
      SuspendThread(main_thrd);

      if (preempt_cnt == 0) {
        CONTEXT ctx;
        ctx.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(main_thrd, &ctx)) {
          ctx.EFlags |= 0x100ULL;
          SetThreadContext(main_thrd, &ctx);
        }
      }

      ResumeThread(main_thrd);
    }
  }

  return 0;
}
#else
static void signal_handler(int num) {
  (void)num;
  thrd_yield();
}
#endif

void timer_init(void) {
#ifdef _WIN32
  DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                  GetCurrentProcess(), &main_thrd, 0,
                  FALSE, DUPLICATE_SAME_ACCESS);
  AddVectoredExceptionHandler(1, signal_handler);
  timer_thrd = CreateThread(NULL, 0, timer_loop, NULL, 0, NULL);
#else
  struct sigaction action;
  memset(&action, 0x0, sizeof(action));
  action.sa_handler = signal_handler;

  if (sigaction(SIGALRM, &action, NULL) == -1) {
    perror("sigaction failed");
    exit(1);
  }

  struct itimerval timer;
  memset(&timer, 0x0, sizeof(timer));
  timer.it_value.tv_usec = PREEMPT_TIMER_INTERVAL;
  timer.it_interval.tv_usec = PREEMPT_TIMER_INTERVAL;

  if (setitimer(ITIMER_REAL, &timer, NULL) == -1) {
    perror("setitimer failed");
    exit(1);
  }
#endif
}
