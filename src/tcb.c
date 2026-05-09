#include "tcb.h"
#include "platform.h"
#include "internal.h"
#include <thrd_ndl/thrd_ndl.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
  #define CALLEE_SAVED_REG_CNT 8
#elif defined(__aarch64__) // no elifdef on C17:[
  #define CALLEE_SAVED_REG_CNT 12

  // required for implicit 'thrd_exit'
  extern void thrd_tramp(void);
#else
  #define CALLEE_SAVED_REG_CNT 6
#endif

tcb_t* tcb_init(void (*entry_point)(void)) {
  // allocate 'tcb_t' on the heap
  tcb_t* tcb = (tcb_t*)malloc(sizeof(tcb_t));
  if (tcb == NULL)
    return NULL;

  // get page-aligned size + one page for stack overflow safety
  // if the bottom page ever is touched, it will just seg fault
  size_t size = align_to_page(THRD_STACK_SIZE + page_size());
  void* stack_ptr = os_alloc(size);
  if (stack_ptr == NULL) {
    free(tcb);
    return NULL;
  }

  // protect the bottom page
  if (protect_page(stack_ptr, page_size()) != 0) {
    free(tcb);
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
  stack[11] = (uint64_t)entry_point;

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
