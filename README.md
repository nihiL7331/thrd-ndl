<div align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/header_dark.svg">
    <source media="(prefers-color-scheme: light)" srcset="docs/assets/header_light.svg">
    <img alt="thrd-ndl" src="docs/assets/header_dark.svg" width="400">
  </picture>

  <p><em>An educational green threads library written in C.</em></p>

</div>

## Table of Contents

- [Introduction](#introduction)
  - [But what exactly are threads?](#but-what-exactly-are-threads)
- [Quick start](#quick-start)
  - [Hello, Thread!](#hello-thread)
- [Public interface](#public-interface)
  - [Return codes](#return-codes)
  - [Threads](#threads)
  - [Mutexes](#mutexes)
  - [Condition variables](#condition-variables)
- [Implementation](#implementation)
  - [Build setup](#build-setup)
  - [Context switching](#context-switching)
  - [Cooperative scheduling](#cooperative-scheduling)
  - [Thread lifecycle](#thread-lifecycle)
  - [Blocking primitives](#blocking-primitives)
  - [Sleep and the heap](#sleep-and-the-heap)
  - [Preemption](#preemption)
  - [Porting](#porting)
- [Roadmap](#roadmap)
- [Sources](#sources)

## Introduction

Sometimes, as a programmer, you might encounter a situation, where you need to run two or more functions side by side.
Think of a music player that needs to stream audio while updating its UI at the same time, or a web server handling multiple requests at once.
This is where **threads** come in.

### But what exactly are threads?

A **thread** is a piece of code that can be temporarily paused while running, allowing other threads to execute in its place, and then resumed at any future point in time. 
Without threads, a program can run **only one** thing at a time, start to finish, in order. 
With threads, **multiple** tasks can make progress without waiting for each other to complete.

## Quick start

Since the premise of the repository is a hands-on experience, you'll probably write and test a bunch of code in the process.

If you want to experiment with the complete, finished library before building it yourself in the tutorial, you can compile it like this:
```bash
mkdir build && cd build
cmake ..

# build the static library (.a)
cmake --build . --config Release
```
Once built, you can link the resulting static library against your own test code.
```bash
gcc my_code.c build/libthrd_ndl.a -Iinclude -o my_code
```

### Hello, Thread!

Below is a basic example on how to use the library.
```c
#include <thrd_ndl/thrd_ndl.h>
#include <stdio.h>

void thrd_a_func(void) {
  for (int i = 0; i < 3; ++i) {
    printf("Hello, Thread A! (%d)\n", i);
    thrd_yield();
  }
}

void thrd_b_func(void) {
  for (int i = 0; i < 3; ++i) {
    printf("Hello, Thread B! (%d)\n", i);
    thrd_yield();
  }
}

int main(void) {
  if (thrd_init() != THRD_SUCCESS)
    return 1;

  thrd_t thrd_a, thrd_b;

  if (thrd_create(&thrd_a, thrd_a_func) != THRD_SUCCESS)
    return 1;
  if (thrd_create(&thrd_b, thrd_b_func) != THRD_SUCCESS)
    return 1;

  thrd_join(thrd_a);
  thrd_join(thrd_b);
}
```
The expected output (for cooperative scheduling) is:
```
Hello, Thread A! (0)
Hello, Thread B! (0)
Hello, Thread A! (1)
Hello, Thread B! (1)
Hello, Thread A! (2)
Hello, Thread B! (2)
```
The code is pretty straight-forward. The two functions that are worthy of a description are:
* `thrd_yield` - voluntarily gives up the thread's turn, letting the next thread run,
* `thrd_join` - blocks the current thread until the passed thread finishes. Notably main has its own, implicitly created thread!

## Public interface

Contrary to other non-educational libraries, this section is here for a different reason.
It's an overview of functions implemented later in the [Implementation](#implementation) section.
While reading the said implementation section, you might return quite often to this section.

### Return codes

Functions that return `int` use these codes:

* `THRD_SUCCESS` - operation completed successfully (always `0`).
* `THRD_ENOMEM`  - out of capacity (pool exhausted, heap full, OS alloc fail).
* `THRD_EINVAL`  - invalid argument (`NULL` ptr, zero size, etc.).
* `THRD_EBUSY`   - resource is held by another thread.
* `THRD_EUNINIT` - passed argument is storing an uninitialized struct.

### Threads

* `int thrd_init` - must be called once before anything else. Sets up the scheduler and creates the implicit main thread. Returns `THRD_SUCCESS` on success, `THRD_EINVAL` if called more than once, or `THRD_ENOMEM` if the underlying OS allocation fails.
* `int thrd_create(thrd_t* out_thread, void (*func)(void))` - creates a new thread. Returns `THRD_SUCCESS` or `THRD_ENOMEM` if the pool is exhausted.
* `void thrd_yield` - voluntarily hands control to the next ready thread.
* `void thrd_sleep(uint64_t time_ms)` - suspends the current thread for at least `time_ms` milliseconds.
* `int thrd_join(thrd_t thrd)` - blocks until `thrd` finishes. Returns `THRD_SUCCESS` on success or if passed argument is already dead, `THRD_EINVAL` if passed argument is `NULL` or the current running thread.
* `void thrd_exit` - explicitly exits the current thread. It's called implicitly when the thread function returns.
* `void thrd_dump` - writes a snapshot of the scheduler state to `stderr`, intended as a debugging procedure, safe to call from any thread.

### Mutexes

* `int mutex_init(mutex_t* mutex)` - initializes the mutex, must be called before first use. Returns `THRD_EINVAL` if `mutex == NULL`, `THRD_SUCCESS` otherwise.
* `int mutex_lock(mutex_t* mutex)` - acquires the `mutex`, blocking the caller if already held. Returns `THRD_EINVAL` if `mutex == NULL`, `THRD_SUCCESS` otherwise.
* `int mutex_trylock(mutex_t* mutex)` - attempts to acquire `mutex` without blocking. Returns `THRD_SUCCESS` if acquired, `THRD_EBUSY` if already held.
* `int mutex_unlock(mutex_t* mutex)` - releases the `mutex` and wakes one waiting thread.  Returns `THRD_SUCCESS` if successfuly unlocked, returns `THRD_EINVAL` if the caller isn't the owner or `mutex` is `NULL`.

### Condition variables

* `int cond_init(cond_t* cond)` - initializes the cond, must be called before first use. Returns `THRD_EINVAL` if `cond == NULL`, `THRD_SUCCESS` otherwise.
* `int cond_wait(cond_t* cond, mutex_t* mutex)` - releases `mutex` and blocks, reacquires on wake. Returns `THRD_SUCCESS` was successfully awaited. Returns `THRD_EINVAL` if `cond` or `mutex` is `NULL`, or if the caller isn't the owner of `mutex`.
* `int cond_signal(cond_t* cond)` - wakes one thread waiting on the `cond` condition. Returns `THRD_EINVAL` if `cond == NULL`, `THRD_SUCCESS` otherwise.
* `int cond_bcast(cond_t* cond)` - wakes all threads waiting on the `cond` condition. Returns `THRD_EINVAL` if `cond == NULL`, `THRD_SUCCESS` otherwise.

## Implementation

This section will serve as a tutorial, split into *sections*, each adding a certain functionality to your threading library.
Each section produces a working library that can be compiled and tested. 
Feel free to experiment after finishing each section.

Up until the last section, [Preemption](#preemption), the library is purely cooperative, meaning that a single thread can run indefinitely, unless explicitly told to stop by calling `thrd_yield`.
This tutorial focuses solely on implementing *M:1* model threading.
That means the whole process of this library runs on one OS thread, creating virtual threads.

The tutorial expects a prior knowledge of the *C* language, as well as a deeper understanding of how the stack works.
An ability to read *assembly* is also recommended. 
The final code can be found in the [src/](src/) directory.
Each section has its source code in the corresponding `layerX` directory.

This entire section will use `x86_64` as the primary example, with the `ARM64` covered at the end.
Similarly, it targets Linux/macOS, the (limited) Windows port is covered at the end, both in the [Porting](#porting) section.

### Preemption

Up to now, the library has been fully cooperative.
Threads must explicitly yield control by calling `thrd_yield` (or by calling `thrd_sleep`/`mutex_lock` which yield internally).

Cooperative scheduling is highly efficient and naturally avoids many race conditions.
But if a user writes a thread with an infinite loop, or just a long task that forgets to yield, it will fry the CPU forever.
No other thread will ever run, and the whole program will hang.

To build a robust system, we need [preemption](https://en.wikipedia.org/wiki/Preemption_(computing)): the ability to forcibly interrupt a running thread, take the CPU away from it, and hand it to someone else.

#### The timer and the signal

To forcibly pause a thread, we need the OS's help. On Unix-like systems, we can ask the OS to send our process a signal at a regular interval using a timer.

We will use `ITIMER_VIRTUAL`.
It only counts down while our process is actively in user space.
When the timer hits zero, the OS fires a `SIGVTALRM` signal, pausing whatever instruction the current thread is executing to run a signal handler.

That signal handler will simply run `thrd_yield`.

Let's declare the functions in `src/platform.h`:

```c
void preempt_disable(void);
void preempt_enable(void);
void timer_init(void);
```

Now, let's implement the timer in `src/platform.c`.
We will set the timer to some arbitrary value around 10ms.
Since the `tv_usec` field expects microseconds, we need a value around 10000.
Setting it to exactly 10ms might be less reliable, since it might perfectly sync with the host OS's internal scheduler ticks.
That's why we'll use a prime number instead.

```c
#include <string.h>            // include for 'memset'
#include <thrd_ndl/thrd_ndl.h> // include for 'thrd_yield'
#include <signal.h>            // include for 'sigaction'
#include <sys/time.h>          // include for 'struct sigaction', 'struct itimerval'
#include <stdio.h>             // include for 'perror'

#define PREEMPT_TIMER_INTERVAL 7331

// ...

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
```

We must call `timer_init` once inside `thrd_init`.
Call it like so:

```c
int thrd_init(void) {
  // ...

  timer_init();

  return THRD_SUCCESS;
}
```

From that moment on, every ~7ms the OS will interrupt the running thread, push a signal frame onto its stack, and jump to `signal_handler`, which yields to the next thread.

The Windows implementation will use a completely different architecture, because Windows doesn't have POSIX signals. It's covered in the [Porting](#porting) section.

#### Protecting the scheduler

However, we just introduced a massive concurrency problem.
Because `SIGVTALRM` can interrupt the thread at any machine instruction, `thrd_yield` can now be called at any time, including while the scheduler is already inside `thrd_yield` or `mutex_lock`.

Imagine a thread is inside `mutex_trylock(&mutex)`:
1. It sees `mutex->owner == NULL`.
2. The timer fires. The thread is preempted.
3. Another thread runs, calls `mutex_lock(&mutex)`, sees `mutex->owner == NULL`, and claims it.
4. The first thread resumes, executes `mutex->owner = get_curr_thrd()`, and overwrites the owner. Now two threads think they own the mutex.

To prevent this, we need to protect our internal scheduler structures by temporarily disabling the timer during critical sections.
We'll implement a recursive counter so we can safely nest these calls.
Add this to `src/platform.c`:

```c
static volatile int preempt_cnt = 0;

// ...

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
```

By using `sigprocmask` to block `SIGVTALRM`, the OS will hold the signal pending until we unblock it.

Now, we must wrap every internal scheduler operation.
The most important one is `thrd_yield` itself.
A thread entering `thrd_yield` disables preemption, does the scheduling work, switches context, and then the newly resumed thread reenables preemption on its way out:

```c
void thrd_yield(void) {
  preempt_disable();

  // ...

  preempt_enable();
}
```

You must also place `preempt_disable` and `preempt_enable` around the logic in `thrd_create`, `thrd_exit`, `thrd_join`, `thrd_sleep`, `resume_thrd`, `cond_wait`, `cond_signal`, `cond_bcast`, `mutex_lock`, `mutex_trylock` and `mutex_unlock`.
For the exact positioning check the source code in [tutorial/section6/](tutorial/section6/).
Every piece of code that reads or modifies a queue or shared scheduler state is a critical section.

#### The first-run problem

By wrapping our scheduler functions in `preempt_disable` and `preempt_enable`, we protected the queues.
But we accidentally introduced a deadlock for new threads.

Look at this logic in `thrd_yield`:

```c
void thrd_yield(void) {
  preempt_disable();

  // ...

  thrd_switch(old_thrd, curr_thrd);

  preempt_enable();
}
```

When `thrd_switch` resumes an existing thread, it returns from the assembly call, hits `preempt_enable`, and execution continues normally.
But when `thrd_switch` jumps to a new thread, the assembly `ret` instruction jumps directly into the user's entry function.
The new thread completely bypasses `preempt_enable`.
The timer remains disabled, and preemption is permanently broken.

To fix this, we will use a thread wrapper.
Instead of pointing the initial stack frame at the user's function, we point it at a wrapper.

First, update the TCB in `src/tcb.h` to store the user's function pointer:

```c
typedef struct tcb {
  void*        rsp;                // the stack pointer
  struct tcb*  next;               // intrusive next link for queue threading
  thrd_state_t state;              // current scheduler state
  void*        bsp;                // base stack pointer
  struct tcb*  join_queue_hd;      // head of joiners waiting on this thread
  struct tcb*  join_queue_tl;      // tail of joiners waiting on this thread
  uint64_t     wakeup_time;        // the abs monotonic time this thread should wake
  void         (*user_proc)(void); // the actual user entry function
} tcb_t;
```

Now, create the wrapper function in `src/tcb.c`:

```c
#include "scheduler.h" // include for 'get_curr_thrd'

static void tcb_wrap(void) {
  preempt_enable();
  get_curr_thrd()->user_proc();
  thrd_exit();
}
```

Finally, update `tcb_init` in `src/tcb.c` to use this wrapper.

We still push `thrd_exit` onto the stack before `tcb_wrap`.
Even though `tcb_wrap` explicitly calls `thrd_exit`, pushing this 8-byte value simulates the return address that a normal call instruction would leave on the stack.
This ensures the stack pointer is 16-byte aligned when `tcb_wrap` begins executing.

```c
tcb_t* tcb_init(void (*entry)(void)) {
  // ...

  tcb->user_proc = entry;

  size_t size  = THRD_STACK_SIZE + page_size();
  uint64_t* sp = (uint64_t*)((uint8_t*)tcb->bsp + size);

  *(--sp) = (uint64_t)thrd_exit; // push the cleanup function (align + fail-safe)
  *(--sp) = (uint64_t)tcb_wrap;  // jump to the wrapper, not 'entry'

  sp -= CALLEE_REG_CNT; // space for callee-saved registers
  memset(sp, 0, CALLEE_REG_CNT * sizeof(void*));

  tcb->rsp   = sp;
  tcb->state = THRD_READY;
  return tcb;
}
```

It fixes the preemption bug, as well as establishes a clean, predictable lifecycle for every thread.

#### A preemptive demo

With preemption and critical sections in place, our threads act like actual OS threads.
Let's prove it by writing a notoriously bad thread that enters an infinite loop and refuses to yield.

```c
#include <stdio.h>
#include <thrd_ndl/thrd_ndl.h>

// the 'polite' thread
static void func_a(void) {
  for (int i = 0; i < 3; ++i) {
    printf("polite thread running\n");
    thrd_sleep(100);
  }
}

// the 'greedy' thread
static void func_b(void) {
  printf("starting an infinite loop\n");
  volatile int counter = 0;
  while (1) {
    counter++;
  }
}

int main(void) {
  if (thrd_init() != THRD_SUCCESS)
    return 1;

  thrd_t thrd_a, thrd_b;
  if (thrd_create(&thrd_a, func_a) != THRD_SUCCESS)
    return 1;
  if (thrd_create(&thrd_b, func_b) != THRD_SUCCESS)
    return 1;

  thrd_join(thrd_a); // only waiting for the 'polite' thread

  printf("done\n");
}
```
Build it as always:

```bash
cmake -B build && cmake --build build && ./build/demo
```

The expected output is:

```
starting an infinite loop
polite thread running
polite thread running
polite thread running
done
```

The OS timer forcefully takes away the control from the infinite loop every ~7ms, checks the sleep queue, sees that `thrd_a` isn't ready yet, and gives back the control to the greedy loop.
But once 100ms passes, `thrd_a` is woken up, scheduled, prints it message, and goes back to sleep.

The complete code for this section lives in [tutorial/section6/](tutorial/section6/)

### Porting

So far, this tutorial has focused entirely on x86_64 architecture running on Unix-like OSs (Linux/macOS).
In this section we'll make this library truly cross-platform, supporting ARM64 architecture and Windows.

#### Windows preemption

Windows support requires replacing `mmap` with `VirtualAlloc`, which is straightforward.
The real challenge is preemption.
Windows does not have POSIX signals, so there is no `SIGVTALRM`.

To solve this, we spin up a dedicated OS timer thread. 
Every ~7ms, this thread explicitly suspends the main OS thread running our scheduler.
But we can't just call `thrd_yield` from the timer thread, it has to execute on the main thread's stack.

To force the main thread to yield itself, we'll use a simple trick.
While the main thread is suspended, the timer thread modifies the main thread's CPU context to set the [trap flag](https://en.wikipedia.org/wiki/Trap_flag).
The trap flag tells the CPU to execute exactly one instruction and then trigger a hardware breakpoint ([`EXCEPTION_SINGLE_STEP`](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-exception_record)).

We register a [vectored exception handler](https://learn.microsoft.com/en-us/windows/win32/debug/vectored-exception-handling) to catch this breakpoint.
When the handler fires, it is executing on the main thread.
It clears the trap flag and calls `thrd_yield`.

Here's the full implementation for `src/platform.c`:

```c
#include "platform.h"
#include <stdint.h>            // include for 'uint64_t'
#include <thrd_ndl/thrd_ndl.h> // include for 'thrd_yield'

#define PREEMPT_TIMER_INTERVAL 7331
static volatile int preempt_cnt = 0;

static inline size_t align_to_page(size_t size);

#ifdef _WIN32

#define NOMINMAX
#include <windows.h>
#include <memoryapi.h>

#define TRAP_FLAG_MASK 0x100ULL

static HANDLE main_thrd;
static HANDLE timer_thrd;

uint64_t get_os_time(void) {
  static LARGE_INTEGER win_freq = {0};
  if (win_freq.QuadPart == 0)
    QueryPerformanceFrequency(&win_freq);

  LARGE_INTEGER ticks;
  QueryPerformanceCounter(&ticks);

  return (ticks.QuadPart * 1000ULL) / win_freq.QuadPart;
}

void os_sleep_ms(uint64_t time_ms) {
  Sleep((DWORD)time_ms);
}

void* os_alloc(size_t size) {
  return VirtualAlloc(NULL, align_to_page(size), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

void os_free(void* ptr, size_t size) {
  (void)size;

  if (ptr == NULL) 
    return;

  VirtualFree(ptr, 0, MEM_RELEASE);
}

int protect_page(void* ptr, size_t size) {
  DWORD old_prot = 0;
  BOOL success = (int)VirtualProtect(ptr, align_to_page(size), PAGE_NOACCESS, &old_prot);
  return success ? 0 : -1;
}

size_t page_size(void) {
  static size_t cached_page_size = 0;

  if (cached_page_size == 0) {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    cached_page_size = (size_t)sysInfo.dwPageSize;
  }

  return cached_page_size;
}

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
    Sleep(PREEMPT_TIMER_INTERVAL / 1000); // milliseconds

    if (preempt_cnt == 0) {
      SuspendThread(main_thrd);

      if (preempt_cnt == 0) {
        CONTEXT ctx;
        ctx.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(main_thrd, &ctx)) {
          ctx.EFlags |= TRAP_FLAG_MASK;
          SetThreadContext(main_thrd, &ctx);
        }
      }

      ResumeThread(main_thrd);
    }
  }

  return 0;
}

void timer_init(void) {
  DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                  GetCurrentProcess(), &main_thrd, 0,
                  FALSE, DUPLICATE_SAME_ACCESS);
                  
  AddVectoredExceptionHandler(1, signal_handler);
  timer_thrd = CreateThread(NULL, 0, timer_loop, NULL, 0, NULL);
}

void preempt_disable(void) {
  preempt_cnt++;
}

void preempt_enable(void) {
  preempt_cnt--;
}

#else

#include <sys/mman.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>

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

#endif

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

static inline size_t align_to_page(size_t size) {
  size_t p_size = page_size();
  return (size + (p_size - 1)) & ~(p_size - 1);
}
```

#### Windows ABI differences

In addition to the timer mechanism, the [Windows x86_64 ABI](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170) differs from the [System V ABI](https://refspecs.linuxbase.org/elf/x86_64-abi-0.99.pdf) in two crucial ways:
1. Instead of `%rdi` and `%rsi`, Windows passes the first two arguments in `%rcx` and `%rdx`.
2. On Windows, the `%rdi` and `%rsi` registers are also considered callee-saved.

That's why we need to create new assembly file, `src/arch/x86_64/context_win64.S`.

```gas
  .text
  .align 16
  .global thrd_switch

thrd_switch:
  /* push callee-saved registers onto the current stack */
  pushq %rbx
  pushq %rbp
  pushq %rdi
  pushq %rsi
  pushq %r12
  pushq %r13
  pushq %r14
  pushq %r15

  /* save the current stack pointer into old_tcb */
  /* old_tcb->rsp is at offset 0 */
  movq %rsp, (%rcx)

  /* load the new stack pointer */
  movq (%rdx), %rsp

  /* pop the registers (reverse order to push) */
  popq %r15
  popq %r14
  popq %r13
  popq %r12
  popq %rsi
  popq %rdi
  popq %rbp
  popq %rbx

  /* jump to new thread */
  ret
```

We also need to update `CMakeLists.txt` to pick the correct assembly file during compilation.

```cmake
// set ...

if(WIN32)
  set(ARCH_SRC src/arch/x86_64/context_win64.S)
else()
  set(ARCH_SRC src/arch/x86_64/context_unix.S)
endif()

add_executable(demo
  ${ARCH_SRC}
  demo/demo.c
  src/tcb.c
  src/scheduler.c
  src/platform.c
  src/pool.c
  src/mutex.c
  src/cond.c
  src/heap.c
)
```

This means our `tcb_init` function needs to allocate space for 8 registers when setting up the initial stack frame.
Replace the old `#define CALLEE_REG_CNT 6` with this:

```c
#ifdef _WIN32
  #define CALLEE_REG_CNT 8
#else
  #define CALLEE_REG_CNT 6
#endif
```

#### Windows TIB

On Windows, the OS tracks the current stack via the [Thread Information Block](https://en.wikipedia.org/wiki/Win32_Thread_Information_Block).
Because here context switch bypasses the OS and doesn't update the TIB's stack bounds, large stack allocations that trigger `__chkstk`, structured exception handling, and some C runtime functions may misbehave.
A complete Windows port requires either manually updating the TIB during `thrd_switch` or utilizing Windows [Fibers](https://learn.microsoft.com/en-us/windows/win32/procthread/fibers), but it's out of scope of this tutorial (for now).

#### ARM64 context switch

To run on ARM processors, the assembly context switch must change.

Instead of pushing `%rbx`, `%rbp`, etc., the ARM64 calling convention requires us to preserve registers `x19-x29`.
Here's the equivalent `src/arch/arm64/context_arm64.S` file:

```gas
#ifdef __APPLE__
  #define SYM_SWITCH _thrd_switch
#else
  #define SYM_SWITCH thrd_switch
#endif

  .text
  .align 4
  .global SYM_SWITCH

SYM_SWITCH:
  /* push callee-saved registers onto the current stack
     pre decrement sp by 16 */
  stp x19, x20, [sp, #-16]!
  stp x21, x22, [sp, #-16]!
  stp x23, x24, [sp, #-16]!
  stp x25, x26, [sp, #-16]!
  stp x27, x28, [sp, #-16]!
  stp x29, x30, [sp, #-16]!

  /* save the current stack pointer into old_tcb
     old_tcb->rsp is at offset 0
     move to tmp register first */
  mov x9, sp
  str x9, [x0]

  /* load the new stack pointer */
  ldr x9, [x1]
  mov sp, x9

  /* pop the registers (reverse order to push)
     post increment sp by 16 */
  ldp x29, x30, [sp], #16
  ldp x27, x28, [sp], #16
  ldp x25, x26, [sp], #16
  ldp x23, x24, [sp], #16
  ldp x21, x22, [sp], #16
  ldp x19, x20, [sp], #16

  /* jump to new thread */
  ret
```

We also need to update `CALLEE_REG_CNT` for ARM64 in `src/tcb.c`:

```c
#ifdef __aarch64__
  #define CALLEE_REG_CNT 12
#elif defined(_WIN32)
  #define CALLEE_REG_CNT 8
#else
  #define CALLEE_REG_CNT 6
#endif
```

Now, we need to again update `CMakeLists.txt` to pick the correct asm file.
Just replace the previous `if`-statements with this approach:

```cmake
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
  if(WIN32)
    set(ARCH_SRC src/arch/x86_64/context_win64.S)
  else()
    set(ARCH_SRC src/arch/x86_64/context_unix.S)
  endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)")
  set(ARCH_SRC src/arch/arm64/context_arm64.S)
else()
  message(FATAL_ERROR "arch '${CMAKE_SYSTEM_PROCESSOR}' not supported")
endif()
```

#### ARM64 `ret` differences and the trampoline

On x86_64, the `ret` instruction pops the target address directly off the top of the stack.
That's why we pushed `tcb_wrap` onto the stack in `tcb_init`.

ARM64 doesn't work this way.
The `ret` instruction doesn't look at the stack at all, instead it jumps to whatever address is stored in the `x30` register.

When the ARM64 `thrd_switch` finishes restoring registers, it loads `x30` from the saved stack frame.
If we want a new thread to start executing, we have to place our starting address into the `x30` slot of our fake stack frame.

But there is a catch.
In ARM64, functions expect their starting context to be perfectly clean, but we are jumping out of a context switch.
To handle this cleanly, we use an assembly **trampoline**.
We store the `tcb_wrap` function in some arbitrary callee-saved register, e.g. `x19`.
We point `x30` to a tiny assembly function called `thrd_tramp`, which will just branch to the address stored in `x19`.

Add this to the `src/arch/arm64/context_arm64.S` file:

```gas
#ifdef __APPLE__
  #define SYM_SWITCH _thrd_switch
  #define SYM_TRAMP  _thrd_tramp
#else
  #define SYM_SWITCH thrd_switch
  #define SYM_TRAMP  thrd_tramp
#endif

// context switch asm...

  .global SYM_TRAMP

SYM_TRAMP:
  br x19
```

To support it across all architectures, we need to update the preprocessor directives in `src/tcb.c` to construct the correct stack frame shape:

```c
#ifdef __aarch64__
  #define CALLEE_REG_CNT 12
  #define X19_REG_POS 10
  #define X30_REG_POS 1
  extern void thrd_tramp(void);
#elif defined(_WIN32)
  #define CALLEE_REG_CNT 8
#else
  #define CALLEE_REG_CNT 6
#endif

tcb_t* tcb_init(void (*entry)(void)) {
  // stack alloc ...

  tcb->user_proc = entry;

  size_t size  = THRD_STACK_SIZE + page_size();
  uint64_t* sp = (uint64_t*)((uint8_t*)tcb->bsp + size);

  *(--sp) = (uint64_t)thrd_exit; // push the cleanup function (align + fail-safe)

#ifdef __aarch64__
  *(--sp) = 0;                  // dummy value for align
#else
  *(--sp) = (uint64_t)tcb_wrap; // jump to the wrapper, not 'entry'
#endif

  sp -= CALLEE_REG_CNT; // space for callee-saved registers
  memset(sp, 0, CALLEE_REG_CNT * sizeof(void*));

#ifdef __aarch64__
  sp[X19_REG_POS] = (uint64_t)tcb_wrap;
  sp[X30_REG_POS] = (uint64_t)thrd_tramp;
#endif

  tcb->rsp   = sp;
  tcb->state = THRD_READY;
  return tcb;
}
```

By storing `tcb_wrap` in `x19` and returning into `thrd_tramp`, we cleanly jump from the assembly domain back into the unified C lifecycle wrapper, abstracting away the architecture differences from the scheduler.

The complete code for this section lives in [tutorial/section7/](tutorial/section7/)

---

## Roadmap

- [ ] Split the README per section to `tutorial/sectionX/README.md`s.
- [x] Cover the Porting section implementation in README.
- [x] Cover the Preemption section implementation in README.
- [x] Cover the Sleep and the heap section of README implementation.
- [x] Cover the Blocking primitives section of README implementation.
- [x] Cover the Thread lifecycle section of README implementation.
- [x] Cover the Cooperative scheduling section of README implementation.
- [x] Cover the Context switching section of README implementation.
- [x] Replace the sleep queue with a binary min-heap.
- [x] Add a debugging `thrd_dump` method.
- [x] Implement `mutex_trylock`.
- [x] Make a introductory README section.
- [x] Find and implement a good and easy solution for preemption. (it isn't easy)
- [x] Optimize allocation via a Pool allocator.
- [x] Implement conditional locking.
- [x] Handle clean up of dead threads.
- [x] Full ARM support.
- [x] Feature mutex locking.
- [x] Implement a sleep queue with `thrd_sleep` API.
- [x] Implement thread blocking (`thrd_join`).

## Sources

* [Thread control block - wikipedia](https://en.wikipedia.org/wiki/Thread_control_block)
* [Concurrent programming by begriffs](https://begriffs.com/posts/2020-03-23-concurrent-programming.html)
* [Threads in C are Pain by Tsoding](https://youtu.be/f-IlYeyTwzY?si=fGNUyaHwZ7GuoHle)
* [libmill by Martin Sustrik](https://github.com/sustrik/libmill)
* [libdill by Martin Sustrik](https://github.com/sustrik/libdill)
* [Threading implementation in xv6, MIT](https://github.com/mit-pdos/xv6-public)
* [The Linux Programming Interface by Michael Kerrisk](https://archive.org/details/The_Linux_Programming_Interface/page/674/mode/2up)
* [Revisiting Coroutines by Ana Lucia de Moura and Roberto Ierusalimschy](https://www.cs.tufts.edu/comp/250RTS/archive/roberto-ierusalimschy/revisiting-coroutines.pdf)
* [Binary Heaps lecture, Carnegie Mellon University](https://www.andrew.cmu.edu/course/15-121/lectures/Binary%20Heaps/heaps.html)
