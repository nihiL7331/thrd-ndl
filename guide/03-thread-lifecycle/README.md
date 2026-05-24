# Thread lifecycle

As the name suggests, in this section we'll handle the thread lifecycle.
In the previous sections, we were fine with leaving every thread non-freed after they've served their purpose.
This section will implement a dead queue to cover that issue.
But before that, we will replace the `malloc` calls with something more suitable for this project - the pool allocator.
Before that, we need to implement a platform layer for calling to the OS for memory directly.
This is exactly what we'll start off with.

## Platform

We'll use this platform layer for two distinct purposes - backing the pool allocator (introduced in the next subsection) and allocating thread stacks with overflow protection (when we get to `thrd_create`).
Both need OS-level [page](https://en.wikipedia.org/wiki/Page_(computer_memory)) allocation, and the stack case additionally needs a way to mark pages unreadable.
Consider the thread's behavior if we used `malloc` to allocate the stack.
When a thread's stack grows past its allocated size, e.g. via deep recursion, it'll silently overwrite adjacent memory unless something stops it.
A guard page is that something: a page marked unreadable, placed immediately past the end the stack would grow into.
Any access to it segfaults.
POSIX exposes two functions that solve this: `mmap` and `mprotect`.
`mmap` allows us to ask for a page of memory directly.
`mprotect` allows us to 'protect' a certain memory page - causing any access to this memory to result in a segmentation fault.

With those two, we can allocate the needed memory + 1 page, and mark the first page (at the lowest address) as protected.
The stack grows downward from the high end of the region, so an overflow eventually crosses into the guard page and faults.

One thing we need to mention is that `mmap` expects a page-aligned size to be passed.
That's why we'll also need to add an `align_to_page` helper.
Since it's an OS-dependent implementation, let's declare it in a new `src/platform.h` file:

```c
#pragma once

#include <stddef.h>

void*  os_alloc(size_t size);
void   os_free(void* ptr, size_t size);

int    protect_page(void* ptr, size_t size);
size_t page_size(void);
```

The implementation is straightforward, put it in `src/platform.c`:

```c
#include "platform.h"
#include <sys/mman.h>
#include <unistd.h>

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
```

Place `src/platform.c` in `CMakeLists.txt`.

Callers don't have to think about page alignment - `os_alloc` handles it.
With this implemented, we can now use it in the pool allocator.

## Pool allocator

A pool allocator handles allocation on a chunk of memory by splitting it up into equally sized chunks.
This allows for *O(1)* allocation and free, without posing any constraints for our use-case.

To track free memory, it uses an intrusive singly-linked list, storing the `next` pointer directly inside the free block.

We'll define the allocator as a struct, which will then be passed to corresponding functions.
To freely walk through the allocator's memory, we need to know each chunk's size, as well as the total size of allocated memory.
We also need to know where that allocated memory lives.
To achieve that *O(1)* free time, we'll also store the head of the intrusive free list.

We'll implement a generic pool allocator in its own file.
Let's begin with the declarations inside the newly created `src/pool.h` file:

```c
#pragma once

#include <stddef.h>

typedef struct {
  size_t chunk_size;
  size_t total_size;
  void*  start_ptr;
  void*  free_hd;
} pool_t;

int   pool_new(pool_t* pool, size_t chunk_size, size_t chunk_align, size_t chunk_cnt);
int   pool_destroy(pool_t* pool);
void* pool_alloc(pool_t* pool);
int   pool_free(pool_t* pool, void* ptr);
int   pool_clear(pool_t* pool);
```

For this implementation we'll need one new return code in `include/thrd_ndl/thrd_ndl.h`:

```c
// return codes ...
#define THRD_EUNINIT 3
```

We'll also need those includes in the newly created `src/pool.c` file:

```c
#include "pool.h"
#include "platform.h"          // include this for `os_alloc`, `os_free`
#include <thrd_ndl/thrd_ndl.h> // for return codes
#include <stdint.h>
```

While we're at it, add `src/pool.c` to `CMakeLists.txt`.

Now, let's go step by step, implementing every function declared in the header.

We'll begin out of order - with the `pool_clear` function.
We do that, because `pool_new` will call `pool_clear` internally.
`pool_clear` is essentially the zero-initializer for an already existing pool.
It walks through every chunk and chains them into a single free list.

```c
int pool_clear(pool_t* pool) {
  if (pool == NULL)
    return THRD_EINVAL;

  if (pool->start_ptr == NULL)
    return THRD_EUNINIT;

  uint8_t* raw_mem = (uint8_t*)pool->start_ptr;
  size_t num_chunks = pool->total_size / pool->chunk_size;

  for (size_t i = 0; i < num_chunks - 1; ++i) {
    void** curr_chunk = (void**)(raw_mem + i * pool->chunk_size);
    void*  next_chunk = raw_mem + (i + 1) * pool->chunk_size;
    *curr_chunk = next_chunk;
  }

  void** last_chunk = (void**)(raw_mem + (num_chunks - 1) * pool->chunk_size);
  *last_chunk = NULL;
  pool->free_hd = pool->start_ptr;

  return THRD_SUCCESS;
}
```

Now we can tackle the `pool_new` function.
It will be responsible for initializing the pool allocator.
The initialized memory's size is based on a few variables:
* The memory size has to be page-aligned (handled in `os_alloc`),
* The minimum chunk size must be big enough to fit a pointer (for the intrusive list),
* The chunk size must be aligned to the passed value.
To handle the last case, we will implement a helper in `src/pool.c`:

```c
static inline size_t align_up(size_t size, size_t align) {
  return (size + (align - 1)) & ~(align - 1);
}
```

With everything in place, we can implement `pool_new` in `src/pool.c`:

```c
int pool_new(pool_t* pool, size_t chunk_size, size_t chunk_align, size_t chunk_cnt) {
  if (pool == NULL || chunk_size == 0 || chunk_align == 0 || chunk_cnt == 0)
    return THRD_EINVAL;

  size_t min_chunk_size = chunk_size;
  if (sizeof(void*) > min_chunk_size)
    min_chunk_size = sizeof(void*);

  pool->chunk_size = align_up(min_chunk_size, chunk_align);
  if (pool->chunk_size > SIZE_MAX / chunk_cnt)
    return THRD_EINVAL;

  pool->total_size = pool->chunk_size * chunk_cnt;

  pool->start_ptr = os_alloc(pool->total_size);
  if (pool->start_ptr == NULL)
    return THRD_ENOMEM;

  pool_clear(pool);

  return THRD_SUCCESS;
}
```

`pool_destroy` will just safely clean the allocated memory.

```c
int pool_destroy(pool_t* pool) {
  if (pool == NULL)
    return THRD_EINVAL;

  if (pool->start_ptr == NULL)
    return THRD_EUNINIT;

  os_free(pool->start_ptr, pool->total_size);
  pool->start_ptr = NULL;
  pool->free_hd = NULL;

  return THRD_SUCCESS;
}
```

`pool_alloc` is the heart of this subsection's implementation.
Despite its significance, its implementation is simple - it just pops the head off the free list, and returns it to the caller.

```c
void* pool_alloc(pool_t* pool) {
  if (pool == NULL)
    return NULL;

  if (pool->free_hd == NULL)
    return NULL;

  // each free chunk's first bytes 
  // hold the address of the next free chunk
  void* ret_head = pool->free_hd;
  pool->free_hd = *(void**)pool->free_hd;

  return ret_head;
}
```

`pool_free` is the opposite of `pool_alloc` - given `ptr` as an argument, it pushes it onto the free list.
Since it's an internal implementation, we don't need to worry about someone passing a pointer from outside the pool's memory.

```c
int pool_free(pool_t* pool, void* ptr) {
  if (pool == NULL || ptr == NULL)
    return THRD_EINVAL;

  if (pool->start_ptr == NULL)
    return THRD_EUNINIT;

  *((void**)ptr) = pool->free_hd;
  pool->free_hd = ptr;

  return THRD_SUCCESS;
}
```

With this, we've successfully implemented the pool allocator, which we'll use in the later sections.

For a more in-depth explanation of how allocators work, feel free to visit [this](https://github.com/nihiL7331/oo-alloc.git) repository.

## Stack allocation with guard pages

In the Platform subsection, we handled the primitives: `os_alloc`, `protect_page`.
For each thread we need a small recipe that combines them into a single 'usable stack' - the helper goes alongside the primitives in `platform.c`.

But first, the scheduler needs to remember each stack's base so that later on it can clean up the threads from the dead queue.
For that, each TCB will store an additional pointer - `bsp` (base stack pointer).
Update the `tcb_t` struct in `src/tcb.h` like so:

```c
typedef struct tcb {
  void*        rsp;   // the stack pointer
  struct tcb*  next;  // intrusive next link for queue threading
  thrd_state_t state; // current scheduler state
  void*        bsp;   // base stack pointer
} tcb_t;
```

Contrary to the saved stack pointer, `bsp` is an immutable base of the stack region.
It'll be set once at creation, and used at destruction.

With this, we can declare two new helpers in the `platform.h` file: `os_alloc_stack` and `os_free_stack`.

```c
void* os_alloc_stack(size_t usable_size);
void  os_free_stack(void* base_ptr, size_t usable_size);
```

The implementation of both is straight-forward, it'll live in the `platform.c` file:

```c
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
```

It's also a good moment to move the `THRD_STACK_SIZE` macro from `src/tcb.c` to its own file.
Let's create a new file, `src/internal.h`, and move it there:

```c
#pragma once

#define THRD_STACK_SIZE 16384
```

Don't forget to include `src/internal.h` in `src/tcb.c`.

<div align="center">
  <picture>
      <source media="(prefers-color-scheme: dark)"
    srcset="../../docs/assets/guard_stack_dark.svg">
      <source media="(prefers-color-scheme: light)"
    srcset="../../docs/assets/guard_stack_light.svg">
      <img alt="guard page stack layout" src="../../docs/assets/guard_stack_dark.svg">
  </picture>

  <p><em>Memory layout of a thread stack: <code>os_alloc_stack</code> returns the base; the guard page sits at the lowest address, rsp starts at the top and grows down into the usable region.</em></p>
</div>

Now `tcb_create`-equivalent code can ask for a stack with one call.
The next subsection abstracts away the whole initialization - pool for the TCB, `os_alloc_stack` for the region, the fake frame written onto the top - into `thrd_create`.

## `thrd_create`

We have all the pieces - pool allocator for TCBs, `os_alloc_stack` for stacks.
This subsection combines them into `thrd_create` and retires `tcb_init` as a public API and `thrd_register` as a temporary.

We'll start off with reworking `tcb_init` to use the new allocators.
We will store our pool allocator as a static variable inside `src/tcb.c`.
But first we need to initialize the pool allocator.
To initialize it, we will expose an internal `tcb_pool_init` function that will be called by `thrd_init`.
Let's declare the function in the header, `src/tcb.h`:

```c
int  tcb_pool_init(void);
```

We will also add a proper thread count macro, place it in `src/internal.h`:

```c
#define POOL_THRD_CNT 1024
```

Then we can implement the helpers inside `src/tcb.c`:

```c
#include "pool.h"

static pool_t tcb_pool = {0};

// tcb_init ...

int tcb_pool_init(void) {
  return pool_new(&tcb_pool, sizeof(tcb_t), _Alignof(tcb_t), POOL_THRD_CNT);
}
```

Now we can update `thrd_init` to initialize the pool in `src/scheduler.c` (the complete `thrd_init` after the rework is shown below):

```c
int thrd_init(void) {
  // if-check ...

  int pool_ret_val = tcb_pool_init();
  if (pool_ret_val != THRD_SUCCESS)
    return pool_ret_val;

  // main thread initialization ...
}
```

But notice, `thrd_init` also uses `malloc` to allocate memory for the main thread's TCB.
To replace it cleanly, we'll add two more pool-related helpers in `src/tcb.h`: let's call them `tcb_alloc` and `tcb_free`:

```c
tcb_t* tcb_alloc(void);
void   tcb_free(tcb_t* tcb);
```

The implementation in `src/tcb.c` is straight-forward:

```c
#include <string.h> // include this for 'memset'

tcb_t* tcb_alloc(void) {
  tcb_t* tcb = pool_alloc(&tcb_pool);
  if (tcb == NULL)
    return NULL;

  memset(tcb, 0, sizeof(tcb_t));
  return tcb;
}

void tcb_free(tcb_t* tcb) {
  if (tcb == NULL)
    return;

  pool_free(&tcb_pool, tcb);
}
```

Now the fully updated `thrd_init` looks like so:

```c
int thrd_init(void) {
  // if the thread is already initialized, just return
  if (curr_thrd != NULL)
    return THRD_EINVAL;

  int pool_ret_val = tcb_pool_init();
  if (pool_ret_val != THRD_SUCCESS)
    return pool_ret_val;

  tcb_t* init_thrd = tcb_alloc();
  if (init_thrd == NULL)
    return THRD_ENOMEM;

  // tcb_alloc zeroed every field
  init_thrd->state = THRD_RUNNING;

  curr_thrd = init_thrd;

  return THRD_SUCCESS;
}
```

Finally, we can update `tcb_init` as well:

```c
#include "platform.h" // include this for 'os_alloc_stack' and 'page_size'

thrd_t tcb_init(void (*entry)(void)) {
  tcb_t* tcb = tcb_alloc();
  if (tcb == NULL)
    return NULL;

  tcb->bsp = os_alloc_stack(THRD_STACK_SIZE);
  if (tcb->bsp == NULL) {
    tcb_free(tcb);
    return NULL;
  }

  size_t size  = THRD_STACK_SIZE + page_size();
  uint64_t* sp = (uint64_t*)((uint8_t*)tcb->bsp + size);

  *(--sp) = 0;               // padding for ABI alignment
  *(--sp) = (uint64_t)entry; // fake return address for 'ret'
  sp     -= CALLEE_REG_CNT;  // space for callee-saved registers

  tcb->rsp   = sp;
  tcb->state = THRD_READY;
  return tcb;
}
```

We'll change the padding slot's value in the next subsection, [`thrd_exit` and the dead queue](#thrd_exit-and-the-dead-queue).

Now we'll retire `thrd_register` (the temporary helper) and demote `tcb_init` to internal-only, both replaced from the user's perspective by `thrd_create`.
Its purpose will be to initialize the TCB and enqueue it onto the ready queue.
First, let's declare it in `include/thrd_ndl/thrd_ndl.h`:

```c
int thrd_create(thrd_t* out_thrd, void (*entry)(void));
```

Its implementation will live in `src/scheduler.c`:

```c
int thrd_create(thrd_t* out_thrd, void (*entry)(void)) {
  if (out_thrd == NULL || entry == NULL)
    return THRD_EINVAL;

  tcb_t* new_thrd = tcb_init(entry);
  if (new_thrd == NULL)
    return THRD_ENOMEM;

  // pass the address to the pointer given by the user
  *out_thrd = (thrd_t)new_thrd;

  // push to ready queue
  rdy_enqueue(new_thrd);

  return THRD_SUCCESS;
}
```

Now we can move `tcb_init` declaration from `include/thrd_ndl/thrd_ndl.h` to `tcb.h`.
Also, replace the opaque return type `thrd_t` with `tcb_t*`.
Since `src/scheduler.h` only contained `thrd_register`, the file can be deleted entirely.

## `thrd_exit` and the dead queue

`thrd_exit` cleanly retires the current thread.
It marks it as finished, hands control to the scheduler, and never returns to the caller.
It's what the demo in [Cooperative scheduling](../02-cooperative-scheduling/README.md) tried to achieve with `while (1) thrd_yield();`, and what the `tcb_init` padding slot has been waiting to point at.

The intuition is simple.
`thrd_exit` frees the dying thread's TCB (back to the pool) and its stack (via `os_free_stack`), then yields.
But there's a problem with the second step.

`thrd_exit` runs on the dying thread's stack. Calling `os_free_stack` on the very stack we're executing from unmaps the memory we're actively accessing.
The next instruction would read from a freed page and segfault.

So the freeing has to happen later, when some other thread is running.
The dying thread enqueues its TCB on a dead queue, yields away, and another thread does the cleanup once it's safe.
This subsection will implement the "enqueueing threads to the dead queue" part.

We start off with adding a new enum value to `thrd_state_t` in `src/tcb.h`:

```c
typedef enum {
  // ... other values
  THRD_DEAD,
} thrd_state_t;
```

We need to add a static dead queue in `scheduler.c`:

```c
static tcb_t* dead_queue_hd = NULL;
```

Notice we don't need the queue's tail here, because on each yield we will just clean out the whole queue.
Now, let's handle the `thrd_exit` itself.
Declare it in the public header, `include/thrd_ndl/thrd_ndl.h`:

```c
#include <stdnoreturn.h> // include this for 'noreturn'

// ...

noreturn void thrd_exit(void);
```

The implementation will do three things:
1. Set `curr_thrd->state` to `THRD_DEAD`.
2. Push `curr_thrd` onto `dead_queue_hd`.
3. Call `thrd_yield`.

`thrd_yield` never returns - the dying thread is dead, so the updated yield (below) won't re-enqueue it, and the context switch hands control to someone else permanently.
That's why it uses the `noreturn` keyword.
Place it in `src/scheduler.c`.

```c
#include <stdnoreturn.h> // include this for 'noreturn'

noreturn void thrd_exit(void) {
  curr_thrd->state = THRD_DEAD;

  curr_thrd->next = dead_queue_hd;
  dead_queue_hd = curr_thrd;

  thrd_yield();

  abort(); 
}
```

`thrd_yield` returns for non-dying threads, so the compiler can't infer that this call never returns here.
The `abort` satisfies the `noreturn` requirements, silencing the error with the `-Werror` flag.

Now, we can apply the implicit `thrd_exit` call in `tcb_init`.
Update `tcb_init`'s fake-frame setup. We just need to place `thrd_exit` where the padding was.

```c
tcb_t* tcb_init(void (*entry)(void)) {
  // ...
  *(--sp) = (uint64_t)thrd_exit; // was: *(--sp) = 0;
  // ...
}
```

Let's look how the frame behaves now:
1. `thrd_switch` into the thread.
2. `ret` pops `entry`'s address.
3. Execution lands in the worker function.
4. Worker function returns.
5. Its `ret` pops the next thing on the stack: `thrd_exit`'s address.
6. Control falls into `thrd_exit` automatically.

<div align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="../../docs/assets/updated_stack_dark.svg">
    <source media="(prefers-color-scheme: light)" srcset="../../docs/assets/updated_stack_light.svg">
    <img alt="fake stack" src="../../docs/assets/updated_stack_dark.svg">
  </picture>

  <p><em>The updated fake initial stack frame written by <code>tcb_init</code>. It falls into the <code>thrd_exit</code> implicitly.</em></p>
</div>

Now let's take a look at `thrd_yield`.
As of now, we unconditionally re-enqueue `curr_thrd`.
However, it might be dead.
Re-enqueuing it would break the invariant we've pointed out a while back: a thread can be on one queue at a time.
That's why we just need to check, if `curr_thrd` is actually running:

```c
void thrd_yield(void) {
  if (curr_thrd->state == THRD_RUNNING) {
    curr_thrd->state = THRD_READY;
    rdy_enqueue(curr_thrd);
  }
  // ...
}
```

This also creates new edge-cases, where the ready queue is empty.
They will be handled in the next subsection.

## Cleaning up dead threads

`thrd_exit` places dying threads on the dead queue, but never frees them.
This subsection adds the cleanup pass that clears the queue, freeing each dead thread's stack and returning its TCB to the pool.

We want to free each thread from the dead queue as early as possible.
From the last subsection we already know that a dying thread can't clear itself, since its stack is still in use.
That means that the clear has to be on top of `thrd_yield`, with a check `if (curr_dead != curr_thrd)`.

Let's look at a example of how this behaves, step by step:
1. Thread A calls `thrd_exit` -> A gets pushed onto `dead_queue`, A yields.
2. Top of A's `thrd_yield`: cleanup sees `thrd_a == curr_thrd`, skips it.
3. State guard skips re-enqueue (since `thrd_a->state == THRD_DEAD`).
4. Dequeue picks thread B. `thrd_switch(thrd_a, thrd_b)`.
5. B runs, eventually yields. Top of B's `thrd_yield` cleans up A.

So all we need to do is iterate over the dead queue and call `os_free_stack` then `tcb_free` on every entry.
We can abstract it away to a helper - let's call it `tcb_destroy`.
Declare it in `src/tcb.h`:

```c
void tcb_destroy(tcb_t* tcb);
```

And it will just call `os_free_stack` and `tcb_free` (in that order).
Place it in `src/tcb.c`.

```c
void tcb_destroy(tcb_t* tcb) {
  if (tcb == NULL)
    return;
 
  os_free_stack(tcb->bsp, THRD_STACK_SIZE);
  tcb_free(tcb);
}
```

If `thrd_exit` gets called on the main thread, since main's `bsp` is `NULL`, `os_free_stack` gets called with `NULL` as the first argument.
It's safe, because `os_free` contains a `NULL`-check.

With all of this settled down, let's add the loop on top of `thrd_yield` in `src/scheduler.c`:

```c
void thrd_yield(void) {
  tcb_t* prev_dead = NULL;
  tcb_t* curr_dead = dead_queue_hd;

  while (curr_dead != NULL) {
    if (curr_dead == curr_thrd) {
      prev_dead = curr_dead;
      curr_dead = curr_dead->next;
    } else {
      tcb_t* dead_thrd = curr_dead;

      // remove from queue
      // must save next before destroy,
      // pool_free overwrites the tcb
      if (prev_dead == NULL)
        dead_queue_hd = curr_dead->next; 
      else
        prev_dead->next = curr_dead->next;
      curr_dead = curr_dead->next;

      // free the tcb
      tcb_destroy(dead_thrd);
    }
  }

  // stop the current thread from running
  if (curr_thrd->state == THRD_RUNNING) {
    curr_thrd->state = THRD_READY;
    rdy_enqueue(curr_thrd);
  }

  // ...
}
```

One important detail is that `tcb_free` will overwrite the TCB, so we need to store its `->next` pointer before destroying.

Before, we said that we'll handle the `next_thrd != NULL` assertion in `thrd_yield`.
Now we'll replace it with a more nuanced check:
* If `next_thrd == NULL` and `curr_thrd->state == THRD_DEAD`, then every thread is gone - exit the program normally.
* If `next_thrd == NULL` and `curr_thrd` is in some other non-`THRD_RUNNING` state, then something has gone wrong.

To exit the program, we'll use `_Exit` instead of `exit`.
[`_Exit`](https://stackoverflow.com/questions/57161596/how-to-use-exit-safely-from-any-thread) skips `atexit` handlers and `stdio` buffer flushing, and we want a clean process exit without running cleanup code on a stack we're about to discard.

So, replace `assert (next_thrd != NULL)` in `thrd_yield` with this:

```c
if (next_thrd == NULL && curr_thrd->state == THRD_DEAD)
  _Exit(0);
else if (next_thrd == NULL && curr_thrd->state != THRD_RUNNING)
  _Exit(1);
```

Now you can remove `#include <assert.h>`.

The dead queue is now self-draining.
`thrd_exit` is responsible for enqueueing, `thrd_yield` for clearing.
With this in place, the next subsection can finally show worker functions returning naturally - the implicit-exit path, the dead queue, and the cleanup loop together let workers come and go without leaks.

## A lifecycle-aware demo

This section got rid of most of the hacks present in the last demo.
Going through the last demo, we had:
* `while (1) thrd_yield();` after each thread's loop - removed. Workers now `return` normally, using the implicit-exit trick from the [`thrd_exit` and the dead queue](#thrd_exit-and-the-dead-queue) subsection.
* `tcb_init(func_a); thrd_register(thrd_a)` replaced with `thrd_create` from [`thrd_create`](#thrd_create).
* `while (completed < 2) thrd_yield();` - this will be solved in the next section, [Blocking primitives](../04-blocking-primitives/README.md).
The demo now uses only the public API, and worker functions look like normal C.

```c
#include <stdio.h>
#include <thrd_ndl/thrd_ndl.h>

static int completed = 0;

static void func_a(void) {
  for (int i = 0; i < 3; ++i) {
    printf("A: %d\n", i);
    thrd_yield();
  }
  completed++;
  // returning here triggers thrd_exit implicitly
}

static void func_b(void) {
  for (int i = 0; i < 3; ++i) {
    printf("B: %d\n", i);
    thrd_yield();
  }
  completed++;
}

int main(void) {
  if (thrd_init() != THRD_SUCCESS)
    return 1;

  thrd_t thrd_a, thrd_b;
  if (thrd_create(&thrd_a, func_a) != THRD_SUCCESS)
    return 1;
  if (thrd_create(&thrd_b, func_b) != THRD_SUCCESS)
    return 1;

  while (completed < 2)
    thrd_yield();

  printf("done\n");
}
```

Build and run the same way as before:

```bash
cmake -B build && cmake --build build && ./build/demo
```

Expected output - same as in the previous demo:

```
A: 0
B: 0
A: 1
B: 1
A: 2
B: 2
done
```

Notice that now when `func_a` falls off the end, its `ret` pops `thrd_exit`'s address from the padding slot.
`thrd_exit` marks the threads state as `THRD_DEAD`, pushes it onto the dead queue, and yields.
The yield's cleanup loop skips the just-exited thread (it's still `curr_thrd`), but the next yield by another thread removes it.
Threads are freed one yield after they exit.

**[<| prev: Cooperative scheduling](../02-cooperative-scheduling/README.md)** | **[next: Blocking primitives |>](../04-blocking-primitives/README.md)**
