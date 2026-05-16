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

While learning about threads, you might want to write your own code. To build this library, use the following:
```bash
mkdir build && cd build
cmake ..

# build the static library (.a)
cmake --build . --config Release
```
After building the library, you can use the outputted file to link it against your own code, using something like:
```bash
gcc my_code.c libthrd_ndl.a
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
* `THRD_ENOMEM` - out of capacity (pool exhausted, heap full, OS alloc fail).
* `THRD_EINVAL` - invalid argument (`NULL` ptr, zero size, etc.).
* `THRD_EBUSY` - resource is held by another thread.

### Threads

* `int thrd_init` - must be called once before anything else. Sets up the scheduler and creates the implicit main thread. Returns `THRD_SUCCESS` on success, `THRD_EINVAL` if called more than once, or `THRD_ENOMEM` if the underlying OS allocation fails.
* `int thrd_create(thrd_t* out_thread, void (*func)(void))` - creates a new thread. Returns `THRD_SUCCESS` or `THRD_ENOMEM` if the pool is exhausted.
* `thrd_yield` - voluntarily hands control to the next ready thread.
* `thrd_sleep(uint64_t time_ms)` - suspends the current thread for at least `time_ms` milliseconds.
* `thrd_join(thrd_t thread)` - blocks until `thread` finishes.
* `thrd_exit` - explicitly exits the current thread. It's called implicitly when the thread function returns.
* `thrd_dump` - writes a snapshot of the scheduler state to `stderr`, intended as a debugging procedure, safe to call from any thread.

### Mutexes

* `int mutex_init(mutex_t* mutex)` - initializes the mutex, must be called before first use. Returns `THRD_SUCCESS` or `THRD_EINVAL`.
* `mutex_lock(mutex_t* mutex)` - acquires the `mutex`, blocking the caller if already held.
* `int mutex_trylock(mutex_t* mutex)` - attempts to acquire `mutex` without blocking. Returns `THRD_SUCCESS` if acquired, `THRD_EBUSY` if already held.
* `mutex_unlock(mutex_t* mutex)` - releases the `mutex` and wakes one waiting thread.

### Condition variables

* `int cond_init(cond_t* cond)` - initializes the cond, must be called before first use. Returns `THRD_SUCCESS` or `THRD_EINVAL`.
* `cond_wait(cond_t* cond, mutex_t* mutex)` - releases `mutex` and blocks, reacquires on wake.
* `cond_signal(cond_t* cond)` - wakes one thread waiting on the `cond` condition.
* `cond_bcast(cond_t* cond)` - wakes all threads waiting on the `cond` condition.

<div align="center">
  <p><em>Procedures don't return anything unless specified.</em></p>
</div>

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

### Build setup

This project will use CMake as a build tool.
Below is a `CMakeLists.txt` file that lists files we'll create during the [Context switching](#context-switching) section.
`cmake -B build` will work after you've written all of them.
```cmake
cmake_minimum_required(VERSION 3.20)
project(thrd_ndl_tutorial LANGUAGES C ASM)

set(CMAKE_C_STANDARD 17)

add_executable(demo
  demo/demo.c
  src/tcb.c
  src/arch/x86_64/context_unix.S
)

