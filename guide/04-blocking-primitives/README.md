# Blocking primitives

In the previous demo, we still had the ugly `while (completed < 2) thrd_yield();` line.
Any kind of "wait until X" operation written with `thrd_yield` wastes turns.
With N waiters, the ready queue is N threads doing nothing.
This section will fix this.

Up to now there were three states: `THRD_READY`, `THRD_RUNNING` and `THRD_DEAD`.
Now we'll add a fourth one - for a thread, that's neither runnable nor finished - it's parked, waiting for an external event.
It will be `THRD_BLOCKED`, along with a corresponding wait queue.

Just update the enum in `src/tcb.h`:

```c
typedef enum {
  THRD_READY,
  THRD_RUNNING,
  THRD_DEAD,
  THRD_BLOCKED,
} thrd_state_t;
```

Every primitive in this section will follow the same three-step recipe:
1. Mark thread as blocked.
2. Push it onto the primitive's private wait queue.
3. Call `thrd_yield`.

Waking a thread up will be a mirrored operation.

## `thrd_join`

`thrd_join` is the simplest primitive included in this section.
It will be responsible for blocking the caller until a target thread finishes, and it will achieve that without spinning.

Waking up joined threads will occur in `thrd_exit`, which only knows about `curr_thrd`.
That means that we need to store reference to the joining threads inside the TCB struct itself.

While [`pthread_join`](https://man7.org/linux/man-pages/man3/pthread_join.3.html) allows for a single joiner for each thread, here we'll implement a slightly more complex list of joiners per thread.
It will allow multiple threads to join the same target, waiting until it finishes.

So we need to store a head and a tail of the join queue in the TCB.
Update the struct in `src/tcb.h`:

```c
typedef struct tcb {
  void*        rsp;           // the stack pointer
  struct tcb*  next;          // intrusive next link for queue threading
  thrd_state_t state;         // current scheduler state
  void*        bsp;           // base stack pointer
  struct tcb*  join_queue_hd; // head of joiners waiting on this thread
  struct tcb*  join_queue_tl; // tail of joiners waiting on this thread
} tcb_t;
```

The join queue fields will be `NULL` until someone joins.
They will be cleared by `thrd_exit` after waking.

With the addition of a new queue, it's a good moment to generalize the `rdy_enqueue`/`rdy_dequeue` helpers to work for any queue.
Place them in a new file, `src/queue.h`:

```c
#pragma once

#include "tcb.h"    // include this for 'tcb_t'
#include <stdlib.h> // include this for 'NULL'

static inline void thrd_enqueue(tcb_t* thrd, tcb_t** hd, tcb_t** tl) {
  thrd->next = NULL;

  if (*tl != NULL)
    (*tl)->next = thrd;
  else
    *hd = thrd;

  *tl = thrd;
}

static inline tcb_t* thrd_dequeue(tcb_t** hd, tcb_t** tl) {
  if (*hd == NULL)
    return NULL;

  tcb_t* pop_thrd = *hd;
  
  *hd = pop_thrd->next;
  if (*hd == NULL)
    *tl = NULL;

  return pop_thrd;
}
```

With this added, you can remove `rdy_enqueue`/`rdy_dequeue` and replace calls to it with their generic `thrd` counterparts.

`thrd_join`, as mentioned in the introduction to this section, will simply mark the current thread as blocked, push it onto the wait queue and call `thrd_yield`.

First, add it to the public API in `include/thrd_ndl/thrd_ndl.h`:

```c
int thrd_join(thrd_t thrd);
```

Then add its implementation in `src/scheduler.c`:

```c
#include "queue.h" // include for 'thrd_enqueue'

int thrd_join(thrd_t thrd) {
  if (thrd == NULL || (tcb_t*)thrd == curr_thrd)
    return THRD_EINVAL;

  tcb_t* cast_thrd = (tcb_t*)thrd;
  if (cast_thrd->state == THRD_DEAD)
    return THRD_SUCCESS;

  curr_thrd->state = THRD_BLOCKED;

  thrd_enqueue(curr_thrd, &cast_thrd->join_queue_hd, &cast_thrd->join_queue_tl);

  thrd_yield();

  return THRD_SUCCESS;
}
```

Reading `cast_thrd->state` here is only safe while the caller knows the target's TCB hasn't been reclaimed.
It means that `thrd_join` must run before any other thread yields after the target exits - otherwise the dead-queue cleanup pass may have already returned the slot to the pool, possibly with a different thread occupying it.

Now, we need to modify `thrd_exit` to wake up every thread from `curr_thrd`'s wait queue.
It's important to wake them up **before** the yield, otherwise the dying thread yields away and the joiners sit blocked until something else happens to schedule (which it won't because nothing else will).

