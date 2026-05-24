#include <thrd_ndl/thrd_ndl.h>
#include <stdlib.h>
#include <assert.h>

volatile int shared_cntr = 0;
mtx_t cntr_mtx;

void inc_task(void) {
  for (int i = 0; i < 10000; ++i) {
    mtx_lock(&cntr_mtx);

    int local_val = shared_cntr;

    thrd_yield();

    local_val++;
    shared_cntr = local_val;

    mtx_unlock(&cntr_mtx);

    thrd_yield();
  }
}

int main(void) {
  assert(thrd_init() == THRD_SUCCESS && "failed to main initialize");

  mtx_init(&cntr_mtx);

  thrd_t t1, t2;

  assert(thrd_create(&t1, inc_task) == THRD_SUCCESS && "failed to create thrd 1");
  assert(thrd_create(&t2, inc_task) == THRD_SUCCESS && "failed to create thrd 2");

  thrd_join(t1);
  thrd_join(t2);

  assert(shared_cntr == 20000 && "mutex fail");
}
