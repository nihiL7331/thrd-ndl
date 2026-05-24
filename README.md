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
  - [0. Build setup](guide/00-build-setup/README.md)
  - [1. Context switching](guide/01-context-switching/README.md)
  - [2. Cooperative scheduling](guide/02-cooperative-scheduling/README.md)
  - [3. Thread lifecycle](guide/03-thread-lifecycle/README.md)
  - [4. Blocking primitives](guide/04-blocking-primitives/README.md)
  - [5. Sleep and the heap](guide/05-sleep-and-the-heap/README.md)
  - [6. Preemption](guide/06-preemption/README.md)
  - [7. Porting](guide/07-porting/README.md)
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
* `int thrd_create(thrd_t* out_thrd, void (*func)(void))` - creates a new thread. Returns `THRD_SUCCESS` or `THRD_ENOMEM` if the pool is exhausted.
* `void thrd_yield` - voluntarily hands control to the next ready thread.
* `void thrd_sleep(uint64_t time_ms)` - suspends the current thread for at least `time_ms` milliseconds.
* `int thrd_join(thrd_t thrd)` - blocks until `thrd` finishes. Returns `THRD_SUCCESS` on success, `THRD_EINVAL` if passed argument is `NULL` or the current running thread.
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

This section will serve as a tutorial, split into chapters. 
Each chapter adds a certain functionality to your threading library and produces a working codebase that can be compiled and tested.
Feel free to experiment after finishing each section.

Up until the [Preemption](guide/06-preemption/README.md) chapter, the library is purely cooperative, meaning that a single thread can run indefinitely, unless explicitly told to stop by calling `thrd_yield`.
This tutorial focuses solely on implementing *M:1* model threading.
That means the whole process of this library runs on one OS thread, creating virtual threads.

The tutorial expects a prior knowledge of the *C* language, as well as a deeper understanding of how the stack works.
An ability to read *assembly* is also recommended. 

The step-by-step educational guide, along with a snapshot of the code at each step, can be found in the [guide/](guide/) directory:

0. [Build setup](guide/00-build-setup/README.md)
1. [Context switching](guide/01-context-switching/README.md)
2. [Cooperative scheduling](guide/02-cooperative-scheduling/README.md)
3. [Thread lifecycle](guide/03-thread-lifecycle/README.md)
4. [Blocking primitives](guide/04-blocking-primitives/README.md)
5. [Sleep and the heap](guide/05-sleep-and-the-heap/README.md)
6. [Preemption](guide/06-preemption/README.md)
7. [Porting](guide/07-porting/README.md)

<div align="center"><p><em>This guide uses `x86_64` on Linux/macOS as the primary example. The Windows and ARM64 ports are covered at the very end in the Porting chapter. The final, complete source code of the entire library lives in the src/ directory.</em></p></div>

---

## Roadmap

- [x] Split the README per section to `tutorial/sectionX/README.md`s.
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

* [Startup of glibc-based programs, GNU](https://www.gnu.org/software/hurd/glibc/startup.html)
* [Page (computer memory) - wikipedia](https://en.wikipedia.org/wiki/Page_(computer_memory))
* [How to use exit safale from any thread, SO](https://stackoverflow.com/questions/57161596/how-to-use-exit-safely-from-any-thread)
* [`pthread_join` - Linux man page](https://man7.org/linux/man-pages/man3/pthread_join.3.html)
* [Understanding Linux Process States by Yogesh Babar](https://access.redhat.com/sites/default/files/attachments/processstates_20120831.pdf)
* [`pthread_mutex_lock` - Linux man page](https://man7.org/linux/man-pages/man3/pthread_mutex_lock.3.html)
* [Binary heaps, Carnegie Mellon University](https://www.andrew.cmu.edu/course/15-121/lectures/Binary%20Heaps/heaps.html)
* [Preemption (computing) - wikipedia](https://en.wikipedia.org/wiki/Preemption_(computing))
* [Windows Fibers by Microsoft](https://learn.microsoft.com/en-us/windows/win32/procthread/fibers)
* [Trap flag - wikipedia](https://en.wikipedia.org/wiki/Trap_flag)
* [Thread control block - wikipedia](https://en.wikipedia.org/wiki/Thread_control_block)
* [Concurrent programming by begriffs](https://begriffs.com/posts/2020-03-23-concurrent-programming.html)
* [Threads in C are Pain by Tsoding](https://youtu.be/f-IlYeyTwzY?si=fGNUyaHwZ7GuoHle)
* [libmill by Martin Sustrik](https://github.com/sustrik/libmill)
* [libdill by Martin Sustrik](https://github.com/sustrik/libdill)
* [Threading implementation in xv6, MIT](https://github.com/mit-pdos/xv6-public)
* [The Linux Programming Interface by Michael Kerrisk](https://archive.org/details/The_Linux_Programming_Interface/page/674/mode/2up)
* [Revisiting Coroutines by Ana Lucia de Moura and Roberto Ierusalimschy](https://www.cs.tufts.edu/comp/250RTS/archive/roberto-ierusalimschy/revisiting-coroutines.pdf)
* [Binary Heaps lecture, Carnegie Mellon University](https://www.andrew.cmu.edu/course/15-121/lectures/Binary%20Heaps/heaps.html)