```c
noreturn void thrd_exit(void) {
  curr_thrd->state = THRD_DEAD;

  curr_thrd->next = dead_queue_hd;
  dead_queue_hd = curr_thrd;

  tcb_t* awake_thrd = curr_thrd->join_queue_hd;
  while (awake_thrd != NULL) {
    tcb_t* next_thrd = awake_thrd->next;

    awake_thrd->state = THRD_READY;
    thrd_enqueue(awake_thrd, &rdy_queue_hd, &rdy_queue_tl);

    awake_thrd = next_thrd;
  }

  curr_thrd->join_queue_hd = NULL;
  curr_thrd->join_queue_tl = NULL;

  thrd_yield();

  abort(); 
}
```

Putting both halves together, here's what the joiner experiences:
1. The joiner calls `thrd_join(target)`.
2. The joiner sets its own state to `THRD_BLOCKED` and enqueues itself onto `target->join_queue_hd/tl`.
3. The joiner calls `thrd_yield`. The `if (state == THRD_RUNNING)` guard skips the ready re-enqueue.
4. The scheduler dequeues some other ready thread, `thrd_switch` saves joiner's callee-saved registers onto its stack and its `%rsp` into the TCB. Joiner is frozen mid-yield.
5. (some other work happens, possibly across many yields)
6. The target eventually falls off the end of its entry function, lands in `thrd_exit`. The wake loop walks the join queue, sets each waiter to `THRD_READY`, and ready-enqueues them.
7. The scheduler later picks joiner from the ready queue. `thrd_switch` restores its saved registers, and `ret` resumes inside `thrd_yield` right after the `thrd_switch` call.
8. The joiner's `thrd_yield` returns into `thrd_join`, which then returns `THRD_SUCCESS` to its caller.

