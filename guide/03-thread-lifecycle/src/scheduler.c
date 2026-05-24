#include "tcb.h"
#include <stdlib.h>
#include <stdnoreturn.h> // include this for 'noreturn'
#include <thrd_ndl/thrd_ndl.h>

static tcb_t* curr_thrd     = NULL;
static tcb_t* rdy_queue_hd  = NULL;
static tcb_t* rdy_queue_tl  = NULL;
static tcb_t* dead_queue_hd = NULL;

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

  // stop the current thread from running
  if (curr_thrd->state == THRD_RUNNING) {
    curr_thrd->state = THRD_READY;
    rdy_enqueue(curr_thrd);
  }

  // pop the ready queue's head
  tcb_t* next_thrd = rdy_dequeue();
  if (next_thrd == NULL && curr_thrd->state == THRD_DEAD)
    _Exit(0);
  else if (next_thrd == NULL && curr_thrd->state != THRD_RUNNING)
    _Exit(1);

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

  int pool_ret_val = tcb_pool_init();
  if (pool_ret_val != THRD_SUCCESS)
    return pool_ret_val;

  tcb_t* init_thrd = tcb_alloc();
  if (init_thrd == NULL)
    return THRD_ENOMEM;

  // tcb_alloc zeroed every field
  init_thrd->state = THRD_RUNNING;

  curr_thrd = init_thrd;

  return THRD_SUCCESS;
}

int thrd_create(thrd_t* out_thrd, void (*entry)(void)) {
  if (out_thrd == NULL || entry == NULL)
    return THRD_EINVAL;

  tcb_t* new_thrd = tcb_init(entry);
  if (new_thrd == NULL)
    return THRD_ENOMEM;

  // pass the address to the pointer given by the user
  *out_thrd = (thrd_t)new_thrd;

  // push to ready queue
  rdy_enqueue(new_thrd);

  return THRD_SUCCESS;
}

noreturn void thrd_exit(void) {
  curr_thrd->state = THRD_DEAD;

  curr_thrd->next = dead_queue_hd;
  dead_queue_hd = curr_thrd;

  thrd_yield();

  abort(); 
}