target_include_directories(demo PRIVATE include src)
```
We will update this file as each section progresses.

### Context switching

#### What's in the CPU state?

By the end of this section, we want a function `thrd_switch(old, new)` that saves the current thread's CPU state and resumes another thread that was previously saved.
You may wonder: what does the CPU state actually contain?

The CPU has a small amount of working memory - called registers - and a much larger working area, the stack. Together, they hold everything about "where this thread is right now".
Together, they store current values, call history, and where to go next in code.

To pause a thread, we save all of that somewhere. To resume it, we put it back.

That was a high-level view of the CPU state. Concretely, what it actually consists of:
* Callee-saved registers - these are values that the caller of the function is relying on us not to modify. On `x86_64` *Linux*, these are: `%rbx`, `%rbp` and `%r12-%r15`. On *Windows* there are more - `%rsi` and `%rdi` also need to be stored. We must preserve these across a switch, so we save them.
* The stack pointer - it points to the top of the current call stack. Everything below it is the thread's history. We also save that value, to later restore the entire stack.
* The instruction pointer - it stores the address of the next instruction to run. We will not save it directly - `ret` and the stack will handle it for us (more on that later).

#### Saving and resuming

We need to store the callee-saved registers somewhere. 
To do that, we will use the thread's own stack, with the `push` instruction.
We also need to save the stack pointer (which, notably, now points past the saved registers).
Unlike the callee-saved registers, we'll store it in the thread's control block, or TCB for short, which we'll define later.

To understand how the instruction pointer is stored, we need to understand what `call` and `ret` actually do.
When a caller does `call thrd_switch`, the CPU `push`es the address of the instruction **after** the call onto the stack, then `jump`s into `thrd_switch`.
So by the time `thrd_switch` starts executing, its return address is already on the stack.
`ret` is a mirror image of the `call` instruction.
It `pop`s the top of the stack and `jump`s there. 
It doesn't care what's there or how it got there.
We use that behavior to store and retrieve the instruction pointer.
When we save `%rsp`, the return address goes with it, since it's a part of the stack.
When `ret` later executes on this thread's restored stack, it pops that address and jumps to it.
The instruction pointer is preserved even though we didn't touch it.

Now that the thread's state is stored, we need to do the opposite to resume the new thread.
First, we load the stack pointer from the struct using the `mov` instruction.
Now we `pop` the callee-saved registers off the stack.
The procedure finishes with `ret`, handling the instruction pointer.

The above works for any thread that has been suspended by `thrd_switch` before. 
A brand-new thread has never been called this way, so its stack starts empty.
This case will be handled when we set up new threads later, in this section.

#### The TCB and the switcher

We have been referring to 'the thread's struct' throughout, so let's define it now.
We'll call the struct *TCB* - Thread Control Block.
It's a name for a data structure used in OS kernels that we'll borrow for our implementation.
It will contain thread-specific information needed to manage the thread - the information that we've defined above.
It will be stored in `src/tcb.h`, which will be accompanied by `src/tcb.c` in a moment.
```c
#pragma once

typedef struct tcb {
  void* rsp; // the stack pointer
} tcb_t;
```
We'll expand on the TCB struct later.
We also need to get the thread's stack.
A keen eye might notice it's not in the TCB struct.
For now, we'll allocate each thread's stack as a static array. 
Section [Thread lifecycle](#thread-lifecycle) (WIP) introduces a proper stack allocator.

But now, let's transfer the earned knowledge about the context switch to some assembly code.
We'll create a `src/arch/x86_64/context_unix.S` file and create the following procedure.
```gas
thrd_switch:
  // push callee-saved registers onto the current stack
  push %rbx
  push %rbp
  push %r12
  push %r13
  push %r14
  push %r15

  // save the current stack pointer into old_tcb
  // old_tcb->rsp is at offset 0
  mov %rsp, (%rdi)

  // load the new stack pointer
  mov (%rsi), %rsp

  // pop the registers (reverse order to push)
  pop %r15
  pop %r14
  pop %r13
  pop %r12
  pop %rbp
  pop %rbx

  // jump to new thread (instruction pointer is handled implicitly!)
  ret
```
The version in [layer1/src/arch/x86_64/context_unix.S](layer1/src/arch/x86_64/context_unix.S) adds a few ELF directives.
They're standard boilerplate, unrelated to the context switching itself.

#### Public API

The code maps perfectly to the logic we've gone over before.
It's a good moment to expose a public API that will be called by the end user.
Create a `include/thrd_ndl/thrd_ndl.h` file (or whatever name suits you best).
For now, we'll keep it simple:
```c
#ifndef THRD_NDL_H
#define THRD_NDL_H

typedef void* thrd_t;

thrd_t      tcb_init(void (*entry)(void));
extern void thrd_switch(thrd_t old_tcb, thrd_t new_tcb);

