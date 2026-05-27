#pragma once

#include "tcb.h"    // include this for 'tcb_t'
#include <stdlib.h> // include this for 'NULL'

static inline void thrd_enqueue(tcb_t* thrd, tcb_t** hd, tcb_t** tl) {
  thrd->next = NULL;

  if (*tl != NULL)
    (*tl)->next = thrd;
  else
    *hd = thrd;

  *tl = thrd;
}

static inline tcb_t* thrd_dequeue(tcb_t** hd, tcb_t** tl) {
  if (*hd == NULL)
    return NULL;

  tcb_t* pop_thrd = *hd;
  
  *hd = pop_thrd->next;
  if (*hd == NULL)
    *tl = NULL;

  return pop_thrd;
}
