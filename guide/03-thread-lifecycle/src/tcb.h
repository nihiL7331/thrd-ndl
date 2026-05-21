#pragma once

typedef enum {
  THRD_READY,
  THRD_RUNNING,
  THRD_DEAD,
} thrd_state_t;

typedef struct tcb {
  void*        rsp;   // the stack pointer
  struct tcb*  next;  // intrusive next link for queue threading
  thrd_state_t state; // current scheduler state
  void*        bsp;   // base stack pointer
} tcb_t;

int  tcb_pool_init(void);

tcb_t* tcb_alloc(void);
void   tcb_free(tcb_t* tcb);
tcb_t* tcb_init(void (*entry)(void));
void   tcb_destroy(tcb_t* tcb);
