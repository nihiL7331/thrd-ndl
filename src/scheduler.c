#include "scheduler.h"
#include <stddef.h>
#include <stdio.h>
#include <thrd_ndl/thrd_ndl.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdnoreturn.h>
#include "internal.h"
#include "platform.h"
#include "tcb.h"
#include "queue.h"
#include "heap.h"

extern void thrd_switch(tcb_t* old_tcb, tcb_t* new_tcb);

static tcb_t* curr_thrd = NULL;
static tcb_t* rdy_queue_hd = NULL;
static tcb_t* rdy_queue_tl = NULL;

static void*  sleep_heap_storage[POOL_THRD_CNT];
static heap_t sleep_queue;
static int wakeup_cmp(const void* a, const void* b);

static tcb_t* dead_queue_hd = NULL;

void thrd_yield(void) {
  preempt_disable();

  // free dead threads, skip ourself
  tcb_t* prev_dead = NULL;
  tcb_t* curr_dead = dead_queue_hd;

  while (curr_dead != NULL) {
    if (curr_dead == curr_thrd) {
      prev_dead = curr_dead;
      curr_dead = curr_dead->next;
    } else {
      tcb_t* dead_thrd = curr_dead;

      // remove from queue
      if (prev_dead == NULL)
        // must save next before destroy,
        // pool_free overwrites the tcb
        dead_queue_hd = curr_dead->next; 
      else
        prev_dead->next = curr_dead->next;
      curr_dead = curr_dead->next;

      // free the tcb
      tcb_destroy(dead_thrd);
    }
  }

  // instantly update the state if the thrd was running
  // blocked/sleeping threads already placed on a wait queue
  if (curr_thrd->state == THRD_RUNNING) {
    curr_thrd->state = THRD_READY;
    thrd_enqueue(curr_thrd, &rdy_queue_hd, &rdy_queue_tl);
  }

  // if the thread that has the closest 'wakeup_time'
  // is waking up, then pop it off the sleep queue
  uint64_t curr_time_ms = get_os_time();
  tcb_t* sleep_hd = (tcb_t*)heap_peek(&sleep_queue);
  while (sleep_hd != NULL && curr_time_ms >= sleep_hd->wakeup_time) {
    tcb_t* awake_thrd = heap_pop(&sleep_queue);
    awake_thrd->state = THRD_READY;
    thrd_enqueue(awake_thrd, &rdy_queue_hd, &rdy_queue_tl);
    sleep_hd = (tcb_t*)heap_peek(&sleep_queue);
  }

  while (rdy_queue_hd == NULL) {
    // there's no one else waiting,
    // keep running the thread
    tcb_t* sleep_hd = (tcb_t*)heap_peek(&sleep_queue);
    if (sleep_hd != NULL) {
      // wait here until thread wakes up,
      curr_time_ms = get_os_time();
      os_sleep_ms(sleep_hd->wakeup_time - curr_time_ms);
      
      curr_time_ms = get_os_time();
      while (sleep_hd != NULL && curr_time_ms >= sleep_hd->wakeup_time) {
        tcb_t* awake_thrd = heap_pop(&sleep_queue);
        awake_thrd->state = THRD_READY;
        thrd_enqueue(awake_thrd, &rdy_queue_hd, &rdy_queue_tl);
        sleep_hd = (tcb_t*)heap_peek(&sleep_queue);
      }

    } else if (curr_thrd->state == THRD_DEAD) // all threads are dead, close the program
      _exit(0);
    else // all threads blocked with no holders
      _exit(1);
  }

  // pop the head
  tcb_t* next_thrd = thrd_dequeue(&rdy_queue_hd, &rdy_queue_tl);

  // set 'next_thrd' as 'curr_thrd'
  tcb_t* old_thrd = curr_thrd;
  curr_thrd = next_thrd;
  curr_thrd->state = THRD_RUNNING;

  // call the asm context switch procedure
  thrd_switch(old_thrd, curr_thrd);

  preempt_enable();
}

int thrd_init(void) {
  // if the thread is already initialized, just return
  if (curr_thrd != NULL)
    return THRD_EINVAL;

  // initialize the thread pool allocator
  int pool_ret_val = tcb_pool_init();
  if (pool_ret_val != THRD_SUCCESS)
    return pool_ret_val;

  // initialize sleep binary heap
  int heap_ret_val = heap_new(&sleep_queue, sleep_heap_storage, POOL_THRD_CNT, wakeup_cmp);
  if (heap_ret_val != THRD_SUCCESS)
    return heap_ret_val;

  // create the main thread
  tcb_t* init_thrd = tcb_alloc();
  if (init_thrd == NULL)
    return THRD_ENOMEM;

  init_thrd->state = THRD_RUNNING;

  curr_thrd = init_thrd;

  // initialize the preemption timer
  // at the end, so that it doesnt fire
  // during the previous thread allocation
  timer_init();

  return THRD_SUCCESS;
}

