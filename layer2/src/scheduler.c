#include "tcb.h"
#include <stdlib.h>
#include <assert.h>
#include <thrd_ndl/thrd_ndl.h>

static tcb_t* curr_thrd = NULL;
static tcb_t* rdy_queue_hd = NULL;
static tcb_t* rdy_queue_tl = NULL;

extern void thrd_switch(tcb_t* old_tcb, tcb_t* new_tcb);

static inline void rdy_enqueue(tcb_t* thrd) {
  thrd->next = NULL;
  
  if (rdy_queue_tl == NULL)
    rdy_queue_hd = thrd;
  else
    rdy_queue_tl->next = thrd;

  rdy_queue_tl = thrd;
}

static inline tcb_t* rdy_dequeue(void) {
  if (rdy_queue_hd == NULL)
    return NULL;

  tcb_t* thrd = rdy_queue_hd;

  rdy_queue_hd = rdy_queue_hd->next;
  if (rdy_queue_hd == NULL)
    rdy_queue_tl = NULL;

  return thrd;
}

void thrd_register(thrd_t thrd) {
  if (thrd == NULL)
    return;

  rdy_enqueue((tcb_t*)thrd);
}

void thrd_yield(void) {
  // stop the current thread from running
  curr_thrd->state = THRD_READY;
  rdy_enqueue(curr_thrd);

  // pop the ready queue's head
  tcb_t* next_thrd = rdy_dequeue();
  assert(next_thrd != NULL);

  // set 'next_thrd' as 'curr_thrd'
  tcb_t* old_thrd = curr_thrd;
  curr_thrd = next_thrd;
  curr_thrd->state = THRD_RUNNING;

  // call the asm context switch procedure
  thrd_switch(old_thrd, curr_thrd);
}

int thrd_init(void) {
  // if the thread is already initialized, just return
  if (curr_thrd != NULL)
    return THRD_EINVAL;

  tcb_t* init_thrd = (tcb_t*)malloc(sizeof(tcb_t));
  if (init_thrd == NULL)
    return THRD_ENOMEM;

  init_thrd->rsp = NULL;
  init_thrd->next = NULL;
  init_thrd->state = THRD_RUNNING;

  curr_thrd = init_thrd;

  return THRD_SUCCESS;
}
