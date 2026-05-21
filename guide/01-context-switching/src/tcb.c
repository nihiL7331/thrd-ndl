#include "tcb.h"
#include <thrd_ndl/thrd_ndl.h>
#include <stdlib.h>
#include <stdint.h>

#define THRD_STACK_SIZE 16384
#define CALLEE_REG_CNT 6

thrd_t tcb_init(void (*entry)(void)) {
  tcb_t* tcb = (tcb_t*)malloc(sizeof(tcb_t));
  if (tcb == NULL)
    return NULL;

  uint8_t* stack = (uint8_t*)malloc(THRD_STACK_SIZE);
  if (stack == NULL) {
    free(tcb);
    return NULL;
  }
  // stack pointer is at the top of the stack
  uint64_t* sp  = (uint64_t*)(stack + THRD_STACK_SIZE);

  *(--sp) = 0;               // padding for ABI alignment
  *(--sp) = (uint64_t)entry; // fake return address for 'ret'
  sp -= CALLEE_REG_CNT;      // space for callee-saved registers

  tcb->rsp = sp;
  return tcb;
}
