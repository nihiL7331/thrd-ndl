#include "cond.h"
#include <thrd_ndl/thrd_ndl.h>
#include <assert.h>

void cond_wait(cond_t* cond, mutex_t* mutex) {
  (void)cond; (void)mutex;
  assert(0 && "TODO");
}

void cond_signal(cond_t* cond) {
  (void)cond;
  assert(0 && "TODO");
}

void cond_bcast(cond_t* cond) {
  (void)cond;
  assert(0 && "TODO");
}
