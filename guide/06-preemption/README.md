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

The Windows implementation will use a completely different architecture, because Windows doesn't have POSIX signals. It's covered in the [Porting](../07-porting/README.md) section.

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
For the exact positioning check the source code.
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

**[<| prev: Sleep and the heap](../05-sleep-and-the-heap/README.md)** | **[next: Porting |>](../07-porting/README.md)**
