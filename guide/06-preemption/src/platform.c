#include "platform.h"
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>              // include for 'struct timespec', 'clock_gettime', 'CLOCK_MONOTONIC', 'nanosleep'
#include <string.h>            // include for 'memset'
#include <thrd_ndl/thrd_ndl.h> // include for 'thrd_yield'
#include <signal.h>            // include for 'sigaction'
#include <sys/time.h>          // include for 'struct sigaction', 'struct itimerval'
#include <stdio.h>             // include for 'perror'

#define PREEMPT_TIMER_INTERVAL 7331
static volatile int preempt_cnt = 0;

static inline size_t align_to_page(size_t size) {
  size_t p_size = page_size();
  return (size + (p_size - 1)) & ~(p_size - 1);
}

void* os_alloc(size_t size) {
  void* ptr = mmap(NULL, align_to_page(size), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
  return (ptr == MAP_FAILED) ? NULL : ptr;
}

void os_free(void* ptr, size_t size) {
  if (ptr == NULL) 
    return;

  munmap(ptr, align_to_page(size));
}

int protect_page(void* ptr, size_t size) {
  return mprotect(ptr, align_to_page(size), PROT_NONE);
}

size_t page_size(void) {
  static size_t cached_page_size = 0;
  if (cached_page_size == 0)
    cached_page_size = (size_t)sysconf(_SC_PAGESIZE);

  return cached_page_size;
}

void* os_alloc_stack(size_t usable_size) {
  size_t total_size = usable_size + page_size(); // page_size for guard

  void* ptr = os_alloc(total_size);
  if (ptr == NULL)
    return NULL;

  if (protect_page(ptr, page_size()) != 0) {
    os_free(ptr, total_size);
    return NULL;
  }

  return ptr;
}

void os_free_stack(void* base_ptr, size_t usable_size) {
  os_free(base_ptr, usable_size + page_size());
}

uint64_t get_os_time(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);

  return (ts.tv_sec * 1000ULL) + (ts.tv_nsec / 1000000ULL);
}

void os_sleep_ms(uint64_t time_ms) {
  struct timespec ts;
  ts.tv_sec = time_ms / 1000ULL;
  ts.tv_nsec = (time_ms - (ts.tv_sec * 1000ULL)) * 1000000ULL;

  nanosleep(&ts, NULL);
}

static void signal_handler(int num) {
  (void)num;
  thrd_yield();
}

void timer_init(void) {
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = signal_handler;

  if (sigaction(SIGVTALRM, &action, NULL) == -1) {
    perror("sigaction failed");
    _Exit(1);
  }

  struct itimerval timer;
  memset(&timer, 0, sizeof(timer));
  timer.it_value.tv_usec = PREEMPT_TIMER_INTERVAL;
  timer.it_interval.tv_usec = PREEMPT_TIMER_INTERVAL;

  if (setitimer(ITIMER_VIRTUAL, &timer, NULL) == -1) {
    perror("setitimer failed");
    _Exit(1);
  }
}

void preempt_disable(void) {
  if (preempt_cnt++ == 0) {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGVTALRM);
    sigprocmask(SIG_BLOCK, &sigset, NULL);
  }
}

void preempt_enable(void) {
  if (--preempt_cnt == 0) {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGVTALRM);
    sigprocmask(SIG_UNBLOCK, &sigset, NULL);
  }
}
