### Sleep and the heap

The overall outcome of this section is to add a proper `thrd_sleep(ms)` functionality to the library.
But before we will be able to put threads to sleep, the scheduler needs a reliable way to tell time.

#### Time and the platform

If a thread wants to sleep for 100ms, we need to record the current time, add 100ms, and wake the thread once the clock passes this threshhold.

We can't use standard C functions like `time` here, nor can we use POSIX `gettimeofday`.
These account for stuff like daylight saving adjustments, user changes, and NTP background synchronization.
If the system clock adjusts backwards by an hour while a thread is sleeping, a 100ms sleep turns into a one-hour (and 100ms!) wait.

To prevent this, we must use a [monotonic clock](https://linux-audit.com/what-is/monotonic-timer/).
A monotonic clock is a specialized OS timer that only ever moves forward at a constant rate, completely detached from the calendar.

We also need a way to put the underlying OS thread to sleep.
If every single thread is sleeping, the scheduler has no work to do.
If it just busy waits, it'll consume 100% of the CPU thread.
By using an OS-level sleep, the scheduler can pause the entire process and yield the CPU back to the host system until the nearest thread is due to wake up.

Let's add two new helpers to `src/platform.h`:

```c
#include <stdint.h> // include for 'uint64_t'

// ...

uint64_t get_os_time(void);
void     os_sleep_ms(uint64_t time_ms);
```

Since we are focusing on Linux/macOS for the time being, we will use `clock_gettime` with `CLOCK_MONOTONIC` to get the time in milliseconds, and `nanosleep` to park the OS thread.
Add the following to `src/platform.c`:

```c
#include <time.h> // include for 'struct timespec', 'clock_gettime', 'CLOCK_MONOTONIC', 'nanosleep'

// ...

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
```

The Windows equivalent will be covered in the [Porting](../07-porting/README.md) section.

With our timekeeping primitives in place, we can move on to the data structure that will hold our sleeping threads, the binary min-heap.

#### The binary min-heap

When threads go to sleep, the scheduler needs a way to keep track of them and wake them up at the correct time.

A naive approach would be to push sleeping threads onto a simple linked list, just like we did with all the queues.
But how do we know which thread to wake up first? 
We would need to either search a whole list on every tick (*O(n)* time), or we could keep the list sorted so the earliest wake-up time is always at the head.
But keeping a linked list sorted means every time a thread goes to sleep, we have to walk the list to find the right insertion point (also *O(n)* time).

Instead, we will use a [binary min-heap](https://www.andrew.cmu.edu/course/15-121/lectures/Binary%20Heaps/heaps.html).
A min-heap is a tree-like data structure where every parent node has a smaller value than its children.
This guarantees that the smallest value (the earliest wake-up time) is always instantly accessible at the root (*O(1)* time).
Inserting a new thread or extracting the root takes at most *O(log n)* steps, being way better than the naive approach.

First, let's update our TCB to store its scheduled wake-up time.
Open `src/tcb.h` and add `wakeup_time`:

```c
#include <stdint.h> // include for 'uint64_t'

// ...

typedef struct tcb {
  void*        rsp;           // the stack pointer
  struct tcb*  next;          // intrusive next link for queue threading
  thrd_state_t state;         // current scheduler state
  void*        bsp;           // base stack pointer
  struct tcb*  join_queue_hd; // head of joiners waiting on this thread
  struct tcb*  join_queue_tl; // tail of joiners waiting on this thread
  uint64_t     wakeup_time;   // the abs monotonic time this thread should wake
} tcb_t;
```

Instead of hardcoding the heap directly for the scheduler, we'll build a generic, reusable heap structure (like we did with the [Pool allocator](../03-thread-lifecycle/README.md#pool-allocator)).
It will take a backing array, a capacity, and a custom comparison function, allowing us to use it for TCBs.

Let's declare the API in a new file, `src/heap.h`:

```c
#pragma once

#include <stddef.h> // include for 'size_t'

typedef struct {
  void** data;
  size_t count;
  size_t cap;
  // comparator returns:
  // < 0 if a should return first,
  // > 0 if b should return first,
  // 0 if equal.
  int (*cmp)(const void* a, const void* b);
} heap_t;

int   heap_new(heap_t* heap, void** storage, size_t cap, int (*cmp_fn)(const void*, const void*));
int   heap_push(heap_t* heap, void* obj);
void* heap_peek(const heap_t* heap);
void* heap_pop(heap_t* heap);
```

Next, create `src/heap.c`.
Add it to your `CMakeLists.txt`.
The logic relies on treating a flat array as a binary tree.
When we push an object, we place it at the end of the array and sift it up until it is no longer smaller than its parent.
When we pop the root, we take the last element in the array, move it to the root, and sift it down until it is smaller than its children.
Place the implementation below in `src/heap.c`.

```c
#include "heap.h"
#include <assert.h>
#include <stddef.h>
#include <thrd_ndl/thrd_ndl.h>

#define PARENT(i)  (((i) - 1) / 2)
#define CHILD_L(i) (((i) * 2) + 1)
#define CHILD_R(i) (((i) * 2) + 2)

static inline void sift_up(heap_t* heap, size_t idx);
static inline void sift_down(heap_t* heap, size_t idx);

int heap_new(heap_t* heap, void** storage, size_t cap, int (*cmp_fn)(const void*, const void*)) {
  if (cap == 0 || heap == NULL || storage == NULL || cmp_fn == NULL)
    return THRD_EINVAL;

  heap->data = storage;
  heap->count = 0;
  heap->cap = cap;
  heap->cmp = cmp_fn;

  return THRD_SUCCESS;
}

int heap_push(heap_t* heap, void* obj) {
  if (heap == NULL || obj == NULL)
    return THRD_EINVAL;

  if (heap->count == heap->cap)
    return THRD_ENOMEM;

  heap->data[heap->count++] = obj;
  sift_up(heap, heap->count - 1);

  return THRD_SUCCESS;
}

void* heap_peek(const heap_t* heap) {
  if (heap == NULL || heap->count == 0)
    return NULL;

  return heap->data[0];
}

void* heap_pop(heap_t* heap) {
  if (heap == NULL || heap->count == 0)
    return NULL;

  void* pop_data = heap->data[0];
  heap->data[0] = heap->data[--heap->count];

  // this does nothing if heap->count <= 1
  sift_down(heap, 0);

  return pop_data;
}

static inline void heap_swap(heap_t* heap, size_t idx_a, size_t idx_b) {
  void* tmp = heap->data[idx_a];
  heap->data[idx_a] = heap->data[idx_b];
  heap->data[idx_b] = tmp;
}

static inline void sift_up(heap_t* heap, size_t idx) {
  while (idx > 0) {
    size_t par_idx = PARENT(idx);

    if (heap->cmp(heap->data[idx], heap->data[par_idx]) >= 0)
      break;

    heap_swap(heap, idx, par_idx);

    idx = par_idx;
  }
}

static inline void sift_down(heap_t* heap, size_t idx) {
  while (1) {
    size_t l_idx = CHILD_L(idx);
    size_t r_idx = CHILD_R(idx);
    size_t min_idx = idx;

    if (l_idx < heap->count && heap->cmp(heap->data[l_idx], heap->data[min_idx]) < 0)
      min_idx = l_idx;
    
    if (r_idx < heap->count && heap->cmp(heap->data[r_idx], heap->data[min_idx]) < 0)
      min_idx = r_idx;

    // if both indices are either out of range, or are bigger than data[idx],
    // then break
    if (min_idx == idx)
      break;

    // else swap and continue from the smallest index
    heap_swap(heap, idx, min_idx);
    idx = min_idx;
  }
}
```

We didn't use the intrusive `next` pointer approach like we did with the TCB, because using the macros defined at the top a binary heap accesses parent and child nodes at *O(1)* time, whereas an intrusive structure would require complex pointer juggling and tree-balancing algorithms to achieve the same result.

With this handled, we can instantiate it inside our scheduler and expose the `thrd_sleep` API.

<div align="center">
  <p><em>For a more in-depth explanation of how this data structure functions, check <a href="https://www.andrew.cmu.edu/course/15-121/lectures/Binary%20Heaps/heaps.html">this</a> (it was also linked above).</em></p>
</div>

#### `thrd_sleep`

With our monotonic clock and priority queue in place, we are ready to implement the `thrd_sleep` function.

We'll begin with adding a new state to the enum in `src/tcb.h`:

```c
typedef enum {
  THRD_READY,
  THRD_RUNNING,
  THRD_DEAD,
  THRD_BLOCKED,
  THRD_ZOMBIE,
  THRD_SLEEPING,
} thrd_state_t;
```

Next, expose the new sleep function in the public API header, `include/thrd_ndl/thrd_ndl.h`:

```c
#include <stdint.h> // include for 'uint64_t'

// ...

void thrd_sleep(uint64_t time_ms);
```

Now we will wire up the generic `heap_t` into the scheduler.
In `src/scheduler.c` we need an array to back the heap, the heap struct itself, and a comparison function that tells the heap how to sort `tcb_t` structs based on theier wakeup time.

Add the following to `src/scheduler.c`:

```c
#include "heap.h"     // include for 'heap_new'
#include "internal.h" // include for 'POOL_THRD_CNT'

static void* sleep_heap_storage[POOL_THRD_CNT];
static heap_t sleep_queue;
static int wakeup_cmp(const void* a, const void* b);

// ...

static int wakeup_cmp(const void* a, const void* b) {
  const uint64_t wakeup_a = ((tcb_t*)a)->wakeup_time;
  const uint64_t wakeup_b = ((tcb_t*)b)->wakeup_time;

  return (wakeup_a > wakeup_b) - (wakeup_a < wakeup_b);
}
```

We must initialize the heap when the scheduler starts.
Hence, add the initialization call to `thrd_init`:

```c
int thrd_init(void) {
  // already init check ...
  // pool init ...

  // initialize sleep binary heap
  int heap_ret_val = heap_new(&sleep_queue, sleep_heap_storage, POOL_THRD_CNT, wakeup_cmp);
  if (heap_ret_val != THRD_SUCCESS)
    return heap_ret_val;

  // create main thrd ...
}
```

Finally, we can implement `thrd_sleep`.

When a thread wants to sleep, it does the following:
1. Asks the OS for the current time.
2. Adds the requested sleep duration to calculate its absolute `wakeup_time`.
3. Changes its state to `THRD_SLEEPING`.
4. Pushes itself onto the heap.
5. Yields.

Add this implementation to `src/scheduler.c`:

```c
#include "platform.h" // include for 'get_os_time'

void thrd_sleep(uint64_t time_ms) {
  if (curr_thrd == NULL)
    return;

  if (time_ms == 0) {
    thrd_yield();
    return;
  }

  curr_thrd->wakeup_time = get_os_time() + time_ms;
  curr_thrd->state = THRD_SLEEPING;

  heap_push(&sleep_queue, curr_thrd);

  thrd_yield();
}
```

Notice that if `time_ms == 0`, then we skip the heap entirely and just call `thrd_yield`.

Because the thread changes its state to `THRD_SLEEPING`, `thrd_yield`'s guard (`if (curr_thrd->state == THRD_RUNNING)`) will skip putting it back onto the ready queue.
The thread will be suspended, sitting at the correctly sorted position in the min-heap.

But right now we have no option to wake it up.
As it stands, a sleeping thread will stay on the heap forever.
In the next subsection, we will update the scheduler loop to process this heap and actually wake threads up when their time arrives.

#### Waking up, the sleeping scheduler

A sleeping thread is completely off the CPU.
It relies entirely on whichever thread that happens to be running `thrd_yield`, to notice that its deadline has passed and move it back to the ready queue.

Every time a thread yields, we need to check the top of the sleep heap.
Because it's a min-heap, the earliest/closest wakeup time is at `heap_peek(&sleep_queue)`.
If the current time is greater than or equal to that thread's wakeup time, it is time to wake it up.
Furthermore, because multiple threads might have deadlines that passed while the CPU was busy, we must check it in a `while` loop until the root is a thread that is still sleeping.

Let's place it after the `THRD_RUNNING` check inside `thrd_yield` in `src/scheduler.c`:

```c
void thrd_yield(void) {
  // ...

  // stop the current thread from running
  if (curr_thrd->state == THRD_RUNNING) {
    curr_thrd->state = THRD_READY;
    thrd_enqueue(curr_thrd, &rdy_queue_hd, &rdy_queue_tl);
  }

  uint64_t curr_time_ms = get_os_time();
  tcb_t* sleep_hd = (tcb_t*)heap_peek(&sleep_queue);
  while (sleep_hd != NULL && curr_time_ms >= sleep_hd->wakeup_time) {
    heap_pop(&sleep_queue);

    sleep_hd->state = THRD_READY;
    thrd_enqueue(sleep_hd, &rdy_queue_hd, &rdy_queue_tl);

    sleep_hd = (tcb_t*)heap_peek(&sleep_queue);
  }

  // ...
}
```

This logic handles handles threads waking up, provided the CPU is constantly calling `thrd_yield`.
But what happens if the ready queue is completely empty, the dead queue is clean, but there are threads sitting in the sleep heap waiting for their time to come?

Currently, if the ready queue is empty, `thrd_yield` reaches the end, sees `next_thrd == NULL` and checks if we should exit:

```c
if (next_thrd == NULL && curr_thrd->state == THRD_DEAD)
  _Exit(0);
else if (next_thrd == NULL && curr_thrd->state != THRD_RUNNING)
  _Exit(1);
```

If we hit this branch while threads are sleeping, the process crashes with `_Exit(1)`, even though it's just resting.
To fix this, if the ready queue is empty, we calculate the time difference between now and the top of the sleep heap, and tell the underlying OS thread to sleep for exactly that duration.
We can structure this cleanly by wrapping the dequeue logic in a `while (rdy_queue_hd == NULL)` loop in `thrd_yield`, replacing the old `if`-statements:

```c
while (rdy_queue_hd == NULL) {
  // there's no one else waiting,
  // keep running the thread
  tcb_t* sleep_hd = (tcb_t*)heap_peek(&sleep_queue);
  if (sleep_hd != NULL) {
    // wait here until thread wakes up,
    curr_time_ms = get_os_time();

    // prevent underflow if late
    if (sleep_hd->wakeup_time > curr_time_ms)
      os_sleep_ms(sleep_hd->wakeup_time - curr_time_ms);
    
    curr_time_ms = get_os_time();
    while (sleep_hd != NULL && curr_time_ms >= sleep_hd->wakeup_time) {
      tcb_t* awake_thrd = heap_pop(&sleep_queue);
      awake_thrd->state = THRD_READY;
      thrd_enqueue(awake_thrd, &rdy_queue_hd, &rdy_queue_tl);
      sleep_hd = (tcb_t*)heap_peek(&sleep_queue);
    }

  } else if (curr_thrd->state == THRD_DEAD) // all threads are dead, close the program
    _Exit(0);
  else // all threads blocked with no holders
    _Exit(1);
}

// guaranteed to have a ready thread now
tcb_t* next_thrd = thrd_dequeue(&rdy_queue_hd, &rdy_queue_tl);
```

By parking the OS thread with `os_sleep_ms`, we keep our scheduler's CPU usage at 0% while all virtual threads are asleep.
When the OS wakes up, we immediately process the heap, move the awakened threads to the ready queue, and break out the loop to context switch to them.

#### A sleep-aware demo

With our heap and scheduler integrated, we can finally test `thrd_sleep`.

We'll update our demo so `func_a` and `func_b` sleep for different durations.
Instead of using mutexes to enforce strict alteration, we'll rely entirely on `thrd_sleep`.
`func_a` will sleep for 200ms, and `func_b` will sleep for 100ms.

Because `func_b` sleeps for half the time of `func_a`, we expect it to print twice as often.

```c
#include <stdio.h>
#include <thrd_ndl/thrd_ndl.h>

static void func_a(void) {
  for (int i = 0; i < 3; ++i) {
    printf("A: %d\n", i);
    thrd_sleep(200);
  }
}

static void func_b(void) {
  for (int i = 0; i < 6; ++i) {
    printf("B: %d\n", i);
    thrd_sleep(100);
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

  thrd_join(thrd_a);
  thrd_join(thrd_b);

  printf("done\n");
}
```

Rebuild exactly as before:

```bash
cmake -B build && cmake -build build && ./build/demo
```

The expected output differs here from the last demos.
It shows the threads naturally interleaving based entirely on the monotonic clock and our min-heap.

```
A: 0
B: 0
B: 1
A: 1
B: 2
B: 3
A: 2
B: 4
B: 5
done
```

If you watch this in the terminal, it won't instantly print.
The 100ms/200ms pauses will be physically visible.
Because of the `os_sleep_ms` call placed at the bottom of `thrd_yield`, the CPU usage during those pauses will sit at 0%.

**[<| prev: Blocking primitives](../04-blocking-primitives/README.md)** | **[next: Preemption |>](../06-preemption/README.md)**
