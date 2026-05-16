#pragma once

typedef enum {
  THRD_READY,
  THRD_RUNNING,
} thrd_state_t;

typedef struct tcb {
  void*        rsp;   // the stack pointer
  struct tcb*  next;  // intrusive next link for queue threading
  thrd_state_t state; // current scheduler state
} tcb_t;