#endif // THRD_NDL_H
```
`thrd_switch` is really an internal primitive.
Starting from the section [Cooperative scheduling](#cooperative-scheduling), this will be abstracted away via `thrd_yield`.
For now, we'll invoke it manually.

#### Thread initialization

Similarly, `tcb_init` will become an internal primitive starting from the section [Thread lifecycle](#thread-lifecycle) (WIP).
Now we need to handle the thread initialization, so that `thrd_switch` works properly.
Create the file `tcb.c`.
It will handle the initialization of the TCB.

`tcb_init` does three things:
* allocates the TCB,
* allocates the stack,
* writes the initial stack frame that makes the first `ret` jump to the entry function.
```c
#include "tcb.h"
#include <thrd_ndl/thrd_ndl.h>
#include <stdlib.h>
#include <stdint.h>

#define STACK_SIZE 16384
#define CALLEE_REG_CNT 6

thrd_t tcb_init(void (*entry)(void)) {
  tcb_t* tcb = (tcb_t*)malloc(sizeof(tcb_t));
  if (tcb == NULL)
    return NULL;

  uint8_t* stack = (uint8_t*)malloc(STACK_SIZE);
  if (stack == NULL) {
    free(tcb);
    return NULL;
  }
  // stack pointer is at the top of the stack
  uint64_t* sp  = (uint64_t*)(stack + STACK_SIZE);

  *(--sp) = 0;               // padding for ABI alignment
  *(--sp) = (uint64_t)entry; // fake return address for 'ret'
  sp -= CALLEE_REG_CNT;      // space for callee-saved registers

  tcb->rsp = sp;
  return tcb;
}
```
The extra 8-byte slot at the top is alignment padding.
The *System V ABI* requires `%rsp` to be 8 mod 16 at function entry, but with only the return address and six callee-saved registers, the math comes out 16-aligned instead.
The dummy slot shifts everything by one word so the entry function gets a properly aligned stack.
<div align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/fake_stack_dark.svg">
    <source media="(prefers-color-scheme: light)" srcset="docs/assets/fake_stack_light.svg">
    <img alt="fake stack" src="docs/assets/fake_stack_dark.svg">
  </picture>

  <p><em>The fake initial stack frame written by <code>tcb_init</code>. The first <code>ret</code> after switching to this thread pops the entry point address and jumps there.</em></p>

</div>

For now we'll not have an option to free the `stack`, but it'll be handled later.

#### Putting it together

Now, with the code finished, we're ready to put it all together. 
The following demo creates two threads that take turns printing a counter, switching to each other manually.
```c
#include <thrd_ndl/thrd_ndl.h>
#include <stdio.h>

static thrd_t main_thrd, thrd_a, thrd_b;

static void func_a(void) {
  for (int i = 0; i < 3; ++i) {
    printf("A: %d\n", i);
    thrd_switch(thrd_a, thrd_b);
  }
  thrd_switch(thrd_a, main_thrd); // hand back when done
}

static void func_b(void) {
  for (int i = 0; i < 3; ++i) {
    printf("B: %d\n", i);
    thrd_switch(thrd_b, thrd_a);
  }
  thrd_switch(thrd_b, main_thrd);
}

int main(void) {
  main_thrd = tcb_init(NULL); // placeholder for "where are we now"
  thrd_a    = tcb_init(func_a);
  thrd_b    = tcb_init(func_b);

  thrd_switch(main_thrd, thrd_a); // kickstart the threads
  printf("done\n");
}
```

To compile and run this demo, use:
```bash
cmake -B build && cmake --build build && ./build/demo
```

The expected output is:
```
A: 0
B: 0
A: 1
B: 1
A: 2
B: 2
done
```

After the loop, each thread explicitly switches back to `main`.
Without this, control would fall off the end of the function and segfault, because we don't have a way to clean up a finished thread implicitly yet.

Manual `thrd_switch` calls are the rawest possible form of cooperative scheduling.
The next section will introduce `thrd_yield`, which delegates the decision of who runs next to the scheduler.

The complete code for this section lives in [layer1/](layer1/).

### Cooperative scheduling

So far we've handled everything in the demo explicitly.
In this section, we'll implement the *scheduler* that'll handle some of that boilerplate for us.

#### The ready queue

In the previous section, we had to explicitly point out which thread fires when.
Now, instead of that, we'll implement a concept known as *the ready queue*.
It's a FIFO queue of threads.
When one of the threads finishes its turn, the thread that is the head of said queue will fire next.

We'll implement the scheduler in a new file: `src/scheduler.c`.
Add `src/scheduler.c` to the `add_executable` block in the `CMakeLists.txt` file.
Alongside the queue, we'll store the pointer to the currently running thread, like so:
```c
#include "tcb.h"

