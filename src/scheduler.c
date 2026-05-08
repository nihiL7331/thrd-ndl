#include "scheduler.h"
#include <thrd_ndl/thrd_ndl.h>
#include <stdlib.h>
#include <stdint.h>
#include "platform.h"
#include "tcb.h"
#include "utils.h"

extern void thrd_ndl_switch(tcb_t* old_tcb, tcb_t* new_tcb);

static tcb_t* curr_thrd = NULL;
static tcb_t* rdy_queue_hd = NULL;
static tcb_t* rdy_queue_tl = NULL;

static tcb_t* sleep_queue_hd = NULL;

void thrd_yield(void) {
  // instantly update the state if the thrd was running
  if (curr_thrd->state ==  THRD_RUNNING) {
    curr_thrd->state = THRD_READY;
    thrd_enqueue(curr_thrd, &rdy_queue_hd, &rdy_queue_tl);
  }

  // if the thread that has the closest 'wakeup_time'
  // is waking up, then pop it off the sleep queue
  uint64_t curr_time_ms = get_os_time();
  while (sleep_queue_hd != NULL && curr_time_ms >= sleep_queue_hd->wakeup_time) {
    tcb_t* awake_thrd = sleep_queue_hd;
    sleep_queue_hd = sleep_queue_hd->next;
    awake_thrd->state = THRD_READY;
    thrd_enqueue(awake_thrd, &rdy_queue_hd, &rdy_queue_tl);
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
    } else if (curr_thrd->state == THRD_DEAD) // all threads are dead, close the program
      exit(0);
    else // all threads are sleeping / UB
      exit(1);
  }

  // pop the head
  tcb_t* next_thrd = thrd_dequeue(&rdy_queue_hd, &rdy_queue_tl);

  // set 'next_thrd' as 'curr_thrd'
  tcb_t* old_thrd = curr_thrd;
  curr_thrd = next_thrd;
  curr_thrd->state = THRD_RUNNING;

  // call the asm context switch procedure
  thrd_ndl_switch(old_thrd, curr_thrd);
}

void thrd_init(void) {
  // if the thread is already initialized, just return
  if (curr_thrd != NULL)
    return;

  // make a dummy thread
  tcb_t* init_thrd = (tcb_t*)malloc(sizeof(tcb_t));
  init_thrd->bsp = NULL;
  init_thrd->rsp = NULL;
  init_thrd->next = NULL;
  init_thrd->state = THRD_RUNNING;

  curr_thrd = init_thrd;
}

int thrd_create(thrd_t* out_thread, void (*func)(void)) {
  tcb_t* new_thrd = tcb_init(func);
  if (new_thrd == NULL)
    return THRD_OOM;

  // pass the address to the pointer given by the user
  if (out_thread != NULL)
    *out_thread = (thrd_t)new_thrd;

  // push to ready queue
  thrd_enqueue(new_thrd, &rdy_queue_hd, &rdy_queue_tl);

  return THRD_SUCCESS;
}

void thrd_exit(void) {
  // make the exiting thread dead
  curr_thrd->state = THRD_DEAD;

  tcb_t* awake_thrd = thrd_dequeue(&curr_thrd->join_queue_hd, &curr_thrd->join_queue_tl);

  // set all the joined threads to ready so they can run
  while (awake_thrd != NULL) {
    awake_thrd->state = THRD_READY;
    thrd_enqueue(awake_thrd, &rdy_queue_hd, &rdy_queue_tl);

    awake_thrd = thrd_dequeue(&curr_thrd->join_queue_hd, &curr_thrd->join_queue_tl);
  }
  
  thrd_yield();
}

void thrd_join(thrd_t thread) {
  tcb_t* cast_thrd = (tcb_t*)thread;

  // if thread is dead, return
  if (cast_thrd->state == THRD_DEAD)
    return;

  // mark it as blocked
  curr_thrd->state = THRD_BLOCKED;

  // append it to 'cast_thrd's join queue
  thrd_enqueue(curr_thrd, &cast_thrd->join_queue_hd, &cast_thrd->join_queue_tl);

  // yield
  thrd_yield();
}

void thrd_sleep(uint64_t time_ms) {
  if (curr_thrd == NULL)
    return;

  // get absolute os time
  uint64_t curr_time_ms = get_os_time();

  // the head of the sleep queue will be compared against
  // absolute os time to determine if it should wake up
  curr_thrd->wakeup_time = curr_time_ms + time_ms;
  curr_thrd->state = THRD_SLEEPING;

  // insert the sleeping thread to sleep queue,
  // assuming that before the insertion the list is sorted
  // by wakeup time, insert it in a way that this promise isnt broken
  tcb_t* curr = sleep_queue_hd;
  tcb_t* prev = NULL;
  while (curr != NULL && curr->wakeup_time < curr_thrd->wakeup_time) {
    prev = curr;
    curr = curr->next;
  }

  // found position of the newly sleeping thread,
  // insert it
  if (prev != NULL)
    prev->next = curr_thrd;
  else
    sleep_queue_hd = curr_thrd;

  curr_thrd->next = curr;

  thrd_yield();
}

tcb_t* get_curr_thrd(void) {
  return curr_thrd;
}

void resume_thrd(tcb_t* thrd) {
  if (thrd == NULL)
    return;

  thrd->state = THRD_READY;
  thrd_enqueue(thrd, &rdy_queue_hd, &rdy_queue_tl);
}
