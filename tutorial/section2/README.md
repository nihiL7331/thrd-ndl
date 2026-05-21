# Cooperative scheduling

So far we've handled everything in the demo explicitly.
In this section, we'll implement the *scheduler* that'll handle some of that boilerplate for us.

## The ready queue

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
#include <stdlib.h> // include this for 'NULL' and later 'malloc'

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

## Thread state

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

## `thrd_yield`

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
Place this code in the `src/scheduler.c` file.
```c
#include <assert.h>            // include this for 'assert'
#include <thrd_ndl/thrd_ndl.h> // and this for 'thrd_switch'

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

## Making `thrd_switch` internal

Back in [Context switching](../section1/README.md), we noted that `thrd_switch` will eventually become internal. With `thrd_yield` now wrapping it, we can deliver on that promise.
Remove the `thrd_switch` declaration from `include/thrd_ndl/thrd_ndl.h`.
At the top of the `src/scheduler.c` file, you can now locally forward-declare `thrd_switch`:
```c
// static declarations ...

extern void thrd_switch(tcb_t* old_tcb, tcb_t* new_tcb);

void thrd_yield(void) { /* ... */ }
```
Since this declaration lives in `scheduler.c` where `tcb_t` is visible, we can use the concrete type instead of the opaque `thrd_t` the public header used.

## `thrd_init`

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
The pool allocator in [Thread lifecycle](../section3/README.md) will replace it anyway.

## Registering workers

The main thread is initialized and `thrd_yield` knows the ready queue, but nothing currently puts threads onto it.
There's one piece missing: getting workers onto the ready queue.
That's why we need to add one more temporary function: `thrd_register`.
In the [Thread lifecycle](../section3/README.md) section, we will replace this temporary solution with `thrd_create`, which will combine `tcb_init` and `thrd_register` into one call.

Because it's temporary, let's place it in a new internal header, `src/scheduler.h`.
```c
#pragma once

#include <thrd_ndl/thrd_ndl.h>

void thrd_register(thrd_t thrd);
```
Implementation lives in `src/scheduler.c`, below the queue helpers it wraps.
```c
void thrd_register(thrd_t thrd) {
  if (thrd == NULL)
    return;

  rdy_enqueue((tcb_t*)thrd);
}
```
We wrap `rdy_enqueue` instead of exposing it directly because the wrapper names what the caller wants to do (register a thread) without forcing them to know how it's implemented (push to the ready queue).
When `thrd_create` arrives, the implementation can change without callers caring.

With this in place, we have everything we need to put `thrd_init`, `thrd_yield`, and `thrd_register` together in a demo where worker functions no longer name each other.

## A scheduler-based demo

With the additions made in this section, worker functions no longer have to name each other.
The new functions call `thrd_yield` and have no idea who runs next.
It's also worth pointing out that there's no `main_thrd` global variable needed, thanks to `thrd_init`.
To set up each thread, we need two steps now: `tcb_init`, then `thrd_register`.
This will be abstracted away via `thrd_create` in the next section, [Thread lifecycle](../section3/README.md).

However, workers can't cleanly terminate yet. 
After the loop, we increment a shared `completed` counter and enter `while (1) thrd_yield();`, yielding forever instead of returning.
Returning would fall off the end of the function and segfault.
This will also be handled in the next section, [Thread lifecycle](../section3/README.md), with the addition of `thrd_exit`.

Main's `while (completed < 2) thrd_yield();` is the same kind of stopgap on the other end.
It blocks by yielding because there's no `thrd_join` yet implemented to make it sleep until workers finish.
[Blocking primitives](../section4/README.md) will implement that.

```c
#include <thrd_ndl/thrd_ndl.h>
#include "scheduler.h"
#include <stdio.h>

static int completed = 0;

static void func_a(void) {
  for (int i = 0; i < 3; ++i) {
    printf("A: %d\n", i);
    thrd_yield();
  }
  completed++;

  while (1)
    thrd_yield(); // can't return yet - no 'thrd_exit'
}

static void func_b(void) {
  for (int i = 0; i < 3; ++i) {
    printf("B: %d\n", i);
    thrd_yield();
  }
  completed++;

  while (1)
    thrd_yield();
}

int main(void) {
  if (thrd_init() != THRD_SUCCESS)
    return 1;

  thrd_t thrd_a = tcb_init(func_a);
  thrd_t thrd_b = tcb_init(func_b);
  if (thrd_a == NULL || thrd_b == NULL)
    return 1;

  thrd_register(thrd_a);
  thrd_register(thrd_b);

  while (completed < 2)
    thrd_yield();

  printf("done\n");
}
```
Including a scheduler-internal header from a demo is generally bad practice.
We do that here, because `thrd_create` doesn't exist yet.

Same as in the first section, to build and run the demo use:
```bash
cmake -B build && cmake --build build && ./build/demo
```
The expected output, also the same as with the first demo, is:
```
A: 0
B: 0
A: 1
B: 1
A: 2
B: 2
done
```
The next section will introduce `thrd_create` and `thrd_exit`, finishing the thread lifecycle.
Beginning from the next section, the scheduler will have to handle a brand new queue: the dead queue.

**[<| prev: Context switching](../section1/README.md)** | **[next: Thread lifecycle |>](../section3/README.md)**