static tcb_t* curr_thrd = NULL;
static tcb_t* rdy_queue_hd = NULL;
static tcb_t* rdy_queue_tl = NULL;
```

But right now we're missing the connection between each thread. 
That's why we also need to add a `next` field in our `tcb_t` struct in the `src/tcb.h` file.
```c
typedef struct tcb {
  void*       rsp;  // the stack pointer
  struct tcb* next; // intrusive next link for queue threading
} tcb_t;
```

We'll also need `enqueue`/`dequeue` helpers. These will live in the `src/scheduler.c`:
```c
static inline void rdy_enqueue(tcb_t* thrd) {
  thrd->next = NULL;
  
  if (rdy_queue_tl == NULL)
    rdy_queue_hd = thrd;
  else
    rdy_queue_tl->next = thrd;

  rdy_queue_tl = thrd;
}

static inline tcb_t* rdy_dequeue(void) {
  if (rdy_queue_hd == NULL)
    return NULL;

  tcb_t* thrd = rdy_queue_hd;

  rdy_queue_hd = rdy_queue_hd->next;
  if (rdy_queue_hd == NULL)
    rdy_queue_tl = NULL;

  return thrd;
}
```
Later we'll generalize these to take queue head/tail pointers as arguments, so the same helpers serve mutex and dead queues.

By keeping the invariant that a thread can live only on one queue at once (the dead queue, mutex wait queues, etc.), we can share that pointer.

#### Thread state

Later we'll have multiple queues, and we'll need a way to tell a dead thread from a blocked, ready, or running one.

That's why in this section we'll add a new enum for the thread's current state.
For now we'll keep it minimal, since currently a thread is either running on the CPU or waiting on the ready queue.

Update your `src/tcb.h` file like so:
```c
typedef enum {
  THRD_READY,
  THRD_RUNNING,
} thrd_state_t;

typedef struct tcb {
  void*        rsp;   // the stack pointer
  struct tcb*  next;  // intrusive next link for queue threading
  thrd_state_t state; // current scheduler state
} tcb_t;
```
Future sections will add more states: `DEAD`, `BLOCKED`, `SLEEPING`.

With new struct fields added, we should also clear them in `src/tcb.c` in the `tcb_init` function:
```c
thrd_t tcb_init(void (*entry)(void)) {
  // ... malloc and stack logic ...

  tcb->rsp = sp;
  tcb->state = THRD_READY;
  return tcb;
}
```
Notice `tcb_init` no longer touches the ready queue - registering a thread with the scheduler is the caller's responsibility, which we'll handle in the upcoming subsections.

We'll also keep an invariant - `thrd->state == THRD_READY` if `thrd` is a part of the ready queue, and `thrd->state == THRD_RUNNING` if `thrd == curr_thrd`.
This invariant will be taken care of in the next subsection, in the implementation of `thrd_yield`.

#### `thrd_yield`

This subsection will introduce the heart of the scheduler - the `thrd_yield` function.
Remember the `thrd_switch` calls at the end of each loop iteration in the previous demo's `func_a`/`func_b`? Those are what `thrd_yield` replaces.
First, declare it in the public API header, `include/thrd_ndl/thrd_ndl.h`:
```c
#ifndef THRD_NDL_H
#define THRD_NDL_H

// ... the rest of the API
void thrd_yield(void);

#endif // THRD_NDL_H
```

Using the ready queue we laid foundations for, it will pick the next thread to run.
This function will end `curr_thrd`'s turn, and pick the head of the ready queue as the next running thread.
If `curr_thrd` is the only ready thread, we switch to ourselves - it's a no-op, but it's harmless.
In later sections, threads will sometimes yield without wanting to be re-queued (e.g. while sleeping). We'll restructure this then.
```c
#include <assert.h> // include this!

// ...

