#include <thrd_ndl/thrd_ndl.h>
#include <stdlib.h>
#include <stdint.h>
#include "internal.h"
#include "platform.h"
#include "tcb.h"

extern void thrd_ndl_switch(tcb_t* old_tcb, tcb_t* new_tcb);

static void thrd_enqueue(tcb_t* ptr);
static tcb_t* thrd_dequeue(void);

static tcb_t* curr_thread = NULL;
static tcb_t* rdy_queue_hd = NULL;
static tcb_t* rdy_queue_tl = NULL;

static tcb_t* sleep_queue_hd = NULL;

void thrd_yield(void) {
  // instantly update the state if the thrd was running
  if (curr_thread->state ==  THRD_RUNNING) {
    curr_thread->state = THRD_READY;
    thrd_enqueue(curr_thread);
  }

  // if the thread that has the closest 'wakeup_time'
  // is waking up, then pop it off the sleep queue
  uint64_t curr_time_ms = get_os_time();
  while (sleep_queue_hd != NULL && curr_time_ms >= sleep_queue_hd->wakeup_time) {
    tcb_t* awake_thread = sleep_queue_hd;
    sleep_queue_hd = sleep_queue_hd->next;
    awake_thread->state = THRD_READY;
    thrd_enqueue(awake_thread);
  }

  // there's no one else waiting,
  // keep running the thread
  if (rdy_queue_hd == NULL) {
    if (sleep_queue_hd != NULL) {
      // wait here until thread wakes up,
      while (get_os_time() < sleep_queue_hd->wakeup_time);
      // then go back to the top of the function
      thrd_yield();
      return;
    } else if (curr_thread->state == THRD_DEAD) // all threads are dead, close the program
      exit(0);
    else // all threads are sleeping / UB
      exit(1);
  }

  // pop the head
  tcb_t* next_thread = thrd_dequeue();

  // set 'next_thread' as 'curr_thread'
  tcb_t* old_thread = curr_thread;
  curr_thread = next_thread;
  curr_thread->state = THRD_RUNNING;

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
  init_thread->state = THRD_RUNNING;

  curr_thread = init_thread;
}

int thrd_create(thrd_t* out_thread, void (*func)(void)) {
  tcb_t* new_thread = tcb_init(func);
  if (new_thread == NULL)
    return THRD_OOM;

  // pass the address to the pointer given by the user
  if (out_thread != NULL)
    *out_thread = (thrd_t)new_thread;

  // push to ready queue
  thrd_enqueue(new_thread);

  return THRD_SUCCESS;
}

void thrd_exit(void) {
  // make the exiting thread dead
  curr_thread->state = THRD_DEAD;

  tcb_t* curr = curr_thread->join_queue_hd;

  // store 'next' because 'thrd_enqueue' overrides 'curr->next'
  tcb_t* next = NULL;

  // set all the joined threads to ready so they can run
  while (curr != NULL) {
    curr->state = THRD_READY;
    next = curr->next;
    thrd_enqueue(curr);
    curr = next;
  }
  
  thrd_yield();
}

void thrd_join(thrd_t thread) {
  tcb_t* cast_thread = (tcb_t*)thread;

  // if thread is dead, return
  if (cast_thread->state == THRD_DEAD)
    return;

  // mark it as blocked
  curr_thread->state = THRD_BLOCKED;

  // append it to 'cast_thread's join queue
  if (cast_thread->join_queue_tl != NULL)
    cast_thread->join_queue_tl->next = curr_thread;
  else
    cast_thread->join_queue_hd = curr_thread;
  cast_thread->join_queue_tl = curr_thread;
  curr_thread->next = NULL;

  // yield
  thrd_yield();
}

void thrd_sleep(uint64_t time_ms) {
  if (curr_thread == NULL)
    return;

  // get absolute os time
  uint64_t curr_time_ms = get_os_time();

  // the head of the sleep queue will be compared against
  // absolute os time to determine if it should wake up
  curr_thread->wakeup_time = curr_time_ms + time_ms;
  curr_thread->state = THRD_SLEEPING;

  // insert the sleeping thread to sleep queue,
  // assuming that before the insertion the list is sorted
  // by wakeup time, insert it in a way that this promise isnt broken
  tcb_t* curr = sleep_queue_hd;
  tcb_t* prev = NULL;
  while (curr != NULL && curr->wakeup_time < curr_thread->wakeup_time) {
    prev = curr;
    curr = curr->next;
  }

  // found position of the newly sleeping thread,
  // insert it
  if (prev != NULL)
    prev->next = curr_thread;
  else
    sleep_queue_hd = curr_thread;

  curr_thread->next = curr;

  thrd_yield();
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
