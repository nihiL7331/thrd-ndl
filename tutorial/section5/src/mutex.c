#include "scheduler.h" // include for 'get_curr_thrd'
#include "queue.h"     // include for 'thrd_enqueue'
#include "tcb.h"       // include for 'tcb_t', 'THRD_READY'
#include <thrd_ndl/thrd_ndl.h>
#include <string.h>

int mutex_init(mutex_t* mutex) {
  if (mutex == NULL)
    return THRD_EINVAL;

  memset(mutex, 0, sizeof(*mutex));

  return THRD_SUCCESS;
}

int mutex_trylock(mutex_t* mutex) {
  if (mutex->owner == NULL) {
    mutex->owner = get_curr_thrd();
    return THRD_SUCCESS;
  }

  return THRD_EBUSY;
}

int mutex_lock(mutex_t* mutex) {
  if (mutex == NULL)
    return THRD_EINVAL;

  if (mutex_trylock(mutex) == THRD_SUCCESS)
    return THRD_SUCCESS;

  tcb_t* curr_thrd = get_curr_thrd();
  curr_thrd->state = THRD_BLOCKED;

  thrd_enqueue(curr_thrd, (tcb_t**)&mutex->wait_queue_hd, (tcb_t**)&mutex->wait_queue_tl);

  thrd_yield();
  
  return THRD_SUCCESS;
}

int mutex_unlock(mutex_t* mutex) {
  if (mutex == NULL || mutex->owner != get_curr_thrd())
    return THRD_EINVAL;

  if (mutex->wait_queue_hd == NULL) {
    mutex->owner = NULL;
    return THRD_SUCCESS;
  }

  tcb_t* pop_thrd = thrd_dequeue((tcb_t**)&mutex->wait_queue_hd, (tcb_t**)&mutex->wait_queue_tl);
  mutex->owner = pop_thrd;
  resume_thrd(pop_thrd);

  return THRD_SUCCESS;
}
