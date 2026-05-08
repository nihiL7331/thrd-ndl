#pragma once

#include <stddef.h>

typedef enum {
  READY,
  RUNNING,
  DEAD,
} thrd_state_t;

typedef struct tcb {
  void* rsp;          // current stack pointer
  thrd_state_t state; // current state of the thread
  void* bsp;          // base stack pointer
  size_t stack_size;
  struct tcb* next;
} tcb_t;

tcb_t* tcb_init(void (*entry_point)(void));
