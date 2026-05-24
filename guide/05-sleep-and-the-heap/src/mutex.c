#include "scheduler.h" // include for 'get_curr_thrd'
#include "queue.h"     // include for 'thrd_enqueue'
#include "tcb.h"       // include for 'tcb_t', 'THRD_READY'
#include <thrd_ndl/thrd_ndl.h>
#include <string.h>

int mtx_init(mtx_t* mtx) {
  if (mtx == NULL)
    return THRD_EINVAL;

  memset(mtx, 0, sizeof(*mtx));

  return THRD_SUCCESS;
}

int mtx_trylock(mtx_t* mtx) {
  if (mtx->owner == NULL) {
    mtx->owner = get_curr_thrd();
    return THRD_SUCCESS;
  }

  return THRD_EBUSY;
}

int mtx_lock(mtx_t* mtx) {
  if (mtx == NULL)
    return THRD_EINVAL;

  if (mtx_trylock(mtx) == THRD_SUCCESS)
    return THRD_SUCCESS;

  tcb_t* curr_thrd = get_curr_thrd();
  curr_thrd->state = THRD_BLOCKED;

  thrd_enqueue(curr_thrd, (tcb_t**)&mtx->wait_queue_hd, (tcb_t**)&mtx->wait_queue_tl);

  thrd_yield();
  
  return THRD_SUCCESS;
}

int mtx_unlock(mtx_t* mtx) {
  if (mtx == NULL || mtx->owner != get_curr_thrd())
    return THRD_EINVAL;

  if (mtx->wait_queue_hd == NULL) {
    mtx->owner = NULL;
    return THRD_SUCCESS;
  }

  tcb_t* pop_thrd = thrd_dequeue((tcb_t**)&mtx->wait_queue_hd, (tcb_t**)&mtx->wait_queue_tl);
  mtx->owner = pop_thrd;
  resume_thrd(pop_thrd);

  return THRD_SUCCESS;
}
