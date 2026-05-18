#include "platform.h"
#include "tcb.h"
#include "scheduler.h"
#include "queue.h"
#include <string.h>
#include <thrd_ndl/thrd_ndl.h>

int mutex_init(mutex_t* mutex) {
  if (mutex == NULL)
    return THRD_EINVAL;

  memset((void*)mutex, 0x0, sizeof(*mutex));

  return THRD_SUCCESS;
}

void mutex_lock(mutex_t* mutex) {
  preempt_disable();

  // if mutex is unlocked,
  // then lock it and return
  if (!mutex->is_locked) {
    mutex->is_locked = 1;
    preempt_enable();
    return;
  }

  // the mutex is contested
  // by 2+ threads, as another
  // thread was first, this thread has to
  // block until it isn't locked
  tcb_t* curr_thrd = get_curr_thrd();
  curr_thrd->state = THRD_BLOCKED;

  // push 'curr_thrd' to mutex wait queue
  thrd_enqueue(curr_thrd, (tcb_t**)&mutex->wait_queue_hd, (tcb_t**)&mutex->wait_queue_tl);

  preempt_enable();

  // preempt enabled before yield is safe,
  // because thread is blocked, and that
  // prevents the re-enqueue.

  // push the thread off of cpu
  thrd_yield();
}

void mutex_unlock(mutex_t* mutex) {
  preempt_disable();

  // if the wait queue is empty,
  // then just mark mutex as unlocked
  if (mutex->wait_queue_hd == NULL) {
    mutex->is_locked = 0;
    preempt_enable();
    return;
  }

  // if there's at least one thread waiting in the queue
  // then pop one thread off
  tcb_t* pop_thrd = thrd_dequeue((tcb_t**)&mutex->wait_queue_hd, (tcb_t**)&mutex->wait_queue_tl);

  // make it ready again and push it to ready queue
  resume_thrd(pop_thrd);

  preempt_enable();
}

int mutex_trylock(mutex_t* mutex) {
  preempt_disable();

  // if mutex is unlocked then lock it,
  if (!mutex->is_locked) {
    mutex->is_locked = 1;
    preempt_enable();
    return THRD_SUCCESS;
  }

  preempt_enable();

  // otherwise just return that it's busy and do nothing
  return THRD_EBUSY;
}
