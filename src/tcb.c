#include "tcb.h"
#include "platform.h"
#include "internal.h"
#include "pool.h"
#include "scheduler.h"
#include <stdio.h>
#include <thrd_ndl/thrd_ndl.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>

#ifdef __aarch64__
  #define CALLEE_SAVED_REG_CNT 12
  #define X19_REG_POS 10
  #define X30_REG_POS 1

  // required for implicit 'thrd_exit'
  extern void thrd_tramp(void);
#elif defined(_WIN32)
  #define CALLEE_SAVED_REG_CNT 8
#else
  #define CALLEE_SAVED_REG_CNT 6
#endif

static pool_t tcb_pool = {0};

// a wrapper for thread procedure call
static void tcb_wrap(void) {
  preempt_enable();
  get_curr_thrd()->user_proc();
  thrd_exit();
}

tcb_t* tcb_init(void (*entry_point)(void)) {
  // allocate 'tcb_t' on the heap
  tcb_t* tcb = tcb_alloc();
  if (tcb == NULL)
    return NULL;

  // get size + one page for stack overflow safety
  // page alignment is handled in platform.c internally
  // if the bottom page ever is touched, it will just seg fault
  tcb->bsp = os_alloc_stack(THRD_STACK_SIZE);
  if (tcb->bsp == NULL) {
    tcb_destroy(tcb);
    return NULL;
  }

  size_t size = THRD_STACK_SIZE + page_size();
  tcb->rsp = (uint8_t*)tcb->bsp + size;

  tcb->user_proc = entry_point;

  // set up the stack frame.
  // it is platform dependent:
  // on windows x86: 8 registers,
  // on linux x86: 6 registers,
  // on arm: 12 registers

  // push the cleanup function
  uint64_t* stack = (uint64_t*)tcb->rsp;
  *(--stack) = (uint64_t)thrd_exit;
  
  // push the instruction pointer ('entry_point')
  // on arm, 'ret' doesn't pop the stack,
  // it looks at x30 register
#ifdef __aarch64__
  *(--stack) = 0x0; // dummy for 16B align
#else
  *(--stack) = (uint64_t)tcb_wrap;
#endif

  // clear the callee-saved registers
  // %rbx, %rbp, %r12, %r13, %r14, %r15
  // additional %rdi and %rsi on windows
  // on arm: x19-x30
  stack -= CALLEE_SAVED_REG_CNT;
  memset(stack, 0x0, CALLEE_SAVED_REG_CNT * sizeof(void*));

#ifdef __aarch64__
  // since x19 is callee-saved,
  // store the function pointer in there safely.
  stack[X19_REG_POS] = (uint64_t)tcb_wrap;

  // this will be called on scope exit
  // (just like entry_point on x86)
  // arm requires a separate asm procedure,
  // because on ret arm doesn't pop the stack
  // (it uses x30 register to get the pointer)
  // we copy this x86 behavior via 'thrd_tramp'
  stack[X30_REG_POS] = (uint64_t)thrd_tramp;
#endif

  tcb->rsp = (void*)stack;

  return tcb;
}

void tcb_destroy(tcb_t* tcb) {
  if (tcb == NULL)
    return;

  os_free_stack(tcb->bsp, THRD_STACK_SIZE);
  pool_free(&tcb_pool, (void*)tcb);
}

tcb_t* tcb_alloc(void) {
  tcb_t* tcb = pool_alloc(&tcb_pool);
  if (tcb == NULL)
    return NULL;

  memset(tcb, 0x0, sizeof(tcb_t));
  return tcb;
}

const char* state_to_str(thrd_state_t state) {
  switch (state) {
  case THRD_READY:
    return "ready";
  case THRD_RUNNING:
    return "running";
  case THRD_DEAD:
    return "dead";
  case THRD_BLOCKED:
    return "blocked";
  case THRD_SLEEPING:
    return "sleeping";
  default:
    return "unknown";
  }
}

void tcb_dump_one(tcb_t* tcb) {
  if (tcb == NULL)
    return;

  const char* format;
  if (tcb->state == THRD_RUNNING)
    format = " addr: %p\n state: %s\n bsp: %p\n rsp: %p\n stack used (stale): %"PRIu64"\n";
  else
    format = " addr: %p\n state: %s\n bsp: %p\n rsp: %p\n stack used: %"PRIu64"\n";

  fprintf(stderr, format,
    (void *)tcb,
    state_to_str(tcb->state),
    (void *)tcb->bsp,
    (void *)tcb->rsp,
    (uint64_t)((uint8_t*)tcb->bsp + THRD_STACK_SIZE + page_size() - (uint8_t*)tcb->rsp)
  );
  if (tcb->state == THRD_SLEEPING)
    fprintf(stderr, "wakeup time: %"PRIu64"\n", tcb->wakeup_time);
}

int tcb_pool_init(void) {
  return pool_new(&tcb_pool, sizeof(tcb_t), _Alignof(tcb_t), POOL_THRD_CNT);
}
