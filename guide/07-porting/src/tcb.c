#include "tcb.h"
#include "internal.h"
#include "pool.h"
#include "platform.h"
#include "scheduler.h"
#include <thrd_ndl/thrd_ndl.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h> // include this for 'memset'

#ifdef __aarch64__
  #define CALLEE_REG_CNT 20
  #define X19_REG_POS 18
  #define X30_REG_POS 9
  extern void thrd_tramp(void);
#elif defined(_WIN32)
  #define CALLEE_REG_CNT 28
#else
  #define CALLEE_REG_CNT 6
#endif

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

#ifdef __aarch64__
  *(--sp) = 0;                  // dummy value for align
#else
  *(--sp) = (uint64_t)tcb_wrap; // jump to the wrapper, not 'entry'
#endif

  sp -= CALLEE_REG_CNT; // space for callee-saved registers
  memset(sp, 0x0, CALLEE_REG_CNT * sizeof(void*));

#ifdef __aarch64__
  sp[X19_REG_POS] = (uint64_t)tcb_wrap;
  sp[X30_REG_POS] = (uint64_t)thrd_tramp;
#endif

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
