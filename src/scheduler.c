#include <thrd_ndl/thrd_ndl.h>
#include <stdlib.h>
#include "tcb.h"

extern void thrd_ndl_switch(tcb_t* old_tcb, tcb_t* new_tcb);

static void thrd_enqueue(tcb_t* ptr);
static tcb_t* thrd_dequeue(void);

static tcb_t* curr_thread = NULL;
static tcb_t* rdy_queue_hd = NULL;
static tcb_t* rdy_queue_tl = NULL;

void thrd_yield(void) {
  // there's no one else waiting,
  // keep running the thread
  if (rdy_queue_hd == NULL)
    return;

  // pop the head
  tcb_t* next_thread = thrd_dequeue();

  // push 'curr_thread' to ready queue
  thrd_enqueue(curr_thread);
  curr_thread->state = READY;

  // set 'next_thread' as 'curr_thread'
  tcb_t* old_thread = curr_thread;
  curr_thread = next_thread;
  curr_thread->state = RUNNING;

  // call the asm context switch procedure
  thrd_ndl_switch(old_thread, curr_thread);
}

void thrd_init(void) {
  // if the thread is already initialized, just return
  if (curr_thread != NULL)
    return;

  // make a dummy thread
  tcb_t* init_thread = (tcb_t*)malloc(sizeof(tcb_t));
  init_thread->bsp = NULL;
  init_thread->rsp = NULL;
  init_thread->next = NULL;
  init_thread->state = RUNNING;

  curr_thread = init_thread;
}
// helpers

static void thrd_enqueue(tcb_t* ptr) {
  ptr->next = NULL;

  if (rdy_queue_tl != NULL)
    rdy_queue_tl->next = ptr;
  else
    rdy_queue_hd = ptr;

  rdy_queue_tl = ptr;
}

static tcb_t* thrd_dequeue(void) {
  tcb_t* pop_thrd = rdy_queue_hd;
  
  rdy_queue_hd = pop_thrd->next;
  if (rdy_queue_hd == NULL)
    rdy_queue_tl = NULL;

  return pop_thrd;
}
