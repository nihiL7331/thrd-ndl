#pragma once

#include <stdint.h> // include for 'uint64_t'

typedef enum {
  THRD_READY,
  THRD_RUNNING,
  THRD_DEAD,
  THRD_BLOCKED,
  THRD_ZOMBIE,
  THRD_SLEEPING,
} thrd_state_t;

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

int  tcb_pool_init(void);

tcb_t* tcb_alloc(void);
void   tcb_free(tcb_t* tcb);
tcb_t* tcb_init(void (*entry)(void));
void   tcb_destroy(tcb_t* tcb);
