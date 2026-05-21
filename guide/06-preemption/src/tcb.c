#include "tcb.h"
#include "internal.h"
#include "pool.h"
#include "platform.h"
#include "scheduler.h"
#include <thrd_ndl/thrd_ndl.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h> // include this for 'memset'

#define CALLEE_REG_CNT 6

static pool_t tcb_pool = {0};

static void tcb_wrap(void) {
  preempt_enable();
  get_curr_thrd()->user_proc();
  thrd_exit();
}

tcb_t* tcb_init(void (*entry)(void)) {
  tcb_t* tcb = tcb_alloc();
  if (tcb == NULL)
    return NULL;

  tcb->bsp = os_alloc_stack(THRD_STACK_SIZE);
  if (tcb->bsp == NULL) {
    tcb_free(tcb);
    return NULL;
  }

  tcb->user_proc = entry;

  size_t size  = THRD_STACK_SIZE + page_size();
  uint64_t* sp = (uint64_t*)((uint8_t*)tcb->bsp + size);

  *(--sp) = (uint64_t)thrd_exit; // push the cleanup function (align + fail-safe)
  *(--sp) = (uint64_t)tcb_wrap;  // jump to the wrapper, not 'entry'

  sp -= CALLEE_REG_CNT; // space for callee-saved registers
  memset(sp, 0x0, CALLEE_REG_CNT * sizeof(void*));

  tcb->rsp   = sp;
  tcb->state = THRD_READY;
  return tcb;
}

int tcb_pool_init(void) {
  return pool_new(&tcb_pool, sizeof(tcb_t), _Alignof(tcb_t), POOL_THRD_CNT);
}

tcb_t* tcb_alloc(void) {
  tcb_t* tcb = pool_alloc(&tcb_pool);
  if (tcb == NULL)
    return NULL;

  memset(tcb, 0, sizeof(tcb_t));
  return tcb;
}

void tcb_free(tcb_t* tcb) {
  if (tcb == NULL)
    return;

  pool_free(&tcb_pool, tcb);
}

void tcb_destroy(tcb_t* tcb) {
  if (tcb == NULL)
    return;
 
  os_free_stack(tcb->bsp, THRD_STACK_SIZE);
  tcb_free(tcb);
}
