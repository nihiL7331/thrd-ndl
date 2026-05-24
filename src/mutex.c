#include "platform.h"
#include "tcb.h"
#include "scheduler.h"
#include "queue.h"
#include <string.h>
#include <thrd_ndl/thrd_ndl.h>

int mtx_init(mtx_t* mtx) {
  if (mtx == NULL)
    return THRD_EINVAL;

  memset((void*)mtx, 0x0, sizeof(*mtx));

  return THRD_SUCCESS;
}

int mtx_lock(mtx_t* mtx) {
  if (mtx == NULL)
    return THRD_EINVAL;

  preempt_disable();

  tcb_t* curr_thrd = get_curr_thrd();

  // if mutex is unlocked,
  // then lock it and return
  if (mtx->owner == NULL) {
    mtx->owner = (thrd_t)curr_thrd;
    preempt_enable();
    return THRD_SUCCESS;
  }

  // the mutex is contested
  // by 2+ threads, as another
  // thread was first, this thread has to
  // block until it isn't locked
  curr_thrd->state = THRD_BLOCKED;

  // push 'curr_thrd' to mutex wait queue
  thrd_enqueue(curr_thrd, (tcb_t**)&mtx->wait_queue_hd, (tcb_t**)&mtx->wait_queue_tl);

  preempt_enable();

  // preempt enabled before yield is safe,
  // because thread is blocked, and that
  // prevents the re-enqueue.

  // push the thread off of cpu
  thrd_yield();

  return THRD_SUCCESS;
}

int mtx_unlock(mtx_t* mtx) {
  if (mtx == NULL || mtx->owner != get_curr_thrd())
    return THRD_EINVAL;

  preempt_disable();

  // if the wait queue is empty,
  // then just mark mutex as unlocked
  if (mtx->wait_queue_hd == NULL) {
    mtx->owner = NULL;
    preempt_enable();
    return THRD_SUCCESS;
  }

  // if there's at least one thread waiting in the queue
  // then pop one thread off
  tcb_t* pop_thrd = thrd_dequeue((tcb_t**)&mtx->wait_queue_hd, (tcb_t**)&mtx->wait_queue_tl);

  // make it the new mutex owner
  mtx->owner = pop_thrd;

  // make it ready again and push it to ready queue
  resume_thrd(pop_thrd);

  preempt_enable();

  return THRD_SUCCESS;
}

int mtx_trylock(mtx_t* mtx) {
  if (mtx == NULL)
    return THRD_EINVAL;

  preempt_disable();

  // if mutex is unlocked then lock it,
  if (mtx->owner == NULL) {
    mtx->owner = (thrd_t)get_curr_thrd();
    preempt_enable();
    return THRD_SUCCESS;
  }

  preempt_enable();

  // otherwise just return that it's busy and do nothing
  return THRD_EBUSY;
}
