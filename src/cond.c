#include "platform.h"
#include "scheduler.h"
#include "tcb.h"
#include "queue.h"
#include <thrd_ndl/thrd_ndl.h>
#include <assert.h>
#include <string.h>

int cond_init(cond_t* cond) {
  if (cond == NULL)
    return THRD_EINVAL;

  memset(cond, 0x0, sizeof(*cond));

  return THRD_SUCCESS;
}

int cond_wait(cond_t* cond, mtx_t* mtx) {
  if (cond == NULL || mtx == NULL || mtx->owner != get_curr_thrd())
    return THRD_EINVAL;

  preempt_disable();

  tcb_t* curr_thrd = get_curr_thrd();

  // push 'curr_thrd' onto 'cond' block queue
  thrd_enqueue(curr_thrd, (tcb_t**)&cond->wait_queue_hd, (tcb_t**)&cond->wait_queue_tl);

  // make 'curr_thrd' blocked
  curr_thrd->state = THRD_BLOCKED;

  // unlock mutex so other thread can grab 
  // the lock and change the shared data
  mtx_unlock(mtx);

  preempt_enable();

  // sleep until signaled

  // safe under preemption,
  // thread is blocked preventing a re-enqueue in yield
  thrd_yield();

  // lock back the mutex
  mtx_lock(mtx);

  return THRD_SUCCESS;
}

int cond_signal(cond_t* cond) {
  if (cond == NULL)
    return THRD_EINVAL;

  preempt_disable();

  if (cond->wait_queue_hd != NULL) {
    // pop the head from wait queue
    tcb_t* signal_thrd = thrd_dequeue((tcb_t**)&cond->wait_queue_hd, (tcb_t**)&cond->wait_queue_tl);

    // make 'signal_thrd' ready and push onto ready queue
    resume_thrd(signal_thrd);
  }

  preempt_enable();

  return THRD_SUCCESS;
}

int cond_bcast(cond_t* cond) {
  if (cond == NULL)
    return THRD_EINVAL;

  preempt_disable();

  // do the same as in 'cond_signal', 
  // but for the whole queue
  while (cond->wait_queue_hd != NULL) {
    tcb_t* signal_thrd = thrd_dequeue((tcb_t**)&cond->wait_queue_hd, (tcb_t**)&cond->wait_queue_tl);

    resume_thrd(signal_thrd);
  }

  preempt_enable();

  return THRD_SUCCESS;
}
