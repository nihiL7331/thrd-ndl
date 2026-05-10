#pragma once

#include <stdint.h>
#include <stddef.h>

typedef enum {
  THRD_READY,
  THRD_RUNNING,
  THRD_DEAD,
  THRD_BLOCKED,
  THRD_SLEEPING,
} thrd_state_t;

typedef struct tcb {
  void* rsp;          // current stack pointer
  thrd_state_t state; // current state of the thread
  void* bsp;          // base stack pointer
  size_t stack_size;

  struct tcb* next;

  void (*user_proc)(void);

  // thread blocking
  struct tcb* join_queue_hd;
  struct tcb* join_queue_tl;

  // sleep
  uint64_t wakeup_time;
} tcb_t;

tcb_t* tcb_init(void (*entry_point)(void));
void tcb_destroy(tcb_t* tcb);
tcb_t* tcb_alloc(void);
int tcb_pool_init(void);
