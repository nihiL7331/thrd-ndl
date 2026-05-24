#include "scheduler.h" // include for 'get_curr_thrd', 'resume_thrd'
#include "queue.h"     // include for 'thrd_enqueue', 'thrd_dequeue'
#include <thrd_ndl/thrd_ndl.h>
#include <string.h>

int cond_init(cond_t* cond) {
  if (cond == NULL)
    return THRD_EINVAL;

  memset(cond, 0, sizeof(*cond));

  return THRD_SUCCESS;
}

int cond_wait(cond_t* cond, mtx_t* mtx) {
  if (cond == NULL || mtx == NULL || mtx->owner != get_curr_thrd())
    return THRD_EINVAL;

  tcb_t* curr_thrd = get_curr_thrd();

  thrd_enqueue(curr_thrd, (tcb_t**)&cond->wait_queue_hd, (tcb_t**)&cond->wait_queue_tl);

  curr_thrd->state = THRD_BLOCKED;

  // unlock mutex so other thread can grab 
  // the lock and change the shared data
  mtx_unlock(mtx);

  // park the thread
  thrd_yield();

  // lock back the mutex before returning to the caller
  mtx_lock(mtx);

  return THRD_SUCCESS;
}

int cond_signal(cond_t* cond) {
  if (cond == NULL)
    return THRD_EINVAL;

  if (cond->wait_queue_hd != NULL) {
    tcb_t* signal_thrd = thrd_dequeue((tcb_t**)&cond->wait_queue_hd, (tcb_t**)&cond->wait_queue_tl);
    resume_thrd(signal_thrd);
  }

  return THRD_SUCCESS;
}

int cond_bcast(cond_t* cond) {
  if (cond == NULL)
    return THRD_EINVAL;

  while (cond->wait_queue_hd != NULL) {
    tcb_t* signal_thrd = thrd_dequeue((tcb_t**)&cond->wait_queue_hd, (tcb_t**)&cond->wait_queue_tl);
    resume_thrd(signal_thrd);
  }

  return THRD_SUCCESS;
}