void thrd_yield(void) {
  // stop the current thread from running
  curr_thrd->state = THRD_READY;
  rdy_enqueue(curr_thrd);

  // pop the ready queue's head
  tcb_t* next_thrd = rdy_dequeue();
  assert(next_thrd != NULL);

  // set 'next_thrd' as 'curr_thrd'
  tcb_t* old_thrd = curr_thrd;
  curr_thrd = next_thrd;
  curr_thrd->state = THRD_RUNNING;

  // call the asm context switch procedure
  thrd_switch(old_thrd, curr_thrd);
}
```
In this section the ready queue can't be empty after the enqueue above, since `curr_thrd` was just placed onto it.
When we add more states like `BLOCKED`, `SLEEPING` or `DEAD`, we'll revisit this branch and handle it correctly.

When this thread is later rescheduled, `thrd_switch` returns into the middle of `thrd_yield`, which then returns to whoever called it - exactly as if the function has paused and resumed.

#### Making `thrd_switch` internal

Back in [Context switching](#context-switching), we noted that `thrd_switch` will eventually become internal. With `thrd_yield` now wrapping it, we can deliver on that promise.
Remove the `thrd_switch` declaration from `include/thrd_ndl/thrd_ndl.h`.
At the top of the `src/scheduler.c` file, you can now locally forward-declare `thrd_switch`:
```c
// static declarations ...

extern void thrd_switch(tcb_t* old_tcb, tcb_t* new_tcb);

void thrd_yield(void) { /* ... */ }
```

#### `thrd_init`

In the previous demo, we wrote hacky but working code for initializing the `main_thrd`.
To replace it with something more proper, we'll implement a `thrd_init` function.
Later, it will also handle things like the pool allocator and timer initialization.
But for now let's keep it simple.

While `tcb_init` constructs a thread that doesn't yet exist, `thrd_init` registers the thread that's already running.
The OS gave main its stack when the program started. Allocating a new one would be pointless.
There's also no need to set up the fake stack frame, since main has no entry point to jump to.
It's already running, having been set up the normal way via [`_start`](https://www.gnu.org/software/hurd/glibc/startup.html)
We don't need to set up `rsp` either. 
The first `thrd_switch` involving main will switch away from it (main as `old_tcb`), writing main's real `%rsp` into this slot before anyone reads it.
`NULL` is just a placeholder until that happens.
We also need to set the state to `THRD_RUNNING` immediately, since when `thrd_init` is called, we're already running the main thread.
After `thrd_init` returns, the scheduler invariants from the [Thread state](#thread-state) subsection hold - `curr_thrd` points to the running thread, the ready queue is empty, and main is ready to be context-switched out the moment the first worker is created and scheduled.

We'll declare `thrd_init` in `include/thrd_ndl/thrd_ndl.h`.
This is also our first function that returns a status code.
We'll add the return code definitions to the public API as well.
```c
#ifndef THRD_NDL_H
#define THRD_NDL_H

// return codes
#define THRD_SUCCESS 0
#define THRD_EINVAL  1
#define THRD_ENOMEM  2

// ... the rest of the API
int thrd_init(void);

#endif // THRD_NDL_H
```
We'll implement it in the `src/scheduler.c` file.
```c
#include <stdlib.h> // include this!

int thrd_init(void) {
  // if the thread is already initialized, just return
  if (curr_thrd != NULL)
    return THRD_EINVAL;

  tcb_t* init_thrd = (tcb_t*)malloc(sizeof(tcb_t));
  if (init_thrd == NULL)
    return THRD_ENOMEM;

  init_thrd->rsp = NULL;
  init_thrd->next = NULL;
  init_thrd->state = THRD_RUNNING;

  curr_thrd = init_thrd;

  return THRD_SUCCESS;
}
```
We don't free the main thread's TCB, since it lives for the lifetime of the program.
The pool allocator in [Thread lifecycle](#thread-lifecycle) will replace it anyway.

---

## Roadmap

- [ ] Cover the Cooperative scheduling section of README implementation.
- [ ] Cover the Thread lifecycle section of README implementation.
- [ ] Cover the Blocking primitives section of README implementation.
- [ ] Cover the Sleep and the heap section of README implementation.
- [ ] Cover the Preemption section implementation in README.
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