int thrd_create(thrd_t* out_thread, void (*func)(void)) {
  if (out_thread == NULL || func == NULL)
    return THRD_EINVAL;

  tcb_t* new_thrd = tcb_init(func);
  if (new_thrd == NULL)
    return THRD_ENOMEM;

  // pass the address to the pointer given by the user
  *out_thread = (thrd_t)new_thrd;

  preempt_disable();

  // push to ready queue
  thrd_enqueue(new_thrd, &rdy_queue_hd, &rdy_queue_tl);

  preempt_enable();

  return THRD_SUCCESS;
}

noreturn void thrd_exit(void) {
  preempt_disable();

  // make the exiting thread dead
  curr_thrd->state = THRD_DEAD;

  // push it onto the dead queue
  curr_thrd->next = dead_queue_hd;
  dead_queue_hd = curr_thrd;

  tcb_t* awake_thrd = curr_thrd->join_queue_hd;

  // set all the joined threads to ready so they can run
  while (awake_thrd != NULL) {
    tcb_t* next_thrd = awake_thrd->next;

    awake_thrd->state = THRD_READY;
    thrd_enqueue(awake_thrd, &rdy_queue_hd, &rdy_queue_tl);

    awake_thrd = next_thrd;
  }

  curr_thrd->join_queue_hd = NULL;
  curr_thrd->join_queue_tl = NULL;

  // here preempt is enabled before yield,
  // because the thread dies in that yield
  preempt_enable();
  
  thrd_yield();

  abort();
}

int thrd_join(thrd_t thrd) {
  if (thrd == NULL || (tcb_t*)thrd == curr_thrd)
    return THRD_EINVAL;

  tcb_t* cast_thrd = (tcb_t*)thrd;

  preempt_disable();

  if (cast_thrd->state == THRD_DEAD) {
    preempt_enable();
    return THRD_SUCCESS;
  }

  curr_thrd->state = THRD_BLOCKED;

  thrd_enqueue(curr_thrd, &cast_thrd->join_queue_hd, &cast_thrd->join_queue_tl);

  preempt_enable();

  thrd_yield();

  return THRD_SUCCESS;
}

void thrd_sleep(uint64_t time_ms) {
  if (curr_thrd == NULL)
    return;

  preempt_disable();

  // get absolute os time
  uint64_t curr_time_ms = get_os_time();

  // the head of the sleep queue will be compared against
  // absolute os time to determine if it should wake up
  curr_thrd->wakeup_time = curr_time_ms + time_ms;
  curr_thrd->state = THRD_SLEEPING;

  // no need to check the return value,
  // it cant fail because heap capacity == pool alloc capacity
  heap_push(&sleep_queue, curr_thrd);

  preempt_enable();

  thrd_yield();
}

static inline void dump_queue(const char* label, tcb_t* head) {
  fprintf(stderr, "====  %s  ====\n", label);
  tcb_t* curr = head;
  uint64_t idx = 0;
  while (curr != NULL) {
    fprintf(stderr, "== THRD %"PRIu64" ==\n", idx++);
    tcb_dump_one(curr);
    curr = curr->next;
  }
}

void thrd_dump(void) {
  if (curr_thrd == NULL) {
    fprintf(stderr, "uninitialized\n");
    return;
  }

  preempt_disable();

  fprintf(stderr, "==== thrd_dump ====\n");
  fprintf(stderr, "====  RUNNING  ====\n");
  tcb_dump_one(curr_thrd);
  dump_queue("READY", rdy_queue_hd);
  dump_queue("DEAD", dead_queue_hd);

  preempt_enable();
}

tcb_t* get_curr_thrd(void) {
  return curr_thrd;
}

void resume_thrd(tcb_t* thrd) {
  if (thrd == NULL)
    return;

  preempt_disable();

  thrd->state = THRD_READY;
  thrd_enqueue(thrd, &rdy_queue_hd, &rdy_queue_tl);

  preempt_enable();
}

static int wakeup_cmp(const void* a, const void* b) {
  const uint64_t wakeup_a = ((tcb_t*)a)->wakeup_time;
  const uint64_t wakeup_b = ((tcb_t*)b)->wakeup_time;

  return (wakeup_a > wakeup_b) - (wakeup_a < wakeup_b);
}
