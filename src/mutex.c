#include "tcb.h"
#include "scheduler.h"
#include "utils.h"
#include <thrd_ndl/thrd_ndl.h>

void mutex_lock(mutex_t* mutex) {
  // if mutex is unlocked,
  // then lock it and return
  if (!mutex->is_locked) {
    mutex->is_locked = 1;
    return;
  } else {
    // the mutex is contested
    // by 2+ threads, as another
    // thread was first, this thread has to
    // block until it isn't locked
    tcb_t* curr_thrd = get_curr_thrd();
    curr_thrd->state = THRD_BLOCKED;

    // push 'curr_thrd' to mutex wait queue
    thrd_enqueue(curr_thrd, (tcb_t**)&mutex->wait_queue_hd, (tcb_t**)&mutex->wait_queue_tl);

    // push the thread off of cpu
    thrd_yield();
  }
}

void mutex_unlock(mutex_t* mutex) {
  // if the wait queue is empty,
  // then just mark mutex as unlocked
  if (mutex->wait_queue_hd == NULL) {
    mutex->is_locked = 0;
    return;
  } else {
    // if there's at least one thread waiting in the queue
    // then pop one thread off
    tcb_t* pop_thrd = thrd_dequeue((tcb_t**)&mutex->wait_queue_hd, (tcb_t**)&mutex->wait_queue_tl);

    // make it ready again and push it to ready queue
    resume_thrd(pop_thrd);
  }
}
