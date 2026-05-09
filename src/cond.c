#include "cond.h"
#include "scheduler.h"
#include "tcb.h"
#include "utils.h"
#include <thrd_ndl/thrd_ndl.h>
#include <assert.h>

void cond_wait(cond_t* cond, mutex_t* mutex) {
  if (cond == NULL || mutex == NULL)
    return;

  tcb_t* curr_thrd = get_curr_thrd();

  // push 'curr_thrd' onto 'cond' wait queue
  thrd_enqueue(curr_thrd, (tcb_t**)mutex->wait_queue_hd, (tcb_t**)mutex->wait_queue_tl);

  // make 'curr_thrd' blocked
  curr_thrd->state = THRD_BLOCKED;

  // unlock mutex so other thread can grab 
  // the lock and change the shared data
  mutex_unlock(mutex);

  // sleep until signaled
  thrd_yield();

  // lock back the mutex
  mutex_lock(mutex);
}

void cond_signal(cond_t* cond) {
  (void)cond;
  assert(0 && "TODO");
}

void cond_bcast(cond_t* cond) {
  (void)cond;
  assert(0 && "TODO");
}