If the joiner is the only candidate the scheduler has and nothing else is ready, `thrd_yield` will hit the `_Exit(1)` branch from [Cleaning up dead threads](../03-thread-lifecycle/README.md#cleaning-up-dead-threads) - that branch doubles as deadlock detection.

<div align="center">
  <picture>
      <source media="(prefers-color-scheme: dark)"
    srcset="../../docs/assets/joiner_timeline_dark.svg">
      <source media="(prefers-color-scheme: light)"
    srcset="../../docs/assets/joiner_timeline_light.svg">
      <img alt="joiner/target relative timeline" src="../../docs/assets/joiner_timeline_dark.svg">
  </picture>

  <p><em>State transitions during a <code>thrd_join</code>. The joiner parks on the target's join queue and stays off-CPU until the target's <code>thrd_exit</code> flips it back to <code>THRD_READY</code>.</em></p>
</div>

Unlike a `thrd_yield`, which re-queues the caller and is guaranteed to resume on its own, a blocked thread can only resume when another thread explicitly wakes it.
This is what makes the primitive "blocking" instead of "polling".

## Zombie

In the previous subsection, we implemented `thrd_join` and noted that it is only safe if the caller knows the target thread hasn't been reclaimed yet.
If we think closely about the timeline of our thread lifecycle, there is a race condition.

Consider this sequence of events:
1. Thread `A` finishes its execution and falls into `thrd_exit`.
2. `A` pushes itself onto the dead queue and calls `thrd_yield`.
3. The scheduler picks up thread `B`. At the top of `thrd_yield`, the cleanup loop completely frees `A`'s TCB and stack to the pool.
4. Thread `C`, which was busy doing other work, finally calls `thrd_join(thrd_a)`.

`C` is now reading a dangling pointer.
Even worse, if the pool already reused that memory for a brand new thread, `C` will park itself on a completely unrelated thread's wait queue.

To fix this, we'll borrow a concept from OS kernels (like [Linux](https://access.redhat.com/sites/default/files/attachments/processstates_20120831.pdf)): a `THRD_ZOMBIE` state.
When a thread finishes executing, it doesn't immediately go to the dead queue.
Instead, it becomes a zombie, meaning that it's off the CPU but its struct and stack are still there.
It's a temporary state between e.g. `THRD_RUNNING` and `THRD_DEAD`.
It only truly dies and gets cleaned up when another thread acknowledges its death by joining it.

First, let's update `thrd_state_t` in `src/tcb.h`:

```c
typedef enum {
  THRD_READY,
  THRD_RUNNING,
  THRD_DEAD,
  THRD_BLOCKED,
  THRD_ZOMBIE,
} thrd_state_t;
```

Now we will rewrite `thrd_exit` in `src/scheduler.c`.
The logic branches based on whether anyone is already waiting for us.
If there are threads in our join queue, we wake them up and proceed to the dead queue normally (the joiner has already acknowledged us).
If there are no joiners, we become a zombie and wait for one.

```c
noreturn void thrd_exit(void) {
  if (curr_thrd->join_queue_hd != NULL) {
    // we have joiners, wake them up and proceed to the dead queue
    curr_thrd->state = THRD_DEAD;

    curr_thrd->next = dead_queue_hd;
    dead_queue_hd = curr_thrd;

    tcb_t* awake_thrd = curr_thrd->join_queue_hd;
    while (awake_thrd != NULL) {
      tcb_t* next_thrd = awake_thrd->next;

      awake_thrd->state = THRD_READY;
      thrd_enqueue(awake_thrd, &rdy_queue_hd, &rdy_queue_tl);

      awake_thrd = next_thrd;
    }

    curr_thrd->join_queue_hd = NULL;
    curr_thrd->join_queue_tl = NULL;
  } else
    // no one is waiting, become a zombie
    curr_thrd->state = THRD_ZOMBIE;

  thrd_yield();

  abort(); 
}
```

Finally, we update `thrd_join`.
If a thread calls `thrd_join` and sees the target is already a `THRD_ZOMBIE`, it means the target finished before we could park on its wait queue.
The joiner simply pushes the zombie onto the dead queue to be cleaned up, and returns immediately without yielding.

Replace the `THRD_DEAD` check in `thrd_join` with this new zombie logic:

```c
int thrd_join(thrd_t thrd) {
  if (thrd == NULL || (tcb_t*)thrd == curr_thrd)
    return THRD_EINVAL;

  tcb_t* cast_thrd = (tcb_t*)thrd;
  
  // if the target is a zombie, bury it and return immediately
  if (cast_thrd->state == THRD_ZOMBIE) {
    cast_thrd->state = THRD_DEAD;

    cast_thrd->next = dead_queue_hd;
    dead_queue_hd = cast_thrd;

    return THRD_SUCCESS;
  }

  curr_thrd->state = THRD_BLOCKED;

  thrd_enqueue(curr_thrd, &cast_thrd->join_queue_hd, &cast_thrd->join_queue_tl);

  thrd_yield();

  return THRD_SUCCESS;
}
```

But what happens if a thread exits but it's never joined?
Under this implementation, it stays a `THRD_ZOMBIE` forever, leaking its TCB and stack memory until the process terminates.
A solution to this would be to introduce a `thrd_detach` function to tell the scheduler that a thread will never be joined, allowing it to bypass the zombie state and clean itself up immediately.
This is out of scope for this implementation (for the time being).

## Mutexes

Up to now, when threads shared state (like the `completed` counter from the [A lifecycle-aware demo](../03-thread-lifecycle/README.md#a-lifecycle-aware-demo) section) we got away with it, but only because the read-modify-write had no yield between its halves.
Once a yield can land there, the writes race.

Consider this simple snippet:

```c
#include <thrd_ndl/thrd_ndl.h>
#include <stdio.h>

static int counter = 0;

static void func_thrd(void) {
  int local_counter = counter;

  thrd_yield();

  counter = local_counter + 1;
}

int main(void) {
  if (thrd_init() != THRD_SUCCESS)
    return 1;

  thrd_t thrd_a, thrd_b;

  if (thrd_create(&thrd_a, func_thrd) != THRD_SUCCESS)
    return 1;
  if (thrd_create(&thrd_b, func_thrd) != THRD_SUCCESS)
    return 1;

  thrd_join(thrd_a);
  thrd_join(thrd_b);

  printf("%d\n", counter);
}
```

You may think that the output will be plain `2`.
Well, you'd be wrong.
Due to a race condition, this will output `1`.

Here's the exact mechanism that occurred:
1. Thread A reads `counter = 0` into its local, it yields.
2. Thread B reads `counter = 0` into its local, it also yields.
3. A resumes after `thrd_yield`, writes `counter = 1`.
4. B resumes after `thrd_yield`, also writes `counter = 1`.

The `thrd_yield` here is explicit only to make the race reproducible.
In real code, any procedure call that internally yields can land between the read and the write - and once [Preemption](../06-preemption/README.md) is finished, even individual instructions can be interrupted.
The mutex closes that window regardless of where the yield actually happens.

This is also a great example that cooperative scheduling doesn't eliminate races, it just narrows where they can happen.

A mutex (**MUT**ual **EX**clusion device) serializes access to shared state.
Only one thread can hold it at a time, and any other thread that tries to acquire it blocks until the holder releases.

Unlike threads, mutexes own no OS resources - they're just an owner pointer and a wait queue.
The caller allocates them wherever the data they protect lives, the wait queue lives on the primitive itself rather than on any TCB, and the API has `mtx_init` but no `mtx_destroy`, since there's nothing internal to destroy.
The trade-off is that the caller must keep the mutex alive while any thread might be parked on it, similar to the lifetime contract from `thrd_join`.
This matches POSIX [`pthread_mutex_t`](https://man7.org/linux/man-pages/man3/pthread_mutex_lock.3.html).

As always, begin with the declaration - this time in the public API header, `include/thrd_ndl/thrd_ndl.h`:

```c
typedef struct {
  thrd_t owner;         // 'NULL' when no owner, otherwise the holding thread
  thrd_t wait_queue_hd; // head of threads parked on this mutex
  thrd_t wait_queue_tl; // tail of threads parked on this mutex
} mtx_t;
```

The mutex struct lives in the public header so callers can allocate it, but its fields reference the opaque `thrd_t`, since callers never read or write these directly.

Now we'll walk over every mutex-related procedure that will be exposed in the public API.
We'll begin with the most trivial one: `mtx_init`.

Its mission will be to zero the owner, zero the queue heads, and validate non-`NULL`.
Notice, that it essentially does the same thing as `mtx_t m = {0};` would do.
This function is mainly here for the symmetry with upcoming [Condition variables](#condition-variables).

Begin with declaring it in the public header, `include/thrd_ndl/thrd_ndl.h`

```c
int mtx_init(mtx_t* mtx);
```

Create a new file, which will contain all of the implementations for this subsection, `src/mutex.c`.
Add it to `CMakeLists.txt`.

```c
#include <thrd_ndl/thrd_ndl.h>
#include <string.h>

int mtx_init(mtx_t* mtx) {
  if (mutex == NULL)
    return THRD_EINVAL;

  memset(mtx, 0, sizeof(*mtx));

  return THRD_SUCCESS;
}
```

Next on our list is `mtx_trylock`.
We'll cover `mtx_trylock` before `mtx_lock`, because `mtx_lock` will be a wrapper over `mtx_trylock`.

Declare it in the public header:

```c
int mtx_trylock(mtx_t* mtx);
```

We want `mtx_trylock` to lock a mutex if it's possible. Otherwise we'll return a new code - we'll call it `THRD_EBUSY`:

```c
#define THRD_EBUSY 4
```

`mtx_trylock` observes the owner and either claims it for `curr_thrd` or reports busy.
However `curr_thrd` is a static variable inside `src/scheduler.c`, and we want to access it here.
We need to expose a getter to it.
Create a file `src/scheduler.h` and place this in it:

```c
#pragma once

#include "tcb.h"

tcb_t* get_curr_thrd(void);
```

Update `src/scheduler.c` with this simple function:

```c
// ...

tcb_t* get_curr_thrd(void) {
  return curr_thrd;
}
```

Finally, we can implement `mtx_trylock` in `src/mutex.c`:

```c
#include "scheduler.h" // include for 'get_curr_thrd'

int mtx_trylock(mtx_t* mtx) {
  if (mtx->owner == NULL) {
    mtx->owner = get_curr_thrd();
    return THRD_SUCCESS;
  }

  return THRD_EBUSY;
}
```

The check->set is atomic because the scheduler is cooperative.
There's no `thrd_yield` running between the read of `mutex->owner` and the write to it, so no other thread can observe the in-between state.
This is a correctness benefit we get for free from the M:1 model, and one we'll have to pay back explicitly in the [Preemption](../06-preemption/README.md) section by disabling preemption around critical sections like this one.

With `mtx_trylock` implemented, we can handle its older brother now - `mtx_lock`.
Thanks to `mtx_trylock`, this implementation will be simple:
1. Try locking via `mtx_trylock`, if succeeded - return.
2. Otherwise mark the `curr_thrd->state` as `THRD_BLOCKED`, enqueue on mutex's wait queue, yield.
3. On wake, don't recall `mtx_trylock`, since `mtx_unlock` set us as owner before waking us. Unlock transfers ownership directly to a waiting thread rather than clearing the field and letting the wakee re-race. This lets `mtx_lock` skip the retry.

As always, update the public header:

```c
int mtx_lock(mtx_t* mtx);
```

And the implementation in `src/mutex.c`:

```c
#include "queue.h" // include for 'thrd_enqueue'

int mtx_lock(mtx_t* mtx) {
  if (mtx == NULL)
    return THRD_EINVAL;

  if (mtx_trylock(mtx) == THRD_SUCCESS)
    return THRD_SUCCESS;

  tcb_t* curr_thrd = get_curr_thrd();
  curr_thrd->state = THRD_BLOCKED;

  thrd_enqueue(curr_thrd, (tcb_t**)&mtx->wait_queue_hd, (tcb_t**)&mtx->wait_queue_tl);

  thrd_yield();
  
  return THRD_SUCCESS;
}
```

We'll finish this section with `mtx_unlock`.
Its responsibility is to:
1. Validate the passed mutex,
2. If wait queue is empty, then clear the owner and return,
3. If wait queue is not empty, then dequeue head, set head as the new owner, flip its state to `THRD_READY` and enqueue it onto the ready queue.

Similarly to the `curr_thrd`, the ready queue is stored as a static variable inside `src/scheduler.c`.
That's why we need to expose a helper inside the scheduler - it will be responsible for resuming a thread.
We'll bring back the internal `src/scheduler.h`, which was deleted back in [`thrd_create`](../03-thread-lifecycle/README.md#thrd_create). `cond_wait` in the next subsection will reuse it.
Add this to the scheduler's header, `src/scheduler.h`:

```c
void resume_thrd(tcb_t* thrd);
```

Implement it in `src/scheduler.c` as so:

```c
void resume_thrd(tcb_t* thrd) {
  if (thrd == NULL)
    return;

  thrd->state = THRD_READY;
  thrd_enqueue(thrd, &rdy_queue_hd, &rdy_queue_tl);
}
```

Now, going back to the `mtx_unlock` implementation, update the public header:

```c
int mtx_unlock(mtx_t* mtx);
```

And cover the implementation in `src/mutex.c`:

```c
#include "tcb.h" // include for 'tcb_t', 'THRD_READY'

int mtx_unlock(mtx_t* mtx) {
  if (mtx == NULL || mtx->owner != get_curr_thrd())
    return THRD_EINVAL;

  if (mtx->wait_queue_hd == NULL) {
    mtx->owner = NULL;
    return THRD_SUCCESS;
  }

  tcb_t* pop_thrd = thrd_dequeue((tcb_t**)&mtx->wait_queue_hd, (tcb_t**)&mtx->wait_queue_tl);
  mtx->owner = pop_thrd;
  resume_thrd(pop_thrd);

  return THRD_SUCCESS;
}
```

With the public API for mutexes handled, notice the unlocker doesn't clear the owner field when there's a waiter, it transfers ownership directly.
The alternative would be to wake one waiter up, and let it call `mtx_trylock` itself when it runs.
However, this leaves a window where a thread calling `mtx_lock` after the unlock (but before the waiter runs) could steal the mutex.
The handoff approach guarantees FIFO fairness.

Before walking through a contention scenario, a few edge cases worth knowing about:
* Recursive lock by the same thread. It's currently undefined, the second `mtx_lock` calls `mtx_trylock`, sees a non-`NULL` owner, and blocks on a wait queue no one will empty. If no other thread is ready, the `_Exit(1)` branch from the [`Cleaning up dead threads`](../03-thread-lifecycle/README.md#cleaning-up-dead-threads) subsection doubles as a deadlock detection. This library doesn't support this kind of locking, but POSIX exposes it via [`PTHREAD_mtx_RECURSIVE`](https://pubs.opengroup.org/onlinepubs/7908799/xsh/pthread_mutexattr_settype.html).
* Unlock from a non-owner returns `THRD_EINVAL` via the `mtx->owner != get_curr_thrd()` check. Without this check a non-holder could transfer ownership to a waiter, breaking the mutual exclusion.
* Unlock of an unheld mutex returns `THRD_EINVAL` also via the `mtx->owner != get_curr_thrd()` check.
* Freeing a mutex while threads are parked on it wait queue corrupts their state when `mtx_unlock` later wakes them, similarly to the `thrd_join` reclaim hazard, the caller must keep the mutex alive until no waiter can reference to it.
* Don't call `memcpy` on `mtx_t`. It will get corrupted on the next enequeue, each mutex needs its own `mtx_init`.

The woken waiter doesn't return immediately, it's just queued.
The unlocker keeps running until it yields.
From the waiter's perspective it was parked in `mtx_lock`'s yield call, and when the scheduler eventually picks it up, the yield returns and `mtx_lock` returns `THRD_SUCCESS`.
The wake mechanism is identical to `thrd_join`'s, what's different is that the wake also carries ownership state with it.

Putting the four operations together, here's what a typical contention sequence looks like.
Assume `A` and `B` are both ready, `mtx` is freshly initialized.
1. `A` calls `mtx_lock(&mtx)`. `mtx_trylock(&mtx)` sees `mtx->owner == NULL`, sets `mtx->owner = A`, returns `THRD_SUCCESS`. `mtx_lock` returns immediately, without blocking or yielding.
2. `A` runs its critical section and yields.
3. `B` is picked up by the scheduler and calls `mtx_lock(&mtx)`. `mtx_trylock(&mtx)` sees `mtx->owner == A`, returns `THRD_EBUSY`.
4. `B` sets its state to `THRD_BLOCKED`, enqueues itself on `mtx`'s wait queue and calls `thrd_yield`. The `if (state == THRD_RUNNING)` guard skips the ready re-enqueue.
5. Scheduler resumes `A`. `B` meanwhile is frozen mid-yield, exactly as the joiner was in `thrd_join`.
6. `A` finishes its critical section and calls `mtx_unlock(&mtx)`. `B` is dequeues from the wait queue, set as the new mutex owner, flipped to `THRD_READY` and enqueued on the ready queue.
7. `A` keeps running, eventually yields.
8. Scheduler picks `B` from the ready queue. `thrd_switch` resumes `B` inside the `thrd_yield` from step 4, yield returns into `mtx_lock`, which returns. `B` is now the owner, and never had to retry the lock.

Two things change compared to `thrd_join`.
First, the wait queue lives on the primitive (the mutex struct), not on a TCB, so that any number of mutexes can exist independently, and a thread can park on whichever one it tries to acquire.
Second, the wake doesn't just unpark the waiter.
It also transfers ownership along with the wake, so the woken thread doesn't have to retry `mtx_trylock`.
This is what avoided the constant wake-storm mentioned earlier.
The next subsection, [Condition variables](#condition-variables), keeps the first property and drops the second.
Condition variable wakes carry no ownership, which is why they need a paired mutex to fill the gap.

<div align="center">
  <picture>
      <source media="(prefers-color-scheme: dark)"
    srcset="../../docs/assets/mutex_dark.svg">
      <source media="(prefers-color-scheme: light)"
    srcset="../../docs/assets/mutex_light.svg">
      <img alt="mutex scenario walkthrough" src="../../docs/assets/mutex_dark.svg">
  </picture>

  <p><em>Ownership during a contended <code>mtx_lock</code>/<code>mtx_unlock</code> cycle. The owner field passes directly from <code>A</code> to <code>B</code> at the unlock moment, it's never <code>NULL</code> which is what prevents a third thread from barging in and stealing the mutex between unlock and <code>B</code>'s resume.</em></p>
</div>

## Condition variables

Mutexes solve the problem of simultaneous access to shared state, but they don't solve the problem of waiting for the state to change.

Consider this scenario: thread `A` wants to pop an item from a shared queue. 
It locks the mutex, sees the queue is empty, and unlocks it.
What could it do next?
It could yield and check again later, but that's wasting CPU cycles on the ready queue just to check an empty list.

We want the thread to instead park itself entirely until another thread pushes an item onto the shared queue.
This is what the **condition variable** is responsible for.
It provides a wait queue where threads can sleep until another thread explicitly signals that a specific condition might now be true.

First, let's add the struct and function declarations to the public header, `include/thrd_ndl/thrd_ndl.h`:

```c
typedef struct {
  thrd_t wait_queue_hd; // head of threads parked on this condition
  thrd_t wait_queue_tl; // tail of threads parked on this condition
} cond_t;

int cond_init(cond_t* cond);
int cond_wait(cond_t* cond, mtx_t* mtx);
int cond_signal(cond_t* cond);
int cond_bcast(cond_t* cond);
```

We'll place the implementation in a new file, `src/cond.c`.
Don't forget to add it to `CMakeLists.txt`.
Just like `mtx_init`, `cond_init` is a simple zero-initializer.

```c
#include <thrd_ndl/thrd_ndl.h>
#include <string.h>

int cond_init(cond_t* cond) {
  if (cond == NULL)
    return THRD_EINVAL;

  memset(cond, 0, sizeof(*cond));

  return THRD_SUCCESS;
}
```

Now for the heart of the mechanism, `cond_wait`.

A thread must always hold the associated mutex before calling `cond_wait`.
Inside the function, the thread does a very specific sequence of actions:
1. Enqueues itself on the condition variable's wait queue.
2. Releases the mutex (so other threads can change the shared state).
3. Yields.
4. Upon waking up, it must reacquire the mutex before returning to the caller.

Here's the implementation in `src/cond.c`:

```c
#include "scheduler.h" // include for 'get_curr_thrd', 'resume_thrd'
#include "queue.h"     // include for 'thrd_enqueue', 'thrd_dequeue'

int cond_wait(cond_t* cond, mtx_t* mtx) {
  if (cond == NULL || mtx == NULL || mtx->owner != get_curr_thrd())
    return THRD_EINVAL;

  tcb_t* curr_thrd = get_curr_thrd();

  thrd_enqueue(curr_thrd, (tcb_t**)&cond->block_queue_hd, (tcb_t**)&cond->block_queue_tl);

  curr_thrd->state = THRD_BLOCKED;

  // unlock mutex so other thread can grab 
  // the lock and change the shared data
  mtx_unlock(mtx);

  // park the thread
  thrd_yield();

  // lock back the mutex before returning to the caller
  mtx_lock(mtx);

  return THRD_SUCCESS;
}
```

Finally, we need a way to make these sleeping threads wake up.
`cond_signal` wakes up exactly one thread from the queue.
`cond_bcast` wakes up all of them.
Unlike `mtx_unlock`, waking a thread up from a cond variable doesn't transfer any ownership, it just moves them from the `cond_t`'s wait queue to the ready queue.

Add these to `src/cond.c`:

```c
int cond_signal(cond_t* cond) {
  if (cond == NULL)
    return THRD_EINVAL;

  if (cond->wait_queue_hd != NULL) {
    tcb_t* signal_thrd = thrd_dequeue((tcb_t**)&cond->wait_queue_hd, (tcb_t**)&cond->wait_queue_tl);
    resume_thrd(signal_thrd);
  }

  return THRD_SUCCESS;
}

int cond_bcast(cond_t* cond) {
  if (cond == NULL)
    return THRD_EINVAL;

  while (cond->wait_queue_hd != NULL) {
    tcb_t* signal_thrd = thrd_dequeue((tcb_t**)&cond->wait_queue_hd, (tcb_t**)&cond->wait_queue_tl);
    resume_thrd(signal_thrd);
  }

  return THRD_SUCCESS;
}
```

To see how these pieces interact, let's trace the exact sequence of a wakeup:
1. Thread `A` calls `cond_wait`. It drops the mutex, goes to sleep, and freezes mid-yield.
2. Thread `B` acquires the mutex, changes the shared state, and calls `cond_signal`. `resume_thrd` pulls `A` off the condition wait queue and puts it on the ready queue.
3. `B` unlocks the mutex and yields. The scheduler picks up `A`.
4. `A` resumes inside `cond_wait` and immediately calls `mtx_lock`.

The last step is the most important part.
If `B` calls `cond_signal` but hasn't yet unlocked the mutex, `A` will wake up, attempt to lock the mutex, and immediately block again, this time on the mutex's wait queue.
`cond_wait` only returns to the caller once the mutex is successfully reacquired.

It's worth noting that another thread might grab the mutex first and change the state again between `A` waking up and actually acquiring the lock.
For this reason, `cond_wait` should not be an `if` statement, rather it should be wrapped in a `while` loop that checks the condition on every wake, like so:

```c
mtx_lock(&mtx);

while (queue_hd == NULL)
  cond_wait(&cond, &mtx);

// safe to pop
mtx_unlock(&mtx);
```

With this covered, we can close out the section with a demo, as usual.

## A blocking-aware demo

With `thrd_join`, mutexes, and condition variables implemented, we have completely eliminated the need for busy-waiting in our user code.
We can now safely synchronize threads and share state.

To showcase this, we will update `func_a` and `func_b` from the previous demos.
Instead of yielding blindly and hoping they alternate perfectly, we will enforce strict alteration using a shared `curr_turn` variable, a mutex, and a cond variable.

Notice how the `while (completed < 2) thrd_yield();` in `main` is gone, replaced by two `thrd_join` calls.

```c
#include <stdio.h>
#include <thrd_ndl/thrd_ndl.h>

static mtx_t  mtx;
static cond_t cond;

static int curr_turn = 0;

static void func_a(void) {
  for (int i = 0; i < 3; ++i) {
    mtx_lock(&mtx);

    while (curr_turn != 0)
      cond_wait(&cond, &mtx);

    printf("A: %d\n", i);
    curr_turn = 1;

    cond_signal(&cond);
    mtx_unlock(&mtx);
  }
}

static void func_b(void) {
  for (int i = 0; i < 3; ++i) {
    mtx_lock(&mtx);

    while (curr_turn != 1)
      cond_wait(&cond, &mtx);

    printf("B: %d\n", i);
    curr_turn = 0;

    cond_signal(&cond);
    mtx_unlock(&mtx);
  }
}

int main(void) {
  if (thrd_init() != THRD_SUCCESS)
    return 1;

  mtx_init(&mtx);
  cond_init(&cond);

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

Build and run exactly as before:

```bash
cmake -B build && cmake --build build && ./build/demo
```

The expected output guarantees strict alteration, regardless of how the scheduler queues the threads behind the scenes:

```
A: 0
B: 0
A: 1
B: 1
A: 2
B: 2
done
```

This demo proves that our blocking architecture is working perfectly.
When `func_b` calls `cond_wait`, it drops `mutex` and sleeps.
This allows `func_a` to acquire `mutex`, print its counter, change the turn and call `cond_signal`.
`func_a` then drops `mutex`, and `func_b` wakes up, automatically reacquiring the lock to continue the cycle.

**[<| prev: Thread lifecycle](../03-thread-lifecycle/README.md)** | **[next: Sleep and the heap |>](../05-sleep-and-the-heap/README.md)**
