#include "tcb.h"
#include "queue.h"       // include for 'thrd_enqueue'
#include "heap.h"        // include for 'heap_new'
#include "internal.h"    // include for 'POOL_THRD_CNT'
#include "platform.h"    // include for 'get_os_time'
#include <stdnoreturn.h> // include this for 'noreturn'
#include <stdlib.h>
#include <thrd_ndl/thrd_ndl.h>

static tcb_t* curr_thrd     = NULL;
static tcb_t* rdy_queue_hd  = NULL;
static tcb_t* rdy_queue_tl  = NULL;
static tcb_t* dead_queue_hd = NULL;

static void* sleep_heap_storage[POOL_THRD_CNT];
static heap_t sleep_queue;
static int wakeup_cmp(const void* a, const void* b);

extern void thrd_switch(tcb_t* old_tcb, tcb_t* new_tcb);

void thrd_register(thrd_t thrd) {
  if (thrd == NULL)
    return;

  thrd_enqueue((tcb_t*)thrd, &rdy_queue_hd, &rdy_queue_tl);
}

void thrd_yield(void) {
  preempt_disable();

  // stop the current thread from running
  if (curr_thrd->state == THRD_RUNNING) {
    curr_thrd->state = THRD_READY;
    thrd_enqueue(curr_thrd, &rdy_queue_hd, &rdy_queue_tl);
  }

  uint64_t curr_time_ms = get_os_time();
  tcb_t* sleep_hd = (tcb_t*)heap_peek(&sleep_queue);
  while (sleep_hd != NULL && curr_time_ms >= sleep_hd->wakeup_time) {
    heap_pop(&sleep_queue);

    sleep_hd->state = THRD_READY;
    thrd_enqueue(sleep_hd, &rdy_queue_hd, &rdy_queue_tl);

    sleep_hd = (tcb_t*)heap_peek(&sleep_queue);
  }

  tcb_t* prev_dead = NULL;
  tcb_t* curr_dead = dead_queue_hd;

  while (curr_dead != NULL) {
    if (curr_dead == curr_thrd) {
      prev_dead = curr_dead;
      curr_dead = curr_dead->next;
    } else {
      tcb_t* dead_thrd = curr_dead;

      // remove from queue
      // must save next before destroy,
      // pool_free overwrites the tcb
      if (prev_dead == NULL)
        dead_queue_hd = curr_dead->next; 
      else
        prev_dead->next = curr_dead->next;
      curr_dead = curr_dead->next;

      // free the tcb
      tcb_destroy(dead_thrd);
    }
  }

  while (rdy_queue_hd == NULL) {
    // there's no one else waiting,
    // keep running the thread
    tcb_t* sleep_hd = (tcb_t*)heap_peek(&sleep_queue);
    if (sleep_hd != NULL) {
      // wait here until thread wakes up,
      curr_time_ms = get_os_time();

      // prevent underflow if late
      if (sleep_hd->wakeup_time > curr_time_ms)
        os_sleep_ms(sleep_hd->wakeup_time - curr_time_ms);
      
      curr_time_ms = get_os_time();
      while (sleep_hd != NULL && curr_time_ms >= sleep_hd->wakeup_time) {
        tcb_t* awake_thrd = heap_pop(&sleep_queue);
        awake_thrd->state = THRD_READY;
        thrd_enqueue(awake_thrd, &rdy_queue_hd, &rdy_queue_tl);
        sleep_hd = (tcb_t*)heap_peek(&sleep_queue);
      }

    } else if (curr_thrd->state == THRD_DEAD) // all threads are dead, close the program
      _Exit(0);
    else // all threads blocked with no holders
      _Exit(1);
  }

  // guaranteed to have a ready thread now
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

  int pool_ret_val = tcb_pool_init();
  if (pool_ret_val != THRD_SUCCESS)
    return pool_ret_val;

  // initialize sleep binary heap
  int heap_ret_val = heap_new(&sleep_queue, sleep_heap_storage, POOL_THRD_CNT, wakeup_cmp);
  if (heap_ret_val != THRD_SUCCESS)
    return heap_ret_val;

  tcb_t* init_thrd = tcb_alloc();
  if (init_thrd == NULL)
    return THRD_ENOMEM;

  // tcb_alloc zeroed every field
  init_thrd->state = THRD_RUNNING;

  curr_thrd = init_thrd;

  timer_init();

  return THRD_SUCCESS;
}

int thrd_create(thrd_t* out_thread, void (*entry)(void)) {
  if (out_thread == NULL || entry == NULL)
    return THRD_EINVAL;

  tcb_t* new_thrd = tcb_init(entry);
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

  if (curr_thrd->join_queue_hd != NULL) {
    // we have joiners, wake them up and proceed to the dead queue
    curr_thrd->state = THRD_DEAD;

    curr_thrd->next = dead_queue_hd;
    dead_queue_hd = curr_thrd;

    tcb_t* awake_thrd = curr_thrd->join_queue_hd;
    while (awake_thrd != NULL) {
      tcb_t* next_thrd = awake_thrd->next;

      awake_thrd->state = THRD_READY;
      thrd_enqueue(awake_thrd, &rdy_queue_hd, &rdy_queue_tl);

      awake_thrd = next_thrd;
    }

    curr_thrd->join_queue_hd = NULL;
    curr_thrd->join_queue_tl = NULL;
  } else
    // no one is waiting, become a zombie
    curr_thrd->state = THRD_ZOMBIE;

  preempt_enable();

  thrd_yield();

  abort(); 
}

int thrd_join(thrd_t thrd) {
  if (thrd == NULL || (tcb_t*)thrd == curr_thrd)
    return THRD_EINVAL;

  tcb_t* cast_thrd = (tcb_t*)thrd;

  preempt_disable();
  
  // if the target is a zombie, bury it and return immediately
  if (cast_thrd->state == THRD_ZOMBIE) {
    cast_thrd->state = THRD_DEAD;

    cast_thrd->next = dead_queue_hd;
    dead_queue_hd = cast_thrd;

    preempt_enable();

    return THRD_SUCCESS;
  }

  curr_thrd->state = THRD_BLOCKED;

  thrd_enqueue(curr_thrd, &cast_thrd->join_queue_hd, &cast_thrd->join_queue_tl);

  preempt_enable();

  thrd_yield();

  return THRD_SUCCESS;
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

void thrd_sleep(uint64_t time_ms) {
  if (curr_thrd == NULL)
    return;

  if (time_ms == 0) {
    thrd_yield();
    return;
  }

  preempt_disable();

  curr_thrd->wakeup_time = get_os_time() + time_ms;
  curr_thrd->state = THRD_SLEEPING;

  heap_push(&sleep_queue, curr_thrd);

  preempt_enable();

  thrd_yield();
}

static int wakeup_cmp(const void* a, const void* b) {
  const uint64_t wakeup_a = ((tcb_t*)a)->wakeup_time;
  const uint64_t wakeup_b = ((tcb_t*)b)->wakeup_time;

  return (wakeup_a > wakeup_b) - (wakeup_a < wakeup_b);
}
