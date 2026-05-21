#include "tcb.h"
#include "queue.h"       // include for 'thrd_enqueue'
#include <stdlib.h>
#include <stdnoreturn.h> // include this for 'noreturn'
#include <thrd_ndl/thrd_ndl.h>

static tcb_t* curr_thrd     = NULL;
static tcb_t* rdy_queue_hd  = NULL;
static tcb_t* rdy_queue_tl  = NULL;
static tcb_t* dead_queue_hd = NULL;

extern void thrd_switch(tcb_t* old_tcb, tcb_t* new_tcb);

void thrd_register(thrd_t thrd) {
  if (thrd == NULL)
    return;

  thrd_enqueue((tcb_t*)thrd, &rdy_queue_hd, &rdy_queue_tl);
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
    thrd_enqueue(curr_thrd, &rdy_queue_hd, &rdy_queue_tl);
  }

  // pop the ready queue's head
  tcb_t* next_thrd = thrd_dequeue(&rdy_queue_hd, &rdy_queue_tl);
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

int thrd_create(thrd_t* out_thread, void (*entry)(void)) {
  if (out_thread == NULL || entry == NULL)
    return THRD_EINVAL;

  tcb_t* new_thrd = tcb_init(entry);
  if (new_thrd == NULL)
    return THRD_ENOMEM;

  // pass the address to the pointer given by the user
  *out_thread = (thrd_t)new_thrd;

  // push to ready queue
  thrd_enqueue(new_thrd, &rdy_queue_hd, &rdy_queue_tl);

  return THRD_SUCCESS;
}

noreturn void thrd_exit(void) {
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

  thrd_yield();

  abort(); 
}

int thrd_join(thrd_t thrd) {
  if (thrd == NULL || (tcb_t*)thrd == curr_thrd)
    return THRD_EINVAL;

  tcb_t* cast_thrd = (tcb_t*)thrd;
  
  // if the target is a zombie, bury it and return immediately
  if (cast_thrd->state == THRD_ZOMBIE) {
    cast_thrd->state = THRD_DEAD;

    cast_thrd->next = dead_queue_hd;
    dead_queue_hd = cast_thrd;

    return THRD_SUCCESS;
  }

  curr_thrd->state = THRD_BLOCKED;

  thrd_enqueue(curr_thrd, &cast_thrd->join_queue_hd, &cast_thrd->join_queue_tl);

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
