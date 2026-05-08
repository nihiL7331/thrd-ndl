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
  // on windows, need to clear 2 more registers

  // push the cleanup function
  uint64_t* cast_ptr = (uint64_t*)tcb->rsp;
  cast_ptr--;
  tcb->rsp = (void*)cast_ptr;
  *cast_ptr = (uint64_t)thrd_exit;
  
  // push the instruction pointer ('entry_point')
  cast_ptr--;
  tcb->rsp = (void*)cast_ptr;
  *cast_ptr = (uint64_t)entry_point;

  // clear the callee-saved registers
  // %rbx, %rbp, %r12, %r13, %r14, %r15
  // additional %rdi and %rsi on windows
  cast_ptr -= CALLEE_SAVED_REG_CNT;
  tcb->rsp = (void*)cast_ptr;
  memset(tcb->rsp, 0x0, CALLEE_SAVED_REG_CNT * sizeof(void*));

  return tcb;
}
