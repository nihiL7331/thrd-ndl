#include "tcb.h"
#include "platform.h"
#include "internal.h"
#include "pool.h"
#include <thrd_ndl/thrd_ndl.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __aarch64__
  #define CALLEE_SAVED_REG_CNT 12

  // required for implicit 'thrd_exit'
  extern void thrd_tramp(void);
#elif defined(_WIN32)
  #define CALLEE_SAVED_REG_CNT 8
#else
  #define CALLEE_SAVED_REG_CNT 6
#endif

static pool_t tcb_pool = {0};
static int pool_init = 0;

tcb_t* tcb_init(void (*entry_point)(void)) {
  // allocate 'tcb_t' on the heap
  tcb_t* tcb = tcb_alloc();
  if (tcb == NULL)
    return NULL;

  // get page-aligned size + one page for stack overflow safety
  // if the bottom page ever is touched, it will just seg fault
  size_t size = align_to_page(THRD_STACK_SIZE + page_size());
  void* stack_ptr = os_alloc(size);
  if (stack_ptr == NULL) {
    tcb_destroy(tcb);
    return NULL;
  }

  // protect the bottom page
  if (protect_page(stack_ptr, page_size()) != 0) {
    tcb_destroy(tcb);
    os_free(stack_ptr, size);
    return NULL;
  }

  tcb->bsp = stack_ptr;
  tcb->rsp = (uint8_t*)stack_ptr + size;

  tcb->join_queue_hd = NULL;
  tcb->join_queue_tl = NULL;

  tcb->wakeup_time = 0;

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
  *(--stack) = (uint64_t)entry_point;
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
  stack[10] = (uint64_t)entry_point;

  // this will be called on scope exit
  // (just like entry_point on x86)
  // arm requires a separate asm procedure,
  // because on ret arm doesn't pop the stack
  // (it uses x30 register to get the pointer)
  // we copy this x86 behavior via 'thrd_tramp'
  stack[1] = (uint64_t)thrd_tramp;
#endif

  tcb->rsp = (void*)stack;

  return tcb;
}

void tcb_destroy(tcb_t* tcb) {
  if (tcb == NULL || !pool_init)
    return;

  pool_free(&tcb_pool, (void*)tcb);
}

tcb_t* tcb_alloc(void) {
  tcb_t* tcb = pool_alloc(&tcb_pool, sizeof(tcb_t), _Alignof(tcb_t));
  if (tcb == NULL)
    return NULL;

  memset(tcb, 0x0, sizeof(tcb_t));
  return tcb;
}

int tcb_pool_init(void) {
  if (!pool_init) {
    int ret_val = pool_new(&tcb_pool, sizeof(tcb_t), _Alignof(tcb_t), POOL_THREAD_CNT);
    if (ret_val == POOL_SUCCESS)
      pool_init = 1;

    return ret_val;
  }

  return POOL_SUCCESS;
}
