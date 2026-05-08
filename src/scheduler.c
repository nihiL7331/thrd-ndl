#include <thrd_ndl/thrd_ndl.h>
#include "tcb.h"

extern void thrd_ndl_switch(tcb_t* old_tcb, tcb_t* new_tcb);

static tcb_t* curr_thread = NULL;
static tcb_t* rdy_queue_hd = NULL;
static tcb_t* rdy_queue_tl = NULL;

void thrd_yield(void) {
  // there's no one else waiting,
  // keep running the thread
  if (rdy_queue_hd == NULL)
    return;

  // pop the head
  tcb_t* next_thread = rdy_queue_hd;
  
  rdy_queue_hd = rdy_queue_hd->next;
  if (rdy_queue_hd == NULL)
    rdy_queue_tl = NULL;

  // push 'curr_thread' to ready queue
  curr_thread->state = READY;
  curr_thread->next = NULL;
  if (rdy_queue_tl != NULL)
    rdy_queue_tl->next = curr_thread;
  else
    rdy_queue_hd = curr_thread;
  rdy_queue_tl = curr_thread;

  // set 'next_thread' as 'curr_thread'
  tcb_t* old_thread = curr_thread;
  curr_thread = next_thread;
  curr_thread->state = RUNNING;

  // call the asm context switch procedure
  thrd_ndl_switch(old_thread, curr_thread);
}
